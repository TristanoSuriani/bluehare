/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WAVE (1.5): wavetables. Felucca's own design and tables (tools/gen_wavetables.py: 8 banks of 8 single-cycle
 * frames, in flash).
 *
 * TBL picks the bank, POS the place between its frames (the two neighbours crossfaded): the SHP routings move it
 * (ENV DEST / LFO DEST SHP, the matrix's SHP: a step of shape is a step of POS), so an envelope or an LFO scans the
 * table. Every frame is stored 7 times, band-limited to 64, 32, .. 2 harmonics and the fundamental alone (512 .. 8
 * samples): each block plays the fullest copy whose harmonics stay below ~20 kHz at the voice's pitch, read with
 * linear interpolation (no aliasing on high notes, the same loudness on every copy).
 * SYNC (0 = off) reads the frame faster than the voice's cycle, 1x .. 8x (at most ~18 kHz: high notes sync less),
 * restarting it every cycle; a half-sine
 * window over the cycle takes the restart's step out (windowed sync: a formant-like sweep, no click); the restart
 * lands between samples where the cycle ends (no jitter: the sync stays on the note's harmonics). DTN adds a
 * second oscillator on the same frames, detuned by up to 127 cents (0: one oscillator, half the cost). Then DRV,
 * the low-pass (CUT RES, the key half tracked) and CRSH: the output held for 1 .. 16 samples and cut to fewer bits.
 * Voice state: ph[0] / ph[1] the oscillators' cycles, ph[2] / s[2] their synced reading phases, s[0] s[1] the
 * filter, s[3] the held sample, s[4] the hold count. */
#include "felucca_wavetables.h"
static const char *const N_WT_BANK[] = {WT_BANK_NAMES_INIT, 0};
static const uint16_t WT_LVL_OFF[7] = {0, 512, 768, 896, 960, 992, 1008};   /* each mip level's start in a frame */
#define WT_LIM 30434000u        /* phase increment of 344 Hz: 64 harmonics reach 20 kHz (0.4535 x 2^32 / 64) */
#define WT_RMAX 1750000000u     /* the fastest a synced frame is read: ~18 kHz */

/* the mip level for phase increment inc: harmonics (64 >> l) x the pitch below WT_LIM x 64 */
static inline uint32_t wt_level(uint32_t inc)
{
    uint32_t l = 0;
    while (l < 6u && inc >= (WT_LIM << l))
        l++;
    return l;
}

/* frame t's level (512 >> l samples) at phase ph, linearly interpolated */
static inline int32_t wt_read(const int16_t *t, uint32_t l, uint32_t ph)
{
    uint32_t i = ph >> (23u + l), f = (ph >> (8u + l)) & 0x7FFFu;
    int32_t a = t[i], b = t[(i + 1u) & ((512u >> l) - 1u)];
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
    uint32_t l1 = wt_level(rinc1), l2 = wt_level(rinc2), o1 = WT_LVL_OFF[l1], o2 = WT_LVL_OFF[l2];
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
    {"SYNC LEAD", {2, 70, 40, 0, 100, 20, 30, 0}, {2, 70, 100, 40}, 20, 1, FX(0, 10, 40, 25)},
    {"RESO BASS", {3, 48, 0, 0, 62, 45, 40, 0}, {0, 60, 60, 25}, 40, 1, FX(10, 0, 10, 5)},
    {"VOX PAD", {4, 64, 0, 10, 90, 10, 0, 0}, {70, 90, 115, 90}, 0, 0, FX(0, 50, 20, 65)},
    {"FM BELL", {5, 90, 0, 6, 110, 0, 0, 0}, {0, 95, 0, 85}, 0, 0, FX(0, 20, 40, 45)},
    {"DIGI KEYS", {6, 50, 0, 4, 96, 15, 0, 18}, {0, 80, 40, 55}, 20, 0, FX(0, 20, 30, 35)},
    {"BIT LEAD", {7, 80, 0, 0, 110, 10, 20, 40}, {0, 70, 100, 30}, 0, 1, FX(0, 0, 40, 20)},
};

static const engine_t ENG_WAVE = {
    .name = "WAVE",
    .id = EID_WAVE,
    .cc = 0x56,
    .cats = "KPLBPUKL",
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
