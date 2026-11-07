#!/usr/bin/env python3
"""Minimal STM32 USART bootloader client for SPRacingF3 over CP2102.

Usage:
    python stm32flash.py --sync
    python stm32flash.py --flash betaflight_4.0.6_SPRACINGF3.hex
"""
import sys
import time
import argparse

import serial

PORT = "COM7"
ACK = 0x79
NACK = 0x1F

BOOT_CMD_GET = 0x00
BOOT_CMD_GET_VERSION = 0x01
BOOT_CMD_GET_ID = 0x02
BOOT_CMD_WRITE = 0x31
BOOT_CMD_ERASE = 0x43
BOOT_CMD_GO = 0x21
BOOT_CMD_EXTENDED_ERASE = 0x44

FLASH_BASE = 0x08000000


def log(msg):
    print(msg, flush=True)


def xor(data):
    c = 0
    for b in data:
        c ^= b
    return c


class Bootloader:
    def __init__(self, ser):
        self.ser = ser
        self.use_extended_erase = False

    def read(self, n, timeout=2.0):
        self.ser.timeout = timeout
        return self.ser.read(n)

    def wait_ack(self, timeout=2.0, what="ack"):
        b = self.read(1, timeout)
        if not b:
            raise RuntimeError("timeout waiting for %s" % what)
        if b[0] == ACK:
            return True
        if b[0] == NACK:
            raise RuntimeError("NACK on %s" % what)
        raise RuntimeError("unexpected byte 0x%02X on %s" % (b[0], what))

    def sync(self, attempts=10):
        for i in range(attempts):
            self.ser.reset_input_buffer()
            self.ser.write(b"\x7f")
            b = self.read(1, 0.6)
            if b and b[0] == ACK:
                log("  sync ACK on attempt %d" % (i + 1))
                return True
            time.sleep(0.05)
        return False

    def cmd(self, code):
        self.ser.write(bytes([code, code ^ 0xFF]))
        self.wait_ack(what="cmd 0x%02X" % code)

    def get(self):
        self.cmd(BOOT_CMD_GET)
        b = self.read(1)
        n = b[0]
        body = self.read(n + 1, timeout=2.0)
        if len(body) < 3:
            raise RuntimeError("short GET response: %s" % body.hex())
        self.ser.reset_input_buffer()
        version = body[0] / 10.0
        commands = body[3:]
        log("  bootloader version %.1f, pid %s, commands %s"
            % (version, body[1:3].hex(), " ".join("%02X" % c for c in commands)))
        self.use_extended_erase = BOOT_CMD_EXTENDED_ERASE in commands
        log("  extended erase: %s" % self.use_extended_erase)
        return n

    def erase_all(self):
        if self.use_extended_erase:
            self.cmd(BOOT_CMD_EXTENDED_ERASE)
            self.ser.write(bytes([0xFF, 0xFF, 0x00]))
            self.wait_ack(timeout=40.0, what="global extended erase")
            log("  global extended erase done")
        else:
            self.cmd(BOOT_CMD_ERASE)
            self.ser.write(bytes([0xFF, 0x00]))
            self.wait_ack(timeout=40.0, what="mass erase")
            log("  mass erase done")



    def write_memory(self, addr, data):
        self.ser.write(bytes([BOOT_CMD_WRITE, BOOT_CMD_WRITE ^ 0xFF]))
        self.wait_ack(what="write cmd")
        ab = bytes([(addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                    (addr >> 8) & 0xFF, addr & 0xFF])
        self.ser.write(ab + bytes([xor(ab)]))
        self.wait_ack(what="write addr")
        n = len(data) - 1
        payload = bytes([n]) + data
        self.ser.write(payload + bytes([xor(payload)]))
        self.wait_ack(timeout=3.0, what="write data")

    def go(self, addr=FLASH_BASE):
        self.cmd(BOOT_CMD_GO)
        ab = bytes([(addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                    (addr >> 8) & 0xFF, addr & 0xFF])
        self.ser.write(ab + bytes([xor(ab)]))
        self.wait_ack(what="go")


def parse_hex(path):
    """Return dict address -> bytes, plus total size."""
    mem = {}
    base = 0
    upper = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            count = raw[0]
            addr = (raw[1] << 8) | raw[2]
            rtype = raw[3]
            data = raw[4:4 + count]
            if rtype == 0x00:
                full = base + addr
                mem[full] = data
            elif rtype == 0x01:
                break
            elif rtype == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
            elif rtype == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
    return mem


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sync", action="store_true")
    ap.add_argument("--flash")
    ap.add_argument("--port", default=PORT)
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.bytesize = 8
    ser.parity = serial.PARITY_EVEN
    ser.stopbits = 1
    ser.timeout = 1
    ser.dtr = False
    ser.rts = False

    if args.sync:
        # Reboot the running firmware into the ROM bootloader using ASCII 'R'
        ser.parity = serial.PARITY_NONE
        ser.open()
        log("port open (8N1), sending 'R' to enter bootloader ...")
        ser.write(b"R")
        ser.flush()
        time.sleep(0.2)
        ser.close()
        time.sleep(1.5)

        ser.parity = serial.PARITY_EVEN
        ser.open()
        bl = Bootloader(ser)
        log("trying bootloader sync (8E1) ...")
        ok = bl.sync()
        if ok:
            bl.get()
            log("RESULT: bootloader reachable")
            bl.go()
            log("  sent GO, board back to firmware")
        else:
            log("RESULT: no ACK - not in bootloader")
        ser.close()
        return 0 if ok else 2

    if args.flash:
        mem = parse_hex(args.flash)
        if not mem:
            log("empty hex")
            return 1
        lo = min(mem)
        hi = max(mem) + len(mem[max(mem)])
        size = hi - lo
        log("hex: base 0x%08X end 0x%08X size %d bytes" % (lo, hi, size))
        if lo != FLASH_BASE:
            log("ERROR: hex does not start at 0x08000000")
            return 1
        if size > 256 * 1024:
            log("ERROR: image larger than 256 KB flash")
            return 1

        ser.parity = serial.PARITY_NONE
        ser.open()
        log("sending 'R' to reboot into bootloader ...")
        ser.write(b"R")
        ser.flush()
        time.sleep(0.2)
        ser.close()
        time.sleep(1.5)

        ser.parity = serial.PARITY_EVEN
        ser.open()
        bl = Bootloader(ser)
        if not bl.sync():
            log("ERROR: bootloader not reachable")
            ser.close()
            return 2
        bl.get()
        bl.erase_all()

        PAGE = 256
        addr = FLASH_BASE
        end = hi
        written = 0
        t0 = time.time()
        while addr < end:
            chunk = bytearray()
            for i in range(PAGE):
                chunk += mem.get(addr + i, b"\xff")
            bl.write_memory(addr, bytes(chunk))
            addr += PAGE
            written += PAGE
            if written % (16 * PAGE) == 0 or written >= size:
                pct = written * 100 // ((end - FLASH_BASE + PAGE - 1) // PAGE * PAGE)
                log("  written %d / %d bytes (%d%%)" % (written, end - FLASH_BASE, pct))
        log("write done in %.1fs, sending GO" % (time.time() - t0))
        bl.go()
        ser.close()
        log("FLASH OK")
        return 0

    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())

