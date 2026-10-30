#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks the music in a Dolphin audio dump against the MP3 it came from.

usage: check_loop.py MUSIC.mp3 DUMP.wav

The reference is the MP3 decoded by ffmpeg, cut as music.c cuts it (LOOP_START,
LOOP_SAMPLES), looped twice, and resampled to the dump's rate. Reports where the
music starts in the dump, how closely each stretch matches, any dropouts, and
the seam where the loop restarts.
"""

import subprocess
import sys

import numpy as np
from scipy.signal import correlate, resample_poly

LOOP_START, LOOP_SAMPLES = 1105, 96 * 44100


def decode(path, rate):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "s16le", "-ac", "2", "-ar", str(rate), "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.int16).reshape(-1, 2).astype(np.float32) / 32768


def main():
    mp3, dump_path = sys.argv[1:3]
    dump = decode(dump_path, 48000)
    ref44 = decode(mp3, 44100)[LOOP_START:LOOP_START + LOOP_SAMPLES]
    ref44[-128:] *= np.linspace(1, 0, 128, endpoint=False)[:, None]
    ref = resample_poly(np.concatenate([ref44, ref44, ref44]), 160, 147, axis=0)
    loop48 = LOOP_SAMPLES * 160 / 147

    # Where the music starts: correlate a few seconds of mono.
    m_dump, m_ref = dump.mean(1), ref.mean(1)
    head = m_ref[:48000 * 4]
    search = m_dump[:48000 * 20]
    c = correlate(search, head, mode="valid", method="fft")
    start = int(np.argmax(c))
    print(f"music starts {start / 48000:.3f} s into the dump")

    seg = dump[start:]
    n = min(len(seg), len(ref))
    seg, r = seg[:n], ref[:n]
    # The gain ASND applied (volume 150 of 255), fitted once.
    gain = float((seg * r).sum() / (r * r).sum())
    err = seg - gain * r
    print(f"gain {gain:.3f}; match over {n / 48000:.1f} s: SNR {10 * np.log10((gain * r).var() / err.var()):.1f} dB")

    win = 4800
    worst = []
    for i in range(0, n - win, win):
        e = np.sqrt((err[i:i + win] ** 2).mean())
        s = np.sqrt(((gain * r[i:i + win]) ** 2).mean()) + 1e-9
        d = np.sqrt((seg[i:i + win] ** 2).mean())
        worst.append((e / s, i / 48000, d, s))
    worst.sort(reverse=True)
    print("worst 100 ms windows (error/signal, at s, dump rms, ref rms):")
    for w in worst[:6]:
        print(f"  {w[0]:.3f} at {w[1]:7.2f}  dump {w[2]:.4f}  ref {w[3]:.4f}")
    drop = [w for w in worst if w[2] < 0.1 * w[3] and w[3] > 0.01]
    print(f"dropouts (dump under a tenth of the music): {len(drop)}")
    seam = int(loop48)
    if seam + 4800 < n:
        a, b = seam - 2400, seam + 2400
        e = np.sqrt((err[a:b] ** 2).mean())
        s = np.sqrt(((gain * r[a:b]) ** 2).mean())
        print(f"seam at {(start + seam) / 48000:.3f} s in the dump: error/signal {e / s:.3f} over the 100 ms around it")
        jump = np.abs(np.diff(seg[seam - 50:seam + 50, 0])).max()
        typical = np.abs(np.diff(seg[seam - 4800:seam - 100, 0])).max()
        print(f"largest sample step at the seam {jump:.4f}, largest in the 0.1 s before {typical:.4f}")
    clip = (np.abs(dump) > 0.999).sum()
    print(f"clipped samples: {clip}")


if __name__ == "__main__":
    main()
