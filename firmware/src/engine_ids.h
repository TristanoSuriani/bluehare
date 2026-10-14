/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* Every engine in the tree: ENGINE(NAME, id, substitute). Included where a table over all of them is built (core.h,
 * engines.c, params.c) and read by tools/gen_dist.py (a distribution names engines from here).
 *   NAME        its name (ENG_<NAME> in its eng_*.c file; the device and the editor show it)
 *   id          fixed for good: what the stores and the editor protocol use. Append-only; 1 DIGITAL, 4 SAMPLE, 8 GRAIN,
 *               13 SLICE are retired (removed in 1.5), 27 ACID too (Fel-Tracker: WASP took its place), and Felucca's
 *               own 0 ANALOG, 2 PHASE, 3 LOFI, 5 VOICE, 6 TRIO, 7 WHEEL, 9 PHYS, 10 DRUM, 11 NOISE, 12 FM6 (Fel-Tracker
 *               removed them: core.h EID_RETIRED), never given again. A new engine takes the next free id and raises
 *               core.h EID_COUNT
 *   substitute  the engine a sound of this one loads as in a distribution without it (and down that one's own, if it
 *               is missing too; at the end the distribution's first engine): engines.c eng_load_id */
ENGINE(WAVE, 14, VECTOR)
ENGINE(BYTE, 15, SID)
ENGINE(WEST, 16, KARPLUS)
ENGINE(HOOVER, 17, SID)
ENGINE(SPEECH, 18, VOSIM)
ENGINE(FMPERC, 19, KIT)
ENGINE(KARPLUS, 20, WEST)
ENGINE(VOSIM, 21, SPEECH)
ENGINE(PARTICLE, 22, WAVE)
ENGINE(CHAOS, 23, GLITCH)
ENGINE(SID, 24, WASP)
ENGINE(DUBSIREN, 25, DUBSIREN8)
ENGINE(BLISS, 26, WAVE)
ENGINE(KIT, 28, GLITCH)
ENGINE(BREAKS, 29, KIT)
ENGINE(RISERS, 30, CHAOS)
ENGINE(DUBSIREN8, 31, HOOVER)
ENGINE(DUBCHORD, 32, WAVE)
ENGINE(WASP, 33, SID)
ENGINE(MODAL, 34, KARPLUS)
ENGINE(VECTOR, 35, WAVE)
ENGINE(ECHOES, 36, WAVE)
ENGINE(GLITCH, 37, KIT)
ENGINE(DARK, 38, SID)
ENGINE(RAZOR, 39, WASP)
ENGINE(GUTS, 40, RAZOR)
ENGINE(SLAG, 41, WAVE)
ENGINE(VENOM, 42, RAZOR)
ENGINE(HONKS, 43, SPEECH)
ENGINE(WAR, 44, KIT)
ENGINE(CHOKE, 45, RUST)
ENGINE(RUST, 46, CHAOS)
ENGINE(DRILL, 47, GLITCH)
ENGINE(ANVIL, 48, MODAL)
ENGINE(SCREAM, 49, VOSIM)
ENGINE(FLUIDS, 50, WAVE)
ENGINE(SCRATCH, 51, GLITCH)
ENGINE(ABYSS, 52, ECHOES)
ENGINE(SHRED, 53, SCRATCH)
