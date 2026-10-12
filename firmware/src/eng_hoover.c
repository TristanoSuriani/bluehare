/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* HOOVER (1.5): the rave lead of the early 90s, after the Alpha Juno's "what the" patch (Fel-Tracker's own design: no
 * circuit modelled). Two detuned oscillators, each a pulse-width-modulated saw, a square an octave down, a bend into
 * the note, a low-pass; the chorus (a preset's CHO send) does the rest.
 *
 * A PWM saw is a saw plus the same saw PW of a cycle on: at PW 0 a saw, towards half a cycle its odd harmonics thin
 * out (the saw an octave up), so sweeping PW moves notches through the spectrum, the Juno's moving buzz; both saws
 * band-limited (polyBLEP). PWM sweeps PW with the voice's own triangle LFO at RATE; the SHP routings move PW too. DTN
 * detunes the second oscillator by up to 127 cents, SUB mixes the square an octave down. BEND starts each note that
 * many semitones away and slides it in over BTIM (the hoover's swoop: below, rising; above, falling). Then CUT, a
 * gently resonant low-pass (the key half tracked).
 * Voice state: ph[0] ph[1] the oscillators, ph[2] the sub, s[0] s[1] the filter, s[2] the bend (1/16 semitone, Q16),
 * s[3] the PWM LFO's phase. */
static void hoover_note_on(track_t *t, voice_t *v)
{
    v->ph[0] = 0;
    v->ph[1] = 0x55555555u;                           /* the detuned one a third of a cycle on: no flam at the start */
    v->ph[2] = 0;
    v->s[0] = v->s[1] = 0;
    v->s[2] = t->p[P_E5] * 16 * 65536;                /* the bend's start */
    v->s[3] = (int32_t)(v->age * 0x9E3779B9u);        /* the LFO somewhere: stacked notes do not sweep together */
}

static inline int32_t hoover_osc(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (osc_saw(ph, inc) + osc_saw(ph + pw, inc)) >> 1;
}

static void hoover_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t bend = v->s[2], tau = 1 + p[P_E6] * p[P_E6] / 8, pb, ct, lfo, pwq, sub = p[P_E4] * 258;
    uint32_t lph = (uint32_t)v->s[3], inc1, inc2, inc3, pw, i;
    int32_t cut = (p[P_E7] << 8) + m->cutoff + (v->pitch16 - 60 * 16) * 4;
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    tsvf_t flt;
    bend -= bend / tau;                               /* the bend slides in: BTIM 0 at once .. 127 ~1.5 s */
    if (bend > -65536 && bend < 65536)                /* (the last 1/16 semitone: there) */
        bend = 0;
    pb = m->pitch16 + (bend >> 16);
    ct = ((bend & 0xFFFF) * 100) >> 20;               /* the bend's fraction of 1/16 semitone, in cents (0..6) */
    inc1 = bend ? cents_inc(pb, ct, m->fine) : m->inc;
    inc2 = cents_inc(pb, ct + p[P_E3], m->fine);
    inc3 = inc1 >> 1;
    lph += 1560000u + (uint32_t)p[P_E2] * 233000u;   /* the PWM LFO, per block (1378 a second): 0.5 .. 10 Hz */
    lfo = osc_tri(lph);                               /* -32768..32767 */
    pwq = clamp((p[P_E0] << 8) + m->shape - (64 << 8) + ((lfo * p[P_E1]) >> 8), 0, 127 << 8);
    pw = (uint32_t)pwq * 66052u;                      /* 0 .. half a cycle */
    tsvf_coef(&flt, cut, 24);
    for (i = 0; i < n; i++) {
        int32_t s = (hoover_osc(ph0, inc1, pw) + hoover_osc(ph1, inc2, pw)) >> 1;
        if (sub)
            s += mulq15(osc_pulse(ph2, inc3, 0x80000000u), sub);
        s = soft_knee(tsvf_lp(&flt, s >> 1, &ic1, &ic2), 16000) << 1;
        out[i] += voice_amp(s, m, i) << 1;
        ph0 += inc1;
        ph1 += inc2;
        ph2 += inc3;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = bend;
    v->s[3] = (int32_t)lph;
}

static const preset_t HOOVER_PRESETS[] = {
    /* the lead: bent up into the note, a wide PWM, the sub, lots of chorus */
    {"HOOVER", {40, 70, 50, 30, 60, -5, 40, 110}, {0, 80, 110, 50}, 0, 1, FX(10, 110, 30, 30)},
    {"DOMINATOR", {60, 90, 40, 40, 80, -12, 60, 110}, {0, 90, 115, 60}, 0, 1, FX(20, 120, 25, 30)},
    {"RAVE STAB", {30, 40, 60, 25, 30, 0, 0, 100}, {0, 55, 0, 40}, 20, 0, FX(0, 90, 50, 35)},
    {"HOOV BASS", {20, 30, 30, 10, 100, -12, 15, 70}, {0, 70, 80, 25}, 10, 1, FX(10, 60, 10, 5)},
    {"HOOV PAD", {50, 60, 20, 20, 20, 0, 0, 90}, {60, 90, 110, 90}, 0, 0, FX(0, 110, 30, 70)},
    {"SIREN", {64, 100, 90, 50, 0, 12, 90, 120}, {0, 90, 120, 70}, 0, 1, FX(0, 80, 50, 40)},
    /* (Fel-Tracker, the IDM review) warm pads, strings: slow PWM, a little detune, the chorus */
    {"WARM SAWS", {50, 40, 20, 15, 30, 0, 0, 70}, {90, 90, 115, 100}, 0, 0, FX(0, 100, 25, 80)},
    {"STRINGS", {64, 50, 30, 25, 0, 0, 0, 85}, {70, 80, 120, 90}, 0, 0, FX(0, 120, 20, 70)},
    /* more of the rave: the detuned bass, brass, a wilder lead, the dive, a soft one */
    {"REESE BASS", {64, 0, 0, 20, 40, 0, 0, 60}, {0, 0, 127, 20}, 0, 1, FX(20, 60, 0, 5)},
    {"PWM BRASS", {40, 50, 40, 10, 0, 0, 0, 90}, {20, 60, 100, 40}, 20, 0, FX(0, 50, 20, 30)},
    {"MENTAL", {30, 110, 70, 50, 40, -24, 30, 120}, {0, 80, 120, 50}, 0, 1, FX(30, 110, 30, 30)},
    {"DIVE BOMB", {50, 80, 60, 30, 60, 24, 100, 110}, {0, 60, 90, 50}, 0, 1, FX(20, 80, 40, 30)},
    {"SOFT LEAD", {50, 30, 30, 8, 20, 0, 0, 80}, {10, 0, 127, 40}, 0, 1, FX(0, 60, 40, 40)},
};

static const engine_t ENG_HOOVER = {
    .name = "HOOVER",
    .id = EID_HOOVER,
    .cc = 0x81,
    .cats = "LLKBPFPPBLLFL",
    .page_title = {"OSC", "BEND"},
    .edit = {
        {"PW", F_PCT, 0, 127, 40, 0, 0},
        {"PWM", F_PCT, 0, 127, 60, 0, 0},
        {"RATE", F_PCT, 0, 127, 50, 0, 0},
        {"DTN", F_INT, 0, 127, 30, 0, "ct"},
        {"SUB", F_PCT, 0, 127, 60, 0, 0},
        {"BEND", F_SEMI, -24, 24, 0, 0, 0},
        {"BTIM", F_TIME, 0, 127, 40, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 110, 0, 0},
    },
    .presets = HOOVER_PRESETS,
    .npresets = NELEM(HOOVER_PRESETS),
    .note_on = hoover_note_on,
    .render = hoover_render,
    .knob = {P_E0, P_E1, P_E3, P_E7},
    .keep = 0x0B,                /* the filter, the LFO (a retrigger keeps the sweep going) */
};
/* favorites.c: HOOVER's stars: presets 0 .. 7 one byte ([15][9]), 8 .. 63 a row's tail (FAV_ROW26) */
_Static_assert(NELEM(HOOVER_PRESETS) <= 64u, "favorites.c FAV_E16, FAV_ROW26");
