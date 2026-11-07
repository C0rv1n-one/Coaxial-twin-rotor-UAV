#!/usr/bin/env python3
"""Non-destructive: if the bootloader waits for erase params, a bad checksum
should NACK and return it to command mode. Then try to sync again."""
import time
import serial

s = serial.Serial("COM7", 115200, timeout=0.5, parity=serial.PARITY_EVEN)
time.sleep(0.3)


def rd(t=0.6):
    r = b""
    end = time.time() + t
    while time.time() < end:
        c = s.read(32)
        if c:
            r += c
            end = time.time() + 0.3
    return r


print("send FF FF 01 (bad checksum) ...")
s.write(bytes([0xFF, 0xFF, 0x01]))
s.flush()
print("  <-", " ".join("%02X" % b for b in rd(1.0)) or "<none>")

for i in range(5):
    s.reset_input_buffer()
    s.write(b"\x7f")
    s.flush()
    r = rd(0.5)
    print("  sync %d -> %s" % (i + 1, " ".join("%02X" % b for b in r) or "<none>"))
    if r and r[0] == 0x79:
        print("SYNC OK")
        break
    time.sleep(0.2)

s.close()
