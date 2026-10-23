#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
"""(2026-10-19) ONYX's voice, step 2 of 2: Kokoro's WAVs (am_onyx and the other voices) (tools/gen_onyx_render.py) analysed into LPC frames,
as the speech chips of the late 70s kept speech (the TMS5220's economy: about 250 bytes a second), into
firmware/baseline/onyx_lpc.h (committed: the build never runs Kokoro). Each phrase: resampled to 8820 Hz (the lattice's
rate, as SPEECH's), its silences trimmed, pre-emphasised, cut in frames of 25 ms; a frame's ten reflection
coefficients (autocorrelation, Levinson; the sign the firmware's lattice wants: checked against a lattice built here as
eng_onyx.c's), its loudness (the prediction error's), its pitch (autocorrelation of the low band; 0: unvoiced).
Quantised and packed in bits, MSB first, frame after frame:
  E 4 bits (0: silence, the frame ends there) | R 1 (repeat: the previous frame's filter) | P 6 (0: unvoiced) |
  (not R) K1 5, K2 5, K3 4, K4 4 | (voiced, not R) K5 4, K6 4, K7 4, K8 3, K9 3, K10 3
The tables (the levels of K's 5, 4 and 3 bits, E's gains, P's periods) are written with it.
  gen_onyx_lpc.py OUT.h WAV_DIR..   (a directory a voice: tools/gen_onyx_render.py's, the same phrases)"""
import json
import math
import sys
import wave
from pathlib import Path

import numpy as np

FS = 8820
N = 220                          # a frame: 25 ms
P = 10
KBITS = [5, 5, 4, 4, 4, 4, 4, 3, 3, 3]
PMIN, PMAX = 18, 170             # the periods P's 63 levels span (filter samples: 490 .. 52 Hz)


def read(path):
    with wave.open(str(path)) as w:
        a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
        sr = w.getframerate()
    n = int(round(len(a) * FS / sr))
    spec = np.fft.rfft(a)
    out = np.fft.irfft(spec[: n // 2 + 1], n) * (n / len(a))     # (FFT resampling: everything over 4.41 kHz gone)
    return out


def levinson(r):
    """-> reflection coefficients k[1..P] (the lattice's sign: synth below), the error"""
    a = np.zeros(P + 1)
    a[0] = 1.0
    e = r[0]
    k = np.zeros(P)
    for i in range(1, P + 1):
        acc = r[i] + sum(a[j] * r[i - j] for j in range(1, i))
        ki = -acc / e if e > 0 else 0.0
        k[i - 1] = ki
        a_new = a.copy()
        for j in range(1, i):
            a_new[j] = a[j] + ki * a[i - j]
        a_new[i] = ki
        a = a_new
        e *= 1 - ki * ki
    return k, e


def lattice(k, x):
    """the firmware's lattice (eng_onyx.c, as SPEECH's): f -= k[j] g[j]; g[j+1] = g[j] + k[j] f, top stage down"""
    g = np.zeros(P)
    y = np.zeros(len(x))
    for n, e in enumerate(x):
        f = e
        for j in range(P - 1, -1, -1):
            f -= k[j] * g[j]
            if j + 1 < P:
                g[j + 1] = g[j] + k[j] * f
        g[0] = f
        y[n] = f
    return y


SIGN = 1.0                       # (found by sign_check: the lattice wants Levinson's k times this)


def sign_check():
    """white noise through a known stable lattice, analysed back: which sign of Levinson's k is the lattice's"""
    global SIGN
    rng = np.random.default_rng(7)
    k0 = np.array([.8, -.5, .3, -.2, .1, 0, 0, 0, 0, 0])
    y = lattice(k0, rng.standard_normal(20000))
    r = np.array([np.dot(y[: len(y) - i], y[i:]) for i in range(P + 1)])
    k, _ = levinson(r)
    if np.abs(k - k0).max() < .05:
        SIGN = 1.0
    elif np.abs(-k - k0).max() < .05:
        SIGN = -1.0
    else:
        raise SystemExit(f"onyx: the lattice and Levinson disagree: {k0} vs {k}")


def klevels(bits):
    n = 1 << bits
    return [math.sin(math.pi / 2 * .97 * (-1 + 2 * i / (n - 1))) for i in range(n)]


KL = {b: klevels(b) for b in (3, 4, 5)}
PL = [0] + [PMIN * (PMAX / PMIN) ** (i / 62) for i in range(63)]      # P's periods (0: unvoiced)


def analyse(x):
    """-> [(gain, period or 0, k[P])] a frame"""
    pre = np.append(x[0], x[1:] - .9375 * x[:-1])
    lp = np.convolve(x, np.ones(9) / 9, mode="same")                  # (the low band, for the pitch)
    win = np.hamming(2 * N)
    frames = []
    for s in range(0, len(x) - N, N):
        seg = pre[max(0, s - N // 2): s + N + N // 2]
        if len(seg) < 2 * N:
            seg = np.pad(seg, (0, 2 * N - len(seg)))
        seg = seg * win
        r = np.array([np.dot(seg[: len(seg) - i], seg[i:]) for i in range(P + 1)])
        r[0] *= 1.0001
        r[1:] *= np.exp(-.5 * (2 * math.pi * 60 / FS * np.arange(1, P + 1)) ** 2)   # (a lag window: 60 Hz)
        if r[0] < 1e-9:
            frames.append((0.0, 0, np.zeros(P)))
            continue
        k, err = levinson(r)
        k = np.clip(k * SIGN, -.97, .97)
        gain = math.sqrt(max(err, 0) / (2 * N * .4))                 # (the error per sample; the window's power)
        z = lp[s: s + 2 * N] if s + 2 * N <= len(lp) else np.pad(lp[s:], (0, s + 2 * N - len(lp)))
        z = z - z.mean()
        c0 = np.dot(z, z)
        best, per = 0.0, 0
        for lag in range(PMIN, PMAX + 1):
            c = np.dot(z[:-lag], z[lag:]) / (c0 + 1e-12)
            if c > best:
                best, per = c, lag
        frames.append((gain, per if best > .38 else 0, k))
    return frames


def trim(frames, gmax):
    on = [i for i, f in enumerate(frames) if f[0] > gmax * 10 ** (-40 / 20)]
    if not on:
        return frames[:1]
    return frames[max(0, on[0] - 1): on[-1] + 2]


class Bits:
    def __init__(self):
        self.b, self.n = [], 0

    def put(self, v, bits):
        for i in range(bits - 1, -1, -1):
            if self.n % 8 == 0:
                self.b.append(0)
            self.b[-1] |= ((v >> i) & 1) << (7 - self.n % 8)
            self.n += 1


def q(v, levels):
    return int(np.argmin([abs(v - l) for l in levels]))


def sing_end(fr):
    """(SING) the held vowels: the frames up to the last one within 6 dB of the loudest (the steady middle's end: HOLD
    sustains it, not the fade)"""
    g = max(f[0] for f in fr)
    return max(i for i, f in enumerate(fr) if f[0] >= g * 10 ** (-6 / 20)) + 1


def encode(bits, fr, EL):
    """a phrase's frames into the stream (byte-aligned start) -> (start byte, frames)"""
    if bits.n % 8:
        bits.put(0, 8 - bits.n % 8)
    start, nf, prev = bits.n // 8, 0, None
    pers = [f[1] for f in fr]
    pers = [int(np.median([pers[max(0, i - 1)], pers[i], pers[min(len(pers) - 1, i + 1)]])) for i in range(len(pers))]
    for (g, _, k), per in zip(fr, pers):
        e = q(g, EL) if g > EL[1] / 2 else 0
        nf += 1
        bits.put(e, 4)
        if not e:
            prev = None
            continue
        ki = [q(k[j], KL[KBITS[j]]) for j in range(P)]
        voiced = per > 0
        rep = prev is not None and prev[1] == voiced and sum(abs(a - b) for a, b in zip(ki[:4], prev[0][:4])) <= 1
        bits.put(int(rep), 1)
        bits.put(q(per, PL[1:]) + 1 if voiced else 0, 6)
        if rep:
            continue
        for j in range(4):
            bits.put(ki[j], KBITS[j])
        if voiced:
            for j in range(4, P):
                bits.put(ki[j], KBITS[j])
        prev = (ki, voiced)
    return start, nf


def main(out, wav_dirs):
    """(2026-10-19) every voice's directory (the same phrases, each its own index.json with its "voice"): one stream,
    each voice's phrases after the last's; the levels each voice's own (its loudest phrase E's top level, so the
    voices speak equally loud: E's table the same for all); a voice's reference period the median of its voiced
    frames (the firmware's WORDS pitch, TONE's C3)"""
    sign_check()
    bits, voices, index = Bits(), [], None
    for d in map(Path, wav_dirs):
        ix = json.loads((d / "index.json").read_text())
        if index is not None and [(p["bank"], p["name"]) for p in ix] != [(p["bank"], p["name"]) for p in index]:
            raise SystemExit(f"onyx: {d}: not the same phrases as {wav_dirs[0]}")
        index = ix
        all_frames = [analyse(read(d / p["file"])) for p in ix]
        gmax = max(f[0] for fr in all_frames for f in fr)
        EL = [0] + [gmax * 10 ** (-(15 - i) * 2.5 / 20) for i in range(1, 16)]   # E's gains: 2.5 dB steps under the loudest
        phr, vper = [], []
        for p, fr in zip(ix, all_frames):
            fr = trim(fr, gmax)
            if p["bank"] == "SING":
                fr = fr[:sing_end(fr)]
            vper += [f[1] for f in fr if f[1] and f[0] > gmax * .05]
            phr.append(encode(bits, fr, EL))
        name = ix[0].get("voice", d.name.upper())
        voices.append((name, phr, float(np.median(vper)), sum(n for _, n in phr) * N / FS))
        print(f"onyx: {name}: {len(phr)} phrases, {voices[-1][3]:.1f} s, its pitch {FS / voices[-1][2]:.0f} Hz")
    banks = []
    for p in index:
        if not banks or banks[-1][0] != p["bank"]:
            banks.append([p["bank"], 0])
        banks[-1][1] += 1

    def short(name):
        n = name.upper().replace("-", " ")
        if len(n) <= 12:
            return n
        cut = n[:12].rsplit(" ", 1)[0]
        return cut if len(cut) >= 6 else n[:12]
    nv = len(voices)
    lines = ["/* generated by tools/gen_onyx_lpc.py from Kokoro TTS's voices (tools/gen_onyx_render.py): do not edit */",
             "#pragma once", f"#define ONYX_FS {FS}", f"#define ONYX_N {N}", f"#define ONYX_P {P}",
             f"#define ONYX_NBANK {len(banks)}", f"#define ONYX_NPHRASE {len(index)}", f"#define ONYX_NVOICE {nv}",
             "static const int16_t ONYX_K5[32] = {" + ", ".join(str(round(v * 32767)) for v in KL[5]) + "};",
             "static const int16_t ONYX_K4[16] = {" + ", ".join(str(round(v * 32767)) for v in KL[4]) + "};",
             "static const int16_t ONYX_K3[8] = {" + ", ".join(str(round(v * 32767)) for v in KL[3]) + "};",
             "static const uint16_t ONYX_E[16] = {" + ", ".join(str(round(20000 * 10 ** (-(15 - i) * 2.5 / 20)) if i else 0)
                                                           for i in range(16)) + "};",
             "static const uint8_t ONYX_PER[64] = {" + ", ".join(str(round(v)) for v in PL) + "};",
             "static const char *const ONYX_VOICE_NAMES[ONYX_NVOICE + 1] = {" + ", ".join(f'"{v[0]}"' for v in voices) + ", 0};",
             "static const float ONYX_REF[ONYX_NVOICE] = {" + ", ".join(f"{v[2]:.2f}f" for v in voices) +
             "};   /* each voice's own period (filter samples) */",
             "static const uint32_t ONYX_START[ONYX_NVOICE][ONYX_NPHRASE] = {" +
             ", ".join("{" + ", ".join(str(s) for s, _ in v[1]) + "}" for v in voices) + "};",
             "static const uint16_t ONYX_FRAMES[ONYX_NVOICE][ONYX_NPHRASE] = {" +
             ", ".join("{" + ", ".join(str(n) for _, n in v[1]) + "}" for v in voices) + "};",
             "static const uint8_t ONYX_BANK_N[ONYX_NBANK] = {" + ", ".join(str(n) for _, n in banks) + "};",
             "static const char *const ONYX_BANK_NAMES[ONYX_NBANK + 1] = {" + ", ".join(f'"{b}"' for b, _ in banks) + ", 0};"]
    first = 0
    for bi, (b, n) in enumerate(banks):
        names = [short(p["name"]) for p in index[first: first + n]]
        lines.append(f"static const char *const ONYX_NAMES_{bi}[{n + 1}] = {{" + ", ".join(f'"{x}"' for x in names) + ", 0};")
        first += n
    lines.append("#define ONYX_NAMES_ALL " + ", ".join(f"ONYX_NAMES_{i}" for i in range(len(banks))))
    lines.append("static const uint16_t ONYX_BANK_FIRST[ONYX_NBANK] = {" +
                 ", ".join(str(sum(n for _, n in banks[:i])) for i in range(len(banks))) + "};")
    lines.append(f"static const uint8_t ONYX_BITS[{len(bits.b)}] = {{")
    for i in range(0, len(bits.b), 24):
        lines.append("    " + ", ".join(str(v) for v in bits.b[i:i + 24]) + ",")
    lines.append("};")
    Path(out).write_text("\n".join(lines) + "\n")
    secs = sum(v[3] for v in voices)
    print(f"onyx: {len(index)} phrases in {len(banks)} banks, {nv} voices, {secs:.1f} s of frames, {len(bits.b)} B "
          f"({len(bits.b) / secs:.0f} B a second; the lattice's sign {SIGN:+.0f}) -> {out}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
