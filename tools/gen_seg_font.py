#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
"""(2026-10-18) Blue Hare's LED face: a TrueType font of fourteen-segment characters, as an alphanumeric VFD shows them
(MENU > STYLE LED: the screen's text in it; the studio's and the emulator's lettering the same shapes). Drawn here,
nothing downloaded: every character a set of the fourteen segments (hexagonal bars, the diagonals parallelograms),
slanted a little, a decimal point beside the cell. Lowercase as uppercase. The metrics match the Inter faces at the
sizes gen_aa_font.py renders it (S 13 px, M 16 px, L 30 px): ascent 930, descent 240, capitals 700 (a line of 15 / 19 /
35 px, the capitals 9 / 11 / 21 px tall, as Inter Tight's at 12 / 15 / 28).
  gen_seg_font.py OUT.ttf"""
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

UPM, ASC, DESC, CAP = 1000, 930, 240, 700
W, T, SLANT = 430, 92, 0.1          # the cell's width, a segment's thickness, the slant (x per y)
ADV = W + 150                       # the advance: the cell, a gap (the decimal point sits in it)
ASC0, DESC0, CAP0, W0, ADV0 = ASC, DESC, CAP, W, ADV
# the unit cell: x 0 .. 1, y 0 .. 2 (top to bottom); a segment from (x1, y1) to (x2, y2)
SEG = {
    "a": (.1, 0, .9, 0), "b": (1, .1, 1, .9), "c": (1, 1.1, 1, 1.9), "d": (.1, 2, .9, 2), "e": (0, 1.1, 0, 1.9),
    "f": (0, .1, 0, .9), "g": (.1, 1, .46, 1), "G": (.54, 1, .9, 1), "h": (.16, .2, .42, .82), "i": (.5, .14, .5, .86),
    "j": (.84, .2, .58, .82), "k": (.42, 1.18, .16, 1.8), "l": (.5, 1.14, .5, 1.86), "m": (.58, 1.18, .84, 1.8),
}
CH = {
    "0": "abcdefjk", "1": "bc", "2": "abdegG", "3": "abcdG", "4": "bcfgG", "5": "acdfgG", "6": "acdefgG", "7": "abc",
    "8": "abcdefgG", "9": "abcdfgG", "A": "abcefgG", "B": "abcdGil", "C": "adef", "D": "abcdil", "E": "adefg",
    "F": "aefg", "G": "acdefG", "H": "bcefgG", "I": "adil", "J": "bcde", "K": "efgjm", "L": "def", "M": "bcefhj",
    "N": "bcefhm", "O": "abcdef", "P": "abefgG", "Q": "abcdefm", "R": "abefgGm", "S": "acdfgG", "T": "ail",
    "U": "bcdef", "V": "efjk", "W": "bcefkm", "X": "hjkm", "Y": "hjl", "Z": "adjk", "-": "gG", "+": "gGil", "/": "jk",
    "\\": "hm", "|": "il", "_": "d", "=": "dgG", "*": "ghijklmG", "(": "jm", ")": "hk", "[": "adef", "]": "abcd",
    "<": "jm", ">": "hk", "'": "i", '"': "fi", "`": "h", "^": "km", "~": "gG", "#": "bcdgGil", "$": "acdfgGil",
    "%": "cfjkgG", "&": "adeghjm", "@": "abdefGi", "?": "abGl", "!": "il", "{": "adgjm", "}": "adGhk",
    "°": "abfgG", "©": "adef", "Ä": "abcefgG", "Ö": "abcdef", "Ü": "bcdef", "ä": "abcefgG",
    "ö": "abcdef", "ü": "bcdef",
}
# narrow characters (their advance, of the cell's; their marks drawn from x 0): Inter's are narrow, a value's "-4.5 dB"
# fits where it fitted
NARROW = {".": .42, ",": .42, ":": .42, ";": .42, " ": .62, "-": .9}
DOTS = {".": [(.12, 2)], ",": [(.12, 2)], ":": [(.12, .6), (.12, 1.45)], ";": [(.12, .6), (.12, 1.45)],
        "!": [(.5, 2)], "?": [(.5, 2)], "·": [(.5, 1)], "…": [(.1, 2), (.5, 2), (.9, 2)]}
EXTRA = [0xA9, 0xB0, 0xB7, 0xC4, 0xD6, 0xDC, 0xE4, 0xF6, 0xFC, 0x2026]


def pt(x, y):                       # the cell's (x, y) in font units, slanted
    fx, fy = x * W, CAP - y * CAP / 2
    return (round(fx + fy * SLANT), round(fy))


def poly(pen, pts):
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:] + pts[:1]))
    if area > 0:                    # TrueType: outer contours clockwise
        pts = pts[::-1]
    pen.moveTo(pts[0])
    for p in pts[1:]:
        pen.lineTo(p)
    pen.closePath()


def segment(pen, name):
    x1, y1, x2, y2 = SEG[name]
    h, v = T / 2 / W, T / 2 / (CAP / 2)            # half a thickness in cell units, across and down
    if y1 == y2:                    # a horizontal bar: pointed ends
        poly(pen, [pt(x1, y1), pt(x1 + h, y1 - v), pt(x2 - h, y1 - v), pt(x2, y1), pt(x2 - h, y1 + v), pt(x1 + h, y1 + v)])
    elif x1 == x2:                  # a vertical bar
        poly(pen, [pt(x1, y1), pt(x1 + h, y1 + v), pt(x1 + h, y2 - v), pt(x1, y2), pt(x1 - h, y2 - v), pt(x1 - h, y1 + v)])
    else:                           # a diagonal: a parallelogram of the thickness, across
        poly(pen, [pt(x1 - h * .8, y1), pt(x1 + h * .8, y1), pt(x2 + h * .8, y2), pt(x2 - h * .8, y2)])


def dot(pen, x, y):
    r, rv = T * .6 / W, T * .6 / (CAP / 2)
    poly(pen, [pt(x - r, y - rv), pt(x + r, y - rv), pt(x + r, y + rv * .2), pt(x - r, y + rv * .2)])


def glyph(ch):
    pen = TTGlyphPen(None)
    segs = CH.get(ch, CH.get(ch.upper(), ""))
    for s in segs:
        segment(pen, s)
    for x, y in DOTS.get(ch, []):
        dot(pen, x, y)
    if ch in ",;":                  # the comma's tail
        poly(pen, [pt(.08, 2.05), pt(.16, 2.05), pt(.06, 2.35), pt(0, 2.35)])
    if not segs and ch not in DOTS and ch not in ",;":
        pen.moveTo((0, 0))          # (an empty glyph: the space)
        pen.lineTo((0, 0))
        pen.closePath()
    return pen.glyph()


def main(out, asc=None, desc=None, cap=None, w=None, adv=None):
    """a TTF at OUT; asc, desc, cap (font units of 1000) for one size's exact line, w and adv its cell and advance
    (gen_aa_font.py led_faces)"""
    global ASC, DESC, CAP, W, ADV
    ASC, DESC, CAP, W, ADV = asc or ASC0, desc or DESC0, cap or CAP0, w or W0, adv or ADV0   # (each call from the defaults)
    chars = [chr(c) for c in range(32, 127)] + [chr(c) for c in EXTRA]
    names = [".notdef"] + [f"u{ord(c):04X}" for c in chars]
    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({ord(c): f"u{ord(c):04X}" for c in chars})
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    for c in chars:
        glyphs[f"u{ord(c):04X}"] = glyph(c) if c != " " else TTGlyphPen(None).glyph()
    fb.setupGlyf(glyphs)
    adv = {f"u{ord(c):04X}": round(ADV * NARROW.get(c, 1)) for c in chars}
    adv[".notdef"] = ADV
    fb.setupHorizontalMetrics({n: (adv[n], 0) for n in names})
    fb.setupHorizontalHeader(ascent=ASC, descent=-DESC)
    fb.setupNameTable({"familyName": "Blue Hare LED", "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=ASC, sTypoDescender=-DESC, usWinAscent=ASC, usWinDescent=DESC, sCapHeight=CAP, sxHeight=CAP)
    fb.setupPost()
    fb.save(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "BlueHareLED.ttf")
