/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* NEBULA (native: engine_ids.h; the native image's alone): PARTICLE's cloud whole. The same controls, meaning the same
 * (DENS, SIZE, SCAT, QNT, CHIRP, SHAPE, WAVE, TONE: eng_particle.c, whose helpers it plays with), so a NEBULA sound
 * loads on the FM-1 as PARTICLE (its substitute). What the device's CPU cut, here whole:
 *   64 grains at once a voice, not 8: a dense cloud of long grains sounds every one (PARTICLE drops the rest);
 *   12 voices a track, not 4;
 *   the chirp follows each grain every 8 samples, not once a block;
 *   the level from the overlap's square root (grains of random phase add as noise does), so a dense cloud keeps its
 *   loudness instead of thinning as PARTICLE's 1 / overlap does once it drops grains.
 * State: the grains of each track's voices (nb_g, the native image's memory: no engine block), the voice's s[] as
 * PARTICLE's: s[0] samples to the next grain, s[1] the random, s[2] the low-pass, s[3] the DC blocker, s[4] its input. */
#define NB_POLY 12
#define NB_NG 64
static pt_grain_t nb_g[NPART][NB_POLY][NB_NG];

static inline pt_grain_t *nb_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < NB_POLY ? nb_g[t - trk][i] : 0;
}

static void nebula_note_on(track_t *t, voice_t *v)
{
    pt_grain_t *g = nb_of(t, v);
    uint32_t i;
    v->s[0] = 0;
    v->s[1] = (int32_t)(0x2F6B1C3Du ^ (v->age * 2654435761u) ^ (v->note << 8));
    v->s[2] = v->s[3] = v->s[4] = 0;
    for (i = 0; g && i < NB_NG; i++)
        g[i].len = 0;
}

static void nb_spawn(track_t *t, voice_t *v, pt_grain_t *g, const vmod_t *m, int32_t at)
{
    const int16_t *p = t->p;
    uint32_t i;
    int32_t r = (int16_t)(pt_rand(v) >> 16);
    for (i = 0; i < NB_NG && g[i].len; i++)
        ;
    if (i == NB_NG)
        return;
    g[i].len = pt_len(p[P_E1]);
    g[i].age = -at;
    g[i].ph = 0;
    g[i].estep = (1u << 24) / (uint32_t)g[i].len;
    g[i].p16 = m->pitch16 + pt_offset(r, p[P_E2], (uint32_t)p[P_E3] & 3u);
    g[i].cstep = (p[P_E4] * 8 << 16) / g[i].len;
}

static void nebula_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    pt_grain_t *g = nb_of(t, v);
    const int16_t *p = t->p;
    int32_t buf[CTL], mean, dc = v->s[3], dc1 = v->s[4], hz = pt_hz(p[P_E0]), len = pt_len(p[P_E1]), gain, lp = v->s[2];
    int32_t k = 1500 + p[P_E7] * 245;
    uint32_t i, j, shape = (uint32_t)p[P_E5] % 3u, wave = (uint32_t)p[P_E6] & 3u;
    double ov;
    if (!g || n > CTL)
        return;
    mean = (int32_t)FS / hz;
    while (v->s[0] < (int32_t)n) {
        nb_spawn(t, v, g, m, v->s[0]);
        v->s[0] += 1 + (int32_t)(((pt_rand(v) >> 16) * (uint32_t)(2 * mean)) >> 16);
    }
    v->s[0] -= (int32_t)n;
    ov = (double)hz * len / FS;                       /* grains overlapping on average */
    gain = (int32_t)(32767.0 * 1.6 / sqrt(1.6 * 1.6 + ov));   /* (one grain: PARTICLE's level; many: as noise adds) */
    for (i = 0; i < n; i++)
        buf[i] = 0;
    for (j = 0; j < NB_NG; j++) {
        pt_grain_t *q = &g[j];
        if (!q->len)
            continue;
        for (i = 0; i < n && q->len; i++, q->age++) {
            uint32_t e, x;
            int32_t w;
            if (q->age < 0)
                continue;
            if (q->age >= q->len) {
                q->len = 0;
                break;
            }
            if (!(q->age & 7) || !q->inc)             /* (the chirp every 8 samples) */
                q->inc = pitch_inc((uint32_t)clamp(q->p16 + (int32_t)(((int64_t)q->cstep * q->age) >> 16), 0, 2047));
            x = ((uint32_t)q->age * q->estep) >> 8;
            x = x > 65535u ? 65535u : x;
            e = shape == 0u ? ((65536u - x) * (65536u - x)) >> 17
              : shape == 1u ? (uint32_t)((32767 - sine_i((x << 16) + 0x40000000u)) >> 1)
              : (x * x) >> 17;
            if (shape == 2u && q->len - q->age < 64)
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
        int32_t s = (int32_t)(((int64_t)(buf[i] >> 1) * gain) >> 14), f = s - dc1 + dc - (dc >> 9);
        dc1 = s;
        dc = f;
        lp += (int32_t)(((int64_t)(f - lp) * (k > 32767 ? 32767 : k)) >> 15);
        out[i] += voice_amp(clamp(lp, -32767, 32767), m, i) << 1;
    }
    v->s[2] = lp;
    v->s[3] = dc;
    v->s[4] = dc1;
}

/* PARTICLE's twelve (a NEBULA sound is a PARTICLE one), then the clouds only the native image holds: dense, long grains */
static const preset_t NEBULA_PRESETS[] = {
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
    {"NEBULA", {127, 110, 12, 2, 0, 1, 0, 90}, {70, 0, 127, 100}, 0, 0, FX(0, 50, 20, 80)},
    {"CHOIR CLOUD", {120, 100, 2, 1, 0, 1, 1, 75}, {60, 0, 127, 90}, 0, 0, FX(0, 70, 20, 70)},
    {"OCEAN", {127, 80, 48, 0, 0, 1, 3, 60}, {80, 0, 127, 100}, 0, 0, FX(0, 20, 10, 80)},
    {"FIFTHS HAZE", {118, 96, 19, 3, 0, 1, 0, 95}, {60, 0, 127, 90}, 0, 0, FX(0, 60, 30, 75)},
    {"BELL MIST", {110, 70, 24, 2, 0, 1, 2, 110}, {40, 0, 127, 90}, 0, 0, FX(0, 40, 40, 75)},
    {"SHEPARD SKY", {120, 110, 24, 2, 24, 1, 0, 90}, {60, 0, 127, 100}, 0, 0, FX(0, 50, 30, 80)},
    {"DEEP SWARM", {127, 60, 7, 0, -8, 1, 1, 70}, {50, 0, 127, 80}, 0, 0, FX(0, 40, 20, 60)},
    {"STATIC FIELD", {127, 30, 0, 0, 0, 1, 3, 80}, {40, 0, 127, 70}, 0, 0, FX(0, 20, 20, 60)},
};

static const engine_t ENG_NEBULA = {
    .name = "NEBULA",
    .id = EID_NEBULA,
    .cc = 0x13,                  /* CC74 DENS, CC71 SCAT */
    .cats = "FFFDPLPPKFFPPPFPKPPF",
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
    .presets = NEBULA_PRESETS,
    .npresets = NELEM(NEBULA_PRESETS),
    .poly = NB_POLY,
    .note_on = nebula_note_on,
    .render = nebula_render,
    .knob = {P_E0, P_E1, P_E2, P_E4},
};
