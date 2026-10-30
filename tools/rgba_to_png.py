#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns the tour's .rgba screenshots ("W H 0 0" line, then RGBA rows) into PNGs.

usage: rgba_to_png.py OUT_DIR FILE.rgba...
"""
import pathlib
import sys

from PIL import Image

out = pathlib.Path(sys.argv[1])
for path in map(pathlib.Path, sys.argv[2:]):
    data = path.read_bytes()
    nl = data.index(b"\n")
    w, h = map(int, data[:nl].split()[:2])
    Image.frombytes("RGBA", (w, h), data[nl + 1:nl + 1 + w * h * 4]).convert("RGB").save(out / (path.stem + ".png"))
