#!/usr/bin/env python3
"""Assume bootloader is already in sync; send GET directly."""
import time
import serial

s = serial.Serial("COM7", 115200, timeout=0.6, parity=serial.PARITY_EVEN)
time.sleep(0.2)
s.reset_input_buffer()

for i in range(3):
    s.reset_input_buffer()
    s.write(bytes([0x00, 0xFF]))
    s.flush()
    time.sleep(0.4)
    r = s.read(64)
    print("GET %d -> %s" % (i + 1, " ".join("%02X" % b for b in r) or "<none>"))
    if r and r[0] == 0x79:
        break

s.close()
