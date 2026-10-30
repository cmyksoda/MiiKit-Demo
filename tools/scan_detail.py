#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Counts broken close-up frames: a drawn Mii covers a big part of the portrait
disc, while a missing or smeared one (thin stripes) covers almost none of it.

usage: scan_detail.py FRAMES_DIR [FIRST_FRAME]
"""
import os
import re
import sys

import numpy as np
from PIL import Image

d = sys.argv[1]
first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
files = sorted(os.listdir(d), key=lambda f: int(re.findall(r"\d+", f)[-1]))[first:]
cover = []
for f in files:
    a = np.asarray(Image.open(os.path.join(d, f)).convert("RGB"), dtype=np.int16)
    h, w = a.shape[:2]
    disc = a[int(h * 140 / 480):int(h * 330 / 480), int(w * 70 / 640):int(w * 240 / 640)]
    cover.append((np.abs(disc - disc[4, 4]).max(2) > 30).mean())
cover = np.array(cover)
bad = cover < 0.15
print(f"{len(files)} frames: {bad.sum()} broken ({100 * bad.mean():.1f}%); median cover {np.median(cover):.2f}")
