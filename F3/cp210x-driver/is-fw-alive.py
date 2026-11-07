#!/usr/bin/env python3
"""Is the firmware running now? (MSP + CLI probes)"""
import time
import serial

s = serial.Serial("COM7", 115200, timeout=1.0, parity=serial.PARITY_NONE)
time.sleep(0.5)

print("--- MSP_API_VERSION request ($M<\\x00\\x03 then crc) ---")
s.reset_input_buffer()
cmd = bytes([0x24, 0x4D, 0x3C, 0x00, 0x03])
crc = 0
for b in [0x00, 0x03]:
    crc ^= b
s.write(cmd + bytes([crc]))
s.flush()
time.sleep(1.0)
r = s.read(200)
print("  ->", " ".join("%02X" % b for b in r) or "<none>")

print("--- CLI '#' ---")
s.reset_input_buffer()
s.write(b"#\r\n")
s.flush()
time.sleep(1.5)
r = s.read(400)
print("  ->", repr(r[:200]))

s.close()
