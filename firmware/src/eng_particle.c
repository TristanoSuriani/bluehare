/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* PARTICLE (1.5): a cloud of synthesized grains (Blue Hare's own: no samples). While a note sounds, grains start at
 * random times, DENS a second on average (~1 .. 400), each SIZE long (~2 .. 200 ms), at the note give or take SCAT
 * semitones (random; QNT snaps the offset to FREE, SEMI semitones, OCT octaves, 5TH fifths and octaves), gliding CHIRP
 * over its length (up: bubbles, down: drips). Each grain is a WAVE (SINE, TRI, FM a bright bell-ish sine, NOIS a
 * crackle) under SHAPE (PERC a hit, HANN a swell, REV backwards: its last 64 samples faded, Blue Hare); TONE a
 * low-pass over the cloud. The ADSR shapes the
 * cloud. Up to PT_NG grains at once a voice (the rest are dropped); the level follows how many overlap.
 * Voice state: the grains in the track's block (engines.c eng_slab), s[0] samples to the next grain, s[1] the random
 * generator, s[2] the low-pass, s[3] the DC blocker, s[4] its last input. */
static const char *const N_PT_QNT[] = {"FREE", "SEMI", "OCT", "5TH", 0};
static const char *const N_PT_SHAPE[] = {"PERC", "HANN", "REV", 0};
static const char *const N_PT_WAVE[] = {"SINE", "TRI", "FM", "NOIS", 0};
#define PT_POLY 4
#define PT_NG 8
typedef struct {
    uint32_t ph, inc, estep;     /* the phase; its increment; the envelope's step (Q24 per sample: 1 over the length) */
    int32_t age, len;            /* samples since the start (< 0: starts later in the block); 0 len: free */
    int32_t p16, cstep;          /* the pitch at the start (1/16 semitone), the glide a sample (Q16 of that) */
} pt_grain_t;

static inline __attribute__((always_inline)) pt_grain_t *pt_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    {
        pt_grain_t (*a)[PT_NG] = eng_slab(t, SLAB_PARTICLE, sizeof(pt_grain_t) * PT_POLY * PT_NG);   /* (engines.c) */
        return a && i < PT_POLY ? a[i] : 0;
    }
}

static uint32_t pt_rand(voice_t *v)
{
    uint32_t x = (uint32_t)v->s[1];
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->s[1] = (int32_t)x;
    return x;
}

static void particle_note_on(track_t *t, voice_t *v)
{
    pt_grain_t *g = pt_of(t, v);
    uint32_t i;
    v->s[0] = 0;                                      /* (a grain at once) */
    v->s[1] = (int32_t)(0x2F6B1C3Du ^ (v->age * 2654435761u) ^ (v->note << 8));
    v->s[2] = v->s[3] = v->s[4] = 0;
    for (i = 0; g && i < PT_NG; i++)
        g[i].len = 0;
}

static int32_t pt_len(int32_t size) { return 88 + size * size * 55 / 100; }   /* ~2 .. 200 ms */
static int32_t pt_hz(int32_t dens) { return 1 + dens * dens / 40; }            /* grains a second: 1 .. ~400 */

/* the offset in semitones (r: -32768 .. 32767, the random) snapped by QNT; -> 1/16 semitone */
static int32_t pt_offset(int32_t r, int32_t scat, uint32_t qnt)
{
    int32_t o16 = (r * scat) >> 11, s, k, m;          /* (r / 32768 x scat x 16) */
    if (!qnt)
        return o16;
    s = (o16 + (o16 >= 0 ? 8 : -8)) / 16;             /* the nearest semitone */
    if (qnt == 1u)
        return s * 16;
    k = (s >= 0 ? s : s - 11) / 12;                   /* the octave below */
    m = s - 12 * k;                                   /* 0 .. 11 */
    if (qnt == 2u)
        return (k + (m >= 6)) * 192;
    return (12 * k + (m < 4 ? 0 : m < 10 ? 7 : 12)) * 16;   /* 5TH: the octave, its fifth, the octave above */
}

static void pt_spawn(track_t *t, voice_t *v, pt_grain_t *g, const vmod_t *m, int32_t at)
{
    const int16_t *p = t->p;
    uint32_t i;
    int32_t r = (int16_t)(pt_rand(v) >> 16);
    for (i = 0; i < PT_NG && g[i].len; i++)
        ;
    if (i == PT_NG)
        return;                                       /* (all busy: this one is dropped) */
    g[i].len = pt_len(p[P_E1]);
    g[i].age = -at;
    g[i].ph = 0;
    g[i].estep = (1u << 24) / (uint32_t)g[i].len;
    g[i].p16 = m->pitch16 + pt_offset(r, p[P_E2], (uint32_t)p[P_E3] & 3u);
    g[i].cstep = (p[P_E4] * 8 << 16) / g[i].len;      /* CHIRP: +-24 semitones over the grain at the ends */
}

static void particle_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    pt_grain_t *g = pt_of(t, v);
    const int16_t *p = t->p;
    int32_t buf[CTL], mean, dc = v->s[3], dc1 = v->s[4], hz = pt_hz(p[P_E0]), len = pt_len(p[P_E1]), ov, gain, lp = v->s[2], k = 1500 + p[P_E7] * 245;
    uint32_t i, j, shape = (uint32_t)p[P_E5] % 3u, wave = (uint32_t)p[P_E6] & 3u;
    if (!g || n > CTL)
        return;
    mean = (int32_t)FS / hz;
    while (v->s[0] < (int32_t)n) {                    /* the grains starting in this block (random intervals: */
        pt_spawn(t, v, g, m, v->s[0]);
        v->s[0] += 1 + (int32_t)(((pt_rand(v) >> 16) * (uint32_t)(2 * mean)) >> 16);   /* (0 .. twice the mean) */
    }
    v->s[0] -= (int32_t)n;
    ov = hz * len / ((int32_t)FS / 256);              /* grains overlapping on average, Q8 */
    gain = (int32_t)((32767u * 512u) / (uint32_t)(512 + ov));
    for (i = 0; i < n; i++)
        buf[i] = 0;
    for (j = 0; j < PT_NG; j++) {
        pt_grain_t *q = &g[j];
        int32_t mid;
        if (!q->len)
            continue;
        mid = q->age + (int32_t)n / 2;                /* the glide at the block's middle */
        mid = mid < 0 ? 0 : mid;
        q->inc = pitch_inc((uint32_t)clamp(q->p16 + (int32_t)(((int64_t)q->cstep * mid) >> 16), 0, 2047));
        for (i = 0; i < n && q->len; i++, q->age++) {
            uint32_t e, x;
            int32_t w;
            if (q->age < 0)
                continue;
            if (q->age >= q->len) {
                q->len = 0;
                break;
            }
            x = ((uint32_t)q->age * q->estep) >> 8;   /* the grain's progress, Q16 */
            x = x > 65535u ? 65535u : x;
            e = shape == 0u ? ((65536u - x) * (65536u - x)) >> 17       /* PERC */
              : shape == 1u ? (uint32_t)((32767 - sine_i((x << 16) + 0x40000000u)) >> 1)   /* HANN */
              : (x * x) >> 17;                                          /* REV */
            if (shape == 2u && q->len - q->age < 64)  /* (REV: the last 1.3 ms faded, not a click a grain) */
                e = e * (uint32_t)(q->len - q->age) >> 6;
            switch (wave) {
            case 0u: w = sine_i(q->ph); break;
            case 1u: w = (int32_t)(q->ph >> 16); w = (w < 32768 ? w : 65535 - w) * 2 - 32767; break;
            case 2u: w = sine_i(q->ph + (uint32_t)(sine_i(q->ph << 1) * 31300)); break;
            default: w = (int16_t)(((q->ph ^ (q->ph >> 13)) * 0x9E3779B1u) >> 16); break;
            }
            buf[i] += (w * (int32_t)e) >> 15;
            q->ph += q->inc;
        }
    }
    for (i = 0; i < n; i++) {
        int32_t s = (buf[i] >> 1) * gain >> 14, f = s - dc1 + dc - (dc >> 9);   /* (the DC blocked: grains */
        dc1 = s;                                      /* cut short leave some) */
        dc = f;
        lp += ((f - lp) * (k > 32767 ? 32767 : k)) >> 15;
        out[i] += voice_amp(clamp(lp, -32767, 32767), m, i) << 1;
    }
    v->s[2] = lp;
    v->s[3] = dc;
    v->s[4] = dc1;
}

/* {DENS, SIZE, SCAT, QNT, CHIRP, SHAPE, WAVE, TONE}. QNT 0 FREE 1 SEMI 2 OCT 3 5TH; SHAPE 0 PERC 1 HANN 2 REV;
 * WAVE 0 SINE 1 TRI 2 FM 3 NOIS. The ADSR: the cloud */
static const preset_t PARTICLE_PRESETS[] = {
    {"RAIN", {90, 10, 36, 0, 0, 0, 0, 100}, {0, 0, 127, 50}, 0, 0, FX(0, 10, 10, 40)},
    {"BUBBLES", {60, 30, 12, 0, 50, 0, 0, 110}, {0, 0, 127, 50}, 0, 0, FX(0, 20, 20, 40)},
    {"CRACKLE", {110, 2, 0, 0, 0, 0, 3, 90}, {0, 0, 127, 40}, 0, 0, FX(0, 0, 0, 20)},
    {"GEIGER", {40, 0, 0, 0, 0, 0, 3, 127}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 10)},
    {"SWARM", {120, 40, 4, 0, 0, 1, 1, 90}, {30, 0, 127, 60}, 0, 0, FX(0, 40, 20, 40)},
    {"STARDUST", {70, 24, 36, 2, 0, 0, 0, 127}, {0, 0, 127, 70}, 0, 0, FX(0, 30, 40, 60)},
    {"GRAIN PAD", {110, 90, 2, 0, 0, 1, 0, 80}, {60, 0, 127, 80}, 0, 0, FX(0, 60, 20, 60)},
    {"CLOUD CHORD", {100, 70, 24, 3, 0, 1, 1, 90}, {40, 0, 127, 80}, 0, 0, FX(0, 50, 30, 60)},
    {"GLASS", {50, 40, 24, 2, 0, 0, 2, 127}, {0, 0, 127, 70}, 0, 0, FX(0, 20, 40, 60)},
    {"DRIP", {30, 36, 12, 1, -40, 0, 0, 110}, {0, 0, 127, 50}, 0, 0, FX(0, 0, 40, 50)},
    {"INSECTS", {100, 16, 8, 0, 63, 1, 2, 100}, {0, 0, 127, 40}, 0, 0, FX(0, 30, 10, 30)},
    {"FROZEN", {90, 110, 7, 1, 0, 2, 1, 70}, {50, 0, 127, 90}, 0, 0, FX(0, 60, 30, 80)},
};

static const engine_t ENG_PARTICLE = {
    .name = "PARTICLE",
    .id = EID_PARTICLE,
    .cc = 0x13,                  /* CC74 DENS, CC71 SCAT */
    .cats = "FFFDPLPPKFFP",
    .page_title = {"CLOUD", "GRAIN"},
    .edit = {
        {"DENS", F_PCT, 0, 127, 80, 0, 0},
        {"SIZE", F_PCT, 0, 127, 30, 0, 0},
        {"SCAT", F_INT, 0, 48, 12, 0, "st"},
        {"QNT", F_ENUM, 0, 3, 0, N_PT_QNT, 0},
        {"CHIRP", F_BIPCT, -64, 63, 0, 0, 0},
        {"SHAPE", F_ENUM, 0, 2, 0, N_PT_SHAPE, 0},
        {"WAVE", F_ENUM, 0, 3, 0, N_PT_WAVE, 0},
        {"TONE", F_PCT, 0, 127, 110, 0, 0},
    },
    .presets = PARTICLE_PRESETS,
    .npresets = NELEM(PARTICLE_PRESETS),
    .poly = PT_POLY,
    .note_on = particle_note_on,
    .render = particle_render,
    .knob = {P_E0, P_E1, P_E2, P_E4},
};
