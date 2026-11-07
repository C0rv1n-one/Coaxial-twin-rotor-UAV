#!/usr/bin/env python3
"""Robust STM32 USART bootloader flasher (resumes whatever state the FC is in).

Handles the quirks seen on this SPRacingF3 bootloader:
  * 0x7F sync is only accepted right after entry; afterwards it is NACKed
    ("already in command mode") - so never insist on sync before commands
  * handshake bytes must be spaced (~50 ms)
  * any packet may be NACKed; just resend it
  * mass erase must use the extended-erase command 0x44 / 0xFFFF

Usage:
  python stm32flash3.py --flash [hex] [--no-reboot]
  python stm32flash3.py --sync
  python stm32flash3.py --verify [hex]
"""
import argparse
import sys
import time

import serial

PORT = "COM7"
BAUD = 115200
ACK = 0x79
NACK = 0x1F

CMD_GET = 0x00
CMD_GET_ID = 0x02
CMD_READ = 0x11
CMD_GO = 0x21
CMD_WRITE = 0x31
CMD_EXT_ERASE = 0x44

PACE = 0.05
CHUNK = 256


def hexs(b):
    return " ".join("%02X" % x for x in b) if b else "<none>"


def p(msg):
    print(msg, flush=True)


class Bootloader:
    def __init__(self, port=PORT, baud=BAUD):
        self.port = port
        self.baud = baud
        self.ser = serial.Serial(port, baud, timeout=1.0,
                                 parity=serial.PARITY_EVEN)
        self.ser.reset_input_buffer()
        time.sleep(0.2)
        self.commands = []

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _send(self, data, paced=True):
        if paced:
            for b in bytes(data):
                self.ser.write(bytes([b]))
                self.ser.flush()
                time.sleep(PACE)
        else:
            self.ser.write(bytes(data))
            self.ser.flush()

    def _read(self, n, timeout=1.0):
        buf = b""
        end = time.time() + timeout
        while len(buf) < n and time.time() < end:
            c = self.ser.read(n - len(buf))
            if c:
                buf += c
                end = time.time() + max(0.3, timeout)
        return buf

    def _ack(self, timeout=2.0):
        b = self._read(1, timeout)
        if not b:
            return "timeout"
        if b[0] == ACK:
            return "ok"
        if b[0] == NACK:
            return "nack"
        return "0x%02X" % b[0]

    # ---------------- session ----------------
    def probe(self):
        """Find out whether we are in bootloader / command mode."""
        for i in range(4):
            self.ser.reset_input_buffer()
            self._send([0x7F], paced=False)
            r = self._read(1, 0.6)
            if r and r[0] == ACK:
                p("  sync ACK (fresh bootloader)")
                return True
            if r and r[0] == NACK:
                p("  bootloader in command mode (0x7F nacked) - continuing")
                return True
        return False

    def get(self, attempts=6):
        for attempt in range(attempts):
            self.ser.reset_input_buffer()
            self._send([CMD_GET, 0xFF])
            if self._ack(1.2) != "ok":
                time.sleep(0.15)
                continue
            n = self._read(1, 1.0)
            if not n:
                continue
            count = n[0]
            body = self._read(count + 1, 1.0)
            if len(body) < count:
                continue
            self.commands = list(body[1:count])
            p("  bootloader version 0x%02X, commands: %s"
              % (body[0], hexs(bytes(self.commands))))
            self.ser.reset_input_buffer()
            return True
        return False

    def get_id(self):
        for _ in range(4):
            self.ser.reset_input_buffer()
            self._send([CMD_GET_ID, 0xFD])
            if self._ack(1.2) != "ok":
                continue
            n = self._read(1, 1.0)
            if not n:
                continue
            body = self._read(n[0] + 1, 1.0)
            self.ser.reset_input_buffer()
            if body:
                p("  product id 0x%02X" % body[0])
                return body[0]
        return -1

    def erase_all(self, attempts=4):
        for attempt in range(attempts):
            self.ser.reset_input_buffer()
            self._send([CMD_EXT_ERASE, 0xBB])
            st = self._ack(3.0)
            if st != "ok":
                p("  erase cmd attempt %d: %s" % (attempt + 1, st))
                time.sleep(0.3)
                continue
            self._send([0xFF, 0xFF, 0x00])
            st = self._ack(45.0)
            if st == "ok":
                p("  mass erase done")
                return True
            p("  erase payload attempt %d: %s" % (attempt + 1, st))
            time.sleep(0.5)
        return False

    def write_chunk(self, addr, data, attempts=8):
        a = [(addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
             (addr >> 8) & 0xFF, addr & 0xFF]
        acs = a[0] ^ a[1] ^ a[2] ^ a[3]
        n = len(data) - 1
        dcs = n
        for b in data:
            dcs ^= b
        last = "?"
        for attempt in range(attempts):
            self.ser.reset_input_buffer()
            self._send([CMD_WRITE, 0xCE])
            st = self._ack(2.5)
            if st != "ok":
                last = "cmd " + st
                time.sleep(0.12)
                continue
            self._send(a + [acs])
            st = self._ack(2.5)
            if st != "ok":
                last = "addr " + st
                time.sleep(0.12)
                continue
            self._send(bytes([n]) + bytes(data) + bytes([dcs]), paced=False)
            st = self._ack(3.0)
            if st == "ok":
                return True
            last = "data " + st
            time.sleep(0.12)
        raise RuntimeError("write 0x%08X failed (%s)" % (addr, last))

    def read_mem(self, addr, length):
        a = [(addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
             (addr >> 8) & 0xFF, addr & 0xFF]
        acs = a[0] ^ a[1] ^ a[2] ^ a[3]
        n = length - 1
        cs = n ^ acs
        self.ser.reset_input_buffer()
        self._send([CMD_READ, 0xEE])
        if self._ack(2.5) != "ok":
            return None
        self._send(a + [acs])
        if self._ack(2.5) != "ok":
            return None
        self._send([n, cs])
        if self._ack(2.5) != "ok":
            return None
        data = self._read(length, 3.0)
        self.ser.reset_input_buffer()
        return data

    def go(self, addr):
        a = [(addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
             (addr >> 8) & 0xFF, addr & 0xFF]
        acs = a[0] ^ a[1] ^ a[2] ^ a[3]
        for attempt in range(6):
            self.ser.reset_input_buffer()
            self._send([CMD_GO, 0xDE])
            if self._ack(2.5) != "ok":
                time.sleep(0.15)
                continue
            self._send(a + [acs])
            if self._ack(2.5) == "ok":
                p("  GO sent")
                return True
            time.sleep(0.15)
        return False


def parse_intel_hex(path):
    mem = {}
    base = 0
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            count, addr, rtype = raw[0], (raw[1] << 8) | raw[2], raw[3]
            data = raw[4:4 + count]
            if rtype == 0x00:
                for i, b in enumerate(data):
                    mem[base + addr + i] = b
            elif rtype == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
            elif rtype == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
            elif rtype == 0x01:
                break
    if not mem:
        raise RuntimeError("no data in hex")
    lo, hi = min(mem), max(mem)
    return bytearray(mem.get(i, 0xFF) for i in range(lo, hi + 1)), lo


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hex", default="C:\\Users\\y9000p\\Desktop\\workspace\\betaflight_4.0.6_SPRACINGF3.hex")
    ap.add_argument("--flash", action="store_true")
    ap.add_argument("--sync", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--no-reboot", action="store_true")
    ap.add_argument("--port", default=PORT)
    args = ap.parse_args()

    if not args.no_reboot and args.flash:
        try:
            s = serial.Serial(args.port, BAUD, timeout=0.5,
                              parity=serial.PARITY_NONE)
            time.sleep(0.2)
            s.write(b"R")
            s.flush()
            time.sleep(0.2)
            s.close()
            p("  sent 'R', waiting for bootloader ...")
            time.sleep(1.5)
        except Exception as e:
            p("  reboot step failed: %s" % e)

    bl = Bootloader(args.port)
    try:
        if not bl.probe():
            p("ERROR: bootloader silent - power cycle the FC and retry")
            return 2
        if not bl.get():
            p("ERROR: GET failed")
            return 2
        bl.get_id()

        if args.sync and not args.flash:
            bl.go(0x08000000)
            p("RESULT: bootloader reachable")
            return 0

        data, start = parse_intel_hex(args.hex)
        p("firmware %s: %d bytes, 0x%08X-0x%08X"
          % (args.hex.split("\\")[-1], len(data), start, start + len(data) - 1))

        if args.verify:
            bad = 0
            checked = 0
            off = 0
            while off < len(data):
                chunk = data[off:off + CHUNK]
                got = bl.read_mem(start + off, len(chunk))
                if got is None or got[:len(chunk)] != bytes(chunk):
                    bad += 1
                    p("  mismatch at 0x%08X" % (start + off))
                checked += 1
                off += len(chunk)
            p("verify: %d chunks, %d mismatched" % (checked, bad))
            return 0 if bad == 0 else 6

        if CMD_EXT_ERASE not in bl.commands:
            p("ERROR: extended erase unavailable")
            return 3
        if not bl.erase_all():
            p("ERROR: erase failed")
            return 3

        off = 0
        total = len(data)
        t0 = time.time()
        while off < total:
            chunk = data[off:off + CHUNK]
            if len(chunk) < 4:
                chunk = chunk + b"\xFF" * (4 - len(chunk))
            bl.write_chunk(start + off, chunk)
            off += len(chunk)
            n_chunk = off // CHUNK
            if n_chunk % 25 == 0:
                el = time.time() - t0
                p("  %5.1f%%  0x%08X  (%.0fs)" % (100.0 * off / total,
                                                  start + off, el))

        p("written %d bytes in %.0fs" % (total, time.time() - t0))
        bl.go(start)
        p("RESULT: OK")
        return 0
    finally:
        bl.close()


if __name__ == "__main__":
    sys.exit(main())
