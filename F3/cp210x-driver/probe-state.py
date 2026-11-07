#!/usr/bin/env python3
"""Probe: is the FC in bootloader, in CLI, or dead?"""
import time
import serial

PORT = "COM7"


def hexs(b):
    return " ".join("%02X" % x for x in b) if b else "<none>"


def probe(parity, label, payload, wait=1.0):
    try:
        s = serial.Serial(PORT, 115200, timeout=0.5, parity=parity)
    except Exception as e:
        print("%-10s open failed: %s" % (label, e))
        return
    s.dtr = False
    s.rts = False
    s.reset_input_buffer()
    time.sleep(0.1)
    s.write(bytes(payload))
    s.flush()
    time.sleep(wait)
    print("%-10s %-10s => %s" % (label, hexs(bytes(payload)), hexs(s.read(200))))
    s.close()


print("--- bootloader probe (8E1, 0x7F) ---")
probe(serial.PARITY_EVEN, "8E1", [0x7F])
print("--- firmware CLI probe (8N1, '#') ---")
probe(serial.PARITY_NONE, "8N1", [0x23, 0x0D, 0x0A], wait=1.2)
