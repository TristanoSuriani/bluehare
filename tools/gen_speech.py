#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
"""The SPEECH engine's data (firmware/src/eng_speech.c): its words, from the vocabulary files in tools/speech/ (a word:
its name and its phonemes with the stress), and the filter of every sound they use.

SPEECH speaks the way the LPC speech chips of the late 70s did: a buzz (the voice) or a hiss (the breath) through a
10-pole lattice filter at 8820 Hz whose reflection coefficients glide from sound to sound. There are no recordings: a
phoneme is a set of formants (5 resonances: frequency, bandwidth) with a voice and a noise level, turned here into the
filter's coefficients (the polynomial of the 5 resonances, stepped down to reflection coefficients) and its gains
(the voice's and the noise's, so every sound comes out at about the same level). A word is a list of segments: a sound,
how long it lasts, how long it takes to get there from the one before, the pitch (stress and the fall across the
word, in quarter semitones, the primary stress on the key's pitch). Stops become a closure, a burst and (unvoiced) an aspiration; diphthongs two vowels.
Phonemes: ARPAbet's (IY IH EH AE AA AO AH UH UW ER AX AY AW OY EY OW, P B T D K G F V TH DH S Z SH ZH HH CH JH M N NG
L R W Y), and a few more the sounds below still know (UE OE EE OO EI UI AU EU OU X C GH RR TS PF: the German and Dutch
banks', gone since). The stress digit follows a vowel: 1 primary, 2 secondary, 0 none. A bank holds 1 .. 32 words.
  gen_speech.py OUT.h"""
import math
import random
import sys
from pathlib import Path

FS = 8820.0                     # the filter's rate (44100 / 5)
P = 10                          # poles
HERE = Path(__file__).resolve().parent / "speech"
BANKS = [("NUM", "num.txt"), ("TECH", "tech.txt"), ("COSMOS", "cosmos.txt"), ("FEEL", "feel.txt"),
         ("DARK", "dark.txt")]   # then PHON
WORDS = 32                      # a bank's most
F45 = [(3300, 250), (3850, 300)]   # the upper resonances of a voiced sound

# ---- the sounds: (F1, F2, F3, B1, B2, B3, voice 0..1, noise 0..1), or 5 resonances for the noisy ones
def v(f1, f2, f3, av=1.0, an=0.0, b=(60, 90, 150)):
    return {"res": [(f1, b[0]), (f2, b[1]), (f3, b[2])] + F45, "av": av, "an": an}


def n(res, an, av=0.0):
    return {"res": res, "av": av, "an": an}


SND = {
    "IY": v(270, 2290, 3010), "IH": v(390, 1990, 2550), "EH": v(530, 1840, 2480), "AE": v(660, 1720, 2410),
    "AA": v(730, 1090, 2440), "AO": v(570, 840, 2410), "AH": v(520, 1190, 2390), "UH": v(440, 1020, 2240),
    "UW": v(300, 870, 2240), "ER": v(490, 1350, 1690), "AX": v(500, 1500, 2500, 0.8), "UE": v(260, 1800, 2150),
    "OE": v(370, 1550, 2300), "EE": v(380, 2300, 2800), "OO": v(400, 750, 2400),
    "W": v(290, 610, 2150, 0.7), "Y": v(260, 2070, 3020, 0.7), "L": v(360, 1300, 2700, 0.7),
    "R": v(330, 1060, 1380, 0.7), "RR": v(450, 1200, 2400, 0.5, 0.25, (100, 150, 200)),
    "M": v(270, 1100, 2200, 0.5, 0.0, (100, 200, 300)), "N": v(270, 1600, 2500, 0.5, 0.0, (100, 200, 300)),
    "NG": v(270, 2000, 2700, 0.5, 0.0, (100, 200, 300)),
    "S": n([(1800, 400), (2800, 400), (3600, 300), (3950, 250), (4250, 300)], 0.6),
    "SH": n([(1800, 200), (2500, 250), (3000, 300), (3600, 350), (4100, 400)], 0.65),
    "F": n([(1200, 600), (2300, 700), (3200, 700), (3800, 700), (4200, 800)], 0.25),
    "TH": n([(1400, 600), (2500, 700), (3300, 700), (3900, 700), (4250, 800)], 0.2),
    "X": n([(1000, 300), (1900, 400), (2800, 500), (3500, 600), (4100, 600)], 0.5),
    "C": n([(2400, 300), (3000, 350), (3600, 400), (3950, 400), (4250, 500)], 0.5),
    "P_B": n([(600, 500), (1500, 600), (2500, 600), (3500, 600), (4100, 700)], 0.5),
    "T_B": n([(2000, 500), (3200, 400), (3700, 400), (4000, 400), (4300, 500)], 0.55),
    "K_B": n([(1500, 300), (2300, 400), (3000, 500), (3600, 500), (4100, 600)], 0.5),
    "VB": v(200, 1100, 2500, 0.15, 0.0, (60, 300, 400)),
    "SIL": v(500, 1500, 2500, 0.0, 0.0),
}
for vo, un, av in (("Z", "S", 0.35), ("ZH", "SH", 0.35), ("V", "F", 0.4), ("DH", "TH", 0.4), ("GH", "X", 0.35)):
    SND[vo] = dict(SND[un], av=av, an=SND[un]["an"] * 0.6)

VOWELS = {"IY", "IH", "EH", "AE", "AA", "AO", "AH", "UH", "UW", "ER", "AX", "UE", "OE", "EE", "OO"}
TENSE = {"IY", "UW", "AA", "AO", "EE", "OO", "UE", "OE"}
DIPH = {"AY": ("AA", "IY"), "AW": ("AA", "UW"), "OY": ("AO", "IY"), "EY": ("EH", "IY"), "OW": ("AO", "UW"),
        "EI": ("EH", "IY"), "UI": ("OE", "UE"), "AU": ("AA", "UH"), "EU": ("AO", "IH"), "OU": ("AO", "UW")}
STOPS = {"P": ("P_B", False), "T": ("T_B", False), "K": ("K_B", False), "B": ("P_B", True), "D": ("T_B", True),
         "G": ("K_B", True)}
AFFR = {"CH": ("T_B", "SH", False), "JH": ("T_B", "ZH", True), "TS": ("T_B", "S", False), "PF": ("P_B", "F", False)}
DUR = {"W": 60, "Y": 55, "L": 65, "R": 65, "RR": 60, "M": 70, "N": 65, "NG": 70, "S": 110, "SH": 110, "F": 100,
       "TH": 90, "X": 100, "C": 100, "Z": 90, "ZH": 90, "V": 70, "DH": 50, "GH": 80, "HH": 60}
PHON = ["AA", "AE", "AH", "AO", "EH", "ER", "IH", "IY", "UH", "UW", "AY", "OW", "AW", "OY", "EY", "M", "N", "NG", "L",
        "R", "W", "Y", "S", "SH", "F", "TH", "Z", "ZH", "V", "DH", "CH", "HH"]


# ---- the filter: resonances -> polynomial -> reflection coefficients; checked against a direct form
def poly(res):
    a = [1.0]
    for f, b in res:
        r = math.exp(-math.pi * b / FS)
        c = 2 * r * math.cos(2 * math.pi * f / FS)
        sec = [1.0, -c, r * r]
        a = [sum(a[i - j] * sec[j] for j in range(3) if 0 <= i - j < len(a)) for i in range(len(a) + 2)]
    return a


def reflect(a):
    a = a[:]
    k = [0.0] * P
    for m in range(P, 0, -1):
        km = a[m]
        if abs(km) >= 1:
            raise SystemExit("gen_speech: an unstable filter")
        k[m - 1] = km
        a = [1.0] + [(a[i] - km * a[m - i]) / (1 - km * km) for i in range(1, m)]
    return k


def lattice(k, x):
    """eng_speech.c's lattice (float): f from the top stage down, g the delayed backward values"""
    g = [0.0] * P
    out = []
    for e in x:
        f = e
        for i in range(P - 1, -1, -1):
            f -= k[i] * g[i]
            if i + 1 < P:
                g[i + 1] = g[i] + k[i] * f
        g[0] = f
        out.append(f)
    return out


def direct(a, x):
    y = []
    for nn, e in enumerate(x):
        y.append(e - sum(a[i] * y[nn - i] for i in range(1, len(a)) if nn - i >= 0))
    return y


def chirp():
    """the voice's pulse, one per period: the derivative of a glottal flow (open 2/3, closing 1/3), 24 samples"""
    no, nc = 16, 8
    g = [0.5 * (1 - math.cos(math.pi * i / no)) for i in range(no)] + [math.cos(math.pi * i / (2 * nc)) for i in range(nc)]
    d = [g[0]] + [g[i] - g[i - 1] for i in range(1, len(g))]
    m = max(abs(x) for x in d)
    return [round(x / m * 32767) for x in d]


CHIRP = chirp()


def q15(x):
    return max(-32767, min(32767, round(x * 32768)))


def gains(k):
    """the voice's gain (a pulse train at 120 Hz) and the noise's (white): both to an RMS of 0.2 of full scale, Q16"""
    kq = [q / 32768 for q in map(q15, k)]
    per = int(FS / 120)
    x = [0.0] * 4000
    for s in range(0, 4000, per):
        for i, c in enumerate(CHIRP):
            if s + i < 4000:
                x[s + i] = c / 32768
    y = lattice(kq, x)[1000:]
    rv = math.sqrt(sum(t * t for t in y) / len(y))
    rnd = random.Random(7)
    xn = [rnd.uniform(-1, 1) for _ in range(4000)]
    yn = lattice(kq, xn)[1000:]
    rn = math.sqrt(sum(t * t for t in yn) / len(yn))
    return [min(1 << 22, round(0.2 / max(r, 1e-6) * 65536)) for r in (rv, rn)]   # (at most 64)


# ---- words -> segments
class Gen:
    def __init__(self):
        self.targets = []           # (k q15 x 10, gv, gn, av 0..255, an 0..255)
        self.index = {}
        self.segs = []              # (target, dur 2 ms, pitch quarter semitones, trans 2 ms)

    def target(self, name, snd=None):
        snd = snd or SND[name]
        key = (tuple(snd["res"]), snd["av"], snd["an"])
        if key not in self.index:
            k = reflect(poly(snd["res"]))
            self.index[key] = len(self.targets)
            self.targets.append(([q15(x) for x in k], *gains(k), round(255 * snd["av"]), round(255 * snd["an"])))
        return self.index[key]

    def word(self, phones):
        toks = phones.split()
        seq = []                    # (sound name or dict, ms, trans ms, stress or None)
        nxt_vowel = lambda i: next((DIPH[t.rstrip("012")][0] if t.rstrip("012") in DIPH else t.rstrip("012")
                                    for t in toks[i + 1:] if t.rstrip("012") in VOWELS | set(DIPH)), "AX")
        aspir = lambda i: {"res": [(f, b * 2.0) for f, b in SND[nxt_vowel(i)]["res"]], "av": 0.0, "an": 0.35}
        seq.append(("SIL", 20, 0, None))
        for i, t in enumerate(toks):
            ph, st = (t[:-1], int(t[-1])) if t[-1] in "012" else (t, None)
            final = i == len(toks) - 1
            if ph in VOWELS or ph in DIPH:
                base = 170 if ph in DIPH else 60 if ph == "AX" else 130 if ph in TENSE else 90
                ms = base * (1.35 if st == 1 else 1.1 if st == 2 else 0.8) * (1.25 if final else 1.0)
                if ph in DIPH:
                    a, b = DIPH[ph]
                    seq.append((a, ms * 0.55, 30, st))
                    seq.append((b, ms * 0.45, ms * 0.4, st))
                else:
                    seq.append((ph, ms, 30, st))
            elif ph in STOPS:
                burst, voiced = STOPS[ph]
                seq.append(("VB" if voiced else "SIL", 30 if i == 0 else 50 if voiced else 60, 10, None))
                seq.append((burst, 8 if voiced else 12, 0, None))
                if not voiced:
                    seq.append((aspir(i), 40, 0, None))
            elif ph in AFFR:
                burst, fric, voiced = AFFR[ph]
                seq.append(("VB" if voiced else "SIL", 40, 10, None))
                seq.append((burst, 8, 0, None))
                seq.append((fric, 70, 0, None))
            elif ph == "HH":
                seq.append((aspir(i), DUR["HH"], 10, None))
            elif ph in SND:
                seq.append((ph, DUR[ph], 15, None))
            else:
                raise SystemExit(f"gen_speech: unknown phoneme {t} in '{phones}'")
        seq.append(("SIL", 40, 30, None))
        total = sum(s[1] for s in seq)
        first, at = len(self.segs), 0.0
        for snd, ms, tr, st in seq:
            frac = (at + ms / 2) / total
            pitch = round(6 - 12 * frac + (8 if st == 1 else 4 if st == 2 else 0) - 8)   # (the stressed syllable on the key)
            tg = self.target(snd) if isinstance(snd, str) else self.target(None, snd)
            self.segs.append((tg, max(1, min(255, round(ms / 2))), pitch, max(0, min(255, round(min(tr, ms) / 2)))))
            at += ms
        hold = max((j for j in range(first, len(self.segs)) if self.targets[self.segs[j][0]][3] > 100), default=first)
        return first, len(self.segs) - first, hold - first


def read(path):
    out = []
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            name, phones = line.split(None, 1)
            out.append((name.upper()[:8], phones.upper()))
    if not 1 <= len(out) <= WORDS:
        raise SystemExit(f"gen_speech: {path}: {len(out)} words (1 .. {WORDS})")
    return out


def main(path):
    # (the lattice is checked against the direct form it stands for)
    k = reflect(poly(SND["AA"]["res"]))
    imp = [1.0] + [0.0] * 199
    if max(abs(a - b) for a, b in zip(lattice(k, imp), direct(poly(SND["AA"]["res"]), imp))) > 1e-6:
        raise SystemExit("gen_speech: the lattice does not match its polynomial")
    g = Gen()
    banks = [(b, read(HERE / f)) for b, f in BANKS] + [("PHON", [(p, p + ("1" if p in VOWELS else "")) for p in PHON])]
    if len(PHON) > WORDS:
        raise SystemExit("gen_speech: PHON: more than 32 sounds")
    words = []
    for _, ws in banks:
        for _, ph in ws:
            words.append(g.word(ph))
    if len(g.targets) > 255:
        raise SystemExit("gen_speech: more than 255 sounds")
    lines = ["/* generated by tools/gen_speech.py from tools/speech/: do not edit */",
             f"#define SP_NBANK {len(banks)}", f"#define SP_WORDS {WORDS}", f"#define SP_P {P}",
             "#define SP_BANK_NAMES_INIT " + ", ".join(f'"{b}"' for b, _ in banks),
             "static const int16_t SP_CHIRP[%d] = {%s};" % (len(CHIRP), ", ".join(map(str, CHIRP))),
             "typedef struct { int16_t k[SP_P]; uint8_t av, an; uint32_t gv, gn; } sp_target_t;   /* k Q15, gains Q16 */",
             f"static const sp_target_t SP_TARGET[{len(g.targets)}] = {{"]
    for kq, gv, gn, av, an in g.targets:
        lines.append("    {{%s}, %d, %d, %d, %d}," % (", ".join(map(str, kq)), av, an, gv, gn))
    lines.append("};")
    lines.append("typedef struct { uint8_t t, dur; int8_t pitch; uint8_t trans; } sp_seg_t;   /* dur, trans: 2 ms */")
    lines.append(f"static const sp_seg_t SP_SEG[{len(g.segs)}] = {{")
    for i in range(0, len(g.segs), 6):
        lines.append("    " + " ".join("{%d, %d, %d, %d}," % s for s in g.segs[i:i + 6]))
    lines.append("};")
    lines.append("typedef struct { uint16_t first; uint8_t n, hold; } sp_word_t;   /* hold: the last voiced segment */")
    lines.append(f"static const sp_word_t SP_WORD[{len(words)}] = {{")
    for i in range(0, len(words), 6):
        lines.append("    " + " ".join("{%d, %d, %d}," % w for w in words[i:i + 6]))
    lines.append("};")
    lines.append("/* each bank's words: SP_WORD[SP_BANK_FIRST[b] ..], SP_N_<b> of them (1 .. SP_WORDS) */")
    for bi, (b, ws) in enumerate(banks):
        lines.append(f"#define SP_N_{bi} {len(ws)}")
    lines.append("static const uint16_t SP_BANK_FIRST[SP_NBANK] = {" +
                 ", ".join(str(sum(len(w) for _, w in banks[:i])) for i in range(len(banks))) + "};")
    lines.append("static const uint8_t SP_BANK_N[SP_NBANK] = {" + ", ".join(f"SP_N_{i}" for i in range(len(banks))) + "};")
    lines.append("#define SP_WD_ALL " + ", ".join(f"SP_WD({i})" for i in range(len(banks))) +
                 "   /* (eng_speech.c SP_WD: a bank's WORD) */")
    for bi, (b, ws) in enumerate(banks):
        lines.append(f"static const char *const SP_NAMES_{bi}[SP_N_{bi} + 1] = {{" + ", ".join(f'"{nm}"' for nm, _ in ws)
                     + ", 0};")
    lines.append("static const char *const *const SP_NAMES[SP_NBANK] = {" +
                 ", ".join(f"SP_NAMES_{i}" for i in range(len(banks))) + "};")
    Path(path).write_text("\n".join(lines) + "\n")
    print(f"speech: {len(banks)} banks, {len(words)} words, {len(g.segs)} segments, {len(g.targets)} sounds, "
          f"{len(g.targets) * 32 + len(g.segs) * 4 + len(words) * 4} B")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "felucca_speech.h")
