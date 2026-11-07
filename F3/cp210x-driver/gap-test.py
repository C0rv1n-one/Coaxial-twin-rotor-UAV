#!/usr/bin/env python3
"""Test: send command bytes back-to-back, but pause between commands."""
import time
import serial

s = serial.Serial("COM7", 115200, timeout=0.5, parity=serial.PARITY_EVEN)
time.sleep(0.3)

for i in range(6):
    s.reset_input_buffer()
    s.write(bytes([0x00, 0xFF]))
    s.flush()
    time.sleep(0.4)
    r = s.read(64)
    print("GET %d -> %s" % (i + 1, " ".join("%02X" % b for b in r) or "<none>"))
    time.sleep(0.25)

s.close()
