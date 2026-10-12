/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* Every engine in the tree: ENGINE(NAME, id, substitute). Included where a table over all of them is built (core.h,
 * engines.c, params.c) and read by tools/gen_dist.py (a distribution names engines from here).
 *   NAME        its name (ENG_<NAME> in its eng_*.c file; the device and the editor show it)
 *   id          fixed for good: what the stores and the editor protocol use. Append-only; 1 DIGITAL, 4 SAMPLE, 8 GRAIN,
 *               13 SLICE are retired (removed in 1.5) and never given again. A new engine takes the next free id and
 *               raises core.h EID_COUNT
 *   substitute  the engine a sound of this one loads as in a distribution without it (and down that one's own, if it
 *               is missing too; at the end the distribution's first engine): engines.c eng_load_id */
ENGINE(ANALOG, 0, SID)
ENGINE(PHASE, 2, ANALOG)
ENGINE(LOFI, 3, BYTE)
ENGINE(VOICE, 5, SPEECH)
ENGINE(TRIO, 6, ANALOG)
ENGINE(WHEEL, 7, FM6)
ENGINE(PHYS, 9, WEST)
ENGINE(DRUM, 10, ANALOG)
ENGINE(NOISE, 11, ANALOG)
ENGINE(FM6, 12, WAVE)
ENGINE(WAVE, 14, ANALOG)
ENGINE(BYTE, 15, LOFI)
ENGINE(WEST, 16, PHYS)
ENGINE(HOOVER, 17, ANALOG)
ENGINE(SPEECH, 18, VOICE)
ENGINE(FMPERC, 19, KIT)
ENGINE(KARPLUS, 20, PHYS)
ENGINE(VOSIM, 21, SPEECH)
ENGINE(PARTICLE, 22, WAVE)
ENGINE(CHAOS, 23, NOISE)
ENGINE(SID, 24, ANALOG)
ENGINE(DUBSIREN, 25, DUBSIREN8)
ENGINE(FMKEYS, 26, FM6)
ENGINE(ACID, 27, SID)
ENGINE(KIT, 28, DRUM)
ENGINE(BREAKS, 29, KIT)
ENGINE(RISERS, 30, NOISE)
ENGINE(DUBSIREN8, 31, HOOVER)
ENGINE(DUBCHORD, 32, WAVE)
