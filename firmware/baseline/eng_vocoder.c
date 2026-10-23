/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-19) VOCODER: ONYX's phrases and SING's vowels (onyx_lpc.h: Kokoro's voices as LPC frames) shaping a synth:
 * the frames' filter, loudness and timing as ONYX speaks them, but what goes through the lattice is a carrier at the
 * key's pitch (C3 130.8 Hz, the true one: a chord on the keys is a chord), not the voice's pulses. The robot choir of
 * the vocoder records; the voice's own intonation is gone, its words and vowels stay. The unvoiced frames (the s, the
 * t, the sh) are spoken with noise, as a vocoder passes its sibilants, so the words stay words.
 * WORD of BANK as ONYX's; END STOP, HOLD (the last sounding frame held while the key is: SING's vowels as a pad), LOOP;
 * RATE half .. double. CARR: SAW, SQUARE, SUPER (seven saws, SPRD their spread); SPRD: SAW and SQUARE a second one
 * detuned (0 .. 25 cents), SUPER the seven over +-0 .. 50 cents; FORM: the filter's clock (the formants up or down, the
 * carrier's pitch kept); VOICE: whose frames (ONYX HEART GEORGE NICOLE: their formants, the women's higher).
 * The carrier runs at the filter's rate (8820 Hz), its steps polyBLEP'd; the saw's own tilt stands for the
 * de-emphasis ONYX undoes the pulses' with (a little kept: the sibilants' hiss). Output held and smoothed as ONYX's.
 * The frame reader, the glide and the lattice are ONYX's (eng_onyx.c: engines.c includes it first) */
static const char *const N_VC_CARR[] = {"SAW", "SQUARE", "SUPER", 0};
#define VC_NOSC 7
#define VC_GAIN 6.0f             /* (the carrier's loudness against ONYX's pulses: measured, tests/baseline/onyx_test.c) */

typedef struct {
    ox_voice_t s;                /* the frames (ONYX's reader) */
    float ph[VC_NOSC];           /* the carrier's phases (cycles) */
} vc_voice_t;
static vc_voice_t vc_v[NTRK][OX_POLY];

static vc_voice_t *vc_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < OX_POLY ? &vc_v[t - trk][i] : 0;
}

static inline float vc_blep(float t, float dt)        /* polyBLEP: the step's residual at phase t for a step dt */
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float vc_saw(float *ph, float dt)        /* a saw, -1 .. 1, one step on */
{
    float t = *ph, y = 2.0f * t - 1.0f - vc_blep(t, dt);
    t += dt;
    *ph = t - (t >= 1.0f ? 1.0f : 0.0f);
    return y;
}
static inline float vc_square(float *ph, float dt)
{
    float t = *ph, t2 = t + 0.5f, y;
    t2 -= t2 >= 1.0f ? 1.0f : 0.0f;
    y = (t < 0.5f ? 1.0f : -1.0f) + vc_blep(t, dt) - vc_blep(t2, dt);
    t += dt;
    *ph = t - (t >= 1.0f ? 1.0f : 0.0f);
    return y;
}

static void vocoder_note_on(track_t *t, voice_t *v)
{
    vc_voice_t *c = vc_of(t, v);
    uint32_t k;
    if (!c)
        return;
    ox_start(t, v, &c->s, ox_phrase_of(t, v, 0), (uint32_t)t->p[P_E7]);
    for (k = 0; k < VC_NOSC; k++)
        c->ph[k] = (float)((k * 0x9E3779B9u) >> 8) * (1.0f / 16777216.0f);   /* (spread: no click of seven at once) */
}

static void vocoder_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const float SUPER[VC_NOSC] = {0.0f, -1.0f, 1.0f, -0.62f, 0.62f, -0.28f, 0.28f};
    vc_voice_t *c = vc_of(t, v);
    ox_voice_t *s;
    uint32_t i, k, clk, end = (uint32_t)t->p[P_E2], carr = (uint32_t)t->p[P_E4], nosc;
    float flen, frac, e, r, dt[VC_NOSC], sprd = (float)t->p[P_E5] / 127.0f, norm;
    if (!c)
        return;
    s = &c->s;
    r = fm_exp2f((float)t->p[P_E6] * (1.5f / 192.0f) * 3.0f);   /* FORM: the filter's clock, 0.7 .. 1.4 (ONYX's) */
    clk = (uint32_t)(13107.0f * r);
    flen = (float)ONYX_N / fm_exp2f((float)(t->p[P_E3] - 64) / 64.0f);
    {   /* the carrier's steps (cycles a filter sample): the key's pitch, C3 130.81 Hz; SPRD's detune */
        float f0 = 130.81f * fm_exp2f((float)(m->pitch16 - 48 * 16) * (1.0f / 192.0f)) / ((float)ONYX_FS * r);
        nosc = carr == 2u ? VC_NOSC : sprd > 0.0f ? 2u : 1u;
        for (k = 0; k < nosc; k++) {
            float cents = carr == 2u ? SUPER[k] * 50.0f * sprd : (float)k * 25.0f * sprd;
            dt[k] = fm_clampf(f0 * fm_exp2f(cents * (1.0f / 1200.0f)), 1e-5f, 0.45f);
        }
        norm = carr == 2u ? 0.45f : nosc == 2u ? 0.7f : 1.0f;   /* (the sum of several at one's loudness, about) */
    }
    for (i = 0; i < n; i++) {
        s->clk += clk;
        if (s->clk >= 65536u) {                         /* a filter sample */
            float x = 0;
            s->clk -= 65536u;
            frac = ox_advance(t, s, flen, end, v->gate);
            e = s->e0 + (s->e1 - s->e0) * frac;
            if (s->voiced) {
                for (k = 0; k < nosc; k++)
                    x += carr == 1u ? vc_square(&c->ph[k], dt[k]) : vc_saw(&c->ph[k], dt[k]);
                x *= e * norm * VC_GAIN;
            } else
                x = ox_noise(s) * e * 1.7f;
            s->de = ox_lattice(s, frac, x) + 0.5f * s->de;
            s->y = ox_dc(s, s->de * 0.35f);
        }
        s->lp += (s->y - s->lp) * 0.5f;
        out[i] += voice_amp((int32_t)s->lp, m, i) << 1;
    }
}

/* {WORD, BANK, END, RATE, CARR, SPRD, FORM, VOICE}; BANK as ONYX's (14 SING: 0 AH 2 EE 3 OH 4 OO 7 WAH); END 0 STOP
 * 1 HOLD 2 LOOP; CARR 0 SAW 1 SQUARE 2 SUPER; VOICE 0 ONYX 1 HEART 2 GEORGE 3 NICOLE */
static const preset_t VOCODER_PRESETS[] = {
    {"ROBOT", {0, 3, 0, 64, 0, 0, 0, 0}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 30, 25)},
    {"VOCODE PAD", {0, 14, 1, 64, 2, 70, 0, 1}, {60, 0, 127, 80}, 0, 0, FX(0, 40, 30, 70)},
    {"CHOIR OO", {4, 14, 1, 64, 2, 50, -6, 2}, {40, 0, 127, 70}, 0, 0, FX(0, 30, 25, 75)},
    {"REPEAT", {0, 6, 2, 64, 1, 20, 0, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 40, 30)},
    {"DATA SQUARE", {0, 0, 0, 70, 1, 0, 8, 1}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 35, 30)},
    {"SIGNAL FIRE", {6, 9, 0, 60, 2, 60, 0, 2}, {0, 0, 127, 40}, 0, 0, FX(20, 0, 45, 40)},
    {"WAH CHORDS", {7, 14, 0, 64, 0, 40, 0, 1}, {0, 70, 90, 30}, 0, 0, FX(0, 20, 50, 45)},
    {"EUPHORIA", {0, 8, 0, 60, 2, 80, 0, 1}, {10, 0, 127, 60}, 0, 0, FX(0, 30, 45, 65)},
};

static const engine_t ENG_VOCODER = {
    .name = "VOCODER",
    .id = EID_VOCODER,
    .cc = 0x76,                  /* CC74 FORM, CC71 SPRD */
    .cats = "LPPLLLKP",
    .page_title = {"PHRASE", "CARRIER"},
    .edit = {
        {"WORD", F_ENUM, 0, (int16_t)(NELEM(ONYX_NAMES_3) - 2), 0, ONYX_NAMES_3, 0},
        {"BANK", F_ENUM, 0, ONYX_NBANK - 1, 3, ONYX_BANK_NAMES, 0},
        {"END", F_ENUM, 0, 2, 0, N_OX_END, 0},
        {"RATE", F_PCT, 0, 127, 64, 0, 0},
        {"CARR", F_ENUM, 0, 2, 0, N_VC_CARR, 0},
        {"SPRD", F_PCT, 0, 127, 0, 0, 0},
        {"FORM", F_INT, -64, 63, 0, 0, 0},
        {"VOICE", F_ENUM, 0, ONYX_NVOICE - 1, 0, ONYX_VOICE_NAMES, 0},
    },
    .presets = VOCODER_PRESETS,
    .npresets = NELEM(VOCODER_PRESETS),
    .poly = OX_POLY,
    .note_on = vocoder_note_on,
    .render = vocoder_render,
    .desc = onyx_desc,
    .knob = {P_E0, P_E3, P_E6, P_E5},
};
