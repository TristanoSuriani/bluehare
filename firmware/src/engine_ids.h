/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Every engine in the tree: ENGINE(NAME, id, substitute). Included where a table over all of them is built (core.h,
 * engines.c, params.c) and read by tools/gen_dist.py (a distribution names engines from here).
 *   NAME        its name (ENG_<NAME> in its eng_*.c file; the device and the editor show it)
 *   id          fixed for good: what the stores and the editor protocol use. Append-only; 1 DIGITAL, 4 SAMPLE, 8 GRAIN,
 *               13 SLICE are retired (removed in 1.5) and never given again. A new engine takes the next free id and
 *               raises core.h EID_COUNT
 *   substitute  the engine a sound of this one loads as in a distribution without it (and down that one's own, if it
 *               is missing too; at the end the distribution's first engine): engines.c eng_load_id */
ENGINE(ANALOG, 0, HOOVER)
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
