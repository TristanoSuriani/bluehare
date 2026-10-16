/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* DUBSIREN8 (1.5; SIREN8 on the device: 8 characters at most): DUBSIREN's 8-bit cousin, for every track (DUBSIREN's 16-bit echo is the last track's alone). The
 * same siren and the same 8 controls (a sound moves between the two unchanged: engines.c eng_for_track), made of
 * fewer bits: the siren is held a sample in two (22 kHz) and cut to 8 bits after its envelope (a fading siren
 * grows grittier), and the echo keeps 8 bits a sample at 11025 a second: 464 ms in 5 KB a track, each pass round
 * the loop a little rougher. The bits are cut towards 0, so a dying echo reaches silence and the voice ends.
 * The siren, its LFO, sweep and envelope: eng_dubsiren.c (ds_block, ds_osc, ds_step, ds_done, ds_target).
 * Voice state: as DUBSIREN's, and s[3] the held siren sample. The echo: the track's block (engines.c eng_slab). */
typedef struct {
    int8_t buf[DS_LEN];          /* the echo line: 8 bits a sample (the value / 256, cut towards 0) */
    uint32_t w, cnt;             /* the next write; the input samples summed so far (4 make one) */
    int32_t acc, lp, d8;         /* their sum; the loop's low-pass; the delay (samples, Q8: glides to TIME) */
} ds8_echo_t;

static inline __attribute__((always_inline)) ds8_echo_t *ds8_of(track_t *t) { return eng_slab(t, SLAB_SIREN8, sizeof(ds8_echo_t)); }   /* (engines.c) */
static inline int32_t ds8_q(int32_t x) { return (x + ((x >> 31) & 255)) >> 8; }   /* x / 256 towards 0 (no divide) */
static inline int32_t ds8_bits(int32_t x) { return ds8_q(x) * 256; }                 /* 8 bits, towards 0 */

static void dubsiren8_note_on(track_t *t, voice_t *v)
{
    ds8_echo_t *e = ds8_of(t);
    v->ph[0] = v->ph[1] = 0;
    v->s[1] = v->s[3] = 0;                            /* (the envelope and the echo go on: a retrigger, no click) */
    if (!v->s[0])
        v->s[2] = 0;
    if (e && !e->d8)
        e->d8 = ds_target(t);
}

static void dubsiren8_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    ds8_echo_t *e = ds8_of(t);
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0] & 3u, i, inc;
    int32_t env, fb = p[P_E6] * 270, dry = 32767 - p[P_E7] * 128, wet = p[P_E7] * 258, level = 0, held = v->s[3];
    if (!e)
        return;
    inc = ds_block(t, v, m, &env);
    if (!v->gate && fb > 26000)                       /* (let go: the runaway settles, the tail ends in seconds) */
        fb = 26000;
    e->d8 += (ds_target(t) - e->d8) >> 6;             /* TIME glides (the tape's pitch bend) */
    for (i = 0; i < n; i++) {
        int32_t s, rp, a, b, y, x;
        uint32_t r0;
        s = ds_osc(wave, v, inc);
        if (!(i & 1u))                                /* a sample in two: 22 kHz, 8 bits after the envelope */
            held = ds8_bits((s >> 1) * env >> 15);
        s = held;
        rp = (int32_t)(e->w << 8) + (int32_t)(e->cnt << 6) - e->d8;
        r0 = (uint32_t)(rp + (int32_t)(DS_LEN << 8) * 2) >> 8;   /* (0 .. 3 lines: wrapped by compares) */
        r0 = r0 >= DS_LEN ? r0 - DS_LEN : r0;
        r0 = r0 >= DS_LEN ? r0 - DS_LEN : r0;
        a = e->buf[r0] * 256;
        b = e->buf[r0 + 1u == DS_LEN ? 0u : r0 + 1u] * 256;
        y = a + (((b - a) * (rp & 255)) >> 8);
        level = (y < 0 ? -y : y) > level ? (y < 0 ? -y : y) : level;
        e->acc += s;
        if (++e->cnt == 4u) {                         /* a sample into the line: the input and the loop fed back */
            e->lp += ((y - e->lp) * 14000) >> 15;     /* (the loop darkened: ~2.4 kHz) */
            x = (e->acc >> 2) + ((e->lp * fb) >> 15);
            e->buf[e->w] = (int8_t)ds8_q(clamp(soft_knee(x, 20000), -32767, 32767));   /* (saturated, 8 bits) */
            e->w = e->w + 1u == DS_LEN ? 0u : e->w + 1u;
            e->acc = 0;
            e->cnt = 0;
        }
        out[i] += voice_amp(clamp(((s * dry) >> 15) + ((y * wet) >> 15), -32767, 32767), m, i) << 1;
    }
    v->s[0] = env;
    v->s[2] = level < 256 ? 0 : level;                /* (below one step of the line: silent) */
    v->s[3] = held;
}

/* {WAVE, RATE, DEPTH, SHAPE, SWEEP, TIME, FDBK, MIX}, as DUBSIREN's; the first 12 are DUBSIREN's 12 in its order
 * (a DUBSIREN sound off the last track keeps its preset number here) */
static const preset_t DUBSIREN8_PRESETS[] = {
    {"BIT SIREN", {0, 60, 12, 2, 0, 80, 90, 70}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 0, 20)},
    {"CHIP NEE NAW", {0, 40, 5, 0, 0, 70, 70, 60}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 0, 20)},
    {"BIT WHOOP", {0, 30, 24, 1, 0, 90, 80, 60}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 0, 20)},
    {"BIT WAIL", {1, 20, 9, 3, 0, 100, 85, 60}, {10, 0, 127, 40}, 0, 1, FX(0, 0, 0, 30)},
    {"CHIP LASER", {2, 110, 24, 2, -40, 60, 95, 70}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 0, 10)},
    {"BIT ALARM", {0, 90, 7, 0, 0, 50, 60, 50}, {0, 0, 127, 15}, 0, 1, FX(0, 0, 0, 10)},
    {"BIT DIVE", {0, 0, 0, 0, -50, 110, 100, 70}, {0, 0, 127, 50}, 0, 1, FX(0, 0, 0, 30)},
    {"BIT RISER", {2, 70, 3, 3, 45, 80, 90, 60}, {40, 0, 127, 40}, 0, 1, FX(0, 20, 0, 30)},
    {"GRIT RUNAWAY", {0, 50, 12, 2, 0, 90, 118, 90}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 0, 20)},
    {"CHIP TRILL", {3, 120, 2, 0, 0, 70, 70, 50}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 0, 20)},
    {"BIT UFO", {3, 85, 14, 3, 10, 100, 95, 70}, {20, 0, 127, 60}, 0, 1, FX(0, 30, 0, 40)},
    {"BIT HORN", {2, 0, 0, 0, 0, 85, 80, 55}, {0, 0, 127, 20}, 0, 1, FX(20, 0, 0, 20)},
    /* (Blue Hare) its own: the 8 bits as the point */
    {"ARCADE ZAP", {2, 127, 36, 2, -64, 30, 60, 50}, {0, 0, 127, 10}, 0, 1, FX(0, 0, 0, 10)},
    {"DIRTY DUB", {0, 45, 7, 3, 0, 95, 112, 95}, {0, 0, 127, 40}, 0, 1, FX(30, 0, 0, 25)},
    {"POCKET HORN", {0, 0, 0, 0, 0, 70, 75, 60}, {0, 0, 127, 15}, 0, 1, FX(0, 0, 0, 15)},
    {"BROKEN TAPE", {3, 25, 3, 3, -8, 120, 105, 85}, {30, 0, 127, 60}, 0, 1, FX(0, 20, 0, 35)},
};

static const engine_t ENG_DUBSIREN8 = {
    .name = "SIREN8",            /* (the protocol's names are 8 characters at most: DUBSIREN8 the code's name) */
    .id = EID_DUBSIREN8,
    .cc = 0x27,                  /* CC74 RATE, CC71 FDBK (as DUBSIREN) */
    .cats = "FFFFFFFFFLFLFFLF",
    .page_title = {"SIREN", "ECHO"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 0, N_DS_WAVE, 0},
        {"RATE", F_PCT, 0, 127, 60, 0, 0},
        {"DEPTH", F_INT, 0, 36, 12, 0, "st"},
        {"SHAPE", F_ENUM, 0, 3, 2, N_DS_SHAPE, 0},
        {"SWEEP", F_BIPCT, -64, 63, 0, 0, 0},
        {"TIME", F_PCT, 0, 127, 80, 0, 0},
        {"FDBK", F_PCT, 0, 127, 90, 0, 0},
        {"MIX", F_PCT, 0, 127, 70, 0, 0},
    },
    .presets = DUBSIREN8_PRESETS,
    .npresets = NELEM(DUBSIREN8_PRESETS),
    .poly = 1,
    .note_on = dubsiren8_note_on,
    .render = dubsiren8_render,
    .knob = {P_E1, P_E2, P_E5, P_E6},
    .ownenv = 1,
    .done = ds_done,
};
