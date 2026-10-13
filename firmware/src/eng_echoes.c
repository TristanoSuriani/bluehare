/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* ECHOES (1.5): a note and its echoes as one instrument (Fel-Tracker's own). Not a delay line: every repeat is played
 * anew, so each can be other than the last. A key plays SRC (BLIP a sine dropping a little, PLUCK a saw through a
 * closing low-pass, STAB a pulse and a saw an octave up, NOISE a burst tuned to the note, FM a two-operator bell),
 * then COUNT repeats at the song's tempo: TIME a note value (1/4 .. 1/64, dotted, triplets), SHAPE how the gaps go
 * (EVEN; BOUNCE each shorter, a dropped ball; SLOW each longer; SWING long, short; RANDOM, the same each time a note
 * is played), DECAY what each keeps of the one before, PITCH semitones a repeat (an octave a repeat: a shimmer),
 * TONE each one darker (down) or thinner (up), SCAT a little of each repeat's time and pitch left to chance. The
 * echoes go on after the key is let go, as echoes do; the voice ends with the last one (engine_t.ownenv: its own
 * envelope; the velocity sets the level).
 * State: the sounds of a voice in the track's block (engines.c eng_slab, EC_NG at once, the oldest dropped), the voice's s[]:
 * s[0] samples to the next, s[1] the repeats so far, s[2] the next's level (Q15), s[3] the random. */
#define EC_POLY 4
#define EC_NG 6
static uint32_t beat_samples(void);                  /* (fx.c: a beat at the song's tempo, or the MIDI clock's) */
static const char *const N_EC_SRC[] = {"BLIP", "PLUCK", "STAB", "NOISE", "FM", 0};
static const char *const N_EC_TIME[] = {"1/4", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32", "1/64", 0};
static const char *const N_EC_SHAPE[] = {"EVEN", "BOUNCE", "SLOW", "SWING", "RANDOM", 0};
static const uint16_t EC_TIME_Q12[9] = {4096, 3072, 2048, 1365, 1536, 1024, 683, 512, 256};   /* of a beat */
static const uint16_t EC_LEN_MS[5] = {90, 260, 200, 70, 420};   /* each source's length (the repeats' too) */
typedef struct {
    uint32_t ph, inc, mph;       /* the phase, its increment; FM's modulator */
    int32_t age, len;            /* samples since it started (< 0: later in the block); 0 len: free */
    int32_t amp, lpk, hpk;       /* its level (Q15); TONE's low-pass and high-pass (Q15 coefficients) */
    int32_t inv;                 /* 2^29 / len (its progress without a divide a sample) */
    int32_t lp, hp, flt;         /* their states; PLUCK's own filter */
    uint8_t src;
} ec_g_t;

static inline __attribute__((always_inline)) ec_g_t *ec_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    {
        ec_g_t (*a)[EC_NG] = eng_slab(t, SLAB_ECHOES, sizeof(ec_g_t) * EC_POLY * EC_NG);   /* (engines.c) */
        return a && i < EC_POLY ? a[i] : 0;
    }
}

static uint32_t ec_rand(voice_t *v)
{
    uint32_t r = (uint32_t)v->s[3];
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    v->s[3] = (int32_t)r;
    return r;
}

/* the gap after repeat r (samples): TIME and SHAPE */
static int32_t ec_gap(const int16_t *p, voice_t *v, uint32_t r)
{
    int32_t g = (int32_t)((beat_samples() * EC_TIME_Q12[(uint32_t)p[P_E1] % 9u]) >> 12), k;
    switch ((uint32_t)p[P_E2] % 5u) {
    case 1u:                                          /* BOUNCE: x 0.82 each */
        for (k = 0; k < (int32_t)r && g > 200; k++)
            g = (g * 26870) >> 15;
        break;
    case 2u:                                          /* SLOW: x 1.18 each */
        for (k = 0; k < (int32_t)r && g < 4 * FS; k++)
            g = (g * 38666) >> 15;
        break;
    case 3u:                                          /* SWING: 2/3 long, 1/3 short (a pair: two gaps) */
        g = r & 1u ? (g * 2) / 3 : (g * 4) / 3;
        break;
    case 4u:                                          /* RANDOM: 0.5 .. 1.5 of the gap */
        g = (g >> 1) + (int32_t)(((ec_rand(v) >> 16) * (uint32_t)g) >> 16);
        break;
    default:
        break;
    }
    if (p[P_E7]) {                                    /* SCAT: up to +-25 % */
        int32_t j = (int32_t)(ec_rand(v) >> 17) - 16384;
        g += (int32_t)(((int64_t)g * j * p[P_E7]) >> 23);
    }
    return g < 64 ? 64 : g;
}

/* repeat r (0: the note itself) at sample offset at, level amp (Q15) */
static void ec_spawn(track_t *t, voice_t *v, ec_g_t *g, const vmod_t *m, int32_t at, uint32_t r, int32_t amp)
{
    const int16_t *p = t->p;
    uint32_t i, old = 0;
    int32_t p16, tone = p[P_E6] * (int32_t)r, lpc, hpc, best = -1;
    for (i = 0; i < EC_NG; i++) {                     /* a free one, else the oldest */
        if (!g[i].len)
            break;
        if (g[i].age > best)
            best = g[i].age, old = i;
    }
    if (i == EC_NG)
        i = old;
    p16 = m->pitch16 + p[P_E5] * 16 * (int32_t)r;
    if (p[P_E7] && r)                                 /* SCAT: up to +-50 cents */
        p16 += (((int32_t)(ec_rand(v) >> 24) - 128) * p[P_E7]) >> 10;
    g[i].src = (uint8_t)((uint32_t)p[P_E0] % 5u);
    g[i].inc = pitch_inc((uint32_t)clamp(p16, 0, 2047));
    g[i].ph = g[i].mph = 0;
    g[i].age = -at;
    g[i].len = (int32_t)((uint32_t)FS / 1000u * EC_LEN_MS[g[i].src]);
    g[i].inv = (1 << 29) / g[i].len;
    g[i].amp = amp;
    lpc = 32767 + clamp(tone, -2000, 0) * 15;          /* TONE down: each darker */
    hpc = clamp(tone, 0, 2000) * 15;                   /* TONE up: each thinner */
    g[i].lpk = clamp(lpc, 1500, 32767);
    g[i].hpk = clamp(hpc, 0, 30000);
    g[i].lp = g[i].hp = 0;
    g[i].flt = 32767;
}

static void echoes_note_on(track_t *t, voice_t *v)
{
    ec_g_t *g = ec_of(t, v);
    uint32_t i;
    if (g)
        for (i = 0; i < EC_NG; i++)
            g[i].len = 0;
    v->s[0] = 0;                                      /* the note itself at once */
    v->s[1] = 0;
    v->s[2] = 4096 + v->vel * 225;                    /* (the velocity: the level, Q15) */
    v->s[3] = (int32_t)(0x2545F491u ^ (v->note * 2654435761u));   /* RANDOM, SCAT: the same each time a note is played */
}

static int echoes_done(track_t *t, voice_t *v)
{
    ec_g_t *g = ec_of(t, v);
    uint32_t i;
    if (!g)
        return 1;
    if (v->s[1] <= t->p[P_E3] && v->s[2] > 200)       /* repeats to come */
        return 0;
    for (i = 0; i < EC_NG; i++)
        if (g[i].len)
            return 0;
    return 1;
}

static void echoes_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    ec_g_t *g = ec_of(t, v);
    const int16_t *p = t->p;
    int32_t buf[CTL], keep = 13000 + p[P_E4] * 155;   /* DECAY: each keeps 0.4 .. 1 of the one before */
    uint32_t i, j, nz = (uint32_t)v->s[3] | 1u;
    if (!g || n > CTL)
        return;
    while (v->s[0] < (int32_t)n && v->s[1] <= p[P_E3] && v->s[2] > 200) {   /* the repeats due in this block */
        ec_spawn(t, v, g, m, v->s[0], (uint32_t)v->s[1], v->s[2]);
        v->s[0] += ec_gap(p, v, (uint32_t)v->s[1]);
        v->s[1]++;
        v->s[2] = (v->s[2] * keep) >> 15;
    }
    v->s[0] -= (int32_t)n;
    for (i = 0; i < n; i++)
        buf[i] = 0;
    for (j = 0; j < EC_NG; j++) {
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
            w = ((q->len - q->age) * q->inv) >> 14;  /* 1 .. 0 over its length, Q15 */
            e = (w * w) >> 15;                        /* (1 - x)^2 */
            if (q->age < 64)                          /* (a 1.5 ms start: no click) */
                e = (e * q->age) >> 6;
            switch (q->src) {
            case 0u:                                  /* BLIP: a sine, dropping a little */
                x = sine_i(q->ph);
                q->ph += q->inc - ((q->inc >> 3) * (uint32_t)(32767 - w) >> 15);
                break;
            case 1u:                                  /* PLUCK: a saw, its low-pass closing */
                q->flt += ((osc_saw(q->ph, q->inc) - q->flt) * ((w >> 1) + 800)) >> 15;
                x = q->flt;
                q->ph += q->inc;
                break;
            case 2u:                                  /* STAB: a pulse and a saw an octave up */
                x = (osc_pulse(q->ph, q->inc, 0x60000000u) + (osc_saw(q->ph << 1, q->inc << 1) >> 1)) >> 1;
                q->ph += q->inc;
                break;
            case 3u:                                  /* NOISE: a burst through a resonance at the note */
                nz ^= nz << 13;
                nz ^= nz >> 17;
                nz ^= nz << 5;
                q->flt += (((int32_t)(int16_t)(nz >> 16) - q->flt) * 6000) >> 15;
                x = (q->flt * sine_i(q->ph)) >> 13;
                q->ph += q->inc;
                break;
            default:                                  /* FM: a bell, the index falling */
                x = sine_i(q->ph + (uint32_t)(sine_i(q->mph) * (w >> 3) * 2));
                q->ph += q->inc;
                q->mph += q->inc * 7u >> 1;
                break;
            }
            q->lp += ((x - q->lp) * q->lpk) >> 15;    /* TONE: darker .. */
            a = q->lp;
            q->hp += ((a - q->hp) * q->hpk) >> 15;    /* .. or thinner */
            a -= q->hpk ? q->hp : 0;
            buf[i] += (((a * e) >> 15) * q->amp) >> 15;
        }
    }
    v->s[3] = (int32_t)nz;
    for (i = 0; i < n; i++)
        out[i] += voice_amp(soft_knee(clamp(buf[i], -65535, 65535), 22000), m, i) << 1;
}

/* {SRC, TIME, SHAPE, COUNT, DECAY, PITCH, TONE, SCAT}; SRC 0 BLIP 1 PLUCK 2 STAB 3 NOISE 4 FM; TIME 0 1/4 1 1/8. 2 1/8
 * 3 1/8T 4 1/16. 5 1/16 6 1/16T 7 1/32 8 1/64; SHAPE 0 EVEN 1 BOUNCE 2 SLOW 3 SWING 4 RANDOM */
static const preset_t ECHOES_PRESETS[] = {
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
};

static const engine_t ENG_ECHOES = {
    .name = "ECHOES",
    .id = EID_ECHOES,
    .cc = 0x57,                  /* CC74 DECAY, CC71 TONE */
    .cats = "UFFFUUFFFFFFUFFU",
    .page_title = {"SOURCE", "REPEATS"},
    .edit = {
        {"SRC", F_ENUM, 0, 4, 0, N_EC_SRC, 0},
        {"TIME", F_ENUM, 0, 8, 1, N_EC_TIME, 0},
        {"SHAPE", F_ENUM, 0, 4, 0, N_EC_SHAPE, 0},
        {"COUNT", F_INT, 1, 16, 8, 0, 0},
        {"DECAY", F_PCT, 0, 127, 95, 0, 0},
        {"PITCH", F_INT, -12, 12, 0, 0, "st"},
        {"TONE", F_BIPCT, -64, 63, 0, 0, 0},
        {"SCAT", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = ECHOES_PRESETS,
    .npresets = NELEM(ECHOES_PRESETS),
    .poly = EC_POLY,
    .note_on = echoes_note_on,
    .render = echoes_render,
    .knob = {P_E1, P_E4, P_E5, P_E6},
    .ownenv = 1,
    .done = echoes_done,
};
