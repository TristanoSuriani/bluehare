#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md
"""Target-side cost estimate of the hot DSP code, from the pi32v2 objdump listing tools/build.py writes
(build/bluehare.dis). For each engine's render function and the audio ISR: the instructions
inside loops (every address range closed by a backward branch: the per-sample loops and what they
contain), and the hardware divides and calls in them. cost = the loop instructions, each weighted 4 per
level of nesting (an inner loop runs several times per sample) and 1 + 8 for a divide (many cycles). A static count, not a cycle count: it changes only when the compiled code changes,
so it is exact run to run, and it catches a render loop that grew (more work per sample, a new divide,
something not inlined any more). Compared with tests/fm1/target_budget.txt at +10 %. The host
CPU check (regress.c) is the primary one; this one sees the target compiler's code.
  tests/fm1/target_budget.py [DIS [BUDGET]]      BUDGET_UPDATE=1 rewrites BUDGET"""
import os
import re
import sys

FUNCS = ["fm1_alnk0_irq", "fm1_timer5_irq",               # the audio ISR; TIMER5: the key / LED scan (hal/fm1_input.h)
         # (Blue Hare: the IDM build has 8 tracks since step 3d: the ISR's loops over the parts twice as long, 37502 -> 67431)
         # (Blue Hare, the stereo buses: the chorus's second read, the sends' low cut, ROOM's side: 68734 -> 77039)
         # (Blue Hare 2026-10: WIDTH on any track, the per-track loop no longer split by the compiler: 61460 -> 74k)
         "uac_tap48",                                       # the USB audio input at 48 kHz: the 44.1 -> 48 resampler (usb.c)
         "click_render",                                    # the metronome's click (click.c), while it sounds
         "mod_begin", "mod_voice", "mod_end",               # the modulation matrix (mod.c), called when active
         "perf_begin", "perf_mute", "perf_pre", "perf_block", "perf_master",   # the FX layer (perform.c), when busy
         "rev_room", "rev_spring", "rev_hall", "rev_side_mix",   # the reverb bus (fx.c): ROOM / SPRING / HALL, its stereo
         # (mix_track: inlined into the ISR again since DUCK, whose calls kept it out of line, was archived 2026-10-16)
         "rev_concert",                                     # (Blue Hare 2026-10) REVERB TYPE CONCERT, the device's (fx.c)
         "mono_lows",                                       # (Blue Hare 2026-10) the master's lows in mono (fx.c)
         "dly_tape",                                        # DELAY TYPE TAPE / WORN (Blue Hare 1.5, fx.c), when chosen
         "rev_plate", "dly_amb", "eq_block",                # (2026-10-17) PLATE, AMB (fx_amb.c), the EQ: float, the FPU
         "sid_render", "wave_render",       # (Blue Hare 2026-10-16: HARE FM1's engines; the others archived)
         "modal_render",                                    # (WEST, SH101, BLISS, STRINGS archived 2026-10-17)
         "obie_render",                                     # (2026-10-17: after the OB-Xa)
         "hoover_render", "dubsiren_render", "kit_render", "breaks_render", "fmp_run",   # (back 2026-10-18)
         "vosim_render"]                                    # (back from the archive 2026-10-16)
# absent from some builds: skipped then; present, checked against these
OPTIONAL = {"uac_tap48": 504}           # (absent from a BLUEHARE_UAC_48K=0 or BLUEHARE_UAC=0 build)
# the engine each engine function belongs to: absent from a distribution without that engine (build/dist.txt names
# the one build/bluehare.dis is: tools/build.py), skipped then
ENGINE_OF = {"sid_render": "SID", "wave_render": "WAVE",
             "modal_render": "MODAL", "vosim_render": "VOSIM", "obie_render": "OBIE", "hoover_render": "HOOVER",
             "dubsiren_render": "DUBSIREN", "kit_render": "KIT", "breaks_render": "BREAKS"}
TOL = 0.10                      # exact (no noise): small edits pass, a grown render loop does not
DIV_W = 8                       # a divide weighs 1 + 8 instructions
NEST = 4                        # an instruction in a loop inside a loop weighs 4, two deep 16, ...
MAXD = 4

dis = sys.argv[1] if len(sys.argv) > 1 else "build/bluehare.dis"
budget = sys.argv[2] if len(sys.argv) > 2 else "tests/fm1/target_budget.txt"
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")


def functions(path):
    """{name: [(addr, text)]}: a function runs to the next label not starting with '.' (the compiler's
    jump-table labels are inside functions); jump-table data ('< n : 0x.. >') is left out"""
    out, cur = {}, None
    with open(path) as f:
        for line in f:
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if name in FUNCS else None
                continue
            m = INSN.match(line)
            if cur is not None and m and not m.group(3).lstrip().startswith("<"):
                cur.append((int(m.group(1), 16), m.group(3).strip()))
    return out


def cost(insns):
    """loops: one span per loop head (the farthest backward branch to it); an instruction inside d
    spans weighs NEST ** (d - 1) (an inner loop runs several times per pass of the outer one)"""
    lo = insns[0][0]
    heads = {}
    for a, t in insns:
        m = TARGET.search(t)
        if m and lo <= int(m.group(1), 16) < a:
            h = int(m.group(1), 16)
            heads[h] = max(heads.get(h, a), a)
    n = divs = calls = 0
    w = 0
    for a, t in insns:
        d = min(sum(1 for h, e in heads.items() if h <= a <= e), MAXD)
        if not d:
            continue
        k = NEST ** (d - 1)
        n += 1
        if re.search(r"= r\d+ / r\d+", t):
            divs += 1
            k *= 1 + DIV_W
        calls += t.startswith("call")
        w += k
    return {"insns": len(insns), "loop": n, "div": divs, "call": calls, "cost": w}


def built_engines():
    """the engines of the distribution build/bluehare.dis holds (build/dist.txt; none written: the default)"""
    here = os.path.dirname(os.path.abspath(__file__))
    d = os.path.join(os.path.dirname(dis) or ".", "dist.txt")
    name = open(d).read().strip() if os.path.exists(d) else "default"
    path = os.path.join(here, "..", "..", "distributions", f"{name}.txt")   # (tests/fm1: two up, since the tests by layer)
    if not os.path.exists(path):                    # (an image of a distribution archived since: stale)
        return name, None
    return name, {ln.split("#", 1)[0].strip().upper() for ln in open(path)} - {""}


def main():
    if not os.path.exists(dis):
        print(f"target: skip ({dis} missing: run ./build.sh)")
        return 0
    dist, engines = built_engines()
    if engines is None:
        print(f"target: skip ({dis} is of {dist}, a distribution no longer in distributions/: run ./build.sh)")
        return 0
    fns = functions(dis)
    res = {n: cost(fns[n]) for n in FUNCS if fns.get(n)}
    elsewhere = [n for n in FUNCS if n not in res and ENGINE_OF.get(n, next(iter(engines))) not in engines]
    missing = [n for n in FUNCS if n not in res and n not in OPTIONAL and n not in elsewhere]
    print(f"target: the {dist} distribution; not in it: {' '.join(elsewhere) or 'none'}")
    base = {}
    if os.path.exists(budget):
        for line in open(budget):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                base[p[0]] = int(p[1])
    if os.environ.get("BUDGET_UPDATE"):
        with open(budget, "w") as f:
            f.write("# Blue Hare target cost budget (tests/fm1/target_budget.py): instructions in the loops of each\n"
                    f"# function in build/bluehare.dis, x{NEST} per nesting level, divides x{1 + DIV_W}. The check allows "
                    f"+{TOL * 100:.0f} %.\n# Rewritten by BUDGET_UPDATE=1.\n")
            for n in FUNCS:                         # (the other distributions' engines: kept as they were)
                if n in res and n not in OPTIONAL:
                    f.write(f"{n} {res[n]['cost']}\n")
                elif n in elsewhere and n in base:
                    f.write(f"{n} {base[n]}\n")
        print(f"target: budget {budget} rewritten ({len(res)} functions)")
    fail = 0
    for n in missing:
        print(f"target: FAIL {n} not found in {dis} (renamed? inlined? update FUNCS)")
        fail += 1
    for n, r in res.items():
        b = base.get(n, OPTIONAL.get(n))
        state = "no budget (BUDGET_UPDATE=1 adds it)" if b is None else "ok"
        if b is not None and not os.environ.get("BUDGET_UPDATE"):
            if r["cost"] > b * (1 + TOL):
                state = f"OVER BUDGET (+{(r['cost'] / b - 1) * 100:.0f} %, limit +{TOL * 100:.0f} %)"
                fail += 1
            elif r["cost"] < b * (1 - TOL):
                state = f"note: {(r['cost'] / b - 1) * 100:.0f} % (BUDGET_UPDATE=1 to keep it)"
        print(f"target: {n:15s} {r['insns']:5d} instructions, {r['loop']:4d} in loops, {r['div']} divides, "
              f"{r['call']:2d} calls there: cost {r['cost']:5d} (budget {b if b is not None else '-'}) {state}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
