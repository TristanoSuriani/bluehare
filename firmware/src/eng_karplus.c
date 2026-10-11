/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* KARPLUS (1.5): a plucked string, Karplus-Strong (Fel-Tracker's own: a delay line one period long, filled at the pluck,
 * fed back through a damping filter).
 *
 * At each note the line is filled with the excitation EXCT: NOIS white noise (a bright pluck), PLUK noise through a
 * low-pass (BRIGHT: how bright), SAW one ramp of the period (a hammered, harder tone), CLIK a single click (a harp, a
 * dulcimer); POS plucks it nearer the bridge (a comb on the excitation: thinner, nasal) or the middle. Each pass round
 * the loop it goes through the damping filter (DAMP: the highs die first) and the feedback gain that makes it ring
 * for DECAY (0.1 .. 10 s to -60 dB, whatever the note), and STIFF adds an allpass (a stiffer string: the overtones
 * sharpen, towards a bell). MUTE damps it on release (a palm mute, staccato); DRIVE saturates the output. The line is
 * KP_LEN samples: the lowest note is ~57 Hz (A#1), one lower plays an octave up. The loop's delay is tuned with the
 * filters' own (a linear interpolation reads between samples).
 * Voice state: the line kp_line[track][voice] (in the pool), s[0] the write place, s[1] the damping filter's last input,
 * s[2] the allpass's state, s[3] the noise generator, s[4] released (MUTE once), s[5] the output's DC blocker, s[6] its
 * last input. */
static const char *const N_KP_EXCT[] = {"NOIS", "PLUK", "SAW", "CLIK", 0};
#define KP_POLY 4
#define KP_LEN 768u
static int16_t kp_line[NPART][KP_POLY][KP_LEN] __attribute__((section(".pool")));

static int16_t *kp_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < KP_POLY ? kp_line[t - trk][i] : 0;
}

/* the loop's length for phase increment inc: samples, Q8 (an octave up until it fits the line) */
static uint32_t kp_len(uint32_t inc)
{
    uint32_t l = 0xFFFFFFFFu / ((inc >> 8) | 1u);
    while (l > (KP_LEN - 4u) << 8)
        l >>= 1;
    return l < (4u << 8) ? 4u << 8 : l;
}

static void karplus_note_on(track_t *t, voice_t *v)
{
    int16_t *line = kp_of(t, v);
    const int16_t *p = t->p;
    uint32_t i, n, ex = (uint32_t)p[P_E0] & 3u, nz = 0x2545F491u ^ (v->age * 2654435761u) ^ v->note;
    int32_t lp = 0, bright = 2000 + p[P_E1] * 240, amp = 32767, pos;
    if (!line)
        return;
    n = kp_len(pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047))) >> 8;
    pos = (int32_t)n * p[P_E4] / 254;                 /* POS: 0 the end .. half the string */
    for (i = 0; i < KP_LEN; i++) {
        int32_t x;
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        if (i >= n) {
            line[i] = 0;
            continue;
        }
        switch (ex) {
        case 0u: x = (int16_t)nz; break;                                      /* NOIS */
        case 1u: lp += (((int16_t)nz - lp) * bright) >> 15; x = lp * 2; break; /* PLUK */
        case 2u: x = (int32_t)(i * 65535u / n) - 32768 + ((int16_t)nz >> 3); break;   /* SAW */
        default: x = i < 2u ? 32767 : 0; break;                               /* CLIK */
        }
        line[i] = (int16_t)clamp(x * amp >> 15, -32767, 32767);
    }
    for (i = n; pos > 0 && i-- > (uint32_t)pos;)       /* POS: the excitation less itself pos samples on */
        line[i] = (int16_t)((line[i] - line[i - (uint32_t)pos]) >> 1);
    v->s[0] = (int32_t)n;                             /* (the write place: after the excitation) */
    v->s[1] = v->s[2] = v->s[4] = v->s[5] = v->s[6] = 0;
    v->s[3] = (int32_t)nz;
}

static void karplus_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int16_t *line = kp_of(t, v);
    const int16_t *p = t->p;
    uint32_t len = kp_len(m->inc), i, w = (uint32_t)v->s[0];
    int32_t damp = p[P_E2] * 128, stiff = p[P_E5] * 200, ap = v->s[2], x1 = v->s[1], drive = p[P_E7];
    int32_t t60 = 100 + (p[P_E3] * p[P_E3] * 10000) / 16129, x, g, d, dl, dc = v->s[5], dc1 = v->s[6];
    uint32_t xq, den, di;
    if (!line)
        return;
    if (!v->gate && p[P_E6])                          /* MUTE: released, the string damped */
        t60 = t60 * (128 - p[P_E6]) / 128 + 20;
    /* the feedback gain a pass: -60 dB in t60 ms (exp(-6.91 len / (44.1 t60)) ~ 1 / (1 + x + x^2 / 2)), Q16 */
    xq = (uint32_t)(452717u / 441u * ((len >> 8) * 10u)) / (uint32_t)t60;   /* x, Q16 */
    den = 65536u + xq + ((xq >> 1) * (xq >> 8) >> 8);
    g = (int32_t)(0xFFFFFFFFu / den);                 /* (2^32 / den: Q16) */
    g = g > 65500 ? 65500 : g;
    /* the read delay: the loop less the damping filter's (damp / 65536 sample) and the allpass's ((1 - a) / (1 + a)) */
    d = (int32_t)len - (damp >> 8) - (stiff ? (int32_t)((((65536 - stiff) << 8) / (65536 + stiff))) : 0);
    d = clamp(d, 2 << 8, (int32_t)(KP_LEN - 2u) << 8);
    di = (uint32_t)(d >> 8);
    for (i = 0; i < n; i++) {                         /* (the line wraps by compares: no divide a sample) */
        uint32_t r = w >= di ? w - di : w + KP_LEN - di, r1 = r ? r - 1u : KP_LEN - 1u;
        int32_t a = line[r], b = line[r1], frac = d & 255, y;
        x = a + (((b - a) * frac) >> 8);              /* (the delay between samples) */
        y = x + (((x1 - x) * damp) >> 16);            /* DAMP: a two-tap low-pass */
        x1 = x;
        if (stiff) {                                  /* STIFF: a first-order allpass (coefficient stiff / 65536) */
            int32_t z = y - ((stiff * ap) >> 16);
            y = ap + ((stiff * z) >> 16);
            ap = z;
        }
        y = (int32_t)(((int64_t)y * g) >> 16);
        line[w] = (int16_t)clamp(y, -32767, 32767);
        w = w + 1u == KP_LEN ? 0u : w + 1u;
        dl = x - dc1 + dc - (dc >> 9);                /* (the output's DC blocked: POS and SAW leave some) */
        dc1 = x;
        dc = dl;
        if (drive)
            dl = softclip((dl * (32 + drive)) >> 5);
        out[i] += voice_amp(dl, m, i) << 1;
    }
    v->s[0] = (int32_t)w;
    v->s[1] = x1;
    v->s[2] = ap;
    v->s[5] = dc;
    v->s[6] = dc1;
}

/* {EXCT, BRIGHT, DAMP, DECAY, POS, STIFF, MUTE, DRIVE}; ADSR: the line rings by itself (SUS full, the release short or
 * MUTE does the stopping) */
static const preset_t KARPLUS_PRESETS[] = {
    {"NYLON", {1, 60, 60, 74, 30, 0, 40, 0}, {0, 0, 127, 40}, 0, 0, FX(0, 10, 20, 30)},
    {"STEEL", {0, 100, 24, 90, 24, 0, 30, 0}, {0, 0, 127, 50}, 0, 0, FX(0, 20, 20, 30)},
    {"HARP", {3, 80, 40, 96, 40, 0, 0, 0}, {0, 0, 127, 80}, 0, 0, FX(0, 20, 30, 50)},
    {"KOTO", {1, 110, 30, 84, 12, 10, 20, 0}, {0, 0, 127, 50}, 0, 0, FX(0, 0, 30, 35)},
    {"PIZZ", {1, 70, 70, 34, 50, 0, 100, 0}, {0, 0, 127, 15}, 0, 0, FX(0, 0, 15, 30)},
    {"MUTED BASS", {1, 40, 90, 56, 30, 0, 120, 30}, {0, 0, 127, 15}, 0, 1, FX(20, 0, 0, 5)},
    {"DUB PLUCK", {1, 50, 70, 60, 36, 0, 60, 0}, {0, 0, 127, 30}, 0, 0, FX(0, 0, 70, 30)},
    {"ACID PLUCK", {2, 100, 50, 50, 10, 0, 80, 90}, {0, 0, 127, 20}, 0, 1, FX(40, 0, 30, 10)},
    {"BELL STRING", {3, 127, 10, 110, 20, 100, 0, 0}, {0, 0, 127, 90}, 0, 0, FX(0, 30, 30, 50)},
    {"DULCIMER", {3, 110, 20, 100, 30, 40, 10, 0}, {0, 0, 127, 60}, 0, 0, FX(0, 20, 25, 40)},
    /* (Fel-Tracker, the IDM review) */
    {"SITAR", {2, 120, 15, 100, 6, 30, 0, 40}, {0, 0, 127, 60}, 0, 0, FX(10, 20, 20, 40)},
    {"UPRIGHT", {1, 30, 80, 70, 40, 0, 90, 10}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 0, 15)},
    {"MUSIC BOX", {3, 127, 5, 90, 10, 127, 0, 0}, {0, 0, 127, 70}, 0, 0, FX(0, 20, 30, 55)},
    {"KALIMBA", {3, 90, 30, 60, 50, 60, 20, 0}, {0, 0, 127, 40}, 0, 0, FX(0, 0, 30, 40)},
    {"FUZZ STRING", {0, 100, 30, 90, 20, 0, 40, 127}, {0, 0, 127, 30}, 0, 1, FX(40, 20, 40, 25)},
    {"BOWED PAD", {1, 50, 50, 127, 40, 0, 0, 0}, {95, 0, 127, 90}, 0, 0, FX(0, 70, 20, 80)},
};

static const engine_t ENG_KARPLUS = {
    .name = "KARPLUS",
    .id = EID_KARPLUS,
    .cc = 0x32,                  /* CC74 BRIGHT, CC71 DAMP */
    .cats = "UUUUUBUUUUUBKULP",
    .page_title = {"PLUCK", "STRING"},
    .edit = {
        {"EXCT", F_ENUM, 0, 3, 1, N_KP_EXCT, 0},
        {"BRIGHT", F_PCT, 0, 127, 80, 0, 0},
        {"DAMP", F_PCT, 0, 127, 40, 0, 0},
        {"DECAY", F_PCT, 0, 127, 80, 0, 0},
        {"POS", F_PCT, 0, 127, 30, 0, 0},
        {"STIFF", F_PCT, 0, 127, 0, 0, 0},
        {"MUTE", F_PCT, 0, 127, 30, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = KARPLUS_PRESETS,
    .npresets = NELEM(KARPLUS_PRESETS),
    .poly = KP_POLY,
    .note_on = karplus_note_on,
    .render = karplus_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},
};
