/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* ABYSS (1.5, FOUNDRY): a cavern that rings (Fel-Tracker's own). One voice a track excites a feedback network of
 * four delay lines (a cavern's walls: SIZE their lengths, ~15 .. ~70 ms, in ratios no two share; TUNE pulls them
 * towards the key's period instead, a cavern that sings the note), FDBK how long it rings (from a slap to near
 * forever), DAMP the walls soaking up the highs, MOD the walls moving a little (a slow, seasick wobble), DRIVE
 * saturating the loop (the dub-industrial grind). SRC what strikes it: PULSE (a click), BURST (noise), TONE (a saw at
 * the key, held), DRONE (noise, held); MIX the strike against the cavern. The network runs at a quarter of the rate,
 * its lines 8 bits a sample (companded: the grit of the deep); the cavern keeps ringing across notes, the voice
 * ends when it has died away (engine_t.ownenv; the velocity the strike's level).
 * State: the cavern (the track's block, engines.c eng_slab: 4 lines of AB_LEN bytes), the voice's s[]: s[0] the strike's
 * samples, s[1] the strike's filter, s[2] the last network output, s[3] the one before (the interpolation), s[4]
 * the samples into the 4, s[5] the noise, s[6] the input summed over the 4, s[7] the level. */
#define AB_LEN 768
#define AB_NL 4
typedef struct {
    int8_t line[AB_NL][AB_LEN];
    uint16_t w;                  /* the write place (the same for every line) */
    int32_t lp[AB_NL];           /* DAMP's filter in each line */
    uint32_t mph;                /* MOD's phase */
} ab_cav_t;
static const uint16_t AB_RATIO[AB_NL] = {256, 325, 387, 469};   /* the lines' lengths against the first, Q8 */
static const char *const N_AB_SRC[] = {"PULSE", "BURST", "TONE", "DRONE", 0};

static inline __attribute__((always_inline)) ab_cav_t *ab_of(track_t *t) { return eng_slab(t, SLAB_ABYSS, sizeof(ab_cav_t)); }   /* (engines.c) */
/* 8 bits companded: the square root of the magnitude, the sign (a quiet tail keeps more of its bits) */
static inline int8_t ab_put(int32_t x, int near)
{
    int32_t a = x < 0 ? -x : x, q = 0, bit;
    a = a > 32767 ? 32767 : a;
    if (a < 6)
        return 0;                                     /* (a dead zone: no tail circling for ever in the last bits) */
    for (bit = 64; bit; bit >>= 1)                    /* (the square root by bits: 7 steps) */
        if ((q + bit) * (q + bit) * 2 <= a)
            q += bit;
    if (near && q >= 6 && q < 127 && (q + 1) * (q + 1) * 2 - a < a - q * q * 2)
        q++;                                          /* (to the nearest while held: rounding down would drain the loop; released, down: it dies) */
    return (int8_t)(x < 0 ? -q : q);
}
static inline int32_t ab_get(int8_t q) { int32_t a = q < 0 ? -q : q; a = a * a * 2; return q < 0 ? -a : a; }

static void abyss_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = 0;
    v->s[7] = 4096 + v->vel * 225;
    if (!v->s[5])
        v->s[5] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

static int abyss_done(track_t *t, voice_t *v)
{
    ab_cav_t *c = ab_of(t);
    uint32_t k, i;
    int32_t big = 0;
    if (!c)
        return 1;
    if (v->gate)
        return 0;
    for (k = 0; k < AB_NL; k++)                       /* (died away: every line quiet) */
        for (i = 0; i < AB_LEN; i += 16u)
            big |= c->line[k][i] > 2 || c->line[k][i] < -2;
    return !big && v->s[0] > 4410;
}

static void abyss_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    ab_cav_t *c = ab_of(t);
    const int16_t *p = t->p;
    uint32_t src = (uint32_t)p[P_E0] & 3u, i, k, nz = (uint32_t)v->s[5], len[AB_NL];
    int32_t age = v->s[0], slp = v->s[1], y0 = v->s[2], y1 = v->s[3], sub = v->s[4], acc = v->s[6], amp = v->s[7];
    int32_t fb = v->gate ? 20000 + p[P_E2] * 98 : clamp(20000 + p[P_E2] * 98, 0, 31000),   /* (released: it dies away) */ damp = 32767 - p[P_E3] * 230, drive = 256 + p[P_E6] * 10, mix = p[P_E7];
    int32_t inv = (int32_t)((256u << 16) / (uint32_t)drive);   /* (DRIVE's gain taken back: it colours the loop, never
                                                       * makes it grow) */
    uint32_t hz = (((m->inc >> 16) * (uint32_t)FS) >> 16) | 1u;   /* the key in Hz (32 bits: no 64-bit divide) */
    int32_t base = 165 + p[P_E1] * 4, per = (int32_t)(((uint32_t)FS / 4u << 16) / hz), tn = p[P_E4];   /* (its period, Q16) */
    if (!c)
        return;
    c->mph += 6000u * 32u * (uint32_t)p[P_E5];        /* MOD */
    for (k = 0; k < AB_NL; k++) {                     /* SIZE (and TUNE: towards the key's period, at a quarter rate) */
        int32_t l = (base * AB_RATIO[k]) >> 8, tl = (per >> 16) * (int32_t)(k + 1u);
        l += ((tl - l) * tn) >> 7;
        l += (sine_i(c->mph + k * 0x40000000u) * p[P_E5]) >> 17;
        len[k] = (uint32_t)clamp(l, 8, AB_LEN - 1);
    }
    for (i = 0; i < n; i++) {
        int32_t x = 0, y;
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        switch (src) {                                /* SRC: the strike */
        case 0u: x = age < 88 ? 30000 * (88 - age) / 88 : 0; break;   /* (a 2 ms knock) */
        case 1u: x = age < 2205 ? (((int32_t)(int16_t)(nz >> 16)) * (2205 - age)) / 2205 : 0; break;
        case 2u: x = v->gate ? osc_saw(v->ph[0], m->inc) >> 1 : 0; v->ph[0] += m->inc; break;
        default: x = v->gate ? (int32_t)(int16_t)(nz >> 16) >> 2 : 0; break;
        }
        slp += ((x - slp) * 9000) >> 15;
        if (!x && slp > -8 && slp < 8)
            slp = 0;                                  /* (the shift rounds down: a silent strike would stick at -3, a DC
                                                       * the cavern holds for ever) */
        x = (slp * amp) >> 15;
        acc += x;
        if (++sub >= 4) {                             /* the cavern, a sample in four */
            int32_t r[AB_NL], s = 0, in = acc << 1;   /* (the strike x 8 into the walls: a short hit carries little) */
            sub = 0;
            acc = 0;
            for (k = 0; k < AB_NL; k++) {
                uint32_t rp = (c->w + AB_LEN - len[k]) % AB_LEN;
                r[k] = ab_get(c->line[k][rp]);
                c->lp[k] += (((r[k] - c->lp[k]) * damp) >> 15);   /* DAMP */
                s += c->lp[k];
            }
            s >>= 1;                                  /* the Householder mix: each line gets the others' sum */
            for (k = 0; k < AB_NL; k++) {
                int32_t f = ((s - c->lp[k]) * fb) >> 15;
                f = (softclip(((f + in) * drive) >> 8) * inv) >> 16;   /* DRIVE: the loop saturated */
                c->line[k][c->w] = ab_put(f, v->gate);
            }
            c->w = (uint16_t)(c->w + 1u == AB_LEN ? 0u : c->w + 1u);
            y1 = y0;
            y0 = s << 1;
        }
        y = y1 + (((y0 - y1) * sub) >> 2);            /* (between the cavern's samples) */
        y = (x * (127 - mix) + y * mix) >> 7;
        out[i] += voice_amp(soft_knee(clamp(y, -65535, 65535), 24000), m, i) << 1;
        age++;
    }
    v->s[0] = age;
    v->s[1] = slp;
    v->s[2] = y0;
    v->s[3] = y1;
    v->s[4] = sub;
    v->s[5] = (int32_t)nz;
    v->s[6] = acc;
}

/* {SRC, SIZE, FDBK, DAMP, TUNE, MOD, DRIVE, MIX}; SRC 0 PULSE 1 BURST 2 TONE 3 DRONE */
static const preset_t ABYSS_PRESETS[] = {
    {"CAVERN", {1, 100, 110, 40, 0, 20, 20, 100}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 0, 20)},
    {"DUB SPACE", {0, 80, 115, 60, 0, 30, 40, 110}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 30)},
    {"PIT", {1, 127, 120, 70, 0, 40, 60, 120}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 0, 30)},
    {"SINGING CAVE", {2, 60, 112, 30, 110, 10, 20, 90}, {10, 0, 127, 60}, 0, 1, FX(0, 20, 10, 40)},
    {"DRONE HOLE", {3, 110, 118, 50, 40, 50, 50, 120}, {60, 0, 127, 100}, 0, 1, FX(10, 30, 0, 50)},
    {"IRON HALL", {0, 70, 116, 20, 60, 20, 80, 110}, {0, 0, 127, 30}, 0, 1, FX(20, 0, 10, 30)},
    {"SEWER", {1, 50, 108, 90, 20, 60, 70, 100}, {0, 0, 127, 20}, 0, 1, FX(20, 0, 10, 20)},
    {"ABYSSAL", {2, 127, 124, 60, 80, 30, 40, 120}, {40, 0, 127, 110}, 0, 1, FX(10, 30, 0, 60)},
    {"MINE SHAFT", {1, 90, 116, 50, 0, 30, 40, 110}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 0, 30)},
    {"TUNNEL KNOCK", {0, 60, 112, 40, 0, 20, 30, 100}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 10, 20)},
    {"WELL", {0, 40, 118, 30, 90, 10, 20, 110}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 0, 20)},
    {"BOILER ROOM", {1, 70, 114, 60, 30, 40, 90, 110}, {0, 0, 127, 20}, 0, 1, FX(20, 0, 10, 30)},
    {"DEEP CHANT", {2, 90, 120, 50, 110, 20, 30, 110}, {40, 0, 127, 100}, 0, 1, FX(0, 30, 10, 50)},
    {"VOID DRONE", {3, 127, 124, 70, 60, 60, 40, 127}, {60, 0, 127, 110}, 0, 1, FX(10, 30, 0, 60)},
    {"RUST TANK", {1, 50, 115, 20, 70, 30, 100, 110}, {0, 0, 127, 20}, 0, 1, FX(20, 0, 10, 30)},
    {"DRIP", {0, 30, 105, 10, 100, 5, 10, 90}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 20, 30)},
    {"BLACK HOLE", {3, 127, 126, 80, 0, 40, 30, 127}, {60, 0, 127, 120}, 0, 1, FX(10, 30, 0, 60)},
    {"CAVE DRIP", {0, 40, 110, 20, 110, 10, 10, 100}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 10, 30)},
    {"BUNKER", {1, 60, 110, 70, 0, 20, 50, 100}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 10, 30)},
    {"SONAR", {2, 50, 116, 30, 127, 5, 10, 100}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 10, 30)},
    {"METRO TUNNEL", {1, 100, 118, 50, 20, 50, 60, 110}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 10, 30)},
    {"SINGING PIT", {2, 80, 121, 40, 120, 20, 30, 115}, {30, 0, 127, 100}, 0, 1, FX(0, 30, 10, 50)},
    {"GRINDER HALL", {1, 70, 115, 40, 40, 30, 127, 110}, {0, 0, 127, 20}, 0, 1, FX(20, 0, 10, 30)},
    {"DEEP KNOCK", {0, 110, 117, 60, 0, 20, 40, 110}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 10, 30)},
};

static const engine_t ENG_ABYSS = {
    .name = "ABYSS",
    .id = EID_ABYSS,
    .cc = 0x34,                  /* CC74 FDBK .. SIZE, CC71 DAMP */
    .cats = "FFFPPFFPFFFFPPFFPFFFFPFF",
    .page_title = {"CAVERN", "WALLS"},
    .edit = {
        {"SRC", F_ENUM, 0, 3, 1, N_AB_SRC, 0},
        {"SIZE", F_PCT, 0, 127, 100, 0, 0},
        {"FDBK", F_PCT, 0, 127, 110, 0, 0},
        {"DAMP", F_PCT, 0, 127, 40, 0, 0},
        {"TUNE", F_PCT, 0, 127, 0, 0, 0},
        {"MOD", F_PCT, 0, 127, 20, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 20, 0, 0},
        {"MIX", F_PCT, 0, 127, 100, 0, 0},
    },
    .presets = ABYSS_PRESETS,
    .npresets = NELEM(ABYSS_PRESETS),
    .poly = 1,
    .note_on = abyss_note_on,
    .render = abyss_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .ownenv = 1,
    .done = abyss_done,
};
