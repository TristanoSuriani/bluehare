#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
"""The web editor's mock device's engine tables (web/editor.html: ENG, by id, and G_ENGSEL's names) from the firmware's
own (tests/descdump.c's JSON: build/host/desc.json, the host tests' build: the primary distribution), between the
markers /*MOCK-ENG-BEGIN*/ .. /*MOCK-ENG-END*/, /*MOCK-ENGSEL-*/ (G_ENGSEL's names) and /*MOCK-TRKDEF-*/ (the power-on
sounds, the first engine), /*MOCK-ORDER-*/ (the engines' display order, ENGINE_ORDER). web/test_web.mjs
checks the mock against the firmware: run this after an engine, a parameter or a preset changes.
  gen_editor_mock.py DESC.json web/editor.html"""
import json
import sys

FMT = ["INT", "PCT", "BIPCT", "TIME", "LFOHZ", "CUTOFF", "DB", "SEMI", "ENUM", "BPM", "NOTE", "ONOFF", "OCT", "STEPS"]


def js(v):
    return json.dumps(v, ensure_ascii=False)


def desc(d):
    if d["label"] == "-" and d["min"] == d["max"] == 0:
        return "NONE"
    if d["fmt"] == 8 and d["names"] and d["min"] == 0 and d["max"] == len(d["names"]) - 1 and not d["unit"]:
        return f'E({js(d["label"])}, {js(d["names"])}, {d["def"]})'
    s = f'D({js(d["label"])}, F.{FMT[d["fmt"]]}, {d["min"]}, {d["max"]}, {d["def"]}'
    if d["names"] or d["unit"]:
        s += f', {js(d["names"]) if d["names"] else "null"}'
    if d["unit"]:
        s += f', {js(d["unit"])}'
    return s + ")"


def engine(i, e):
    if e["name"] == "-":
        return f"    GONE,                                         /* {i} */"
    ed = ",\n        ".join(desc(d) for d in e["edit"])
    pr = ",\n        ".join(f'P({js(p["name"])}, {js(p["e"])}, {js(p["env"])}, {p["mono"]})' for p in e["presets"])
    return (f'    {{ name: {js(e["name"])}, titles: {js(e["titles"])},   /* {i} */\n      edit: [{ed}],\n'
            f'      presets: [{pr}] }},')


def splice(text, begin, end, body):
    i, j = text.index(begin) + len(begin), text.index(end)
    return text[:i] + "\n" + body + "\n  " + text[j:]


def main(dj, html):
    d = json.load(open(dj))
    text = open(html).read()
    text = splice(text, "/*MOCK-ENG-BEGIN*/", "/*MOCK-ENG-END*/",
                  "\n".join(engine(i, e) for i, e in enumerate(d["ENG"])))
    text = splice(text, "/*MOCK-TRKDEF-BEGIN*/", "/*MOCK-TRKDEF-END*/",
                  f"  const TRK_DEF = {js(d['TRK_DEF'])};   /* engines.c TRK_DEF (ids: ENG is by id) */\n"
                  f"  const FIRST_E = E_OF({js(d['ORDER'][0])});   /* the build's first engine */")
    i, j = text.index("/*MOCK-ORDER-BEGIN*/") + len("/*MOCK-ORDER-BEGIN*/"), text.index("/*MOCK-ORDER-END*/")
    text = text[:i] + f"\nconst ENGINE_ORDER = {js(d['ORDER'])};\n" + text[j:]
    names = d["GP"][d["G_ENGSEL"]]["names"]
    text = splice(text, "/*MOCK-ENGSEL-BEGIN*/", "/*MOCK-ENGSEL-END*/", f"    E(\"ENG\", {js(names)}, 0),")
    open(html, "w").write(text)
    print(f"editor mock: {sum(e['name'] != '-' for e in d['ENG'])} engines of {len(d['ENG'])} ids")


if __name__ == "__main__":
    main(*sys.argv[1:3])
