/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* BREAKS (1.5): a breakbeat player, synthesized (no samples): a key starts a one-bar pattern of kick, snare (accents
 * and ghost notes) and hats (closed, open) at the song's tempo, looping while held; let go, it stops (the ADSR's
 * release). The key picks the pattern (12 an octave: the tracker changes the break row by row; a new note restarts
 * it: chops), SLICE the 16th it starts from (the chop's place), RATE plays it at half, normal, one and a half or double
 * time, SWING delays the off 16ths, STUTR rolls a 16th into 32nds or 64ths now and then, SHUF jumps to another 16th
 * now and then (a cut-up break), TUNE (+-12 semitones) and DECAY move the drums, CRUSH takes the rate and the bits
 * down (a sampler's grit). The drums are FMPERC's voices (fmp_run), three lanes (kick, snare, hats) a part.
 * Voice state: s[0] samples into the 16th (or the roll's part of it), s[1] the step (0 .. 15), s[2] the random
 * generator, s[3] the roll's hits still to come, s[6] its division (0 none, 2 32nds, 4 64ths), s[4] the crush's held
 * sample, s[5] its counter; the lanes br_lane[part][] (in RAM; the hats one lane: a closed hat cuts the open one). */
static uint32_t beat_samples(void);                  /* (fx.c: a beat at the song's tempo, or the MIDI clock's) */
typedef struct {
    voice_t v;                   /* fmp_run's state */
    int32_t env, gain;           /* the hit's envelope (Q15), its level (accent / ghost) */
    uint8_t open;                /* the hat lane: an open hat */
} br_lane_t;
static br_lane_t br_lane[NPART][3];

/* the patterns: kick (x), snare (x accent, o ghost), hats (x closed, o open), a 16th a character; my own, in the
 * manner of the breaks */
static const char *const BR_PAT[12][3] = {
    {"x.x...x...x..x..", "....x..o.o..x..o", "x.x.x.x.x.x.x.x."},   /* FUNKY */
    {"x.x.......xx....", "....x..o.x..x..o", "x.x.x.x.x.x.x.x."},   /* CLASSIC */
    {"x.....x...x.....", "....x.......x...", "x.xxx.x.x.xxx.x."},   /* THINK */
    {"x.........x.....", "........x.......", "x.x.x.x.x.x.x.x."},   /* HALFTIME */
    {"x......x..x.....", "....x.......x...", "x.x.x.x.x.x.x.xo"},   /* BOOM BAP */
    {"x.........x.....", "....x.oxo..ox.oo", "x.x.x.x.x.x.x.xo"},   /* JUNGLE */
    {"x......x..x.....", "....x.......x...", ".x.x.x.x.x.x.x.x"},   /* TWO STEP */
    {"x..x..x...x..x..", "....x..o....x..o", "x.xx.xx.xx.xx.x."},   /* SHUFFLE */
    {"x.x...x.x.x.....", "....x.....o.x...", "xxxxxxxxxxxxxxxx"},   /* BUSY */
    {"x.xx..x.x..x.x..", "..o.x.oox.o.xoxo", "xxxxxxxxxxxxxxxx"},   /* DRILL */
    {"x...........x...", "......x.........", "..x...x...x...x."},   /* SPARSE */
    {"x..x..x.x..x..x.", "....x.......x...", "x.xxx.xxx.xxx.xx"},   /* GALLOP */
};
static const char *const N_BR_RATE[] = {"1/2", "1", "3/2", "2", 0};
/* the drums: {note, RATIO, INDEX, FDEC, SWEEP, NTYP, FDBK, NOISE, TONE, decay}: kick, snare, closed hat, open hat */
static const int8_t BR_V[4][10] = {
    {36, 1, 24, 20, 30, 0, 0, 14, -16, 60}, {52, 11, 50, 16, 10, 0, 50, 90, 10, 48},
    {76, 12, 120, 10, 0, 0, 120, 70, 48, 30}, {76, 12, 120, 30, 0, 2, 120, 64, 46, 70},
};

static uint32_t br_part(const track_t *t) { return (uint32_t)(t - trk) % NPART; }
static uint32_t br_rand(voice_t *v)
{
    uint32_t x = (uint32_t)v->s[2];
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->s[2] = (int32_t)x;
    return x;
}

static uint32_t br_age;
static void br_hit(br_lane_t *L, int32_t gain, int open)
{
    L->v.age = ++br_age;                              /* (each hit its own noise) */
    fmperc_note_on(0, &L->v);
    L->env = 32767;
    L->gain = gain;
    L->open = (uint8_t)open;
}

/* the step's hits */
static void br_step(track_t *t, voice_t *v, uint32_t step)
{
    const char *const *P = BR_PAT[v->note % 12u];
    br_lane_t *L = br_lane[br_part(t)];
    char k = P[0][step & 15u], s = P[1][step & 15u], h = P[2][step & 15u];
    int32_t vg = v->vel * 258;
    if (k == 'x')
        br_hit(&L[0], vg, 0);
    if (s == 'x' || s == 'o')
        br_hit(&L[1], s == 'x' ? vg : vg * 3 / 10, 0);
    if (h == 'x' || h == 'o')
        br_hit(&L[2], (step & 1u) ? vg * 6 / 10 : vg * 8 / 10, h == 'o');   /* (the off 16ths softer) */
}

static void breaks_note_on(track_t *t, voice_t *v)
{
    uint32_t i;
    v->s[0] = 0;
    v->s[1] = t->p[P_E0] & 15;                        /* SLICE: the 16th it starts from */
    v->s[2] = (int32_t)(0x51ED270Bu ^ (v->age * 2654435761u));
    v->s[3] = v->s[6] = 0;
    v->s[4] = v->s[5] = 0;
    for (i = 0; t >= &trk[0] && t < &trk[NPART] && i < 3u; i++)
        br_lane[br_part(t)][i].env = 0;
    if (t >= &trk[0] && t < &trk[NPART])
        br_step(t, v, (uint32_t)v->s[1]);
}

static void breaks_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint8_t RATE_Q2[4] = {2, 4, 6, 8};  /* RATE x 4 */
    const int16_t *p = t->p;
    br_lane_t *L;
    uint32_t i, j, len16, cur, cr = (uint32_t)p[P_E7], sw = (uint32_t)p[P_E2];
    int32_t acc[CTL], y[CTL], dk = (p[P_E6] - 64) / 2, tune = (p[P_E5] - 64) * 3, hold = v->s[4];
    if (t < &trk[0] || t >= &trk[NPART] || n > CTL)
        return;
    L = br_lane[br_part(t)];
    len16 = beat_samples() / RATE_Q2[(uint32_t)p[P_E1] & 3u];   /* a 16th at the tempo and RATE */
    len16 = len16 < 256u ? 256u : len16;
    if (v->s[6])                                      /* a roll: its part of the 16th */
        cur = len16 / (uint32_t)v->s[6];
    else                                              /* SWING: an on 16th longer, so the off one after it late */
        cur = (v->s[1] & 1) ? len16 - ((len16 * sw) >> 8) : len16 + ((len16 * sw) >> 8);
    v->s[0] += (int32_t)n;
    if ((uint32_t)v->s[0] >= cur) {
        v->s[0] = 0;
        if (v->s[3] > 0) {                            /* the roll's next hit */
            v->s[3]--;
            br_step(t, v, (uint32_t)v->s[1]);
        } else {                                      /* the next 16th */
            uint32_t r = br_rand(v);
            v->s[6] = 0;
            v->s[1] = (v->s[1] + 1) & 15;
            if ((int32_t)((r >> 8) & 127u) < p[P_E4])   /* SHUF: another 16th */
                v->s[1] = (int32_t)((r >> 16) & 15u);
            if ((int32_t)((r >> 24) & 127u) < p[P_E3]) {   /* STUTR: this 16th rolled */
                v->s[6] = (r & 1u) ? 2 : 4;
                v->s[3] = v->s[6] - 1;
            }
            br_step(t, v, (uint32_t)v->s[1]);
        }
    }
    for (i = 0; i < n; i++)
        acc[i] = 0;
    for (j = 0; j < 3u; j++) {
        const int8_t *K = BR_V[j == 2u && L[j].open ? 3u : j];
        int16_t e[8];
        int32_t env = L[j].env, tau, d;
        if (env < 16)
            continue;
        for (i = 0; i < 8u; i++)
            e[i] = K[i + 1];
        fmp_run(e, &L[j].v, y, n, K[0] * 16 + tune, 0);
        d = clamp(K[9] + dk, 0, 127);
        if (v->s[6] && j == 1u)
            d = d / 2;                                /* (a roll's snares short) */
        tau = 1 + d * d / 6;
        L[j].env = env - env / tau - 1;
        for (i = 0; i < n; i++)
            acc[i] += ((y[i] * env >> 15) * L[j].gain) >> 15;
    }
    for (i = 0; i < n; i++) {
        int32_t s = clamp(acc[i], -32767, 32767);
        if (cr) {                                     /* CRUSH: held 1 .. 16 samples, 16 .. 6 bits */
            if (++v->s[5] >= 1 + (int32_t)(cr >> 3)) {
                v->s[5] = 0;
                hold = s & ~((1 << (cr / 13u)) - 1);
            }
            s = hold;
        }
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->s[4] = hold;
}

/* {SLICE, RATE, SWING, STUTR, SHUF, TUNE, DECAY, CRUSH}; RATE 0 1/2 1 1 2 3/2 3 2. The key picks the break: C FUNKY,
 * C# CLASSIC, D THINK, D# HALFTIME, E BOOM BAP, F JUNGLE, F# TWO STEP, G SHUFFLE, G# BUSY, A DRILL, A# SPARSE, B GALLOP */
static const preset_t BREAKS_PRESETS[] = {
    {"BREAKS", {0, 1, 0, 0, 0, 64, 64, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 0, 15)},
    {"HALF TIME", {0, 0, 20, 0, 0, 60, 76, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 10, 25)},
    {"ROLLS", {0, 1, 0, 50, 0, 64, 60, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 10, 15)},
    {"CUT UP", {0, 1, 0, 20, 50, 64, 64, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 10, 15)},
    {"DRILL N BASS", {0, 3, 0, 70, 40, 70, 50, 0}, {0, 0, 127, 5}, 0, 1, FX(20, 0, 10, 10)},
    {"CRUSHED", {0, 1, 0, 20, 20, 64, 64, 110}, {0, 0, 127, 5}, 0, 1, FX(30, 0, 10, 10)},
    {"DUB BREAK", {0, 0, 30, 10, 0, 56, 80, 30}, {0, 0, 127, 10}, 0, 1, FX(10, 0, 60, 30)},
    /* (Blue Hare, the IDM review) */
    {"CHOPPED", {4, 1, 0, 10, 30, 64, 64, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 0, 15)},
    {"SHRED", {0, 3, 0, 90, 90, 80, 50, 60}, {0, 0, 127, 5}, 0, 1, FX(30, 0, 20, 10)},
};

/* the keys: the breaks from the first C (C2 = 36 FUNKY), an octave of them */
static int32_t breaks_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

static const engine_t ENG_BREAKS = {
    .name = "BREAKS",
    .id = EID_BREAKS,
    .cc = 0x45,                  /* CC74 STUTR, CC71 SHUF */
    .cats = "DDDDDDDDD",
    .page_title = {"BREAK", "MANGLE"},
    .edit = {
        {"SLICE", F_INT, 0, 15, 0, 0, 0},
        {"RATE", F_ENUM, 0, 3, 1, N_BR_RATE, 0},
        {"SWING", F_PCT, 0, 127, 0, 0, 0},
        {"STUTR", F_PCT, 0, 127, 0, 0, 0},
        {"SHUF", F_PCT, 0, 127, 0, 0, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"CRUSH", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = BREAKS_PRESETS,
    .npresets = NELEM(BREAKS_PRESETS),
    .poly = 1,
    .note_on = breaks_note_on,
    .render = breaks_render,
    .knob = {P_E0, P_E2, P_E3, P_E4},
    .keys = breaks_keys,
};
