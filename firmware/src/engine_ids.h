/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* Every engine in the tree: ENGINE(NAME, id, substitute). Included where a table over all of them is built (core.h,
 * engines.c, params.c) and read by tools/gen_dist.py (a distribution names engines from here).
 *   NAME        its name (ENG_<NAME> in its eng_*.c file; the device and the editor show it)
 *   id          fixed for good: what the stores and the editor protocol use. Append-only; 1 DIGITAL, 4 SAMPLE, 8 GRAIN,
 *               13 SLICE are retired (removed in 1.5), 27 ACID too (Blue Hare: WASP took its place), and Felucca's
 *               own 0 ANALOG, 2 PHASE, 3 LOFI, 5 VOICE, 6 TRIO, 7 WHEEL, 9 PHYS, 10 DRUM, 11 NOISE, 12 FM6 (Blue Hare
 *               removed them: core.h EID_RETIRED), never given again. A new engine takes the next free id and raises
 *               core.h EID_COUNT
 *   substitute  the engine a sound of this one loads as in a distribution without it (and down that one's own, if it
 *               is missing too; at the end the distribution's first engine): engines.c eng_load_id
 * (Blue Hare) An engine marked native (the comment after it) is the native image's alone (BLUEHARE_NATIVE: more than
 * the FM-1's CPU or memory holds): only a NATIVE distribution may list it (tools/gen_dist.py), and on the device its
 * sounds load as its substitute, which shares its controls
 * ARCHIVED(NAME, id): an engine taken out (below); empty unless the includer defines it */
#ifndef ARCHIVED
#define ARCHIVED(N, id)
#define ARCHIVED_DEFAULT
#endif
ENGINE(WAVE, 14, BLISS)
ENGINE(WEST, 16, MODAL)
ENGINE(SID, 24, WAVE)
ENGINE(BLISS, 26, WAVE)
ENGINE(MODAL, 34, WEST)
ENGINE(SH101, 61, SID)        /* (2026-10, Milestone 1) the little monosynth: VCO, sub, noise, 24 dB ladder */
ENGINE(VOSIM, 21, WAVE)       /* (back 2026-10-16) two formants of pulse trains: voices, reeds, brass; WAVE has vowels */
ENGINE(STRINGS, 55, WAVE)     /* (back 2026-10-17, in ECHOES' place) a string ensemble, its ensemble chorus */

/* (Blue Hare, 2026-10-16: Milestone 1) archived: the sources in archive/engines/, out of every build and test.
 * Their ids stay theirs (core.h EID_RETIRED: a stored sound of one is refused): one comes back by moving its
 * eng_*.c back and turning its ARCHIVED line into an ENGINE line again */
ARCHIVED(BYTE, 15)
ARCHIVED(ECHOES, 36)          /* (2026-10-17: STRINGS took its place) */
ARCHIVED(CANYON, 58)          /* (2026-10-17, with ECHOES: its repeats are ECHOES') */
ARCHIVED(HOOVER, 17)
ARCHIVED(SPEECH, 18)
ARCHIVED(FMPERC, 19)
ARCHIVED(KARPLUS, 20)
ARCHIVED(PARTICLE, 22)
ARCHIVED(CHAOS, 23)
ARCHIVED(DUBSIREN, 25)
ARCHIVED(KIT, 28)
ARCHIVED(BREAKS, 29)
ARCHIVED(RISERS, 30)
ARCHIVED(DUBSIREN8, 31)
ARCHIVED(DUBCHORD, 32)
ARCHIVED(WASP, 33)
ARCHIVED(VECTOR, 35)
ARCHIVED(GLITCH, 37)
ARCHIVED(DARK, 38)
ARCHIVED(RAZOR, 39)
ARCHIVED(GUTS, 40)
ARCHIVED(SLAG, 41)
ARCHIVED(VENOM, 42)
ARCHIVED(HONKS, 43)
ARCHIVED(WAR, 44)
ARCHIVED(CHOKE, 45)
ARCHIVED(RUST, 46)
ARCHIVED(DRILL, 47)
ARCHIVED(ANVIL, 48)
ARCHIVED(SCREAM, 49)
ARCHIVED(FLUIDS, 50)
ARCHIVED(SCRATCH, 51)
ARCHIVED(ABYSS, 52)
ARCHIVED(SHRED, 53)
ARCHIVED(LINNKIT, 54)
ARCHIVED(FLOOR, 56)
ARCHIVED(NEBULA, 57)
ARCHIVED(CHIPKIT, 59)
ARCHIVED(SANDSTORM, 60)
#ifdef ARCHIVED_DEFAULT
#undef ARCHIVED
#undef ARCHIVED_DEFAULT
#endif
