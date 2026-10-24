/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* What differs between the targets, in one place (Blue Hare; the architecture's item 4). The FM-1 build gets the empty
 * versions below, inlined to nothing (the device's code and its renders as before); the host targets (BLUEHARE_NATIVE:
 * the native image and the plugin's cores) define theirs in native/target_host.c (Native's: the baseline only
 * declares them, 2026-10-17):
 *   TGT_NOW()                the audio load meter's clock (audio.c): the device's timer, or the computer's
 *   TGT_INFO_VERSION         MENU > INFO's first row: FIRMWARE, or VERSION (an application)
 *   tgt_beat_samples()       a host's tempo in samples a beat, over BPM and CLOCK (fx.c beat_samples); 0: none
 *   tgt_seq_block(n, &mode)  the samples the sequencer moves on in a block of n (seq.c events_block); a host's
 *                            transport sets the clock mode to internal and follows the host
 *   tgt_block_end(n)         a block of n rendered (seq.c: the host's frame count)
 *   tgt_track_out(k, n, mg)  1: track k went to its own output, not the main mix (fx.c mix_track)
 *   tgt_mix_end(n)           the mix's block of n done (fx.c mix_block: the own outputs' write position)
 * Left where they are: what a host target has more of (DEV_NAT sizes, the native engines and reverbs).
 * (2026-10-24, the second core, after ChoralRoot FM-1 and Melodee: hal/fm1_cpu1.h, fm1/bluehare.c)
 *   TGT_CORE()               the core this code runs on: 0, or 1 in the second core's job
 *   tgt_c1_run(fn, arg)      hand fn(arg) to the second core: 1 handed (then tgt_c1_wait), 0 not (core 0 does it)
 *   tgt_c1_wait()            0 done; else it failed and is given up (the job's block is lost: fx.c mix_block)
 *   The host (no BLUEHARE_CPU1): c1_split_test runs the job inline, as core 1, at the wait (tests/baseline/
 *   dualcore_test.c: the split's mix bit for bit one core's) */
#ifndef TARGET_H
#define TARGET_H
#ifdef BLUEHARE_NATIVE
#define TGT_NOW() native_ticks()
#define TGT_INFO_VERSION "VERSION"
static uint32_t tgt_beat_samples(void);
static uint32_t tgt_seq_block(uint32_t n, uint32_t *clock_mode);
static void tgt_block_end(uint32_t n);
static int tgt_track_out(uint32_t k, uint32_t n, int32_t mg);
static void tgt_mix_end(uint32_t n);
#else
#define TGT_NOW() fm1_ticks()
#define TGT_INFO_VERSION "FIRMWARE"
static inline uint32_t tgt_beat_samples(void) { return 0; }
static inline uint32_t tgt_seq_block(uint32_t n, uint32_t *clock_mode) { (void)clock_mode; return n; }
static inline void tgt_block_end(uint32_t n) { (void)n; }
static inline int tgt_track_out(uint32_t k, uint32_t n, int32_t mg) { (void)k; (void)n; (void)mg; return 0; }
static inline void tgt_mix_end(uint32_t n) { (void)n; }
#endif
#if BLUEHARE_CPU1
#define TGT_CORE() fm1_cnum_now()                       /* (hal/fm1_cpu1.h, included before this file) */
static int tgt_c1_run(void (*fn)(uint32_t), uint32_t arg);
static int tgt_c1_wait(void);
#else
static uint8_t tgt_core_now, c1_split_test;
static void (*tgt_c1_fn)(uint32_t);
static uint32_t tgt_c1_arg;
#define TGT_CORE() ((uint32_t)tgt_core_now)
static inline int tgt_c1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!c1_split_test)
        return 0;
    tgt_c1_fn = fn;
    tgt_c1_arg = arg;
    return 1;
}
static inline int tgt_c1_wait(void)
{
    tgt_core_now = 1;
    tgt_c1_fn(tgt_c1_arg);
    tgt_core_now = 0;
    return 0;
}
#endif
#endif
