#!/usr/bin/env python3
"""Raw protocol dump: see exactly what the STM32 bootloader sends back."""
import time
import serial

PORT = "COM7"


def hexs(b):
    return " ".join("%02X" % x for x in b) if b else "<none>"


def enter_bootloader():
    s = serial.Serial(PORT, 115200, timeout=1)
    s.parity = serial.PARITY_NONE
    s.dtr = False
    s.rts = False
    s.write(b"R")
    s.flush()
    time.sleep(0.2)
    s.close()
    time.sleep(1.5)
    s = serial.Serial(PORT, 115200, timeout=0.5)
    s.parity = serial.PARITY_EVEN
    s.dtr = False
    s.rts = False
    s.reset_input_buffer()
    s.write(b"\x7f")
    time.sleep(0.2)
    print("sync  ->", hexs(s.read(64)))
    return s


def talk(s, payload, label, wait=0.8):
    s.reset_input_buffer()
    s.write(bytes(payload))
    time.sleep(wait)
    print("%-22s %s  =>  %s" % (label, hexs(bytes(payload)), hexs(s.read(300))))


s = enter_bootloader()
talk(s, [0x00, 0xFF], "GET")
talk(s, [0x02, 0xFD], "GET_ID")
talk(s, [0x43, 0xBC], "ERASE cmd")
talk(s, [0xFF, 0x00], "ERASE all (wait 12s)", wait=12.0)
s.close()
