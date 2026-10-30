#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rasterizes FOT-Rodin into GX I4 glyph pages for source/font.c.

usage: make_font.py FONT.otf SIZE OUT.bin

Only the rendered glyphs ship, never the font file. Every glyph but the
ideographs ships, so any name a Wii keyboard can type draws.
"""

import os
import struct
import sys
import tempfile

from fontTools.pens.boundsPen import BoundsPen
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

# Everything the font has but the ideographs and the radicals: Mii names come
# from the Wii's keyboards, which type none of those.
SKIP = [(0x2E80, 0x2FDF), (0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xF900, 0xFAFF), (0x20000, 0x10FFFF)]
PAGE = 512
# Rodin sets Greek, Cyrillic and most symbols full-width, as kana; its pwid
# alternates are proportional, as those scripts are meant to be.
PROPORTIONAL = ["pwid"]


def japanese(c):
    return 0x3000 <= c <= 0x33FF or 0xFF00 <= c <= 0xFFEF
PAD = 1


def with_latin_zones(path):
    """A temporary copy of the font whose Latin hints know its x-height and cap height.

    Rodin's have zones only for the baseline and the kanji's top, so at these sizes round
    letters stand a pixel above flat ones; with the zones, FreeType's hinter lines them up.
    """
    tt = TTFont(path)
    top = tt["CFF "].cff.topDictIndex[0]
    cmap, glyphs = tt.getBestCmap(), top.CharStrings

    def height(ch):
        pen = BoundsPen(glyphs)
        glyphs[cmap[ord(ch)]].draw(pen)
        return pen.bounds[3]

    order = {name: i for i, name in enumerate(top.charset)}
    private = top.FDArray[top.FDSelect[order[cmap[ord("x")]]]].Private
    private.BlueValues = sorted(private.BlueValues + [height("x"), height("o"), height("H"), height("O")])
    fd, copy = tempfile.mkstemp(suffix=".otf")
    os.close(fd)
    tt.save(copy)
    return copy


def i4_tiles(img):
    """GX I4: 8x8 blocks of 4-bit texels, two to a byte, the left one high."""
    w, h = img.size
    px = img.load()
    out = bytearray()
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for y in range(by, by + 8):
                for x in range(bx, bx + 8, 2):
                    out.append((px[x, y] >> 4) << 4 | px[x + 1, y] >> 4)
    return bytes(out)


def main():
    path, size, out_path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    tt = TTFont(path, lazy=True)
    cmap, hmtx = tt.getBestCmap(), tt["hmtx"]
    zoned = with_latin_zones(path)
    font = ImageFont.truetype(zoned, size)
    ascent, descent = font.getmetrics()
    chars = sorted(c for c in cmap if c >= 0x20 and not any(a <= c <= b for a, b in SKIP))

    pages = [Image.new("L", (PAGE, PAGE))]
    glyphs = []
    x = y = row_h = 0
    for c in chars:
        ch = chr(c)
        feat = PROPORTIONAL if hmtx[cmap[c]][0] >= 1000 and not japanese(c) else None
        advance = font.getlength(ch, features=feat)
        # The ink, from the rendered mask: getbbox gives the em box for some glyphs.
        mask, (ox, oy) = font.getmask2(ch, anchor="ls", features=feat)
        ink = mask.getbbox()
        x0, y0, x1, y1 = (ox + ink[0], oy + ink[1], ox + ink[2], oy + ink[3]) if ink else (0, 0, 0, 0)
        w, h = x1 - x0, y1 - y0
        draw_x = x0
        if feat and w and advance >= size * 0.95:
            # No alternate either: the ink with a small bearing each side, as palt would set it.
            bearing = round(size * 0.07)
            draw_x, advance = bearing, w + 2 * bearing
        if w and h:
            if x + w + 2 * PAD > PAGE:
                x, y, row_h = 0, y + row_h, 0
            if y + h + 2 * PAD > PAGE:
                pages.append(Image.new("L", (PAGE, PAGE)))
                x = y = row_h = 0
            ImageDraw.Draw(pages[-1]).text((x + PAD - x0, y + PAD - y0), ch, font=font, fill=255, anchor="ls",
                                           features=feat)
            u, v = x + PAD, y + PAD
            x += w + 2 * PAD
            row_h = max(row_h, h + 2 * PAD)
        else:
            u = v = 0
        glyphs.append(struct.pack(">IhhHHHHHH", c, draw_x, y0, w, h, round(advance * 64), len(pages) - 1, u, v))

    with open(out_path, "wb") as f:
        f.write(struct.pack(">4sHHhhHHI", b"RFNT", size, ascent + descent, ascent, descent,
                            len(pages), PAGE, len(glyphs)))
        f.write(b"".join(glyphs))
        f.write(bytes(-f.tell() % 32))
        for p in pages:
            f.write(i4_tiles(p))
    os.unlink(zoned)
    print(f"{out_path}: {len(glyphs)} glyphs, {len(pages)} pages of {PAGE} px")


if __name__ == "__main__":
    main()
