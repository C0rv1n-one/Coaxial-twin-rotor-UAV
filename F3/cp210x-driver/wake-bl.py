#!/usr/bin/env python3
"""Try to wake the bootloader with repeated sync bytes; sweep DTR/RTS combos."""
import time
import serial

PORT = "COM7"


def hexs(b):
    return " ".join("%02X" % x for x in b) if b else "<none>"


def try_sync(parity, dtr, rts, attempts=6, label=""):
    s = serial.Serial(PORT, 115200, timeout=0.4, parity=parity)
    s.dtr = dtr
    s.rts = rts
    time.sleep(0.15)
    acks = 0
    for i in range(attempts):
        s.reset_input_buffer()
        s.write(b"\x7f")
        s.flush()
        time.sleep(0.15)
        r = s.read(8)
        if r and r[0] == 0x79:
            acks += 1
            print("  %s attempt %d -> %s  ACK" % (label, i + 1, hexs(r)))
            s.close()
            return True
        if i < 2:
            print("  %s attempt %d -> %s" % (label, i + 1, hexs(r)))
    s.close()
    return False


print("=== sweep parity/DTR/RTS ===")
for parity, pname in ((serial.PARITY_EVEN, "8E1"), (serial.PARITY_NONE, "8N1")):
    for dtr in (True, False):
        for rts in (True, False):
            label = "%s dtr=%d rts=%d" % (pname, dtr, rts)
            if try_sync(parity, dtr, rts, label=label):
                print("WORKING COMBO:", label)
                raise SystemExit(0)
print("no combo worked")
