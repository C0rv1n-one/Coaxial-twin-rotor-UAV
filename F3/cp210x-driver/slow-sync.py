#!/usr/bin/env python3
"""Open once, wait, then sync repeatedly; also listen passively first."""
import time
import serial

PORT = "COM7"

s = serial.Serial(PORT, 115200, timeout=0.5, parity=serial.PARITY_EVEN)
print("opened, waiting 2s passively ...")
time.sleep(2.0)
passive = s.read(200)
print("  passive bytes:", " ".join("%02X" % b for b in passive) or "<none>")

for i in range(8):
    s.reset_input_buffer()
    s.write(b"\x7f")
    s.flush()
    time.sleep(0.25)
    r = s.read(16)
    print("  sync %d -> %s" % (i + 1, " ".join("%02X" % b for b in r) or "<none>"))
    if r and r[0] == 0x79:
        print("SYNC OK")
        break

s.close()
