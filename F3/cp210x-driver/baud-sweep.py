#!/usr/bin/env python3
"""Sweep baud rates / parity to find one where sync ACKs."""
import time
import serial

PORT = "COM7"
BAUDS = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]

for parity, pname in ((serial.PARITY_EVEN, "8E1"), (serial.PARITY_NONE, "8N1")):
    for baud in BAUDS:
        try:
            s = serial.Serial(PORT, baud, timeout=0.35, parity=parity)
        except Exception as e:
            print("%s %-7d open failed %s" % (pname, baud, e))
            continue
        time.sleep(0.1)
        s.reset_input_buffer()
        s.write(b"\x7f")
        s.flush()
        time.sleep(0.2)
        r = s.read(16)
        s.close()
        print("%s %-7d -> %s" % (pname, baud, " ".join("%02X" % b for b in r) or "<none>"))
        if r and r[0] == 0x79:
            print("SYNC OK at %s %d" % (pname, baud))
            raise SystemExit(0)
print("no working baud")
