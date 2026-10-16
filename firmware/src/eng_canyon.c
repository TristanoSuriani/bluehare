/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* CANYON (native: engine_ids.h; the native image's alone): ECHOES' repeats, all of them. The same controls, meaning the
 * same (SRC, TIME, SHAPE, COUNT, DECAY, PITCH, TONE, SCAT: eng_echoes.c, whose sources, gaps and spawning it plays
 * with), so a CANYON sound loads on the FM-1 as ECHOES (its substitute; COUNT past 16 as 16). What the device's CPU
 * cut, here whole:
 *   COUNT up to 64 repeats, not 16;
 *   64 sounds ringing at once a voice, not 6: a long source over quick repeats keeps every one to its end (ECHOES
 *   cuts the oldest), so a fast PLUCK or FM bell piles into a wash;
 *   8 voices a track, not 4.
 * State: the sounds of each track's voices (cy_g, the native image's memory: no engine block), the voice's s[] as
 * ECHOES': s[0] samples to the next, s[1] the repeats so far, s[2] the next's level, s[3] the random. */
#define CY_POLY 8
#define CY_NG 64
static ec_g_t cy_g[NPART][CY_POLY][CY_NG];

static inline ec_g_t *cy_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < CY_POLY ? cy_g[t - trk][i] : 0;
}

/* the sounds a free one goes to (ec_spawn picks among EC_NG: here a free one of CY_NG, else the oldest) */
static void cy_spawn(track_t *t, voice_t *v, ec_g_t *g, const vmod_t *m, int32_t at, uint32_t r, int32_t amp)
{
    uint32_t i, old = 0;
    int32_t best = -1;
    for (i = 0; i < CY_NG; i++) {
        if (!g[i].len)
            break;
        if (g[i].age > best)
            best = g[i].age, old = i;
    }
    i = i == CY_NG ? old : i;
    g[i].len = 0;                                     /* (free: ec_spawn fills its first, g + i, and looks no further) */
    ec_spawn(t, v, g + i, m, at, r, amp);
}

static void canyon_note_on(track_t *t, voice_t *v)
{
    ec_g_t *g = cy_of(t, v);
    uint32_t i;
    if (g)
        for (i = 0; i < CY_NG; i++)
            g[i].len = 0;
    v->s[0] = 0;
    v->s[1] = 0;
    v->s[2] = 4096 + v->vel * 225;
    v->s[3] = (int32_t)(0x2545F491u ^ (v->note * 2654435761u));
}

static int canyon_done(track_t *t, voice_t *v)
{
    ec_g_t *g = cy_of(t, v);
    uint32_t i;
    if (!g)
        return 1;
    if (v->s[1] <= t->p[P_E3] && v->s[2] > 200)
        return 0;
    for (i = 0; i < CY_NG; i++)
        if (g[i].len)
            return 0;
    return 1;
}

static void canyon_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    ec_g_t *g = cy_of(t, v);
    const int16_t *p = t->p;
    int32_t buf[CTL], keep = 13000 + p[P_E4] * 155;
    uint32_t i, j, nz = (uint32_t)v->s[3] | 1u;
    if (!g || n > CTL)
        return;
    while (v->s[0] < (int32_t)n && v->s[1] <= p[P_E3] && v->s[2] > 200) {
        cy_spawn(t, v, g, m, v->s[0], (uint32_t)v->s[1], v->s[2]);
        v->s[0] += ec_gap(p, v, (uint32_t)v->s[1]);
        v->s[1]++;
        v->s[2] = (v->s[2] * keep) >> 15;
    }
    v->s[0] -= (int32_t)n;
    for (i = 0; i < n; i++)
        buf[i] = 0;
    for (j = 0; j < CY_NG; j++) {                     /* (ECHOES' sounds, as echoes_render plays them) */
        ec_g_t *q = &g[j];
        if (!q->len)
            continue;
        for (i = 0; i < n; i++, q->age++) {
            int32_t x, e, w, a;
            if (q->age < 0)
                continue;
            if (q->age >= q->len) {
                q->len = 0;
                break;
            }
            w = ((q->len - q->age) * q->inv) >> 14;
            e = (w * w) >> 15;
            if (q->age < 64)
                e = (e * q->age) >> 6;
            switch (q->src) {
            case 0u:
                x = sine_i(q->ph);
                q->ph += q->inc - ((q->inc >> 3) * (uint32_t)(32767 - w) >> 15);
                break;
            case 1u:
                q->flt += ((osc_saw(q->ph, q->inc) - q->flt) * ((w >> 1) + 800)) >> 15;
                x = q->flt;
                q->ph += q->inc;
                break;
            case 2u:
                x = (osc_pulse(q->ph, q->inc, 0x60000000u) + (osc_saw(q->ph << 1, q->inc << 1) >> 1)) >> 1;
                q->ph += q->inc;
                break;
            case 3u:
                nz ^= nz << 13;
                nz ^= nz >> 17;
                nz ^= nz << 5;
                q->flt += (((int32_t)(int16_t)(nz >> 16) - q->flt) * 6000) >> 15;
                x = (q->flt * sine_i(q->ph)) >> 13;
                q->ph += q->inc;
                break;
            default:
                x = sine_i(q->ph + (uint32_t)(sine_i(q->mph) * (w >> 3) * 2));
                q->ph += q->inc;
                q->mph += q->inc * 7u >> 1;
                break;
            }
            q->lp += ((x - q->lp) * q->lpk) >> 15;
            a = q->lp;
            q->hp += ((a - q->hp) * q->hpk) >> 15;
            a -= q->hpk ? q->hp : 0;
            buf[i] += (((a * e) >> 15) * q->amp) >> 15;
        }
    }
    v->s[3] = (int32_t)nz;
    for (i = 0; i < n; i++)                           /* (ECHOES' level: many at once meet its knee) */
        out[i] += voice_amp(soft_knee(clamp(buf[i], -65535, 65535), 22000), m, i) << 1;
}

/* ECHOES' 24 (a CANYON sound is an ECHOES one), then the long ones only the native image holds */
static const preset_t CANYON_PRESETS[] = {
    {"DUB BLIP", {0, 1, 0, 8, 95, 0, -20, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 30)},
    {"BOUNCE BALL", {0, 2, 1, 14, 105, 0, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 20)},
    {"RISING ECHO", {0, 5, 0, 10, 100, 2, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 30)},
    {"OCTAVE RAIN", {4, 5, 0, 6, 100, 12, 10, 10}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 50)},
    {"FALLING STAB", {2, 3, 0, 8, 95, -1, -10, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 35)},
    {"PLUCK DELAY", {1, 1, 0, 7, 90, 0, -25, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 30)},
    {"SWING TICKS", {3, 5, 3, 12, 95, 0, 20, 10}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 20)},
    {"BROKEN TAPE", {1, 2, 4, 10, 100, 0, -15, 90}, {0, 0, 127, 20}, 0, 0, FX(10, 20, 0, 30)},
    {"SLOWDOWN", {0, 7, 2, 12, 100, -1, -10, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 30)},
    {"GLASS FIFTHS", {4, 2, 0, 8, 100, 7, 15, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 50)},
    {"STUTTER BLIP", {0, 8, 0, 16, 110, 0, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 15)},
    {"NOISE ROLL", {3, 7, 1, 16, 100, 0, 0, 10}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 20)},
    {"DUB STAB ECHO", {2, 1, 0, 8, 100, 0, -30, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 40)},
    {"SCATTER", {4, 6, 4, 12, 100, 0, 0, 127}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 45)},
    {"SPIRAL", {0, 5, 1, 16, 105, 1, 10, 20}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 35)},
    {"FOURTHS DOWN", {1, 4, 0, 10, 98, -5, -10, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 35)},
    {"SOFT TICKS", {1, 4, 0, 8, 95, 0, -10, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 30)},
    {"FOURTHS UP", {4, 4, 0, 10, 100, 5, 10, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 45)},
    {"DOTTED PLUCK", {1, 1, 0, 8, 100, 0, -15, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 35)},
    {"WARM REPEAT", {1, 1, 0, 6, 90, 0, -30, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 40)},
    {"ARP ECHO", {0, 4, 0, 8, 100, 3, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 35)},
    {"TAPE BLIP", {0, 1, 2, 10, 100, 0, -25, 10}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 35)},
    {"MALLET ECHO", {4, 3, 0, 8, 95, 0, -5, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 40)},
    {"HALO", {4, 0, 0, 6, 110, 12, 20, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 30, 0, 70)},
    {"CANYON", {1, 2, 0, 40, 118, 0, -12, 15}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 60)},
    {"BELL CASCADE", {4, 7, 0, 64, 122, 0, -6, 20}, {0, 0, 127, 20}, 0, 0, FX(0, 30, 0, 70)},
    {"PLUCK STORM", {1, 8, 4, 64, 120, 0, -10, 60}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 50)},
    {"ENDLESS DUB", {2, 1, 0, 24, 116, 0, -20, 5}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 0, 50)},
    {"LONG BOUNCE", {0, 0, 1, 40, 120, 0, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 0, 30)},
    {"FM RAIN", {4, 6, 4, 64, 118, 0, 5, 40}, {0, 0, 127, 20}, 0, 0, FX(0, 30, 0, 60)},
    {"WHOLE TONE", {4, 5, 0, 30, 115, 2, 0, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 55)},
    {"AVALANCHE", {1, 7, 2, 18, 122, 0, -15, 10}, {0, 0, 127, 20}, 0, 0, FX(0, 20, 0, 55)},
};

static const engine_t ENG_CANYON = {
    .name = "CANYON",
    .id = EID_CANYON,
    .cc = 0x57,                  /* CC74 DECAY, CC71 TONE */
    .cats = "UFFFUUFFFFFFUFFUUUUUUUUFUUFUFFUU",
    .page_title = {"SOURCE", "REPEATS"},
    .edit = {
        {"SRC", F_ENUM, 0, 4, 0, N_EC_SRC, 0},
        {"TIME", F_ENUM, 0, 8, 1, N_EC_TIME, 0},
        {"SHAPE", F_ENUM, 0, 4, 0, N_EC_SHAPE, 0},
        {"COUNT", F_INT, 1, 64, 8, 0, 0},
        {"DECAY", F_PCT, 0, 127, 95, 0, 0},
        {"PITCH", F_INT, -12, 12, 0, 0, "st"},
        {"TONE", F_BIPCT, -64, 63, 0, 0, 0},
        {"SCAT", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = CANYON_PRESETS,
    .npresets = NELEM(CANYON_PRESETS),
    .poly = CY_POLY,
    .note_on = canyon_note_on,
    .render = canyon_render,
    .knob = {P_E1, P_E4, P_E5, P_E6},
    .ownenv = 1,
    .done = canyon_done,
};
