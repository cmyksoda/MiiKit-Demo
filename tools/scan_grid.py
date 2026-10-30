#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Counts glitched frames of the line-up in a Dolphin frame dump: the Mii area
suddenly much darker (smeared parts) or much emptier (Miis gone) than usual.

usage: scan_grid.py FRAMES_DIR [FIRST_FRAME]
"""
import os
import re
import sys

import numpy as np
from PIL import Image

d = sys.argv[1]
first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
files = sorted(os.listdir(d), key=lambda f: int(re.findall(r"\d+", f)[-1]))[first:]
dark, filled = [], []
for f in files:
    a = np.asarray(Image.open(os.path.join(d, f)).convert("RGB"), dtype=np.int16)
    h, w = a.shape[:2]
    area = a[int(h * 78 / 480):int(h * 434 / 480), int(w * 40 / 640):int(w * 600 / 640)]
    lum = area.mean(2)
    dark.append((lum < 60).mean())
    filled.append((np.abs(area - np.array([250, 250, 251])).max(2) > 14).mean())
dark, filled = np.array(dark), np.array(filled)
md, mf = np.median(dark), np.median(filled)
bad = (dark > 3 * md + 0.01) | (filled < 0.5 * mf)
print(f"{len(files)} frames: {bad.sum()} glitched ({100 * bad.mean():.1f}%); median dark {md:.3f}, filled {mf:.3f}")
runs = [i for i in range(len(bad)) if bad[i] and (i == 0 or not bad[i - 1])]
print("glitch stretches start at frames:", [first + i for i in runs[:20]])
