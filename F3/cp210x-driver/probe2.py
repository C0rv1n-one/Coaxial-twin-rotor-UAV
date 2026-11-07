#!/usr/bin/env python3
"""Byte-level probe: try several bootloader commands at 8E1/8N1 and print replies."""
import time
import serial

PORT = "COM7"


def hexs(b):
    return " ".join("%02X" % x for x in b) if b else "<none>"


CMDS = [("GET", [0x00, 0xFF]), ("GET-ID", [0x02, 0xFD]), ("SYNC", [0x7F]),
        ("GET", [0x00, 0xFF]), ("ERASE-EXT", [0x44, 0xBB])]

for parity, pname in ((serial.PARITY_EVEN, "8E1"), (serial.PARITY_NONE, "8N1")):
    print("=== %s ===" % pname)
    s = serial.Serial(PORT, 115200, timeout=0.5, parity=parity)
    time.sleep(0.3)
    for name, cmd in CMDS:
        s.reset_input_buffer()
        for b in cmd:
            s.write(bytes([b]))
            s.flush()
            time.sleep(0.05)
        time.sleep(0.5)
        r = s.read(128)
        print("  %-10s %-10s => %s" % (name, hexs(bytes(cmd)), hexs(r)))
    s.close()
