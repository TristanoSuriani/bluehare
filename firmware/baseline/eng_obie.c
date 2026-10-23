/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* OBIE (2026-10-17; POLY for its first hours; since 2026-10-23 a flavour of VA's core, eng_va.c: six voices, UNISON of
 * six, oversampled as VA): an analog polysynth after the Oberheim OB-Xa of 1981 (Blue Hare's own design, in
 * float on the FPU: no circuit modelled, no code of anyone's). Each voice: two oscillators, band-limited (polyBLEP),
 * WAVE SAW, PULSE (PW: 50 % down to ~5 %; the SHAPE routings move it: PWM) or both; the second INTV semitones away
 * and DTN apart (0 .. ~25 cents: the thickness). WAVE SYNC: the second a saw hard-synced to the first, INTV its
 * pitch over it, the SHAPE routings sweeping it (ENV -> SHAPE: the sync lead), its restart band-limited too. Then the
 * filter, 12 dB (a state-variable low-pass: the open, warm OB sound) or 24 dB (a four-pole ladder, its feedback solved
 * for the sample), both zero-delay, the input saturating (a rational tanh), RES towards self-oscillation; CUT half
 * follows the keyboard, the velocity opens it (-1.5 .. +0.5 octave around 100: the original had none; MODAL showed
 * what a touch does). SLOP: the analog's spread, per voice: each oscillator a little off (up to ~6 cents) and the filter
 * (up to ~3 semitones), set as the voice starts, and a slow drift on top: a chord breathes, no two notes the same.
 * Voice state: VA's core's (va_v: the oscillators' phases, the filter, the offsets, the drift, the
 * synced oscillator's sample not yet out) */
static const char *const N_PO_WAVE[] = {"SAW", "PULSE", "SAW+P", "SYNC", 0};
static const char *const N_PO_FILT[] = {"12 dB", "24 dB", 0};

static void obie_note_on(track_t *t, voice_t *v) { va_start(t, v); }

/* OBIE through VA's core: WAVE the oscillators' (SYNC: osc 2 a saw synced to osc 1, osc 1 unheard), PW their
 * width (the SHAPE routings: PWM, or with SYNC the synced pitch), DTN apart, INTV osc 2 away, the filter 12 dB
 * (SVF) or 24 dB (the ladder), SLOP the voice's spread and drift */
static void obie_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint8_t W[4] = {VA_SAW, VA_PULSE, VA_SAWP, VA_SAW};
    const int16_t *p = t->p;
    va_voice_t *s = va_of(t, v);
    uint32_t wave = (uint32_t)p[P_E0] & 3u;
    int32_t slop = p[P_E7], ct, ivl, sh = m->shape - (64 << 8);
    va_cfg_t c;
    if (!s)
        return;
    ct = va_drift(s, slop);                           /* (up to ~+-4 cents with SLOP at 127) */
    c.nosc = 2;
    c.sync = wave == 3u;                              /* SYNC: osc 2 a saw synced to osc 1, osc 1 unheard */
    c.inc[0] = cents_inc(m->pitch16, ct + s->o1 * slop / 2600 - p[P_E2] / 10, m->fine);
    ivl = p[P_E3] * 100;
    if (wave == 3u)                                   /* SYNC: the SHAPE routings sweep the synced pitch up */
        ivl += (sh > 0 ? sh : 0) * 3600 / (64 << 8);
    c.inc[1] = cents_inc(m->pitch16, ct + ivl + s->o2 * slop / 2600 + p[P_E2] / 10, m->fine);
    c.incs = 0;
    c.w[0] = c.w[1] = W[wave];
    c.ws = VA_SAW;
    c.g[0] = wave == 3u ? 0.0f : 0.5f;
    c.g[1] = wave == 3u ? 1.0f : 0.5f;
    c.gs = 0.0f;
    c.pw = 0.5f - 0.45f / (127.0f * 256.0f) * (float)clamp((p[P_E1] << 8) + (wave == 3u ? 0 : sh), 0, 127 << 8);
    c.cut = va_cut(v, m, p[P_E4], s->of, slop);
    c.res = (float)p[P_E5] * (1.0f / 127.0f);
    c.ladder = (uint8_t)(p[P_E6] & 1);
    c.drive = c.ladder ? 1.0f : 1.2f;
    va_run_out(s, out, n, m, &c);
}
static uint32_t obie_unison(const track_t *t) { (void)t; return 6u; }

/* {WAVE, PW, DTN, INTV, CUT, RES, FILT, SLOP}; WAVE 0 SAW 1 PULSE 2 SAW+P 3 SYNC; FILT 0 12 dB 1 24 dB */
static const preset_t OBIE_PRESETS[] = {
    {"OB BRASS", {0, 64, 30, 0, 55, 20, 0, 50}, {5, 70, 100, 40}, 40, 0, FX(0, 30, 10, 40)},
    {"BIG STAB", {0, 64, 25, 0, 70, 10, 0, 40}, {0, 60, 90, 30}, 30, 0, FX(0, 30, 15, 45)},
    {"OB STRINGS", {0, 64, 45, 0, 75, 5, 0, 60}, {70, 90, 115, 85}, 0, 0, FX(0, 70, 15, 70)},
    {"PWM PAD", {1, 40, 20, 0, 70, 10, 0, 50}, {80, 90, 115, 95}, 0, 0, FX(0, 50, 20, 75), .lshp = 30, .lrate = 20},
    {"BRASS SWELL", {0, 64, 30, 0, 40, 25, 1, 45}, {60, 80, 110, 60}, 50, 0, FX(0, 30, 15, 55)},
    {"SYNC LEAD", {3, 64, 0, 12, 100, 15, 1, 20}, {0, 80, 100, 30}, 0, 1, FX(10, 0, 35, 25), .eshp = 45},
    {"SQUARE LEAD", {1, 0, 10, 12, 85, 20, 1, 25}, {0, 70, 110, 35}, 20, 1, FX(0, 10, 40, 30)},
    {"POLY BASS", {0, 64, 10, -12, 45, 35, 1, 20}, {0, 55, 70, 15}, 45, 1, FX(5, 0, 0, 5)},
    {"RESO SWEEP", {2, 64, 15, 0, 30, 95, 1, 30}, {0, 100, 60, 60}, 55, 0, FX(0, 20, 30, 50)},
    {"OB KEYS", {2, 64, 20, 0, 60, 15, 0, 35}, {0, 60, 40, 30}, 35, 0, FX(0, 30, 15, 40)},
    {"FIFTHS STAB", {0, 64, 15, 7, 65, 20, 0, 30}, {0, 70, 90, 40}, 30, 0, FX(0, 20, 25, 45)},
};

static const engine_t ENG_OBIE = {
    .name = "OBIE",
    .id = EID_OBIE,
    .cc = 0x56,                  /* CC74 CUT, CC71 RES */
    .cats = "LLPPLLLBFKL",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 0, N_PO_WAVE, 0},
        {"PW", F_PCT, 0, 127, 0, 0, 0},
        {"DTN", F_PCT, 0, 127, 20, 0, 0},
        {"INTV", F_INT, -12, 24, 0, 0, "st"},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"FILT", F_ENUM, 0, 1, 0, N_PO_FILT, 0},
        {"SLOP", F_PCT, 0, 127, 40, 0, 0},
    },
    .presets = OBIE_PRESETS,
    .npresets = NELEM(OBIE_PRESETS),
    .poly = VA_POLY,
    .note_on = obie_note_on,
    .render = obie_render,
    .unison = obie_unison,
    .knob = {P_E4, P_E5, P_E2, P_REL},
    .plain = {P_E4, P_E5, P_E2, P_E7},
};
