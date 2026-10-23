/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* HOOVER (1.5): the rave lead of the early 90s, after the Alpha Juno's "what the" patch (Blue Hare's own design: no
 * circuit modelled). Back 2026-10-23 as a flavour of VA's core (eng_va.c): six voices, UNISON of six, the voice at
 * twice the rate. Two detuned oscillators, each a pulse-width-modulated saw, a square an octave down, a bend into
 * the note, a low-pass; the chorus (a preset's CHO send) does the rest.
 *
 * A PWM saw is a saw plus the same saw PW of a cycle on (VA_PWSAW): at PW 0 a saw, towards half a cycle its odd
 * harmonics thin out (the saw an octave up), so sweeping PW moves notches through the spectrum, the Juno's moving
 * buzz. PWM sweeps PW with the voice's own triangle LFO at RATE; the SHP routings move PW too. DTN detunes the second
 * oscillator by up to 127 cents, SUB mixes the square an octave down (the core's sub). BEND starts each note that
 * many semitones away and slides it in over BTIM (the hoover's swoop: below, rising; above, falling). Then CUT, the
 * core's 12 dB low-pass, gently resonant (the key half tracked).
 * Voice state: VA's core's (va_v); s[2] the bend (1/16 semitone, Q16), s[3] the PWM LFO's phase. */
static void hoover_note_on(track_t *t, voice_t *v)
{
    int sounding = v->env != 0;
    va_start(t, v);
    v->s[2] = t->p[P_E5] * 16 * 65536;                /* the bend's start */
    if (!sounding)
        v->s[3] = (int32_t)(v->age * 0x9E3779B9u);    /* the LFO somewhere: stacked notes do not sweep together */
}

static void hoover_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    va_voice_t *s = va_of(t, v);
    int32_t bend = v->s[2], tau = 1 + p[P_E6] * p[P_E6] / 8, pb, ct, lfo, pwq;
    uint32_t lph = (uint32_t)v->s[3];
    va_cfg_t c;
    if (!s)
        return;
    bend -= bend / tau;                               /* the bend slides in: BTIM 0 at once .. 127 ~1.5 s */
    if (bend > -65536 && bend < 65536)                /* (the last 1/16 semitone: there) */
        bend = 0;
    pb = m->pitch16 + (bend >> 16);
    ct = ((bend & 0xFFFF) * 100) >> 20;               /* the bend's fraction of 1/16 semitone, in cents (0..6) */
    lph += 1560000u + (uint32_t)p[P_E2] * 233000u;   /* the PWM LFO, per block (1378 a second): 0.5 .. 10 Hz */
    lfo = osc_tri(lph);                               /* -32768..32767 */
    pwq = clamp((p[P_E0] << 8) + m->shape - (64 << 8) + ((lfo * p[P_E1]) >> 8), 0, 127 << 8);
    c.nosc = 2;
    c.sync = 0;
    c.inc[0] = cents_inc(pb, ct, m->fine);
    c.inc[1] = cents_inc(pb, ct + p[P_E3], m->fine);
    c.incs = c.inc[0] >> 1;
    c.w[0] = c.w[1] = VA_PWSAW;
    c.ws = VA_PULSE;
    c.g[0] = c.g[1] = 0.5f;
    c.gs = (float)p[P_E4] * (0.5f / 127.0f);
    c.pw = (float)pwq * (0.5f / (127.0f * 256.0f));
    c.cut = (float)((p[P_E7] << 8) + m->cutoff + (v->pitch16 - 60 * 16) * 4) * (1.0f / 256.0f);
    c.res = 0.2f;
    c.ladder = 0;
    c.drive = 0.9f;
    va_run_out(s, out, n, m, &c);
    v->s[2] = bend;
    v->s[3] = (int32_t)lph;
}
static uint32_t hoover_unison(const track_t *t) { (void)t; return 6u; }

static const preset_t HOOVER_PRESETS[] = {
    /* the lead: bent up into the note, a wide PWM, the sub, lots of chorus */
    {"HOOVER", {40, 70, 50, 30, 60, -5, 40, 110}, {0, 80, 110, 50}, 0, 1, FX(10, 110, 30, 30), VA_UNI(45)},
    {"DOMINATOR", {60, 90, 40, 40, 80, -12, 60, 110}, {0, 90, 115, 60}, 0, 1, FX(20, 120, 25, 30), VA_UNI(45)},
    {"RAVE STAB", {30, 40, 60, 25, 30, 0, 0, 100}, {0, 55, 0, 40}, 20, 0, FX(0, 90, 50, 35)},
    {"HOOV BASS", {20, 30, 30, 10, 100, -12, 15, 70}, {0, 70, 80, 25}, 10, 1, FX(10, 60, 10, 5)},
    {"SIREN", {64, 100, 90, 50, 0, 12, 90, 120}, {0, 90, 120, 70}, 0, 1, FX(0, 80, 50, 40)},
    /* more of the rave: the detuned bass, brass, a wilder lead, the dive */
    {"REESE BASS", {64, 0, 0, 20, 40, 0, 0, 60}, {0, 0, 127, 20}, 0, 1, FX(20, 60, 0, 5)},
    {"PWM BRASS", {40, 50, 40, 10, 0, 0, 0, 90}, {20, 60, 100, 40}, 20, 0, FX(0, 50, 20, 30)},
    {"MENTAL", {30, 110, 70, 50, 40, -24, 30, 120}, {0, 80, 120, 50}, 0, 1, FX(30, 110, 30, 30), VA_UNI(45)},
    {"DIVE BOMB", {50, 80, 60, 30, 60, 24, 100, 110}, {0, 60, 90, 50}, 0, 1, FX(20, 80, 40, 30)},
};

static const engine_t ENG_HOOVER = {
    .name = "HOOVER",
    .id = EID_HOOVER,
    .cc = 0x81,
    .cats = "LLKBFBLLF",
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
    .poly = VA_POLY,
    .note_on = hoover_note_on,
    .render = hoover_render,
    .unison = hoover_unison,
    .knob = {P_E0, P_E1, P_E3, P_E7},
};
/* favorites.c: HOOVER's stars: presets 0 .. 7 one byte ([15][9]), 8 .. 63 a row's tail (FAV_ROW26) */
_Static_assert(NELEM(HOOVER_PRESETS) <= 64u, "favorites.c FAV_E16, FAV_ROW26");
