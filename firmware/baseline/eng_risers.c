/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* RISERS (1.5; back 2026-10-24 on VA's core, eng_va.c, track 3: the user's "a risers ... engine based on VA"):
 * build-ups and their landings, timed to the song (Blue Hare's own). A note starts one, LEN beats long at the tempo
 * (1 .. 32); it climbs RANGE semitones (the pitch, or the noise's band) along CURVE (straight .. slow then fast) and
 * holds at the top while the key is held. TYPE: NOIS white noise through the core's resonant band-pass rising (RES),
 * SAW three of the core's saws (SPRD apart) rising through its ladder as it opens, SINE one sine rising, SHEP a Shepard
 * tone (five octaves of sines, each faded by where it stands: it rises for ever), DOWN the way down (the noise's band
 * and a sine falling: a downlifter), BOOM the landing (a sub sine dropping fast, a burst of noise). BUILD gates it
 * faster and faster towards the top (a snare roll's acceleration: quarters to 32nds), NOISE mixes noise into the
 * tonal ones. It swells in over the first half (BOOM: struck). The ADSR's release ends it.
 * Voice state: VA's core's (va_v: the oscillators, the filter); s[0] blocks since the note, s[1] SHEP's climb (1/16
 * semitone Q16, wrapping at the octave), s[4] the gate's phase. */
static uint32_t beat_samples(void);
static const char *const N_RS_TYPE[] = {"NOIS", "SAW", "SINE", "SHEP", "DOWN", "BOOM", 0};
static const char *const N_RS_LEN[] = {"1", "2", "4", "8", "16", "32", 0};
enum { RS_NOIS, RS_SAW, RS_SINE, RS_SHEP, RS_DOWN, RS_BOOM };

static void risers_note_on(track_t *t, voice_t *v)
{
    v->env = 0;                                       /* (a new riser: from the bottom, the core's state cleared) */
    va_start(t, v);
    v->s[0] = 0;
    v->s[1] = 0;
    v->s[4] = 0;
}

static inline float rs_cut_of(int32_t p16)            /* a pitch (1/16 semitone) as the core's cutoff (0 .. 127) */
{
    float hz = 440.0f * fm_exp2f(((float)p16 * (1.0f / 16.0f) - 69.0f) * (1.0f / 12.0f));
    return fm_log2f(fm_maxf(hz, 20.0f) * (1.0f / 20.0f)) * (127.0f / 9.8f);
}
static inline uint32_t rs_inc(int32_t p16)            /* a pitch's step at the sample rate */
{
    float hz = 440.0f * fm_exp2f(((float)p16 * (1.0f / 16.0f) - 69.0f) * (1.0f / 12.0f));
    return (uint32_t)fm_minf(hz / (float)FS * 4294967296.0f, 0.45f * 4294967296.0f);
}

static void risers_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    va_voice_t *s = va_of(t, v);
    uint32_t type = (uint32_t)p[P_E0] % 6u, len, i, k, blocks = (uint32_t)v->s[0];
    int32_t range16 = p[P_E2] * 16, sprd = p[P_E4], p16;
    float x, xc, amp, gate = 1.0f, nmix = (float)p[P_E7] * (1.0f / 127.0f), y[CTL], curve = (float)p[P_E3] * (1.0f / 127.0f);
    va_cfg_t c;
    if (!s || n > CTL)
        return;
    len = beat_samples() * (1u << ((uint32_t)p[P_E1] % 6u)) / CTL;   /* LEN in blocks */
    len = len ? len : 1u;
    if (type == RS_BOOM)
        len = len / 8u + 1u;                          /* (BOOM: its drop an eighth of LEN) */
    x = blocks >= len ? 1.0f : (float)blocks / (float)len;   /* the way up, 0 .. 1 */
    xc = x + (x * x - x) * curve;                     /* CURVE: straight .. squared */
    if (type == RS_DOWN || type == RS_BOOM)
        xc = 1.0f - xc;                               /* (from the top) */
    if (blocks < 0x7FFFFFFFu)
        v->s[0] = (int32_t)(blocks + 1u);
    amp = type == RS_BOOM || type == RS_DOWN ? 1.0f : x < 0.5f ? 2.0f * x : 1.0f;   /* the swell (BOOM: struck) */
    if (p[P_E6] && type < RS_DOWN) {                  /* BUILD: a gate from quarters to 32nds as it climbs */
        uint32_t div = 1u << (uint32_t)(x * 2.999f); /* 1, 2, 4 a beat .. */
        div <<= x > 0.85f ? 1u : 0u;                  /* .. 8 at the end */
        v->s[4] = (int32_t)((uint32_t)v->s[4] + (0xFFFFFFFFu / ((beat_samples() / CTL) | 1u)) * div);
        gate = (uint32_t)v->s[4] < 0x60000000u ? 1.0f : 1.0f - (float)p[P_E6] * (1.0f / 127.0f);
    }
    p16 = m->pitch16 + (int32_t)((float)range16 * xc);
    c.nosc = 2;
    c.sync = 0;
    c.pw = 0.5f;
    c.incs = 0;
    c.ws = VA_WHITE;                                  /* the noise in the sub's place */
    c.gs = 0.0f;
    c.drive = 1.0f;
    c.res = (float)p[P_E5] * (0.92f / 127.0f);
    c.w[0] = c.w[1] = VA_SINE;
    c.g[0] = 0.6f;
    c.g[1] = 0.0f;
    c.inc[0] = c.inc[1] = rs_inc(p16);
    c.ladder = VA_LP12;
    c.cut = 127.0f;
    switch (type) {
    case RS_NOIS:
    case RS_DOWN:                                     /* the noise's band from the note up RANGE (DOWN: and a sine) */
        c.g[0] = type == RS_DOWN ? 0.35f : 0.0f;
        c.gs = 0.9f;
        c.ladder = VA_BP;
        c.cut = rs_cut_of(p16);
        break;
    case RS_SAW:                                      /* three saws SPRD apart, the ladder opening with them */
        c.nosc = 5;                                   /* (the core's five: the last two silent) */
        for (k = 0; k < 5u; k++) {
            c.w[k] = VA_SAW;
            c.g[k] = k < 3u ? 0.33f : 0.0f;
            c.inc[k] = rs_inc(p16 + (k == 1u ? sprd / 2 : k == 2u ? -sprd / 2 : 0));
        }
        c.ladder = VA_LADDER;
        c.res = (float)p[P_E5] * (0.6f / 127.0f);
        c.cut = 52.0f + 74.0f * xc;                  /* (~400 Hz at the start: heard from the first beat) */
        c.gs = 0.5f * nmix;
        break;
    case RS_SINE:
        c.gs = 0.5f * nmix;
        break;
    case RS_SHEP: {                                   /* five octaves climbing RANGE over LEN, wrapping at the octave */
        uint32_t base;
        v->s[1] = (int32_t)(((uint32_t)v->s[1] + ((uint32_t)range16 << 16) / len) % (192u << 16));
        base = (uint32_t)v->s[1] >> 16;
        c.nosc = 5;
        for (k = 0; k < 5u; k++) {
            float pos = (float)(base + k * 192u) * (1.0f / 960.0f);   /* 0 .. 1 over the five octaves */
            c.w[k] = VA_SINE;
            c.g[k] = 0.3f * (0.5f - 0.5f * fm_cosf(6.2831853f * pos));   /* a bell: the outer octaves faded */
            c.inc[k] = rs_inc(m->pitch16 - 30 * 16 + (int32_t)(base + k * 192u));
        }
        c.gs = 0.5f * nmix;
        break;
    }
    default: {                                        /* BOOM: the sub's drop, the burst over a tenth of a second */
        c.inc[0] = c.inc[1] = rs_inc(p16 - 24 * 16);
        c.g[0] = 0.9f;
        c.gs = blocks < 140u ? (float)(140u - blocks) * (0.8f / 140.0f) : 0.0f;
        break;
    }
    }
    va_run(s, y, n, &c);
    for (i = 0; i < n; i++)
        out[i] += voice_amp((int32_t)fm_clampf(y[i] * amp * gate * 34000.0f, -32767.0f, 32767.0f), m, i) << 1;
}

/* {TYPE, LEN, RANGE, CURVE, SPRD, RES, BUILD, NOISE} */
static const preset_t RISERS_PRESETS[] = {
    {"NOISE RISE", {0, 3, 48, 60, 0, 70, 0, 0}, {0, 0, 127, 40}, 0, 1, FX(0, 0, 20, 40)},
    {"WHITE WASH", {0, 4, 60, 30, 0, 30, 0, 0}, {0, 0, 127, 60}, 0, 1, FX(0, 20, 20, 60)},
    {"SAW RISE", {1, 3, 24, 50, 30, 30, 0, 10}, {0, 0, 127, 40}, 0, 1, FX(10, 50, 20, 40)},
    {"SUPER RISE", {1, 4, 36, 70, 60, 40, 40, 30}, {0, 0, 127, 50}, 0, 1, FX(10, 70, 30, 50)},
    {"SINE RISE", {2, 3, 36, 40, 0, 0, 0, 0}, {0, 0, 127, 40}, 0, 1, FX(0, 0, 30, 40)},
    {"SNARE BUILD", {0, 3, 24, 40, 0, 50, 127, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 10, 30)},
    {"ENDLESS", {3, 4, 12, 0, 0, 0, 0, 0}, {0, 0, 127, 60}, 0, 1, FX(0, 30, 20, 50)},
    {"SHEPARD BUILD", {3, 3, 24, 0, 0, 0, 80, 20}, {0, 0, 127, 40}, 0, 1, FX(0, 30, 20, 40)},
    {"DOWNLIFTER", {4, 2, 36, 70, 0, 50, 0, 0}, {0, 0, 127, 60}, 0, 1, FX(0, 0, 30, 60)},
    {"FALL", {4, 3, 48, 30, 0, 80, 0, 0}, {0, 0, 127, 70}, 0, 1, FX(0, 0, 40, 60)},
    {"IMPACT", {5, 2, 36, 80, 0, 0, 0, 0}, {0, 100, 0, 100}, 0, 1, FX(20, 0, 0, 60)},
    {"SUB DROP", {5, 3, 24, 60, 0, 0, 0, 0}, {0, 110, 0, 100}, 0, 1, FX(10, 0, 0, 30)},
};

static const engine_t ENG_RISERS = {
    .name = "RISERS",
    .id = EID_RISERS,
    .cc = 0x37,                  /* CC74 RANGE, CC71 BUILD */
    .cats = "FFFFFFFFFFFF",
    .page_title = {"RISE", "SHAPE"},
    .edit = {
        {"TYPE", F_ENUM, 0, 5, 0, N_RS_TYPE, 0},
        {"LEN", F_ENUM, 0, 5, 3, N_RS_LEN, 0},
        {"RANGE", F_INT, 0, 60, 36, 0, "st"},
        {"CURVE", F_PCT, 0, 127, 50, 0, 0},
        {"SPRD", F_INT, 0, 127, 30, 0, "/16"},
        {"RES", F_PCT, 0, 127, 50, 0, 0},
        {"BUILD", F_PCT, 0, 127, 0, 0, 0},
        {"NOISE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = RISERS_PRESETS,
    .npresets = NELEM(RISERS_PRESETS),
    .poly = 2,
    .note_on = risers_note_on,
    .render = risers_render,
    .knob = {P_E1, P_E2, P_E3, P_E6},
};
