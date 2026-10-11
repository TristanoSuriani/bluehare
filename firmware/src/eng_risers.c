/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* RISERS (1.5): build-ups and their landings, timed to the song (Felucca's own). A note starts one, LEN beats long at
 * the tempo (1 .. 32); it climbs RANGE semitones (the pitch, or the noise's band) along CURVE (straight .. slow then
 * fast) and holds at the top while the key is held. TYPE: NOIS white noise through a resonant band-pass rising (RES),
 * SAW a detuned stack of saws (SPRD) rising with its filter opening, SINE one sine rising, SHEP a Shepard tone (it
 * rises for ever: the octaves fade in below as they fade out above), DOWN the way down (noise and pitch falling, a
 * downlifter), BOOM the landing (a sub sweep dropping fast, a burst of noise, a long tail). BUILD gates it faster and
 * faster towards the top (a snare roll's acceleration: quarters to 32nds), NOISE mixes noise into the tonal ones.
 * It swells in over the first half (BOOM: struck). The ADSR's release ends it.
 * Voice state: ph[0..2] the oscillators (SHEP: three of its six octaves), s[0] blocks since the note, s[1] / s[2] the
 * filter (SHEP: s[1] its climb, 1/16 semitone Q16, wrapping at the octave), s[3] the noise generator, s[4] the gate's
 * phase, s[5..7] SHEP's other three octaves. */
static uint32_t beat_samples(void);
static const char *const N_RS_TYPE[] = {"NOIS", "SAW", "SINE", "SHEP", "DOWN", "BOOM", 0};
static const char *const N_RS_LEN[] = {"1", "2", "4", "8", "16", "32", 0};

static void risers_note_on(track_t *t, voice_t *v)
{
    uint32_t i;
    (void)t;
    v->ph[0] = 0;
    v->ph[1] = 0x55555555u;
    v->ph[2] = 0xAAAAAAAAu;
    for (i = 0; i < 8u; i++)
        v->s[i] = 0;
    v->s[3] = (int32_t)(0x7A3B9C1Du ^ (v->age * 2654435761u));
}

static int32_t rs_noise(int32_t *st)
{
    uint32_t x = (uint32_t)*st;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *st = (int32_t)x;
    return (int16_t)(x >> 16);
}

static void risers_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t type = (uint32_t)p[P_E0] % 6u, len, i, blocks = (uint32_t)v->s[0], inc[3], gate_inc;
    int32_t x, xc, range16 = p[P_E2] * 16, curve = p[P_E3], sprd = p[P_E4], amp, gate = 32767, nmix = p[P_E7] * 258;
    int32_t ic1 = v->s[1], ic2 = v->s[2], k = 8192 - p[P_E5] * 7800 / 127, p16, cut;
    tsvf_t flt;
    len = beat_samples() * (1u << ((uint32_t)p[P_E1] % 6u)) / CTL;   /* LEN in blocks */
    len = len ? len : 1u;
    if (type == 5u)
        len = len / 8u + 1u;                          /* (BOOM: its drop an eighth of LEN) */
    x = blocks >= len ? 32767 : (int32_t)((blocks << 15) / len);   /* the way up, Q15 */
    xc = x + (((((x * x) >> 15) - x) * curve) >> 7);  /* CURVE: straight .. squared */
    if (type == 4u || type == 5u)
        xc = 32767 - xc;                              /* (DOWN, BOOM: from the top) */
    if (blocks < 0x7FFFFFFFu)
        v->s[0] = (int32_t)(blocks + 1u);
    amp = type == 5u ? 32767 : x < 16384 ? x * 2 : 32767;   /* the swell (BOOM: struck) */
    if (type == 4u)
        amp = 32767;
    if (p[P_E6] && type < 4u) {                       /* BUILD: a gate from quarters to 32nds as it climbs */
        uint32_t div = 1u << ((uint32_t)(x * 3) >> 15);   /* 1 .. 8 a beat */
        gate_inc = (0xFFFFFFFFu / ((beat_samples() / CTL) | 1u)) * div;
        v->s[4] = (int32_t)((uint32_t)v->s[4] + gate_inc);
        gate = (uint32_t)v->s[4] < 0x60000000u ? 32767 : 32767 - p[P_E6] * 258;
    }
    p16 = m->pitch16 + ((range16 * xc) >> 15);
    if (type == 0u || type == 4u) {                   /* the noise's band: from the note up RANGE */
        cut = clamp((p16 * 299 >> 4) - 6730, 0, 126 << 8);   /* (the cutoff index of that pitch: 1.17 a semitone) */
        tsvf_coef_k(&flt, cut, k);
    } else if (type == 1u) {                          /* SAW: the filter opening with it */
        tsvf_coef_k(&flt, clamp(40 * 256 + xc * 86 / 128, 0, 126 << 8), 6000);   /* (index 40 .. 126) */
    }
    if (type == 3u) {                                 /* SHEP: six octaves, the whole climbing RANGE over LEN, wrapping */
        uint32_t o, base;
        int32_t acc[CTL];
        v->s[1] = (int32_t)(((uint32_t)v->s[1] + ((uint32_t)range16 << 16) / len) % (192u << 16));
        base = (uint32_t)v->s[1] >> 16;
        for (i = 0; i < n; i++)
            acc[i] = 0;
        for (o = 0; o < 6u; o++) {
            int32_t pos = (int32_t)(base + o * 192u), w, wl;   /* 1/16 st above the lowest octave (0 .. 1152) */
            uint32_t *ph = o < 3u ? &v->ph[o] : (uint32_t *)&v->s[5 + o - 3u], oi;
            oi = pitch_inc((uint32_t)clamp(m->pitch16 - 36 * 16 + pos, 0, 2047));
            w = 32767 - sine_i((uint32_t)pos * 3728270u + 0x40000000u);   /* a bell over the six octaves */
            wl = w >> 3;
            for (i = 0; i < n; i++) {
                acc[i] += (sine_i(*ph) * wl) >> 15;
                *ph += oi;
            }
        }
        for (i = 0; i < n; i++) {
            int32_t s = acc[i] + ((rs_noise(&v->s[3]) * nmix) >> 17);
            out[i] += voice_amp(clamp(((s * amp >> 15) * gate) >> 15, -32767, 32767), m, i) << 1;
        }
        return;
    }
    for (i = 0; i < 3u; i++)                          /* SAW: three, SPRD apart; the others one */
        inc[i] = pitch_inc((uint32_t)clamp(p16 + (int32_t)(i == 0u ? 0 : i == 1u ? sprd / 2 : -sprd / 2), 0, 2047));
    for (i = 0; i < n; i++) {
        int32_t s, nz = rs_noise(&v->s[3]);
        switch (type) {
        case 0u:
        case 4u: {                                    /* the noise band-passed (v1); DOWN: with a falling sine */
            int32_t v3 = (nz >> 1) - ic2, v1 = (flt.a1 * ic1 + flt.a2 * v3) >> 13;
            int32_t v2 = ic2 + ((flt.a2 * ic1 + flt.a3 * v3) >> 13);
            ic1 = clamp(2 * v1 - ic1, -150000, 150000);
            ic2 = clamp(2 * v2 - ic2, -150000, 150000);
            s = clamp(v1 * 2, -32767, 32767);
            if (type == 4u) {
                s = (s + sine_i(v->ph[0])) >> 1;
                v->ph[0] += inc[0];
            }
            break;
        }
        case 1u:
            s = (osc_saw(v->ph[0], inc[0]) + osc_saw(v->ph[1], inc[1]) + osc_saw(v->ph[2], inc[2])) / 3;
            s = tsvf_lp(&flt, s, &ic1, &ic2);
            v->ph[0] += inc[0];
            v->ph[1] += inc[1];
            v->ph[2] += inc[2];
            s += (nz * nmix) >> 16;
            break;
        case 2u:
            s = sine_i(v->ph[0]) + ((nz * nmix) >> 16);
            v->ph[0] += inc[0];
            break;
        default: {                                    /* BOOM: the sub, the burst fading over a tenth of a second */
            int32_t burst = blocks < 140u ? (int32_t)(140u - blocks) * 234 : 0;
            s = sine_i(v->ph[0]) + ((nz * burst) >> 15);
            v->ph[0] += inc[0];
            break;
        }
        }
        out[i] += voice_amp(clamp(((s * amp >> 15) * gate) >> 15, -32767, 32767), m, i) << 1;
    }
    v->s[1] = ic1;
    v->s[2] = ic2;
}

/* {TYPE, LEN, RANGE, CURVE, SPRD, RES, BUILD, NOISE}; TYPE 0 NOIS 1 SAW 2 SINE 3 SHEP 4 DOWN 5 BOOM; LEN 0 1 beat ..
 * 5 32 beats. The release: how long it lingers after the key */
static const preset_t RISERS_PRESETS[] = {
    {"NOISE RISE", {0, 3, 48, 60, 0, 70, 0, 0}, {0, 0, 127, 40}, 0, 1, FX(0, 0, 20, 40)},
    {"WHITE WASH", {0, 4, 60, 30, 0, 30, 0, 0}, {0, 0, 127, 60}, 0, 1, FX(0, 20, 20, 60)},
    {"SAW RISE", {1, 3, 24, 50, 30, 0, 0, 10}, {0, 0, 127, 40}, 0, 1, FX(10, 50, 20, 40)},
    {"SUPER RISE", {1, 4, 36, 70, 60, 0, 40, 30}, {0, 0, 127, 50}, 0, 1, FX(10, 70, 30, 50)},
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
