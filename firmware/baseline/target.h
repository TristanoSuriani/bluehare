/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* What differs between the targets, in one place (Blue Hare; the architecture's item 4). The FM-1 build gets the empty
 * versions below, inlined to nothing (the device's code and its renders as before); the host targets (BLUEHARE_NATIVE:
 * the native image and the plugin's cores) define theirs in target_host.c:
 *   TGT_NOW()                the audio load meter's clock (audio.c): the device's timer, or the computer's
 *   TGT_INFO_VERSION         MENU > INFO's first row: FIRMWARE, or VERSION (an application)
 *   tgt_beat_samples()       a host's tempo in samples a beat, over BPM and CLOCK (fx.c beat_samples); 0: none
 *   tgt_seq_block(n, &mode)  the samples the sequencer moves on in a block of n (seq.c events_block); a host's
 *                            transport sets the clock mode to internal and follows the host
 *   tgt_block_end(n)         a block of n rendered (seq.c: the host's frame count)
 *   tgt_track_out(k, n, mg)  1: track k went to its own output, not the main mix (fx.c mix_track)
 *   tgt_mix_end(n)           the mix's block of n done (fx.c mix_block: the own outputs' write position)
 * Left where they are: what a host target has more of (DEV_NAT sizes, the native engines and reverbs). */
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
#endif
