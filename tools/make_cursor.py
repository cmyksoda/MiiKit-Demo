#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns drmr's Wii Homebrew Cursors (assets/cursor, see licenses/) into the
GX RGBA8 texture gfx.c draws: the generic pointing hand over its shadow.

usage: make_cursor.py OUT.bin [PREVIEW.png]
"""

import pathlib
import struct
import sys

from PIL import Image

ASSETS = pathlib.Path(__file__).resolve().parents[1] / "assets" / "cursor"
TIP = (48, 42)          # the fingertip in the 96x96 originals
SHADOW_AT = (3, 4)


def hand():
    point = Image.open(ASSETS / "generic_point.png").convert("RGBA")
    shadow = Image.open(ASSETS / "shadow_point.png").convert("RGBA")
    img = Image.new("RGBA", point.size, (0, 0, 0, 0))
    img.paste(shadow, SHADOW_AT, shadow)
    img.alpha_composite(point)
    x0, y0, x1, y1 = img.getchannel("A").getbbox()
    x0, y0 = x0 - 1, y0 - 1
    w, h = (x1 - x0 + 4) // 4 * 4, (y1 - y0 + 4) // 4 * 4
    return img.crop((x0, y0, x0 + w, y0 + h)), (TIP[0] - x0, TIP[1] - y0)


def rgba8_tiles(img):
    """GX RGBA8: 4x4 blocks, each 16 AR pairs then 16 GB pairs."""
    w, h = img.size
    px = img.load()
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            ar, gb = bytearray(), bytearray()
            for y in range(by, by + 4):
                for x in range(bx, bx + 4):
                    r, g, b, a = px[x, y]
                    ar += bytes((a, r))
                    gb += bytes((g, b))
            out += ar + gb
    return bytes(out)


def main():
    img, hot = hand()
    with open(sys.argv[1], "wb") as f:
        f.write(struct.pack(">4sHHHH", b"CURS", *img.size, *hot))
        f.write(bytes(32 - 12))
        f.write(rgba8_tiles(img))
    if len(sys.argv) > 2:
        preview = Image.new("RGBA", img.size, (250, 250, 251, 255))
        preview.alpha_composite(img)
        preview.resize((img.width * 4, img.height * 4), Image.NEAREST).save(sys.argv[2])


if __name__ == "__main__":
    main()
