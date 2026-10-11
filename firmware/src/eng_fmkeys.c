/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FMKEYS (1.5): FM keyboards (Felucca's own, in the manner of the 80s FM electric pianos and bells), four operators as
 * two pairs. The body: a sine at the note whose phase a modulator at RATIO times the note moves by INDEX (FDBK feeds
 * the modulator back into itself: brassier, reedier); the index falls over DECAY to a quarter, so a note starts
 * bright and mellows, and harder notes start brighter (VEL: how much the velocity counts). The tine: a sine at the
 * note (DTUNE cents off the body: the beating of a real instrument) modulated at 14 times the note, its own fast
 * decay: the metallic strike of the attack, TINE its level. KSCL takes the brightness down up the keyboard (as an
 * instrument's: the top notes softer). The amplitude is the ADSR (the presets: SUS 0, a long decay). The index gives
 * way on high notes so the spectrum stays below ~14 kHz (as FMPERC).
 * Voice state: ph[0] the body's carrier, ph[1] the tine's carrier (its modulator: 14 times that phase), ph[2] the body's
 * modulator, s[0] the index envelope (Q15), s[1] the tine's envelope (Q15), s[2] the modulator's last output. */
static const char *const N_FK_RATIO[] = {"1/2", "1", "2", "3", "4", "5", "6", "7", "8", "11", "14", "1.41", "2.76",
                                        "3.5", "4.2", "7.1", 0};
static const uint16_t FK_RATIO[16] = {128, 256, 512, 768, 1024, 1280, 1536, 1792, 2048, 2816, 3584, 361, 707, 896,
                                      1075, 1818};   /* Q8 */

static void fmkeys_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[0] = v->s[1] = 32767;
    v->s[2] = 0;
}

static void fmkeys_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t rm = FK_RATIO[(uint32_t)p[P_E0] & 15u], inc = m->inc, minc, tinc, i;
    int32_t ix = v->s[0], te = v->s[1], prev = v->s[2], fb = p[P_E6], tl = p[P_E2] * 258;
    int32_t tau = 1 + p[P_E3] * p[P_E3] / 6, idx, bw, wx, fq, tq, vel, ks;
    ix -= (ix - 8192) / tau;                          /* the index falls to a quarter over DECAY */
    te -= te / (1 + tau / 8) + (te > 0);              /* the tine, eight times faster, to nothing */
    vel = 128 - ((p[P_E4] * (127 - v->vel)) >> 7);    /* VEL: soft notes duller (128 at full) */
    ks = 128 - ((p[P_E7] * clamp(m->pitch16 - 48 * 16, 0, 60 * 16)) / (60 * 16));   /* KSCL: from C3 up */
    idx = (((p[P_E1] * 12 * ix) >> 15) * vel >> 7) * ks >> 7;   /* radians Q8 (at most ~6) */
    bw = (int32_t)(FMP_BW / ((inc >> 8) | 4096u));
    wx = 256 + (((idx + (idx < 256 ? idx : 256)) * (int32_t)rm) >> 8);   /* (Carson: the sidebands' top, in notes) */
    if (wx > bw)
        idx = bw > 256 + (int32_t)rm ? (((bw - 256) << 8) / (int32_t)rm) >> 1 : 0;
    fq = idx * 20861 >> 12;                           /* idx as phase per unit of the modulator, >> 4 */
    tq = bw > 256 + 14 * 256 ? (te * vel) >> 7 : 0;   /* the tine's index (~1.5 rad at full), none if it aliases */
    minc = (inc >> 8) >= 0xFFFFFFFFu / rm ? 0xFFFFFFFFu : (inc >> 8) * rm;
    tinc = cents_inc(m->pitch16, p[P_E5], m->fine);   /* DTUNE */
    for (i = 0; i < n; i++) {
        int32_t mo = sine_i(v->ph[2] + ((uint32_t)(prev * fb) << 8)), s, tm;
        prev = (prev + mo) >> 1;
        s = sine_i(v->ph[0] + ((uint32_t)(mo * fq) << 4));
        tm = sine_i(v->ph[1] * 14u);
        s += (mulq15(sine_i(v->ph[1] + (uint32_t)(tm * tq)), tl) * te) >> 15;
        out[i] += voice_amp(clamp(s * 3 / 4, -32767, 32767), m, i) << 1;
        v->ph[0] += inc;
        v->ph[1] += tinc;
        v->ph[2] += minc;
    }
    v->s[0] = ix;
    v->s[1] = te;
    v->s[2] = prev;
}

/* {RATIO, INDEX, TINE, DECAY, VEL, DTUNE, FDBK, KSCL}; RATIO 0 1/2 1 1 2 2 3 3 .. 8 8, 9 11, 10 14, 11 1.41, 12 2.76,
 * 13 3.5, 14 4.2, 15 7.1 */
static const preset_t FMKEYS_PRESETS[] = {
    {"E.PIANO", {1, 40, 70, 60, 90, 4, 0, 60}, {0, 95, 0, 60}, 0, 0, FX(0, 40, 15, 25)},
    {"TINE EP", {1, 30, 110, 55, 100, 6, 0, 50}, {0, 100, 0, 60}, 0, 0, FX(0, 60, 20, 30)},
    {"SOFT EP", {1, 20, 40, 70, 60, 3, 0, 70}, {0, 100, 0, 70}, 0, 0, FX(0, 50, 20, 40)},
    {"REED EP", {1, 60, 30, 50, 80, 2, 40, 40}, {0, 90, 30, 50}, 0, 0, FX(15, 20, 15, 25)},
    {"GLASS BELL", {12, 70, 60, 90, 60, 7, 0, 30}, {0, 115, 0, 100}, 0, 0, FX(0, 30, 30, 60)},
    {"TUBULAR", {13, 60, 40, 100, 40, 5, 0, 20}, {0, 120, 0, 110}, 0, 0, FX(0, 20, 30, 70)},
    {"MARIMBA", {10, 50, 20, 25, 80, 0, 0, 40}, {0, 60, 0, 50}, 0, 0, FX(0, 0, 15, 30)},
    {"VIBES", {7, 40, 50, 60, 60, 8, 0, 30}, {0, 105, 0, 80}, 0, 0, FX(0, 60, 25, 45)},
    {"FM CLAV", {2, 90, 30, 30, 100, 0, 30, 30}, {0, 50, 20, 20}, 0, 0, FX(10, 30, 10, 15)},
    {"FM ORGAN", {2, 30, 0, 127, 0, 5, 20, 0}, {0, 0, 127, 20}, 0, 0, FX(10, 70, 10, 30)},
    {"FM HARPSI", {3, 80, 60, 30, 70, 3, 10, 40}, {0, 70, 0, 50}, 0, 0, FX(0, 20, 15, 30)},
    {"KALIMBA", {14, 45, 70, 35, 70, 0, 0, 40}, {0, 75, 0, 60}, 0, 0, FX(0, 10, 25, 40)},
    {"CELESTA", {8, 35, 80, 50, 60, 4, 0, 20}, {0, 90, 0, 80}, 0, 0, FX(0, 30, 30, 60)},
    {"MUSIC BOX", {15, 50, 90, 40, 50, 6, 0, 10}, {0, 85, 0, 75}, 0, 0, FX(0, 20, 40, 60)},
};

static const engine_t ENG_FMKEYS = {
    .name = "FMKEYS",
    .id = EID_FMKEYS,
    .cc = 0x24,                  /* CC74 INDEX, CC71 DECAY */
    .cats = "KKKKKKUKKKKUKK",
    .page_title = {"FM", "TOUCH"},
    .edit = {
        {"RATIO", F_ENUM, 0, 15, 1, N_FK_RATIO, 0},
        {"INDEX", F_PCT, 0, 127, 40, 0, 0},
        {"TINE", F_PCT, 0, 127, 70, 0, 0},
        {"DECAY", F_TIME, 0, 127, 60, 0, 0},
        {"VEL", F_PCT, 0, 127, 90, 0, 0},
        {"DTUNE", F_INT, 0, 30, 4, 0, "ct"},
        {"FDBK", F_PCT, 0, 127, 0, 0, 0},
        {"KSCL", F_PCT, 0, 127, 60, 0, 0},
    },
    .presets = FMKEYS_PRESETS,
    .npresets = NELEM(FMKEYS_PRESETS),
    .note_on = fmkeys_note_on,
    .render = fmkeys_render,
    .knob = {P_E1, P_E2, P_E3, P_DEC},
};
