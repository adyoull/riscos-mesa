#!/usr/bin/env python3
"""png2sprite.py - PNGs into a RISC OS sprite file (from riscos-gtk, unchanged apart from this line).

    png2sprite.py [--dpi 180] OUT,ff9 NAME=FILE.png [NAME=FILE.png ...]

Each sprite is 32bpp (&BBGGRR words) at 90 x 90 dpi, or 180 x 180 with
--dpi 180 (for a !Sprites11 file; a new-format mode word, RISC OS 3.5+;
32bpp sprites need RISC OS 3.5 or later) with a
1bpp mask from the PNG's alpha (opaque where alpha > 127). Names are
lower-cased, at most 12 characters. MIT licence.
"""
import struct
import sys
from PIL import Image


def sprite(name, path, dpi=90):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    mwords = (w + 31) // 32
    img = bytearray()
    mask = bytearray()
    px = im.load()
    for y in range(h):
        mrow = bytearray(mwords * 4)
        for x in range(w):
            r, g, b, a = px[x, y]
            img += struct.pack("<I", r | (g << 8) | (b << 16))
            if a > 127:
                mrow[x >> 3] |= 1 << (x & 7)
        mask += mrow
    name = name.lower()[:12].encode("latin-1")
    hdr_size = 44
    size = hdr_size + len(img) + len(mask)
    mode = (6 << 27) | (dpi << 14) | (dpi << 1) | 1
    hdr = struct.pack("<i12siiiiiii", size, name.ljust(12, b"\0"), w - 1, h - 1, 0, 31,
                      hdr_size, hdr_size + len(img), mode)
    return hdr + bytes(img) + bytes(mask)


def main():
    args = sys.argv[1:]
    dpi = 90
    if args[:1] == ["--dpi"]:
        dpi = int(args[1])
        args = args[2:]
    out = args[0]
    sprites = [sprite(*a.split("=", 1), dpi=dpi) for a in args[1:]]
    body = b"".join(sprites)
    # a sprite file is a sprite area without its first word
    area = struct.pack("<iii", len(sprites), 16, 16 + len(body))
    with open(out, "wb") as f:
        f.write(area + body)


if __name__ == "__main__":
    main()
