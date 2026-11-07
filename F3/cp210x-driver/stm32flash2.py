#!/usr/bin/env python3
"""Hardened STM32 USART bootloader flasher for SPRacingF3 (COM7).

Key quirks discovered on this board:
  * handshake bytes must be spaced out (~50 ms) or the bootloader NACKs
  * command list contains 0x44 (extended erase); plain 0x43 is unsupported
  * every response is ACK(0x79) / NACK(0x1F)

Usage:
  python stm32flash2.py --sync
  python stm32flash2.py --flash [hexfile] [--no-reboot]
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
CMD_WRITE = 0x31
CMD_GO = 0x21
CMD_EXT_ERASE = 0x44

PACE = 0.05          # gap between handshake bytes
CHUNK = 256          # write payload per packet


def p(msg):
    print(msg, flush=True)


def hexs(b):
    return " ".join("%02X" % x for x in b) if b else "<none>"


class Bootloader:
    def __init__(self, port=PORT, baud=BAUD):
        self.port = port
        self.baud = baud
        self.ser = None
        self.commands = []

    # ---- low level -------------------------------------------------
    def open(self):
        self.ser = serial.Serial(self.port, self.baud, timeout=1.0,
                                 parity=serial.PARITY_EVEN)
        self.ser.reset_input_buffer()
        time.sleep(0.2)

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            finally:
                self.ser = None

    def _send(self, data, paced=False):
        if paced:
            for b in bytes(data):
                self.ser.write(bytes([b]))
                self.ser.flush()
                time.sleep(PACE)
        else:
            self.ser.write(bytes(data))
            self.ser.flush()

    def _read(self, n, timeout=1.0):
        self.ser.timeout = timeout
        buf = b""
        end = time.time() + timeout
        while len(buf) < n and time.time() < end:
            c = self.ser.read(n - len(buf))
            if c:
                buf += c
                end = time.time() + max(0.35, timeout)
        return buf

    def wait_ack(self, timeout=1.5):
        b = self._read(1, timeout)
        if not b:
            raise RuntimeError("no response (timeout)")
        if b[0] == ACK:
            return True
        if b[0] == NACK:
            raise RuntimeError("NACK")
        raise RuntimeError("unexpected byte %02X" % b[0])

    # ---- protocol --------------------------------------------------
    def sync(self, attempts=12):
        for i in range(attempts):
            self.ser.reset_input_buffer()
            self._send([0x7F])
            try:
                self.wait_ack(0.6)
                p("  sync ACK on attempt %d" % (i + 1))
                return True
            except RuntimeError as e:
                p("  sync %d: %s" % (i + 1, e))
                time.sleep(0.15)
        return False

    def get(self):
        for attempt in range(6):
            self.ser.reset_input_buffer()
            self._send([CMD_GET, 0xFF], paced=True)
            try:
                self.wait_ack(1.0)
            except RuntimeError as e:
                p("  GET %d: %s" % (attempt + 1, e))
                time.sleep(0.15)
                continue
            n = self._read(1, 1.0)
            if not n:
                continue
            count = n[0]
            body = self._read(count + 1, 1.0)
            if len(body) < count:
                continue
            version = body[0]
            self.commands = list(body[1:count])
            p("  bootloader version 0x%02X, %d commands: %s"
              % (version, len(self.commands), hexs(bytes(self.commands))))
            self.ser.reset_input_buffer()
            return True
        return False

    def get_id(self):
        self.ser.reset_input_buffer()
        self._send([CMD_GET_ID, 0xFD], paced=True)
        self.wait_ack(1.0)
        n = self._read(1, 1.0)[0]
        body = self._read(n + 1, 1.0)
        pid = body[0] if body else -1
        self.ser.reset_input_buffer()
        p("  product id 0x%02X" % pid)
        return pid

    def erase_all(self):
        self.ser.reset_input_buffer()
        self._send([CMD_EXT_ERASE, 0xBB], paced=True)
        self.wait_ack(2.0)
        # global mass erase: N = 0xFFFF, checksum 0x00
        self._send([0xFF, 0xFF, 0x00], paced=True)
        self.wait_ack(45.0)
        self.ser.reset_input_buffer()
        p("  mass erase done")

    def write_chunk(self, addr, data):
        self.ser.reset_input_buffer()
        self._send([CMD_WRITE, 0xCE], paced=True)
        self.wait_ack(2.0)
        a = [(addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF]
        self._send(a + [a[0] ^ a[1] ^ a[2] ^ a[3]], paced=True)
        self.wait_ack(2.0)
        n = len(data) - 1
        payload = bytes([n]) + bytes(data)
        cs = n
        for b in data:
            cs ^= b
        self._send(payload + bytes([cs]), paced=False)
        self.wait_ack(3.0)

    def go(self, addr):
        self.ser.reset_input_buffer()
        self._send([CMD_GO, 0xDE], paced=True)
        self.wait_ack(2.0)
        a = [(addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF]
        self._send(a + [a[0] ^ a[1] ^ a[2] ^ a[3]], paced=True)
        self.wait_ack(2.0)
        p("  GO sent")


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


def reboot_to_bootloader(port=PORT):
    s = serial.Serial(port, BAUD, timeout=0.5, parity=serial.PARITY_NONE)
    time.sleep(0.2)
    s.write(b"R")
    s.flush()
    time.sleep(0.2)
    s.close()
    p("  sent 'R' reboot command, waiting ...")
    time.sleep(1.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sync", action="store_true")
    ap.add_argument("--flash", nargs="?", const="C:\\Users\\y9000p\\Desktop\\workspace\\betaflight_4.0.6_SPRACINGF3.hex")
    ap.add_argument("--no-reboot", action="store_true")
    ap.add_argument("--port", default=PORT)
    args = ap.parse_args()

    if not args.sync and not args.flash:
        ap.error("choose --sync or --flash")

    if not args.no_reboot:
        reboot_to_bootloader(args.port)

    bl = Bootloader(args.port)
    bl.open()
    try:
        if not bl.sync():
            p("ERROR: bootloader not reachable")
            bl.close()
            return 2
        if not bl.get():
            p("WARNING: GET failed")
        else:
            bl.get_id()

        if args.sync and not args.flash:
            if CMD_GO in bl.commands or True:
                try:
                    bl.go(0x08000000)
                except RuntimeError as e:
                    p("  go failed: %s" % e)
            p("RESULT: bootloader reachable")
            return 0

        data, start = parse_intel_hex(args.flash)
        p("firmware: %s (%d bytes, 0x%08X-0x%08X)"
          % (args.flash, len(data), start, start + len(data) - 1))

        if CMD_EXT_ERASE not in bl.commands:
            p("ERROR: extended erase not supported")
            return 3
        bl.erase_all()

        addr = start
        off = 0
        total = len(data)
        while off < total:
            chunk = data[off:off + CHUNK]
            if len(chunk) < 4:
                chunk = chunk + b"\xFF" * (4 - len(chunk))
            ok = False
            for attempt in range(4):
                try:
                    bl.write_chunk(addr, chunk)
                    ok = True
                    break
                except RuntimeError as e:
                    p("  write 0x%08X attempt %d failed: %s" % (addr, attempt + 1, e))
                    time.sleep(0.2)
                    if not bl.sync(4):
                        p("ERROR: lost bootloader")
                        return 4
            if not ok:
                p("ERROR: write failed at 0x%08X" % addr)
                return 5
            off += len(chunk)
            addr += len(chunk)
            if (off // CHUNK) % 16 == 0:
                p("  %5.1f%%  (0x%08X)" % (100.0 * off / total, addr))

        p("flash complete, %d bytes written" % total)
        bl.go(start)
        p("RESULT: OK")
        return 0
    finally:
        bl.close()


if __name__ == "__main__":
    sys.exit(main())
