/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* GLITCH (1.5): a kit of micro-sounds, the clicks and cuts of the form (Blue Hare's own). Every key from C2 is another
 * sound, made anew at each hit (as DRUM's lanes; the octave above the same sixteen, an octave up): CLICK, POP, TICK,
 * ZAP (a sweep down), BITERR (a square jumping between random pitches), DATA (noise gated by random bits), DROP (a
 * dropout: a thump, a gap, a click), FAX and MODEM (the chirps of a line), CRACKL (dust on a record), SPARK, BLIP,
 * SHARD (noise through a high resonance), BUZZ (a hum, fizzing), SKIP (a fragment repeated, a CD skipping), PING.
 * KIT colours them all: CLEAN, HARSH (driven), LOFI (crushed, dark), BROKEN (holes in every sound). PITCH moves them
 * (+-24 semitones), DECAY stretches them (a quarter .. four times), ROLL repeats the hit while the key is held at the
 * song's tempo (1/32 .. 1/128: a ratchet), CRUSH takes bits and rate, VARY makes each hit differ (pitch, length),
 * DENS scatters extra micro-hits after each one, TONE darkens (down) or thins (up). Its own envelope (engine_t.ownenv:
 * the velocity sets the level); the voice ends when its hits do.
 * State: the track's block (engines.c eng_slab, GL_POLY voices a track). */
#define GL_POLY 8
static uint32_t beat_samples(void);                  /* (fx.c: a beat at the song's tempo, or the MIDI clock's) */
static const char *const N_GL_KIT[] = {"CLEAN", "HARSH", "LOFI", "BROKEN", 0};
static const char *const N_GL_ROLL[] = {"OFF", "1/32", "1/48", "1/64", "1/96", "1/128", 0};
static const uint8_t GL_ROLL_DIV[6] = {0, 8, 12, 16, 24, 32};   /* hits a beat */
static const uint16_t GL_LEN_MS[16] = {3, 25, 4, 40, 30, 80, 60, 120, 150, 200, 15, 30, 20, 120, 80, 150};
static const uint16_t GL_HZ[16] = {0, 120, 0, 4000, 800, 2000, 60, 1100, 900, 0, 5000, 1000, 3000, 50, 400, 1200};
typedef struct {
    uint32_t ph, inc, ph2, rng;  /* the phases, the pitch's increment, the noise */
    int32_t age, len, amp;       /* samples since the hit; its length; its level (Q15) */
    int32_t next, dens;          /* samples to the next hit (ROLL, DENS; < 0: none), extra hits still to come */
    int32_t lp, hp, hold, hc;    /* TONE's filter; CRUSH's held sample and count */
    int32_t aux;                 /* a sound's own (BITERR's pitch, DATA's bit, PING's FM) */
    int32_t inv;                 /* 2^29 / len (the hit's progress without a divide a sample) */
    uint8_t snd;
} gl_voice_t;

static inline __attribute__((always_inline)) gl_voice_t *gl_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    {
        gl_voice_t *a = eng_slab(t, SLAB_GLITCH, sizeof(gl_voice_t) * GL_POLY);   /* (engines.c) */
        return a && i < GL_POLY ? &a[i] : 0;
    }
}

static uint32_t gl_rand(gl_voice_t *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

/* a hit: the sound under the voice's note, PITCH, DECAY, VARY (dv: 0 the hit, 1 a DENS micro-hit, smaller and off) */
static void gl_hit(track_t *t, voice_t *v, gl_voice_t *g, int dv)
{
    const int16_t *p = t->p;
    int32_t k = (int32_t)v->note - 36, oct, vary = p[P_E5] + (p[P_E0] == 3 ? 40 : 0), st, len, hz;
    k = ((k % 32) + 32) % 32;                         /* (two octaves of sixteen: the second an octave up) */
    oct = k >= 16;
    g->snd = (uint8_t)(k & 15);
    st = p[P_E1] * 16 + oct * 192;
    if (vary || dv)
        st += (((int32_t)(gl_rand(g) >> 24) - 128) * (vary + (dv ? 60 : 0))) >> 4;   /* VARY: up to ~+-1 octave */
    len = GL_LEN_MS[g->snd] * (int32_t)FS / 1000;
    len = (len * (16 + p[P_E2] * 2)) >> 6;            /* DECAY: a quarter .. four times */
    if (vary)
        len += (int32_t)(((int64_t)len * ((int32_t)(gl_rand(g) >> 22) - 512) * vary) >> 17);
    if (dv)
        len >>= 1;
    g->len = len < 32 ? 32 : len;
    g->inv = (1 << 29) / g->len;
    hz = GL_HZ[g->snd] ? GL_HZ[g->snd] : 1000;
    g->inc = (uint32_t)((uint64_t)hz * 97391u * (uint64_t)(sp_pow2(st) >> 4) >> 12);   /* (2^32 / FS = 97391.5) */
    g->age = 0;
    g->ph = g->ph2 = 0;
    g->aux = 0;
    g->amp = dv ? (int32_t)(gl_rand(g) >> 18) + 4096 : 4096 + v->vel * 225;
}

static void glitch_note_on(track_t *t, voice_t *v)
{
    gl_voice_t *g = gl_of(t, v);
    if (!g)
        return;
    if (!g->rng)
        g->rng = 0x9E3779B9u ^ (v->age * 2654435761u);
    gl_hit(t, v, g, 0);
    g->dens = t->p[P_E6] / 16;                        /* DENS: 0 .. 7 extra micro-hits */
    g->next = g->dens ? 200 + (int32_t)(gl_rand(g) >> 21) : -1;
    if (t->p[P_E3])                                   /* ROLL: the next at the rate */
        g->next = (int32_t)(beat_samples() / GL_ROLL_DIV[(uint32_t)t->p[P_E3] % 6u]);
    g->lp = g->hp = g->hold = g->hc = 0;
}

static int glitch_done(track_t *t, voice_t *v)
{
    gl_voice_t *g = gl_of(t, v);
    if (!g)
        return 1;
    return g->age >= g->len && !(t->p[P_E3] && v->gate) && g->dens <= 0;
}

/* sound s at time a (samples), its progress w (Q15, 1 .. 0) */
static int32_t gl_sound(gl_voice_t *g, uint32_t s, int32_t a, int32_t w)
{
    int32_t x;
    uint32_t r;
    switch (s) {
    case 0u: return a < 4 ? 32767 : a < 10 ? -26000 : 0;   /* CLICK (a few samples: the TONE filter leaves it a click) */
    case 1u:                                          /* POP: a sine falling from 3 times */
        x = sine_i(g->ph);
        g->ph += g->inc + (uint32_t)(((uint64_t)g->inc * 2u * (uint32_t)w * (uint32_t)w) >> 30);
        return x;
    case 2u: return (int32_t)(int16_t)(gl_rand(g) >> 16);   /* TICK: a burst of noise */
    case 3u:                                          /* ZAP: down from 4 kHz to ~100 Hz */
        x = sine_i(g->ph);
        g->ph += (uint32_t)(((uint64_t)g->inc * (uint32_t)w * (uint32_t)w) >> 30) + (g->inc >> 6);
        return x;
    case 4u:                                          /* BITERR: a square, its pitch jumping */
        if (!(a & 63))
            g->aux = (int32_t)(gl_rand(g) >> 20);
        g->ph += g->inc + (uint32_t)g->aux * (g->inc >> 9);
        return g->ph & 0x80000000u ? 24000 : -24000;
    case 5u:                                          /* DATA: noise gated by random bits */
        g->ph += g->inc;
        if (g->ph < g->inc)
            g->aux = (int32_t)(gl_rand(g) & 1u);
        return g->aux ? (int32_t)(int16_t)(gl_rand(g) >> 16) : 0;
    case 6u:                                          /* DROP: a thump, a gap, a click at the end */
        if (a < 600)
            return (sine_i(g->ph += g->inc) * (600 - a)) / 600;
        return w < 600 ? (w < 300 ? 26000 : -26000) : 0;
    case 7u:                                          /* FAX: two tones, 10 ms each (a x 9511 >> 22: a / 441) */
        g->ph += (a * 9511 >> 22) & 1 ? g->inc : (g->inc << 1) - (g->inc >> 3);   /* (~1.9: two tones) */
        return sine_i(g->ph);
    case 8u:                                          /* MODEM: two tones, their phases jumping */
        if (!(a & 255))
            g->ph2 += gl_rand(g);
        g->ph += g->inc;
        return (sine_i(g->ph) + sine_i(g->ph2 += g->inc + (g->inc >> 1))) >> 1;
    case 9u:                                          /* CRACKL: sparse random clicks */
        r = gl_rand(g);
        return r < 30000000u ? (int32_t)(int16_t)(r >> 8) * 2 : 0;
    case 10u:                                         /* SPARK: a high ping and a hiss */
        g->ph += g->inc;
        return (sine_i(g->ph) >> 1) + ((int32_t)(int16_t)(gl_rand(g) >> 16) >> 2);
    case 11u:                                         /* BLIP: a sine */
        g->ph += g->inc;
        return sine_i(g->ph);
    case 12u:                                         /* SHARD: noise through a high resonance */
        g->aux += (((int32_t)(int16_t)(gl_rand(g) >> 16) - g->aux) * 9000) >> 15;
        g->ph += g->inc;
        return (g->aux * sine_i(g->ph)) >> 12;
    case 13u:                                         /* BUZZ: a narrow pulse at a hum's pitch, fizzing */
        g->ph += g->inc;
        return (g->ph < 0x20000000u ? 22000 : -3000) + ((int32_t)(int16_t)(gl_rand(g) >> 16) >> 3);
    case 14u:                                         /* SKIP: 10 ms of a saw, three times, gaps between */
        if (!((a * 9511 >> 22) & 1)) {
            if (a == (a * 9511 >> 22) * 441)          /* (each fragment from its start) */
                g->ph = 0;
            g->ph += g->inc;
            return osc_saw(g->ph, g->inc);
        }
        return 0;
    default:                                          /* PING: a sine, a touch of FM */
        g->ph2 += g->inc * 3u;
        g->ph += g->inc + (uint32_t)(sine_i(g->ph2) * (w >> 6));
        return sine_i(g->ph);
    }
}

static void glitch_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    gl_voice_t *g = gl_of(t, v);
    const int16_t *p = t->p;
    uint32_t i, kit = (uint32_t)p[P_E0] & 3u;
    int32_t crush = p[P_E4] + (kit == 2u ? 50 : 0), bits = 16 - crush * 13 / 127, hold = 1 + crush / 16, tone = p[P_E7] - 64;
    int32_t mask = bits >= 16 ? -1 : ~((1 << (16 - (bits < 3 ? 3 : bits))) - 1), lpk, hpk;
    if (!g || n > CTL)
        return;
    lpk = tone < 0 ? 32767 + tone * 480 : 32767;      /* TONE: darker .. thinner */
    hpk = tone > 0 ? tone * 300 : 0;
    if (kit == 2u)
        lpk = lpk * 2 / 3;
    for (i = 0; i < n; i++) {
        int32_t x = 0, w, e;
        if (g->next >= 0 && --g->next < 0) {          /* the next hit: ROLL while held, else a DENS micro-hit */
            if (p[P_E3] && v->gate) {
                gl_hit(t, v, g, 0);
                g->next = (int32_t)(beat_samples() / GL_ROLL_DIV[(uint32_t)p[P_E3] % 6u]);
            } else if (g->dens > 0) {
                gl_hit(t, v, g, 1);
                g->dens--;
                g->next = g->dens ? 300 + (int32_t)(gl_rand(g) >> 20) : -1;
            }
        }
        if (g->age < g->len) {
            w = ((g->len - g->age) * g->inv) >> 14;   /* 1 .. 0 over the hit, Q15 */
            e = (w * w) >> 15;
            if (g->snd == 6u || g->snd == 14u)        /* (DROP, SKIP: their own shape) */
                e = 32767;
            x = gl_sound(g, g->snd, g->age, w);
            if (kit == 3u && ((gl_rand(g) >> 24) < 12u))   /* BROKEN: holes */
                x = 0;
            if (kit == 1u)                            /* HARSH: driven */
                x = softclip(x * 3);
            x = (((x * e) >> 15) * g->amp) >> 15;
            g->age++;
        }
        g->lp += ((x - g->lp) * lpk) >> 15;
        x = g->lp;
        if (hpk) {
            g->hp += ((x - g->hp) * hpk) >> 15;
            x -= g->hp;
        }
        if (crush) {                                  /* CRUSH: fewer bits, held samples */
            if (--g->hc <= 0) {
                g->hc = hold;
                g->hold = x & mask;
            }
            x = g->hold;
        }
        out[i] += voice_amp(soft_knee(clamp(x, -65535, 65535), 22000), m, i) << 1;
    }
}

/* {KIT, PITCH, DECAY, ROLL, CRUSH, VARY, DENS, TONE}; KIT 0 CLEAN 1 HARSH 2 LOFI 3 BROKEN; ROLL 0 OFF 1 1/32 2 1/48
 * 3 1/64 4 1/96 5 1/128. The keys from C2: CLICK POP TICK ZAP BITERR DATA DROP FAX MODEM CRACKL SPARK BLIP SHARD BUZZ
 * SKIP PING */
static const preset_t GLITCH_PRESETS[] = {
    {"CLICKS", {0, 0, 64, 0, 0, 20, 0, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
    {"MICRO CUTS", {0, 0, 40, 0, 0, 50, 30, 80}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 15, 15)},
    {"HARSH GLITCH", {1, 0, 64, 0, 30, 40, 0, 70}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 15)},
    {"LOFI ERRORS", {2, 0, 70, 0, 90, 40, 10, 40}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 15, 15)},
    {"BROKEN DATA", {3, 0, 64, 0, 60, 90, 40, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 20, 20)},
    {"RATCHET", {0, 0, 50, 3, 0, 30, 0, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
    {"ROLL 128", {1, 0, 40, 5, 20, 30, 0, 70}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 10)},
    {"DUST", {0, 0, 90, 0, 0, 80, 90, 50}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 20, 30)},
    {"PITCHED DOWN", {0, -12, 100, 0, 20, 30, 0, 50}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 20)},
    {"HIGH SPARKS", {0, 12, 40, 0, 0, 40, 20, 90}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 20, 30)},
    {"BIT ROT", {3, -5, 80, 1, 110, 100, 60, 40}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 20, 20)},
};

static const engine_t ENG_GLITCH = {
    .name = "GLITCH",
    .id = EID_GLITCH,
    .cc = 0x35,                  /* CC74 DECAY, CC71 CRUSH */
    .cats = "DDDDDDDDDDD",
    .page_title = {"KIT", "MANGLE"},
    .edit = {
        {"KIT", F_ENUM, 0, 3, 0, N_GL_KIT, 0},
        {"PITCH", F_INT, -24, 24, 0, 0, "st"},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"ROLL", F_ENUM, 0, 5, 0, N_GL_ROLL, 0},
        {"CRUSH", F_PCT, 0, 127, 0, 0, 0},
        {"VARY", F_PCT, 0, 127, 20, 0, 0},
        {"DENS", F_PCT, 0, 127, 0, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = GLITCH_PRESETS,
    .npresets = NELEM(GLITCH_PRESETS),
    .poly = GL_POLY,
    .note_on = glitch_note_on,
    .render = glitch_render,
    .keys = drum_keys,
    .knob = {P_E2, P_E4, P_E5, P_E6},
    .ownenv = 1,
    .done = glitch_done,
};
