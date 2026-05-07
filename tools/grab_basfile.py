#!/usr/bin/env python3
"""One-off: dump B:server.bas (or other file) over CDC by typing TYPE."""
import os, sys, time, termios, glob, select

PROBE = "/dev/cu.usbmodem102"
ports = [p for p in sorted(glob.glob("/dev/cu.usbmodem*")) if p != PROBE]
if not ports:
    sys.exit("no target port")
path = ports[0]

fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
attrs = termios.tcgetattr(fd)
cflag = (attrs[2] & ~termios.CSIZE) | termios.CS8 | termios.CREAD | termios.CLOCAL
attrs[2] = cflag; attrs[6][termios.VMIN] = 0; attrs[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, attrs)

def drain(t):
    deadline = time.time() + t
    out = bytearray()
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            out.extend(os.read(fd, 4096))
    return bytes(out)

# Send Ctrl-C, newline, then TYPE
os.write(fd, b"\x03\r\n")
drain(0.5)
fname = sys.argv[1] if len(sys.argv) > 1 else "B:server.bas"
os.write(fd, f'LOAD "{fname}":LIST ALL\r\n'.encode())
data = drain(8.0)
sys.stdout.buffer.write(data)
os.close(fd)
