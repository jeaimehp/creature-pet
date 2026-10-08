"""Pixel-exact screenshots and animations from the Himop board over serial.

The firmware answers "shot" and "rec N" with the raw 480x480 canvas
(byte-swapped RGB565) at 1 Mbaud in 1 KB chunks with
Adler-32 checks and resends. "act NAME" forces a behavior first.

  python tools/capture.py PORT shot OUT.png [act NAME] [wait SECONDS] [until TEXT] [tap X Y]
  python tools/capture.py PORT rec N OUT.gif [act NAME] [wait SECONDS] [tap X Y] [every K] [size PX]

rec captures N frames, one every K ticks of the board's 33 ms clock (default 3,
so 10 fps), and writes a GIF at SIZE pixels square (default 360).

Opening the port resets the board, so the script waits for it to boot first.
Needs pyserial, numpy, and Pillow (pip install --user pyserial):
  python3 tools/capture.py /dev/tty.usbserial-12430 shot out.png
"""
import sys
import time
import zlib

import numpy as np
import serial
from PIL import Image

W = H = 480
FRAME = W * H * 2
DUMP_BAUD = 1_000_000
CHUNK = 1024


def read_line(port, deadline):
    buf = b""
    while time.time() < deadline:
        ch = port.read(1)
        if not ch:
            continue
        if ch == b"\n":
            return buf.decode(errors="replace").strip()
        buf += ch
    return None


def read_frame(port):
    deadline = time.time() + 30
    while True:
        line = read_line(port, deadline)
        if line is None:
            raise TimeoutError("no SHOT header")
        if line.startswith("SHOT"):
            break
    port.baudrate = DUMP_BAUD
    data = b""
    while len(data) < FRAME:
        want = min(CHUNK, FRAME - len(data)) + 4
        got = b""
        end = time.time() + 0.5
        while len(got) < want and time.time() < end:
            got += port.read(want - len(got))
        if len(got) == want and zlib.adler32(got[:-4]) == int.from_bytes(got[-4:], "little"):
            data += got[:-4]
            port.write(b"A")
        else:
            time.sleep(0.05)
            port.reset_input_buffer()
            port.write(b"R")
    time.sleep(0.05)
    port.baudrate = 115200
    px = np.frombuffer(data, dtype=">u2").reshape(H, W).astype(np.uint32)
    r = ((px >> 11) & 31) * 255 // 31
    g = ((px >> 5) & 63) * 255 // 63
    b = (px & 31) * 255 // 31
    return Image.fromarray(np.dstack([r, g, b]).astype(np.uint8))


def main(argv):
    port_name, mode = argv[1], argv[2]
    if mode == "shot":
        count, out, rest = 1, argv[3], argv[4:]
    else:
        count, out, rest = int(argv[3]), argv[4], argv[5:]
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = port_name, 115200, 0.2
    port.dtr = port.rts = False
    port.open()
    time.sleep(15)  # boot, SD load, Wi-Fi
    port.reset_input_buffer()

    every = 3
    size = 360
    i = 0
    while i < len(rest):
        if rest[i] == "act":
            port.reset_input_buffer()  # so "until" only sees lines after this
            port.write(f"act {rest[i + 1]}\n".encode())
            i += 2
        elif rest[i] == "wait":
            time.sleep(float(rest[i + 1]))
            i += 2
        elif rest[i] == "until":
            # Wait (up to 60 s) for a log line containing TEXT, e.g. a caption.
            deadline = time.time() + 60
            while True:
                line = read_line(port, deadline)
                if line is None:
                    raise SystemExit(f"timed out waiting for {rest[i + 1]!r}")
                if rest[i + 1] in line:
                    break
            i += 2
        elif rest[i] == "tap":
            port.write(f"tap {rest[i + 1]} {rest[i + 2]}\n".encode())
            i += 3
        elif rest[i] == "every":
            every = int(rest[i + 1])
            i += 2
        elif rest[i] == "size":
            size = int(rest[i + 1])
            i += 2
        else:
            raise SystemExit(f"unknown option {rest[i]}")
    port.reset_input_buffer()

    if mode == "shot":
        port.write(b"shot\n")
        read_frame(port).save(out, optimize=True)
    else:
        port.write(f"rec {count} {every}\n".encode())
        frames = [read_frame(port) for _ in range(count)]
        if size != W:
            frames = [f.resize((size, size), Image.LANCZOS) for f in frames]
        pal = [f.convert("P", palette=Image.ADAPTIVE, colors=255) for f in frames]
        pal[0].save(out, save_all=True, append_images=pal[1:], duration=33 * every, loop=0, optimize=True)
    print("saved", out)


if __name__ == "__main__":
    main(sys.argv)
