#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
"""The WAVE engine's wavetables (firmware/src/eng_wave.c): Fel-Tracker's own, from formulas in this file.

8 banks of 8 single-cycle frames; WAVE scans between a bank's frames (POS). Each frame is kept as 7 band-limited
copies (mip levels) so high notes do not alias: level l is 512 >> l samples long and holds harmonics 1 .. 64 >> l
(8 samples a harmonic: the images of the linear interpolation stay 30 dB and more under the harmonics). A frame is given as a waveform (sampled at 2048
points, its harmonics taken by DFT) or as a spectrum; every level of a frame has the level-0 copy's gain (peak
0.9 of full scale), so a level change keeps the loudness.
  gen_wavetables.py OUT.h"""
import math
import random
import sys
from operator import mul

N = 2048                        # points a waveform is sampled at, for its DFT
HMAX = 64                       # harmonics of level 0
LEVELS = [(512, 64), (256, 32), (128, 16), (64, 8), (32, 4), (16, 2), (8, 1)]   # (length, harmonics) per mip level
FRAMES = 8


def dft(x):
    """harmonics 1..HMAX of one cycle x (N points): [(a_k cos, b_k sin)]"""
    out = []
    for k in range(1, HMAX + 1):
        c = [math.cos(2 * math.pi * k * n / N) for n in range(N)]
        s = [math.sin(2 * math.pi * k * n / N) for n in range(N)]
        out.append((2 * sum(map(mul, x, c)) / N, 2 * sum(map(mul, x, s)) / N))
    return out


def wave(fn):
    """a frame from a waveform fn(phase 0..1) -> -1..1"""
    return dft([fn(n / N) for n in range(N)])


def spec(amp):
    """a frame from sine harmonics: amp(k) for k = 1..HMAX"""
    return [(0.0, amp(k)) for k in range(1, HMAX + 1)]


def frac(x):
    return x - math.floor(x)


def saw(p):
    return 2 * frac(p) - 1


def pulse(w):
    return lambda p: 1.0 if frac(p) < w else -1.0


def tri(p):
    p = frac(p)
    return 4 * p - 1 if p < 0.5 else 3 - 4 * p


def bank_basic():               # sine -> triangle -> saws -> square -> narrowing pulses
    return [spec(lambda k: 1.0 if k == 1 else 0.0), wave(tri),
            spec(lambda k: 1 / k ** 1.6), wave(saw), wave(pulse(0.5)),
            wave(pulse(0.3)), wave(pulse(0.18)), wave(pulse(0.08))]


def bank_addtv():               # additive: from one partial to many (organ-like, then bright)
    sets = [[1], [1, 2], [1, 2, 3], [1, 2, 3, 4], [1, 3, 5, 7, 9], list(range(1, 9)), list(range(1, 17)),
            list(range(1, 33))]
    return [spec(lambda k, s=s, i=i: (1 / k ** (1.0 if i < 7 else 0.5)) if k in s else 0.0)
            for i, s in enumerate(sets)]


def bank_sync():                # a saw hard-synced to the cycle, its ratio rising (the sync sweep, band-limited)
    return [wave(lambda p, r=r: saw(r * p)) for r in (1, 1.5, 2, 2.5, 3, 4, 5.5, 7)]


def bank_reso():                # a saw through a resonant low-pass whose peak climbs the harmonics
    def frame(hc, q=6.0):
        def amp(k):
            x = k / hc
            return (1 / k) / math.sqrt((1 - x * x) ** 2 + (x / q) ** 2)
        return spec(amp)
    return [frame(hc) for hc in (2, 3, 4, 6, 8, 12, 16, 24)]


def bank_vox():                 # sung vowels: a buzz (harmonics of 110 Hz) through three formants each
    vowels = [(730, 1090, 2440), (530, 1840, 2480), (270, 2290, 3010), (390, 1990, 2550),
              (500, 1350, 1700), (570, 840, 2410), (300, 870, 2240), (660, 1700, 2400)]   # A E I IH ER O U AE

    def frame(fs):
        def amp(k):
            f = 110.0 * k
            g = 0.0
            for i, (fc, bw) in enumerate(zip(fs, (80, 100, 140))):
                g += (0.5 ** i) / (1 + ((f - fc) / bw) ** 2)
            return g / k ** 0.5
        return spec(amp)
    return [frame(v) for v in vowels]


def bank_fm():                  # two-operator FM: ratio, index
    pairs = [(1, 0.5), (1, 1.2), (1, 2.4), (2, 1.0), (2, 2.5), (3, 2.0), (1, 4.0), (4, 3.0)]
    return [wave(lambda p, r=r, i=i: math.sin(2 * math.pi * p + i * math.sin(2 * math.pi * r * p))) for r, i in pairs]


def bank_digi():                # sparse, metallic spectra (a few partials at random heights; fixed seed)
    rnd = random.Random(1987)
    out = []
    for i in range(FRAMES):
        ks = set(rnd.sample(range(1, 9 + 7 * i), 3 + i))
        ks.add(1)
        amps = {k: rnd.uniform(0.3, 1.0) / k ** 0.3 for k in ks}
        phs = {k: rnd.uniform(0, 2 * math.pi) for k in ks}
        out.append([(amps.get(k, 0.0) * math.sin(phs.get(k, 0.0)), amps.get(k, 0.0) * math.cos(phs.get(k, 0.0)))
                    for k in range(1, HMAX + 1)])
    return out


def bank_bits():                # stepped: a sine in fewer and fewer levels, then in fewer steps, then a bit pattern
    def levels(n):
        return lambda p: round(math.sin(2 * math.pi * p) * n) / n

    def steps(n):
        return lambda p: math.sin(2 * math.pi * (math.floor(frac(p) * n) + 0.5) / n)
    bits = [1, 0, 1, 1, 0, 1, 0, 0]
    return [wave(levels(8)), wave(levels(4)), wave(levels(2)), wave(levels(1)), wave(steps(8)), wave(steps(5)),
            wave(steps(3)), wave(lambda p: 1.0 if bits[int(frac(p) * 8)] else -1.0)]


BANKS = [("BASIC", bank_basic), ("ADDTV", bank_addtv), ("SYNC", bank_sync), ("RESO", bank_reso),
         ("VOX", bank_vox), ("FM", bank_fm), ("DIGI", bank_digi), ("BITS", bank_bits)]


def render(h, length, hmax, gain):
    out = []
    for n in range(length):
        x = 2 * math.pi * n / length
        v = sum(a * math.cos(k * x) + b * math.sin(k * x) for k, (a, b) in enumerate(h[:hmax], 1))
        out.append(max(-32767, min(32767, round(v * gain))))
    return out


def main(path):
    data, names = [], []
    for name, fn in BANKS:
        frames = fn()
        assert len(frames) == FRAMES, name
        names.append(name)
        for h in frames:
            top = render(h, LEVELS[0][0], LEVELS[0][1], 1.0)
            peak = max(1, max(abs(v) for v in top))
            gain = 0.9 * 32767 / peak
            for length, hmax in LEVELS:
                data += render(h, length, hmax, gain)
    flen = sum(n for n, _ in LEVELS)
    lines = ["/* generated by tools/gen_wavetables.py: do not edit */",
             f"#define WT_NBANK {len(BANKS)}",
             f"#define WT_NFRAME {FRAMES}",
             f"#define WT_FRAME_LEN {flen}   /* the {len(LEVELS)} mip levels of a frame, longest first: " +
             ", ".join(f"{n} samples / {h} harmonics" for n, h in LEVELS) + " */",
             "#define WT_BANK_NAMES_INIT " + ", ".join(f'"{n}"' for n in names),
             f"static const int16_t WT_DATA[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(str(v) for v in data[i:i + 16]) + ",")
    lines.append("};")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wavetables: {len(BANKS)} banks x {FRAMES} frames, {2 * len(data)} B")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "felucca_wavetables.h")
