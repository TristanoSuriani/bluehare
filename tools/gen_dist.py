#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
"""The distributions (distributions/<name>.txt: the engines in display order, one a line, at most 16; and optionally a
`TRACKS 4` or `TRACKS 8` line, the build's track count, 4 without one; a `PALETTE <name>` line, the palette a
device starts with, tools/gen_ui_palettes.py's, GREY without one) -> a header each, OUT_DIR/dist_<name>.h, that firmware/baseline/core.h includes for the one being built (BLUEHARE_DIST=<name>):
  BLUEHARE_DIST_TAG         "" for one named "default", else " <NAME>", _ a space (the version string, the package name)
  BLUEHARE_DIST_PRIMARY     1 for distributions/BUILD's (what a plain build builds: the host tests' full set), else 0
  BLUEHARE_ENG_<NAME>       1 for the engines in it, 0 for every other engine of firmware/baseline/engine_ids.h
  BLUEHARE_DIST_ENGINES(X)  X(NAME) for each, in order: the build's engine indices
  BLUEHARE_TRACKS           its track count (4 or 8: firmware/baseline/core.h NTRK)
  BLUEHARE_DIST_PALETTE     UI_PAL_<name> (or UI_GREY_INDEX): the palette until the settings hold one
  ENGI_<NAME> ENG_ABSENT   for every engine it leaves out
  BLUEHARE_FX_<NAME>, BLUEHARE_PLUGIN_<NAME>, BLUEHARE_UI_<NAME>   (2026-10-17) 1 for the components of that kind it
                            picks (`FX <NAME>` .. lines; none of a kind: every one), 0 for the others
And (2026-10-17) dist_all.h, no file's: every engine and every component of the registry, in its order, as the primary
distribution otherwise (tracks, palette): what Blue Hare Native builds (native/build.sh), Native having every component.
The registry (firmware/components.txt, 2026-10-17): every component a distribution may pick, its kind, layer, files,
what it is and its credit; its engines must be engine_ids.h's, its files must exist.
  gen_dist.py SRC_ROOT OUT_DIR
  gen_dist.py --credits SRC_ROOT     AUTHORS.md's "Components in the build" table, from the registry
  gen_dist.py --credits-check SRC_ROOT   (the tests) fails if that table is not the registry's"""
import re
import sys
from pathlib import Path

MAX = 16


def registry(root):
    text = (root / "firmware" / "baseline" / "engine_ids.h").read_text()
    return [m.group(1) for m in re.finditer(r"^ENGINE\((\w+),\s*\d+,\s*\w+\)", text, re.M)]


KINDS = ("ENGINE", "FX", "PLUGIN", "UI")


def components(root):
    """the registry: [(kind, name, layer, files, what, credit)], checked against engine_ids.h and the files"""
    path = root / "firmware" / "components.txt"
    out = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        head = parts[0].split()
        if len(parts) != 3 or len(head) != 4 or head[0] not in KINDS or head[2] not in ("baseline", "os", "fm1"):
            raise SystemExit(f"{path}:{n}: KIND NAME LAYER FILES | what | credit (KIND: {' '.join(KINDS)})")
        kind, name, layer, files = head
        for f in files.split(","):
            if not (root / "firmware" / layer / f).exists():
                raise SystemExit(f"{path}:{n}: {name}: no firmware/{layer}/{f}")
        out.append((kind, name, layer, files.split(","), parts[1], parts[2]))
    eng = [c[1] for c in out if c[0] == "ENGINE"]
    ids = registry(root)
    if sorted(eng) != sorted(ids):
        raise SystemExit(f"{path}: its engines {' '.join(eng)} != engine_ids.h's {' '.join(ids)}")
    return out


def credits(root, check=False):
    """AUTHORS.md's table between <!-- COMPONENTS-BEGIN --> and <!-- COMPONENTS-END --> from the registry"""
    root = Path(root)
    rows = ["| Component | Kind | What | Where | Credit |", "| --- | --- | --- | --- | --- |"]
    for kind, name, layer, files, what, credit in components(root):
        where = ", ".join(f"`firmware/{layer}/{f}`" for f in files)
        rows.append(f"| {name} | {kind.lower()} | {what} | {where} | {credit} |")
    path = root / "AUTHORS.md"
    text = path.read_text()
    a, b = "<!-- COMPONENTS-BEGIN -->", "<!-- COMPONENTS-END -->"
    i, j = text.index(a) + len(a), text.index(b)
    new = text[:i] + "\n" + "\n".join(rows) + "\n" + text[j:]
    if check:                                        # (run_tests.sh: the table as the registry gives it)
        if new != text:
            raise SystemExit("AUTHORS.md: the components table is not the registry's: make tables")
        print(f"AUTHORS.md: the components table is current ({len(rows) - 2} components)")
        return
    path.write_text(new)
    print(f"AUTHORS.md: {len(rows) - 2} components")


def read(path, known, comps):
    names, tracks, palette, voices = [], 4, None, 8
    picked = {k: [] for k in KINDS[1:]}
    for n, line in enumerate(path.read_text().splitlines(), 1):
        line = line.split("#", 1)[0].strip().upper()
        if not line:
            continue
        if line.startswith("TRACKS"):
            words = line.split()
            if len(words) != 2 or words[1] not in ("4", "8"):
                raise SystemExit(f"{path}:{n}: TRACKS 4 or TRACKS 8")
            tracks = int(words[1])
            continue
        if line.startswith("VOICES"):                # (Blue Hare, 2026-10) the voice budget always there: 8 or 12
            words = line.split()
            if len(words) != 2 or words[1] not in ("8", "12"):
                raise SystemExit(f"{path}:{n}: VOICES 8 or VOICES 12")
            voices = int(words[1])
            continue
        words = line.split()
        if words[0] in picked:                       # (2026-10-17) FX / PLUGIN / UI <NAME>: a component it picks
            if len(words) != 2 or words[1] not in comps[words[0]]:
                raise SystemExit(f"{path}:{n}: {words[0]} <NAME> ({' '.join(comps[words[0]])}: firmware/components.txt)")
            picked[words[0]].append(words[1])
            continue
        if line.startswith("PALETTE"):               # (Blue Hare) the palette a device starts with
            words = line.split()
            if len(words) != 2 or not re.fullmatch(r"[A-Z][A-Z0-9-]*", words[1]):
                raise SystemExit(f"{path}:{n}: PALETTE <name> (tools/gen_ui_palettes.py)")
            palette = words[1].replace("-", "_")
            continue
        if line not in known:
            raise SystemExit(f"{path}:{n}: unknown engine {line} (firmware/baseline/engine_ids.h: {' '.join(known)})")
        if line in names:
            raise SystemExit(f"{path}:{n}: {line} twice")
        names.append(line)
    if not names or len(names) > MAX:
        raise SystemExit(f"{path}: {len(names)} engines (1 .. {MAX})")
    for k in picked:                                 # (none of a kind: every one)
        picked[k] = picked[k] or list(comps[k])
    return names, tracks, palette, voices, picked


def primary(root):
    lines = [l.strip() for l in (root / "distributions" / "BUILD").read_text().splitlines()]
    return next(l for l in lines if l and not l.startswith("#")).lower()


def main(root, out):
    root, out = Path(root), Path(out)
    known = registry(root)
    reg = components(root)
    comps = {k: [c[1] for c in reg if c[0] == k] for k in KINDS[1:]}
    first = primary(root)
    out.mkdir(parents=True, exist_ok=True)
    dists = []
    for path in sorted((root / "distributions").glob("*.txt")):
        name = path.stem.lower()
        if not re.fullmatch(r"[a-z][a-z0-9_]*", name) or name == "all":
            raise SystemExit(f"{path}: a distribution's name is lower case letters, digits, _ (not all)")
        dists.append((name, path.name, *read(path, known, comps)))
    prim = next(d for d in dists if d[0] == first)   # (all: every engine and component, the primary's tracks, palette)
    dists.append(("all", "firmware/components.txt", [c[1] for c in reg if c[0] == "ENGINE"], prim[3], prim[4], prim[5],
                  dict(comps)))
    for name, src, names, tracks, palette, voices, picked in dists:
        tag = "" if name in ("default", "all") else " " + name.upper().replace("_", " ")
        lines = [f"/* generated by tools/gen_dist.py from {src}: do not edit */",
                 f'#define BLUEHARE_DIST_TAG "{tag}"', f"#define BLUEHARE_DIST_PRIMARY {int(name == first)}",
                 f"#define BLUEHARE_DIST_REFERENCE {int(name == first)}"]   # (Blue Hare: the host tests' build, the shipped one)
        lines += [f"#define BLUEHARE_ENG_{k} {int(k in names)}" for k in known]
        lines.append("#define BLUEHARE_DIST_ENGINES(X) " + " ".join(f"X({k})" for k in names))
        lines.append(f"#define BLUEHARE_TRACKS {tracks}")
        lines.append(f"#define BLUEHARE_VOICE_BASE {voices}")
        lines.append(f"#define BLUEHARE_DIST_PALETTE {'UI_PAL_' + palette if palette else 'UI_GREY_INDEX'}")
        lines += [f"#define ENGI_{k} ENG_ABSENT" for k in known if k not in names]
        lines += [f"#define BLUEHARE_{kind}_{c} {int(c in picked[kind])}" for kind in comps for c in comps[kind]]
        (out / f"dist_{name}.h").write_text("\n".join(lines) + "\n")
        extra = " ".join(c for kind in comps for c in comps[kind] if c not in picked[kind])
        print(f"distribution {name}: {len(names)} engines, {tracks} tracks{', without ' + extra if extra else ''}")


if __name__ == "__main__":
    if sys.argv[1:2] in (["--credits"], ["--credits-check"]):
        credits(sys.argv[2], sys.argv[1] == "--credits-check")
    else:
        main(*sys.argv[1:3])
