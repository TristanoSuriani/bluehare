/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* FLOOR (1.5, WAREHOUSE): four-on-the-floor beats, as BREAKS is the breaks (Blue Hare's own, synthesized: no samples).
 * A key starts a one-bar groove of kick, clap, hats (closed, open) and percussion at the song's tempo, looping while
 * held; let go, it stops. The key picks the groove (12 an octave: the tracker changes it row by row), each with its
 * own kick (its pitch, its fall, its length, its grit: a gabber kick is no disco kick): TECHNO, HOUSE, DEEP HOUSE, TECH
 * HOUSE, MINIMAL, DUB TECHNO, TRANCE, PROGRESSIVE, GABBER, HARD TECHNO, EDM, DISCO. SWING delays the off 16ths; KICK,
 * CLAP, HATS and PERC the lanes' levels; TUNE and DECAY move the drums; DRIVE pushes the whole beat into clipping
 * (the kick first). The drums are FMPERC's voices (fmp_run), four lanes a part.
 * Voice state: s[0] samples into the 16th, s[1] the step (0 .. 15). The lanes: the track's block (engines.c eng_slab;
 * the hats one lane: a closed hat cuts the open one). */
typedef struct {
    voice_t v;                   /* fmp_run's state */
    int32_t env, gain;           /* the hit's envelope (Q15), its level */
    uint8_t open;                /* the hat lane: an open hat */
} fl_lane_t;
typedef struct { fl_lane_t l[4]; uint32_t age; } fl_part_t;

/* the grooves: kick (x, o softer), clap (x, o a ghost), hats (x closed, o open), perc (x, o softer), a 16th a
 * character; then the kick: {note, its fall, its length, its grit}. My own, in the manner of the styles */
static const char *const FL_PAT[12][4] = {
    {"x...x...x...x...", "....x.......x...", "..o...o...o...o.", "o.xoo.xoo.xoo.xo"},   /* TECHNO */
    {"x...x...x...x...", "....x.......x...", "..o...o...o...o.", "..x...x...x..x.."},   /* HOUSE */
    {"x...x...x...x...", "....x.......x..o", "..x...x...x...x.", "x..x..x...x..x.."},   /* DEEP HOUSE */
    {"x...x...x...x...", "....x.......x...", "..o...o...o...o.", ".xx..xx..xx..x.x"},   /* TECH HOUSE */
    {"x...x...x...x...", "........x.......", "...x...x...x...x", "x..x....x.x....."},   /* MINIMAL */
    {"x...x...x...x...", "....o.......o...", "..o...o...o...o.", "......x.......x."},   /* DUB TECHNO */
    {"x...x...x...x...", "....x.......x...", "..o...o...o...o.", "xxxxxxxxxxxxxxxx"},   /* TRANCE */
    {"x...x...x...x...", "....x.......x...", "x.o.x.o.x.o.x.o.", "..x..x..x..x..x."},   /* PROGRESSIVE */
    {"x...x...x...x...", "........x.......", "..x...x...x...x.", "x.x.x.x.x.x.x.x."},   /* GABBER */
    {"x...x...x...x.x.", "....x.......x...", "..o...o...o...o.", "xxxxxxxxxxxxxxxx"},   /* HARD TECHNO */
    {"x...x...x...x...", "....x.......x...", "..o...o...o...o.", "....x..x....x..x"},   /* EDM */
    {"x...x...x...x...", "....x.......x...", "x.o.x.o.x.o.x.o.", "x.x.x.x.x.x.x.x."},   /* DISCO */
};
static const int8_t FL_KICK[12][4] = {  /* {note, the fall (SWEEP), the length (decay), grit 0..127} */
    {33, 30, 55, 20}, {35, 24, 50, 0}, {33, 20, 60, 0}, {34, 26, 45, 10}, {36, 20, 35, 0}, {31, 26, 70, 0},
    {33, 36, 60, 10}, {34, 30, 55, 0}, {31, 40, 75, 120}, {32, 36, 60, 70}, {33, 40, 70, 40}, {36, 16, 40, 0},
};
/* the drums: {note, RATIO, INDEX, FDEC, SWEEP, NTYP, FDBK, NOISE, TONE, decay}: kick (the groove's note, fall and
 * length over it), clap, closed hat, open hat, perc (a shaker's tick) */
static const int8_t FL_V[5][10] = {
    {36, 1, 24, 20, 30, 0, 0, 10, -16, 60}, {52, 11, 30, 10, 0, 0, 40, 120, 20, 45},
    {76, 12, 120, 10, 0, 0, 120, 70, 48, 30}, {76, 12, 120, 30, 0, 2, 120, 64, 46, 70},
    {84, 12, 100, 8, 0, 1, 100, 90, 60, 18},
};

static fl_part_t *flr_of(track_t *t) { return eng_slab(t, SLAB_FLOOR, sizeof(fl_part_t)); }

static void fl_hit(fl_part_t *P, uint32_t j, int32_t gain, int open)
{
    fl_lane_t *L = &P->l[j];
    L->v.age = ++P->age;                              /* (each hit its own noise) */
    fmperc_note_on(0, &L->v);
    L->env = 32767;
    L->gain = gain;
    L->open = (uint8_t)open;
}

/* the step's hits */
static void fl_step(track_t *t, voice_t *v, uint32_t step)
{
    const char *const *G = FL_PAT[v->note % 12u];
    fl_part_t *P = flr_of(t);
    int32_t vg = v->vel * 258, lv[4];
    uint32_t j;
    if (!P)
        return;
    for (j = 0; j < 4u; j++)
        lv[j] = vg * t->p[P_E1 + j] / 127;            /* KICK CLAP HATS PERC */
    for (j = 0; j < 4u; j++) {
        char c = G[j][step & 15u];
        if (c == 'x' || c == 'o')
            fl_hit(P, j, c == 'x' ? lv[j] : lv[j] * (j == 1u ? 3 : 6) / 10, j == 2u && c == 'o');
    }
}

static void floor_note_on(track_t *t, voice_t *v)
{
    fl_part_t *P = flr_of(t);
    uint32_t i;
    v->s[0] = 0;
    v->s[1] = 0;
    for (i = 0; P && i < 4u; i++)
        P->l[i].env = 0;
    fl_step(t, v, 0);
}

static void floor_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    fl_part_t *P = flr_of(t);
    const int8_t *KK = FL_KICK[v->note % 12u];
    uint32_t i, j, len16, cur, sw = (uint32_t)p[P_E0];
    int32_t acc[CTL], y[CTL], dk = (p[P_E6] - 64) / 2, tune = (p[P_E5] - 64) * 3, drive = 256 + p[P_E7] * 12;
    int32_t grit = 256 + KK[3] * 16;
    if (!P || n > CTL)
        return;
    len16 = beat_samples() / 4u;                      /* a 16th at the tempo */
    len16 = len16 < 256u ? 256u : len16;
    cur = (v->s[1] & 1) ? len16 - ((len16 * sw) >> 8) : len16 + ((len16 * sw) >> 8);   /* SWING */
    v->s[0] += (int32_t)n;
    if ((uint32_t)v->s[0] >= cur) {
        v->s[0] = 0;
        v->s[1] = (v->s[1] + 1) & 15;
        fl_step(t, v, (uint32_t)v->s[1]);
    }
    for (i = 0; i < n; i++)
        acc[i] = 0;
    for (j = 0; j < 4u; j++) {
        fl_lane_t *L = &P->l[j];
        const int8_t *K = FL_V[j == 0u ? 0u : j == 1u ? 1u : j == 2u ? (L->open ? 3u : 2u) : 4u];
        int16_t e[8];
        int32_t env = L->env, tau, d, note = K[0];
        if (env < 16)
            continue;
        for (i = 0; i < 8u; i++)
            e[i] = K[i + 1];
        d = K[9];
        if (j == 0u) {                                /* the groove's kick */
            note = KK[0];
            e[3] = KK[1];
            d = KK[2];
        }
        fmp_run(e, &L->v, y, n, note * 16 + tune, 0);
        d = clamp(d + dk, 0, 127);
        tau = 1 + d * d / 6;
        L->env = env - env / tau - 1;
        for (i = 0; i < n; i++) {
            int32_t s = ((y[i] * env >> 15) * L->gain) >> 15;
            if (j == 0u && KK[3])                     /* (the kick's grit) */
                s = softclip((s * grit) >> 8);
            acc[i] += s;
        }
    }
    for (i = 0; i < n; i++) {
        int32_t s = softclip((clamp(acc[i], -65535, 65535) * drive) >> 8);   /* DRIVE */
        out[i] += voice_amp(s, m, i) << 1;
    }
}

/* {SWING, KICK, CLAP, HATS, PERC, TUNE, DECAY, DRIVE}. The key picks the groove: C TECHNO, C# HOUSE, D DEEP HOUSE,
 * D# TECH HOUSE, E MINIMAL, F DUB TECHNO, F# TRANCE, G PROGRESSIVE, G# GABBER, A HARD TECHNO, A# EDM, B DISCO */
static const preset_t FLOOR_PRESETS[] = {
    {"FLOOR", {0, 110, 90, 80, 60, 64, 64, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 0, 15)},
    {"PEAK TIME", {0, 120, 90, 90, 70, 64, 60, 20}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 0, 15)},
    {"DEEP NIGHT", {20, 100, 70, 70, 60, 58, 72, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 10, 25)},
    {"WAREHOUSE", {0, 120, 80, 85, 80, 62, 66, 40}, {0, 0, 127, 5}, 0, 1, FX(10, 0, 10, 30)},
    {"AFTER HOURS", {10, 105, 60, 70, 75, 60, 70, 10}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 20, 30)},
    {"SWUNG HOUSE", {45, 110, 90, 80, 70, 64, 64, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 0, 20)},
    {"DRY TECHNO", {0, 115, 80, 80, 70, 64, 56, 10}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 0, 5)},
    {"HARD FLOOR", {0, 127, 90, 90, 90, 66, 60, 70}, {0, 0, 127, 5}, 0, 1, FX(20, 0, 0, 15)},
    {"GABBER RAGE", {0, 127, 70, 80, 70, 64, 70, 110}, {0, 0, 127, 5}, 0, 1, FX(30, 0, 0, 10)},
    {"TRANCE GATE", {0, 115, 85, 90, 80, 64, 60, 10}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 15, 25)},
    {"DISCO BALL", {25, 100, 90, 95, 70, 66, 64, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 10, 30)},
    {"MINIMAL CLICK", {10, 100, 60, 70, 90, 70, 40, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 15, 20)},
    {"DUB FLOOR", {15, 105, 60, 75, 50, 58, 80, 0}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 60, 35)},
    {"BIG ROOM", {0, 127, 100, 85, 70, 64, 70, 30}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 10, 45)},
    {"LOFI FLOOR", {20, 110, 80, 70, 60, 58, 64, 50}, {0, 0, 127, 5}, 0, 1, FX(20, 10, 0, 20)},
    {"TOOL", {0, 120, 50, 80, 100, 64, 56, 20}, {0, 0, 127, 5}, 0, 1, FX(0, 0, 10, 15)},
};

/* the keys: the grooves from the first C (C2 = 36 TECHNO), an octave of them */
static int32_t floor_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

static const engine_t ENG_FLOOR = {
    .name = "FLOOR",
    .id = EID_FLOOR,
    .cc = 0x18,                  /* CC74 SWING, CC71 DRIVE */
    .cats = "DDDDDDDDDDDDDDDD",
    .page_title = {"GROOVE", "MIX"},
    .edit = {
        {"SWING", F_PCT, 0, 127, 0, 0, 0},
        {"KICK", F_PCT, 0, 127, 110, 0, 0},
        {"CLAP", F_PCT, 0, 127, 90, 0, 0},
        {"HATS", F_PCT, 0, 127, 80, 0, 0},
        {"PERC", F_PCT, 0, 127, 60, 0, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = FLOOR_PRESETS,
    .npresets = NELEM(FLOOR_PRESETS),
    .poly = 1,
    .note_on = floor_note_on,
    .render = floor_render,
    .knob = {P_E0, P_E1, P_E3, P_E7},
    .keys = floor_keys,
};
