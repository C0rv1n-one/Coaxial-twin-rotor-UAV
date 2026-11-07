#!/usr/bin/env python3
"""The bootloader may be waiting for extended-erase params (global erase FF FF 00)."""
import time
import serial

s = serial.Serial("COM7", 115200, timeout=0.5, parity=serial.PARITY_EVEN)
time.sleep(0.3)

print("sending global extended erase payload FF FF 00 ...")
s.write(bytes([0xFF, 0xFF, 0x00]))
s.flush()

deadline = time.time() + 45
got = b""
while time.time() < deadline:
    chunk = s.read(16)
    if chunk:
        got += chunk
        print("  <-", " ".join("%02X" % b for b in chunk))
        break
if not got:
    print("  no reply within 45s")

print("--- now try GET ---")
s.reset_input_buffer()
s.write(bytes([0x00, 0xFF]))
s.flush()
time.sleep(0.6)
r = s.read(64)
print("  GET ->", " ".join("%02X" % b for b in r) or "<none>")

s.close()
