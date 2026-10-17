/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* WAVE (1.5): wavetables. Blue Hare's own design and tables (tools/gen_wavetables.py: 8 banks of 8 single-cycle
 * frames, in flash).
 *
 * TBL picks the bank, POS the place between its frames (the two neighbours crossfaded): the SHP routings move it
 * (ENV DEST / LFO DEST SHP, the matrix's SHP: a step of shape is a step of POS), so an envelope or an LFO scans the
 * table. Every frame is stored 8 times, band-limited to 256, 128, .. 2 harmonics (1024 .. 8 samples; 2026-10-17: was
 * 64 .. 1 in 512 .. 8, the low notes' highs cut): each block plays the fullest copy whose harmonics alias no lower
 * than ~20 kHz at the voice's pitch,
 * read with linear interpolation (no aliasing on high notes, the same loudness on every copy).
 * SYNC (0 = off) reads the frame faster than the voice's cycle, 1x .. 8x (at most ~18 kHz: high notes sync less),
 * restarting it every cycle; a half-sine
 * window over the cycle takes the restart's step out (windowed sync: a formant-like sweep, no click); the restart
 * lands between samples where the cycle ends (no jitter: the sync stays on the note's harmonics). DTN adds a
 * second oscillator on the same frames, detuned by up to 127 cents (0: one oscillator, half the cost). Then DRV,
 * the low-pass (CUT RES, the key half tracked) and CRSH: the output held for 1 .. 16 samples and cut to fewer bits.
 * Voice state: ph[0] / ph[1] the oscillators' cycles, ph[2] / s[2] their synced reading phases, s[0] s[1] the
 * filter, s[3] the held sample, s[4] the hold count. */
#include "bluehare_wavetables.h"
static const char *const N_WT_BANK[] = {WT_BANK_NAMES_INIT, 0};
#define WT_NLVL 8u
static const uint16_t WT_LVL_OFF[WT_NLVL] = {0, 1024, 1536, 1792, 1920, 1984, 2016, 2032};   /* each level's start */
_Static_assert(WT_FRAME_LEN == 2040, "gen_wavetables.py's levels: 1024 .. 8 samples");
#define WT_LIM 9130458u         /* phase increment of 94 Hz: 256 harmonics reach 24 kHz (0.5442 x 2^32 / 256), past
                                 * Nyquist but folding back above 20 kHz (Blue Hare: was 20 kHz, a copy switched to
                                 * half its harmonics ~20 % lower) */
#define WT_LIM_SYNC 7608500u    /* SYNC's: 86 Hz, 256 harmonics reach 20 kHz (its window spreads each harmonic: the
                                 * ones past Nyquist would fold below 20 kHz) */
#define WT_RMAX 1750000000u     /* the fastest a synced frame is read: ~18 kHz */

/* the mip level for phase increment inc: harmonics (256 >> l) x the pitch below lim x 256 */
static inline uint32_t wt_level(uint32_t inc, uint32_t lim)
{
    uint32_t l = 0;
    while (l < WT_NLVL - 1u && inc >= (lim << l))
        l++;
    return l;
}

/* frame t's level (1024 >> l samples) at phase ph, linearly interpolated */
static inline int32_t wt_read(const int16_t *t, uint32_t l, uint32_t ph)
{
    uint32_t i = ph >> (22u + l), f = (ph >> (7u + l)) & 0x7FFFu;
    int32_t a = t[i], b = t[(i + 1u) & ((1024u >> l) - 1u)];
    return a + (((b - a) * (int32_t)f) >> 15);
}

/* phase increment inc read ratio (Q8, < 8x) times as fast, at most WT_RMAX (~18 kHz: one harmonic still below
 * Nyquist) */
static inline uint32_t wt_speed(uint32_t inc, uint32_t ratio)
{
    return (inc >> 8) >= WT_RMAX / ratio ? WT_RMAX : (inc >> 8) * ratio;
}

static void wave_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;
    v->ph[1] = 0x40000000u;                           /* the detuned one a quarter cycle on: no flam at the start */
    v->ph[2] = 0;
    v->s[0] = v->s[1] = 0;
    v->s[2] = 0x40000000;
    v->s[3] = 0;
    v->s[4] = 0;
}

static void wave_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t pos = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    int32_t fq = pos * 7 / 127, xf = (fq & 255) << 7;   /* frame position, Q8; the crossfade to the next, Q15 */
    uint32_t fi = (uint32_t)fq >> 8, bank = (uint32_t)p[P_E0] % WT_NBANK;
    const int16_t *fa = WT_DATA + (bank * WT_NFRAME + fi) * WT_FRAME_LEN;
    const int16_t *fb = fi + 1u < WT_NFRAME ? fa + WT_FRAME_LEN : fa;
    uint32_t sync = (uint32_t)p[P_E2], det = (uint32_t)p[P_E3], i;
    uint32_t inc1 = m->inc, inc2 = det ? cents_inc(m->pitch16, (int32_t)det, m->fine) : 0u;
    uint32_t ratio = 256u + sync * 1792u / 127u;      /* SYNC: the reading speed, Q8 (1x .. 8x) */
    uint32_t rinc1 = sync ? wt_speed(inc1, ratio) : inc1, rinc2 = sync ? wt_speed(inc2, ratio) : inc2;
    uint32_t er1 = sync ? rinc1 / ((inc1 >> 8) | 1u) : 0u, er2 = sync && det ? rinc2 / ((inc2 >> 8) | 1u) : 0u;   /* (as
                                                       * read: WT_RMAX may have slowed it), Q8 */
    uint32_t lim = sync ? WT_LIM_SYNC : WT_LIM, l1 = wt_level(rinc1, lim), l2 = wt_level(rinc2, lim);
    uint32_t o1 = WT_LVL_OFF[l1], o2 = WT_LVL_OFF[l2];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (v->pitch16 - 60 * 16) * 4;
    int32_t drv = p[P_E6], drive = 32768 + drv * 512, crsh = p[P_E7];
    int32_t hold = 1 + (crsh >> 3), mask = ~((1 << (crsh >> 3)) - 1);
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], r0 = v->ph[2], r1 = (uint32_t)v->s[2];
    int32_t ic1 = v->s[0], ic2 = v->s[1], held = v->s[3], cnt = v->s[4];
    tsvf_t flt;
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t a, b, s;
        if (sync) {                                   /* read at the sync speed, restarted with the cycle, windowed */
            a = wt_read(fa + o1, l1, r0);
            a += ((wt_read(fb + o1, l1, r0) - a) * xf) >> 15;
            a = (a * (sine_i(ph0 >> 1) >> 1)) >> 14;
            r0 += rinc1;
        } else {
            a = wt_read(fa + o1, l1, ph0);
            a += ((wt_read(fb + o1, l1, ph0) - a) * xf) >> 15;
        }
        ph0 += inc1;
        if (sync && ph0 < inc1)                       /* the cycle ended: restart where it did, between samples */
            r0 = (ph0 >> 8) * er1;
        if (det) {                                    /* the second oscillator, as the first */
            uint32_t q = sync ? r1 : ph1;
            b = wt_read(fa + o2, l2, q);
            b += ((wt_read(fb + o2, l2, q) - b) * xf) >> 15;
            if (sync) {
                b = (b * (sine_i(ph1 >> 1) >> 1)) >> 14;
                r1 += rinc2;
            }
            ph1 += inc2;
            if (sync && ph1 < inc2)
                r1 = (ph1 >> 8) * er2;
            a = (a + b) >> 1;
        }
        s = a;
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);
        s = soft_knee(tsvf_lp(&flt, s >> 1, &ic1, &ic2), 16000) << 1;
        if (crsh) {                                   /* CRSH: held, fewer bits */
            if (--cnt <= 0) {
                cnt = hold;
                held = s & mask;
            }
            s = held;
        }
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = r0;
    v->s[2] = (int32_t)r1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[3] = held;
    v->s[4] = cnt;
}

static const preset_t WAVE_PRESETS[] = {
    /* a short filtered stab for chords into the delay (dub techno) */
    {"DUB CHORD", {0, 60, 0, 12, 52, 30, 0, 0}, {0, 62, 0, 55}, 34, 0, FX(0, 20, 60, 50)},
    {"SCAN PAD", {1, 40, 0, 18, 80, 10, 0, 0}, {80, 90, 115, 95}, 10, 0, FX(0, 50, 25, 70)},
    {"TABLE SYNC", {2, 70, 40, 0, 100, 20, 30, 0}, {2, 70, 100, 40}, 20, 1, FX(0, 10, 40, 25)},
    {"RESO BASS", {3, 48, 0, 0, 62, 45, 40, 0}, {0, 60, 60, 25}, 40, 1, FX(10, 0, 10, 5)},
    {"VOX PAD", {4, 64, 0, 10, 90, 10, 0, 0}, {70, 90, 115, 90}, 0, 0, FX(0, 50, 20, 65)},
    {"TABLE BELL", {5, 90, 0, 6, 110, 0, 0, 0}, {0, 95, 0, 85}, 0, 0, FX(0, 20, 40, 45)},
    {"DIGI KEYS", {6, 50, 0, 4, 96, 15, 0, 18}, {0, 80, 40, 55}, 20, 0, FX(0, 20, 30, 35)},
    {"BIT LEAD", {7, 80, 0, 0, 110, 10, 20, 40}, {0, 70, 100, 30}, 0, 1, FX(0, 0, 40, 20)},
    /* (Blue Hare, the IDM review) warm pads: slow, detuned, dark, plenty of chorus and reverb */
    {"WARM PAD", {0, 20, 0, 24, 60, 10, 0, 0}, {90, 90, 115, 100}, 10, 0, FX(0, 70, 20, 75)},
    {"STRING PAD", {1, 50, 0, 30, 72, 5, 0, 0}, {100, 90, 120, 105}, 8, 0, FX(0, 90, 20, 80)},
    {"GLASS PAD", {5, 30, 0, 16, 100, 10, 0, 0}, {80, 100, 110, 110}, 0, 0, FX(0, 60, 35, 85)},
    {"CHOIR PAD", {4, 90, 0, 20, 85, 10, 0, 0}, {90, 90, 120, 100}, 0, 0, FX(0, 80, 20, 80)},
    /* basses, an arpeggio's pluck */
    {"SUB BASS", {0, 0, 0, 0, 50, 10, 20, 0}, {0, 50, 100, 20}, 10, 1, FX(10, 0, 0, 5)},
    {"SYNC BASS", {2, 50, 60, 0, 70, 30, 40, 0}, {0, 60, 70, 20}, 30, 1, FX(15, 0, 10, 5)},
    {"BIT ARP", {7, 40, 0, 0, 110, 0, 0, 60}, {0, 40, 0, 30}, 0, 0, FX(0, 0, 50, 20)},
    /* (Blue Hare) the table moving: an LFO scanning it (the frames morphing), or each note's envelope (lrate: the
     * LFO's RATE + 1, ~0.2 Hz at 26; lwave: its WAVE + 1, 1 SIN 2 TRI 5 S&H) */
    {"MORPH PAD", {1, 50, 0, 16, 85, 10, 0, 0}, {80, 90, 115, 95}, 0, 0, FX(0, 60, 25, 75), .lshp = 50, .lrate = 26},
    {"VOX MORPH", {4, 64, 0, 12, 90, 10, 0, 0}, {70, 90, 120, 95}, 0, 0, FX(0, 60, 25, 70), .lshp = 63, .lrate = 31,
     .lwave = 2},
    {"SWEEP LEAD", {2, 15, 0, 6, 100, 15, 20, 0}, {0, 70, 100, 30}, 0, 1, FX(0, 10, 40, 25), .eshp = 60},
    {"SCAN BASS", {0, 15, 0, 0, 100, 30, 30, 0}, {0, 70, 40, 20}, 20, 1, FX(10, 0, 10, 5), .eshp = 63},
    {"BELL SCAN", {5, 10, 0, 6, 110, 0, 0, 0}, {0, 95, 0, 85}, 0, 0, FX(0, 20, 40, 45), .eshp = 63},
    {"WOBBLE", {0, 64, 0, 4, 80, 25, 40, 0}, {0, 0, 127, 20}, 0, 1, FX(15, 0, 10, 5), .lshp = 55, .lrate = 68, .lwave = 1},
    {"DIGI STEPS", {6, 64, 0, 0, 100, 10, 0, 20}, {0, 0, 127, 30}, 0, 0, FX(0, 10, 40, 30), .lshp = 63, .lrate = 58,
     .lwave = 5},
    {"EVOLVER", {1, 30, 30, 20, 90, 15, 10, 0}, {90, 90, 120, 100}, 0, 0, FX(0, 70, 30, 80), .eshp = 30, .lshp = 35,
     .lrate = 22},
    {"SYNC SCAN", {2, 20, 70, 0, 100, 10, 30, 0}, {0, 60, 90, 30}, 0, 1, FX(0, 10, 40, 25), .lshp = 45, .lrate = 40},
};

static const engine_t ENG_WAVE = {
    .name = "WAVE",
    .id = EID_WAVE,
    .cc = 0x56,
    .cats = "KPLBPUKLPPPPBBUPPLBUBKPL",
    .page_title = {"WAVE", "TONE"},
    .edit = {
        {"TBL", F_ENUM, 0, WT_NBANK - 1, 0, N_WT_BANK, 0},
        {"POS", F_PCT, 0, 127, 0, 0, 0},
        {"SYNC", F_PCT, 0, 127, 0, 0, 0},
        {"DTN", F_INT, 0, 127, 0, 0, "ct"},
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = WAVE_PRESETS,
    .npresets = NELEM(WAVE_PRESETS),
    .note_on = wave_note_on,
    .render = wave_render,
    .knob = {P_E1, P_E4, P_E5, P_REL},
    .keep = 0x03,                /* the filter */
};
