/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* WEST (1.5): west coast. A complex oscillator, a wavefolder and a low-pass gate, after the Buchla way of building a
 * voice (Blue Hare's own design: no circuit modelled).
 *
 * A modulating sine at RATIO times the note (whole and half ratios, and four inharmonic ones for bells and metal)
 * moves the phase of the carrier sine by FM; FDBK feeds the modulator back into itself (sine -> saw-like -> noise).
 * The carrier goes through a sine folder: FOLD sets how many times it folds over (0: once, a soft sine), SYM shifts it
 * off centre (even harmonics; the DC blocked). The SHP routings move FOLD. Then the low-pass gate: a filter whose
 * cutoff follows the envelope through a vactrol-like lag (quick to open, slower to close), LPG of the way from CUT
 * down to closed, so a note darkens as it fades: the plucked, struck sound of the form. TMBR opens FM and FOLD with
 * the same lagged envelope (a hit is brighter at its start). FM and FOLD give way on high notes so the spectrum stays
 * below ~16 kHz (no audible aliasing): high notes are plainer.
 * Voice state: ph[0] the carrier, ph[1] the modulator, s[0] s[1] the filter, s[2] the vactrol (Q15), s[3] the
 * modulator's last output (feedback), s[4] s[5] the DC blocker's last in / out. */
static const char *const N_WEST_RATIO[] = {"1/2", "1", "3/2", "2", "5/2", "3", "4", "5", "6", "7", "8", "1.41", "1.73",
                                          "2.76", "3.55", "4.61", 0};
static const uint16_t WEST_RATIO[16] = {128, 256, 384, 512, 640, 768, 1024, 1280, 1536, 1792, 2048, 361, 443, 707, 909,
                                        1180};   /* Q8 */
#define WEST_BW 1558264779u     /* 16 kHz as a phase increment (16000 x 2^32 / 44100): the spectrum's top; / (inc >> 8):
                                 * that in notes, Q8. (Blue Hare: was 14 kHz; 16 keeps the aliasing ~-43 dB or less
                                 * up to C7, measured against 8x: high notes keep more of their FM and fold) */

static void west_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = v->s[1] = 0;
    v->s[2] = 0;                                      /* the gate closed: every note strikes it open */
    v->s[3] = v->s[4] = v->s[5] = 0;
}

static void west_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t rm = WEST_RATIO[(uint32_t)p[P_E0] & 15u], inc = m->inc, minc, i;
    int32_t vc = v->s[2], env = m->envq15, tm = p[P_E4];
    int32_t fold = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8) >> 8;
    int32_t idx, a, wx, amax, bw = (int32_t)(WEST_BW / ((inc >> 8) | 4096u)), g, fq, fb = p[P_E7], bias = p[P_E3] << 8;
    int32_t cut, ic1 = v->s[0], ic2 = v->s[1], prev = v->s[3], x1 = v->s[4], y1 = v->s[5];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];
    tsvf_t flt;
    vc += env > vc ? (env - vc) >> 1 : (env - vc) >> 4;   /* the vactrol: opens in ~1.5 ms, closes in ~12 ms */
    /* FM's index (radians, Q8) and the fold's (a sine of a x: a in radians, Q8; 402 = pi / 2 a quarter cycle), opened
     * by TMBR. The spectrum's top, in notes: FM's sidebands reach 1 + (idx + 1) x ratio (Carson; a small index: 1 +
     * 2 idx x ratio), the folder widens that (a + 1) times, less at its least (fold 0: the 3rd harmonic at -18 dB,
     * the 5th at -45): (a + 0.22). Kept under bw (14 kHz / the note): first FM gives way, then the fold */
    idx = p[P_E1] * 10 + ((tm * 8 * vc) >> 15);
    a = 402 + fold * 30 + ((tm * 24 * vc) >> 15);
    wx = 256 + (((idx + (idx < 256 ? idx : 256)) * (int32_t)rm) >> 8);
    amax = (bw << 8) / 458;                           /* the widest FM may be, the fold at its least (a + 0.22: 458) */
    if (wx > amax) {                                  /* (the first sideband past the top: none, however small) */
        int32_t r = amax > 256 + (int32_t)rm ? ((amax - 256) << 8) / (int32_t)rm : 0;   /* = idx + min(idx, 1) */
        idx = r <= 512 ? r >> 1 : r - 256;
        wx = 256 + (((idx + (idx < 256 ? idx : 256)) * (int32_t)rm) >> 8);
    }
    amax = (bw << 8) / wx - 56;
    a = clamp(a, 402, amax > 402 ? amax : 402);
    g = a * 256 / 402;                                /* the fold: quarter cycles at full scale, Q8 */
    fq = idx * 20861 >> 12;                           /* idx as phase per unit of the modulator, >> 4: 2^32 / 2pi / 2^15 */
    minc = (inc >> 8) >= 0xFFFFFFFFu / rm ? 0xFFFFFFFFu : (inc >> 8) * rm;
    cut = (p[P_E6] << 8) + m->cutoff + (v->pitch16 - 60 * 16) * 4 - ((p[P_E5] * (32767 - vc)) >> 7);
    tsvf_coef(&flt, cut, 12);
    for (i = 0; i < n; i++) {
        int32_t mo = sine_i(ph1 + ((uint32_t)(prev * fb) << 8)), c, s, y;
        prev = (prev + mo) >> 1;                      /* (the two last averaged: the feedback stays calm until high) */
        c = sine_i(ph0 + ((uint32_t)(mo * fq) << 4));
        s = sine_i((uint32_t)((c + bias) * g) << 7);   /* the folder: 2^30 a quarter cycle */
        y = s - x1 + y1 - (y1 >> 9);                  /* the DC blocker (SYM): ~14 Hz */
        x1 = s;
        y1 = y;
        s = soft_knee(tsvf_lp(&flt, y >> 1, &ic1, &ic2), 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;           /* (ANALOG's level: a full-scale sine as its saws) */
        ph0 += inc;
        ph1 += minc;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = vc;
    v->s[3] = prev;
    v->s[4] = x1;
    v->s[5] = y1;
}

static const preset_t WEST_PRESETS[] = {
    /* struck: a short envelope the gate follows shut */
    {"BONGO", {1, 18, 10, 0, 70, 120, 110, 0}, {0, 46, 0, 40}, 0, 0, FX(0, 0, 30, 25)},
    {"LOG DRUM", {2, 30, 20, 8, 60, 115, 100, 0}, {0, 52, 0, 48}, 0, 0, FX(0, 0, 35, 30)},
    {"METAL", {13, 45, 25, 0, 80, 110, 120, 0}, {0, 62, 0, 58}, 0, 0, FX(0, 10, 40, 35)},
    {"FOLD BASS", {0, 10, 45, 12, 50, 70, 80, 0}, {0, 70, 60, 30}, 0, 1, FX(10, 0, 10, 5)},
    {"TIN BELL", {14, 50, 10, 0, 60, 60, 120, 0}, {0, 95, 0, 90}, 0, 0, FX(0, 20, 45, 45)},
    {"FOLD LEAD", {1, 0, 70, 20, 30, 40, 110, 8}, {2, 70, 90, 40}, 0, 1, FX(0, 0, 40, 20)},
    {"DRONE", {3, 25, 40, 0, 0, 30, 90, 0}, {70, 90, 110, 90}, 0, 0, FX(0, 50, 25, 70)},
    {"RIM NOISE", {15, 60, 30, 0, 90, 120, 120, 100}, {0, 30, 0, 25}, 0, 0, FX(0, 0, 30, 20)},
    /* (Blue Hare, the IDM review) warm: the gate hardly closing, slow envelopes */
    {"FOLD PAD", {1, 15, 35, 10, 20, 20, 80, 0}, {80, 90, 110, 100}, 0, 0, FX(0, 70, 25, 80)},
    {"WARM DRONE", {0, 10, 30, 5, 0, 10, 70, 0}, {100, 90, 120, 110}, 0, 0, FX(0, 70, 25, 80)},
    /* struck and plucked */
    {"WEST PLUCK", {3, 30, 30, 0, 70, 110, 110, 0}, {0, 55, 0, 50}, 0, 0, FX(0, 10, 40, 40)},
    {"FOLD MARIMBA", {6, 25, 0, 0, 80, 120, 100, 0}, {0, 50, 0, 45}, 0, 0, FX(0, 0, 25, 35)},
    {"GONG", {13, 60, 30, 10, 60, 60, 110, 20}, {0, 115, 0, 110}, 0, 0, FX(0, 20, 30, 70)},
    {"WOOD BASS", {1, 20, 20, 0, 60, 100, 70, 0}, {0, 60, 40, 30}, 0, 1, FX(0, 0, 10, 10)},
    /* (Blue Hare, the 8-track plan) plucks, KARPLUS's place in IDM: struck bright (TMBR), the gate shutting (LPG high) */
    {"LPG HARP", {3, 20, 5, 0, 60, 110, 115, 0}, {0, 105, 0, 95}, 0, 0, FX(0, 10, 30, 45)},
    {"NYLON GATE", {1, 25, 15, 10, 50, 105, 110, 0}, {0, 98, 0, 85}, 0, 0, FX(0, 10, 20, 30)},
    {"KOTO FOLD", {5, 35, 30, 20, 80, 115, 115, 0}, {0, 95, 0, 85}, 0, 0, FX(0, 0, 30, 35)},
    {"THUMB WOOD", {13, 20, 0, 0, 75, 118, 110, 0}, {0, 92, 0, 80}, 0, 0, FX(0, 0, 25, 35)},
    {"BUZZ PLUCK", {2, 50, 60, 30, 90, 112, 120, 20}, {0, 100, 0, 90}, 0, 0, FX(0, 10, 30, 40)},
    {"PIZZ GATE", {1, 10, 20, 0, 40, 100, 108, 0}, {0, 72, 0, 50}, 0, 0, FX(0, 0, 15, 30)},
    {"GLASS PLUCK", {14, 30, 10, 0, 70, 110, 125, 0}, {0, 108, 0, 100}, 0, 0, FX(0, 20, 40, 55)},
    {"RUBBER PLUCK", {0, 40, 40, -20, 85, 115, 105, 10}, {0, 90, 0, 75}, 0, 0, FX(0, 0, 25, 25)},
};

static const engine_t ENG_WEST = {
    .name = "WEST",
    .id = EID_WEST,
    .cc = 0x73,
    .cats = "DDUBKLPDPPUKFBUUUUUUUU",
    .page_title = {"OSC", "GATE"},
    .edit = {
        {"RATIO", F_ENUM, 0, 15, 1, N_WEST_RATIO, 0},
        {"FM", F_PCT, 0, 127, 20, 0, 0},
        {"FOLD", F_PCT, 0, 127, 20, 0, 0},
        {"SYM", F_INT, -64, 63, 0, 0, 0},
        {"TMBR", F_PCT, 0, 127, 50, 0, 0},
        {"LPG", F_PCT, 0, 127, 100, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 110, 0, 0},
        {"FDBK", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = WEST_PRESETS,
    .npresets = NELEM(WEST_PRESETS),
    .note_on = west_note_on,
    .render = west_render,
    .knob = {P_E1, P_E2, P_E4, P_DEC},
    .keep = 0x07,                /* the filter, the vactrol */
};
/* favorites.c: WEST's stars are one byte, BYTE's presets must not reach it ([15][8]: presets 64..71) */
_Static_assert(NELEM(WEST_PRESETS) <= 64u && NELEM(BYTE_PRESETS) <= 64u, "favorites.c FAV_E16, FAV_ROW26");
