/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SCREAM (1.5, FOUNDRY): a voice breaking under terror (Fel-Tracker's own). Not a whistle: what makes a scream frighten
 * is a voice failing, so a buzzing glottal pulse at the key (a band-limited saw, a narrow pulse in it) is made unstable
 * period by period. ROUGH: its loudness torn at 30 .. 150 Hz (the "roughness" band screams and alarms live in and
 * speech avoids; its rate wanders); CHAOS: each period's length and level jittered, from a strained voice to a chaotic
 * one; BREAK: the voice snapping, at random, for 20 .. 200 ms: a crack an octave or a fifth up, a fall into the half or
 * the third (period doubling: every other period weak), a burst of chaos. STRAIN is the effort, building while the key
 * is held (and across legato notes; the velocity pushes it): the pitch rises (up to 5 semitones), and the roughness,
 * the breaks, the chaos and the drive grow with it. Through two open-mouth formants (VOWEL: A, AE, E, I, shrill at the
 * top) and SHRILL, a narrow peak at 2.4 .. 3.9 kHz (where hearing is the most sensitive); RASP, breath and fry noise let
 * through while the folds are open (it tears with the voice); CROWD 1 .. 3 voices, each with its own jitter and breaks,
 * out of tune and out of step (a panicking crowd). A lopsided hard clip last. Mono (one throat a track).
 * State: the track's block (engines.c eng_slab); the voice's own state unused. */
#define SC_NC 3
typedef struct {
    uint32_t ph[SC_NC], inc[SC_NC];
    int32_t jit[SC_NC], amp[SC_NC], brk[SC_NC], mode[SC_NC], cnt[SC_NC];
    uint32_t rph, nz;
    int32_t rr8, strain;         /* the roughness's rate (Hz, Q8), the effort (Q24) */
    int32_t a1, a2, b1, b2, c1, c2, dx, dy;   /* the formants, SHRILL, the DC blocker */
} sc_t;
static const int16_t SC_VOW[4][2] = {{1000, 1500}, {850, 1900}, {600, 2300}, {420, 2800}};   /* A AE E I (Hz) */
static const int32_t SC_DET[SC_NC] = {65536, 66871, 63670};   /* the crowd: in tune, +35, -50 cents (Q16) */
static const uint16_t SC_LVL[SC_NC + 1] = {0, 32767, 23170, 18919};
enum { SC_NONE, SC_CRACK, SC_FIFTH, SC_HALF, SC_THIRD, SC_CHAOS };

static inline __attribute__((always_inline)) sc_t *sc_of(track_t *t) { return eng_slab(t, SLAB_SCREAM, sizeof(sc_t)); }   /* (engines.c) */

static inline uint32_t sc_rnd(sc_t *c)
{
    uint32_t r = c->nz;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return c->nz = r;
}

static void scream_note_on(track_t *t, voice_t *v)
{
    sc_t *c = sc_of(t);
    uint32_t k;
    if (!c)
        return;
    if (!v->env && !v->env_out) {                     /* (from silence: the throat at rest, already tight) */
        memset(c, 0, sizeof *c);
        c->nz = 0x2545F491u ^ (v->age * 2654435761u);
        for (k = 0; k < SC_NC; k++) {
            c->ph[k] = k * 0x55555555u;
            c->amp[k] = 32767;
        }
        c->rr8 = 70 << 8;
        c->strain = (t->p[P_E0] * 258 * (80 + v->vel) / 207) << 9;
        c->strain /= 3;
    }
}

/* a formant's coefficients at hz, its damping k (Q12) */
static void sc_form(tsvf_t *f, int32_t hz, int32_t k)
{
    int32_t i = 0;
    while (i < 126 && CUTOFF_HZ[i + 1] <= (uint32_t)hz)
        i++;                                          /* (the cutoff table's index under hz: once a block) */
    tsvf_coef_k(f, i << 8, k);
}

/* the TPT SVF's band-pass (tsvf), a sample */
static inline int32_t sc_bp(const tsvf_t *f, int32_t x, int32_t *s1, int32_t *s2)
{
    int32_t v3 = x - *s2, v1 = (f->a1 * *s1 + f->a2 * v3) >> 13, v2 = *s2 + ((f->a2 * *s1 + f->a3 * v3) >> 13);
    *s1 = clamp(2 * v1 - *s1, -150000, 150000);
    *s2 = clamp(2 * v2 - *s2, -150000, 150000);
    return v1;
}

/* a new period of crowd voice k: its jitter, its level, maybe a break; its increment from base */
static void sc_period(sc_t *c, const int16_t *p, uint32_t k, uint32_t base, int32_t st)
{
    uint32_t r = sc_rnd(c);
    int32_t cj = 200 + p[P_E3] * p[P_E3] / 4 + (st >> 4), mul = 65536, f;
    int32_t pb = (p[P_E2] * p[P_E2] * (st + 6000)) >> 20;   /* BREAK: ~7 a second at the top, at 800 Hz */
    if (c->brk[k] <= 0) {
        c->mode[k] = SC_NONE;
        if ((int32_t)(r & 0xFFFFu) < pb) {
            uint32_t q = sc_rnd(c);
            c->mode[k] = 1 + (int32_t)(q % 5u);
            c->brk[k] = 900 + (int32_t)((q >> 8) % 8000u);   /* 20 .. 200 ms */
        }
    }
    c->cnt[k]++;
    switch (c->mode[k]) {
    case SC_CRACK: mul = 131072; break;
    case SC_FIFTH: mul = 98304; break;
    case SC_CHAOS: cj = cj * 4 > 20000 ? 20000 : cj * 4; break;
    default: break;
    }
    c->jit[k] = ((int32_t)(int16_t)(r >> 16) * cj) >> 15;   /* CHAOS: the period's length */
    c->amp[k] = 32767 - (int32_t)(((sc_rnd(c) & 0x7FFFu) * (uint32_t)(p[P_E3] * 120 + 2000)) >> 15);   /* .. its level */
    if (c->mode[k] == SC_HALF || c->mode[k] == SC_THIRD) {   /* period doubling (tripling): the weak periods */
        int32_t d = c->mode[k] == SC_HALF ? 2 : 3;
        if (c->cnt[k] % d)
            c->amp[k] = (c->amp[k] * 10000) >> 15;
        c->jit[k] += c->cnt[k] & 1 ? 1500 : -1500;
    }
    f = (int32_t)(((int64_t)mul * (65536 + c->jit[k])) >> 16);
    f = (int32_t)(((int64_t)f * SC_DET[k]) >> 16);
    c->inc[k] = (uint32_t)(((uint64_t)base * (uint32_t)f) >> 16);
    if (c->inc[k] > 0x20000000u)
        c->inc[k] = 0x20000000u;
}

static void scream_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    sc_t *c = sc_of(t);
    const int16_t *p = t->p;
    uint32_t i, k, nv = (uint32_t)clamp(p[P_E7], 1, SC_NC), base, rinc;
    int32_t tgt, st, rd, vw, vf, f1, f2, dr, rasp, lvl;
    tsvf_t fa, fb, fs;
    if (!c)
        return;
    tgt = (p[P_E0] * 258 * (80 + v->vel) / 207) << 9;   /* STRAIN: the effort it builds to (~1.5 s) */
    tgt = tgt > (32767 << 9) ? 32767 << 9 : tgt;
    c->strain += (tgt - c->strain) >> 11;
    st = c->strain >> 9;
    base = cents_inc(clamp(m->pitch16 + ((st * 80) >> 15), 0, 2047), 0, m->fine);   /* (up to 5 semitones up) */
    c->rr8 += ((int32_t)(sc_rnd(c) >> 24) - 128) * (1 + p[P_E3] / 16);   /* ROUGH's rate wanders, 40 .. 140 Hz */
    c->rr8 = clamp(c->rr8, 40 << 8, 140 << 8);
    rinc = (uint32_t)c->rr8 * 380u;
    rd = clamp(p[P_E1] * (180 + (st >> 8)), 0, 30000);
    vw = p[P_E4] * 3 / 128;                           /* VOWEL: between A AE E I */
    vf = p[P_E4] * 3 % 128;
    f1 = SC_VOW[vw][0] + (((SC_VOW[vw + 1 < 4 ? vw + 1 : 3][0] - SC_VOW[vw][0]) * vf) >> 7) + ((st * 200) >> 15);
    f2 = SC_VOW[vw][1] + (((SC_VOW[vw + 1 < 4 ? vw + 1 : 3][1] - SC_VOW[vw][1]) * vf) >> 7);
    sc_form(&fa, f1, 2400);
    sc_form(&fb, f2, 2000);
    sc_form(&fs, 2400 + p[P_E5] * 12, 1600 - p[P_E5] * 8);   /* SHRILL: higher and narrower */
    dr = 560 + (st >> 6);                             /* the drive: x 2.2 .. x 4.2 with the effort */
    rasp = p[P_E6];
    lvl = SC_LVL[nv];
    for (i = 0; i < n; i++) {
        int32_t x = 0, y, g, nzv, open = 0;
        for (k = 0; k < nv; k++) {
            int32_t s;
            if (c->ph[k] + c->inc[k] < c->ph[k] || !c->inc[k])
                sc_period(c, p, k, base, st);
            s = (osc_saw(c->ph[k], c->inc[k]) * 3 + osc_pulse(c->ph[k], c->inc[k], 0x50000000u)) >> 2;
            x += (s * c->amp[k]) >> 15;
            open |= c->ph[k] < 0x60000000u;
            c->ph[k] += c->inc[k];
            c->brk[k]--;
        }
        x = (x * lvl) >> 15;
        nzv = (int32_t)(int16_t)(sc_rnd(c) >> 16);
        if (rasp && open)                             /* RASP: through the open folds */
            x += (((nzv * rasp) >> 7) * (16384 + (st >> 1))) >> 15;
        g = 32767 - ((rd * (32767 + sine_i(c->rph))) >> 16);   /* ROUGH */
        c->rph += rinc;
        x = (x * g) >> 15;
        y = sc_bp(&fa, x, &c->a1, &c->a2) + ((sc_bp(&fb, x, &c->b1, &c->b2) * 3) >> 2) + (x >> 2);
        y += (sc_bp(&fs, x, &c->c1, &c->c2) * p[P_E5]) >> 6;
        y = (y * dr) >> 8;                            /* the clip: lopsided, hard */
        if (y > 24000)
            y = 24000 + ((y - 24000) >> 3);
        y = clamp(y, -18000, 28000);
        x = y - c->dx + c->dy - (c->dy >> 9);         /* (the DC the clip leaves) */
        c->dx = y;
        c->dy = x;
        out[i] += voice_amp(clamp(x, -32767, 32767), m, i) << 1;
    }
}

/* {STRAIN, ROUGH, BREAK, CHAOS, VOWEL, SHRILL, RASP, CROWD} */
static const preset_t SCREAM_PRESETS[] = {
    {"TERROR", {100, 90, 70, 60, 20, 80, 40, 1}, {0, 0, 127, 40}, 0, 1, FX(20, 0, 20, 40)},
    {"PANIC", {110, 110, 90, 80, 40, 90, 50, 1}, {0, 0, 127, 30}, 0, 1, FX(30, 0, 25, 35)},
    {"AGONY", {127, 100, 60, 90, 10, 60, 90, 1}, {0, 0, 127, 60}, 0, 1, FX(30, 0, 20, 50)},
    {"VOICE CRACK", {90, 40, 127, 40, 30, 70, 20, 1}, {0, 0, 127, 40}, 0, 1, FX(10, 0, 25, 35)},
    {"NIGHT TERROR", {80, 127, 50, 70, 60, 110, 30, 1}, {0, 0, 127, 80}, 0, 1, FX(10, 20, 30, 80)},
    {"MOB PANIC", {110, 100, 80, 80, 40, 80, 50, 3}, {0, 0, 127, 50}, 0, 1, FX(20, 0, 20, 50)},
    {"BREAKDOWN", {127, 127, 127, 127, 20, 100, 70, 1}, {0, 0, 127, 40}, 0, 1, FX(50, 0, 20, 40)},
    {"SIREN CRY", {70, 60, 20, 30, 50, 127, 10, 1}, {0, 0, 127, 50}, 0, 1, FX(10, 0, 30, 50)},
    {"SHRIEK", {100, 80, 40, 50, 100, 127, 20, 1}, {0, 0, 127, 30}, 0, 1, FX(20, 0, 25, 40)},
    {"TORMENT", {120, 90, 90, 110, 0, 70, 110, 2}, {0, 0, 127, 60}, 0, 1, FX(40, 0, 20, 50)},
    {"RAW THROAT", {90, 70, 40, 60, 10, 40, 127, 1}, {0, 0, 127, 30}, 0, 1, FX(40, 0, 15, 25)},
    {"HYSTERIA", {127, 110, 110, 100, 70, 110, 40, 2}, {0, 0, 127, 40}, 0, 1, FX(30, 0, 30, 45)},
    {"SCREAM LEAD", {50, 30, 10, 20, 30, 60, 10, 1}, {0, 30, 110, 30}, 0, 1, FX(20, 10, 35, 30)},
    {"SCREAM STAB", {100, 90, 60, 60, 20, 80, 40, 1}, {0, 55, 0, 25}, 0, 1, FX(30, 0, 35, 30)},
    {"CROWD TERROR", {120, 110, 90, 90, 30, 90, 60, 3}, {0, 0, 127, 60}, 0, 1, FX(30, 10, 20, 60)},
    {"LAST BREATH", {60, 100, 60, 100, 0, 30, 127, 1}, {0, 0, 127, 90}, 0, 1, FX(20, 0, 20, 70)},
    {"FRENZY", {127, 120, 100, 110, 50, 100, 40, 1}, {0, 0, 127, 40}, 0, 1, FX(30, 0, 25, 40)},
    {"DREAD", {60, 110, 40, 60, 10, 70, 60, 1}, {20, 0, 127, 80}, 0, 1, FX(10, 20, 30, 80)},
    {"WAIL", {70, 50, 30, 40, 40, 90, 20, 1}, {5, 0, 127, 70}, 0, 1, FX(10, 10, 30, 60)},
    {"MASSACRE", {127, 127, 110, 120, 30, 100, 80, 3}, {0, 0, 127, 60}, 0, 1, FX(50, 0, 20, 60)},
    {"PIERCING", {110, 70, 50, 50, 110, 127, 10, 1}, {0, 0, 127, 30}, 0, 1, FX(30, 0, 25, 40)},
    {"STRANGLED", {120, 90, 100, 90, 0, 40, 127, 1}, {0, 0, 127, 30}, 0, 1, FX(40, 0, 15, 30)},
    {"ALARM VOICE", {90, 127, 20, 30, 60, 120, 10, 2}, {0, 0, 127, 40}, 0, 1, FX(30, 0, 25, 40)},
    {"SCREAM BASS", {80, 80, 60, 60, 0, 20, 60, 1}, {0, 40, 110, 20}, 0, 1, FX(40, 0, 0, 10), .oct = -1},
};

static const engine_t ENG_SCREAM = {
    .name = "SCREAM",
    .id = EID_SCREAM,
    .cc = 0x12,                  /* CC74 ROUGH, CC71 BREAK */
    .cats = "LLLLFLLLLLLLLKLFLFLLLLLB",
    .page_title = {"VOICE", "THROAT"},
    .edit = {
        {"STRAIN", F_PCT, 0, 127, 100, 0, 0},
        {"ROUGH", F_PCT, 0, 127, 90, 0, 0},
        {"BREAK", F_PCT, 0, 127, 70, 0, 0},
        {"CHAOS", F_PCT, 0, 127, 60, 0, 0},
        {"VOWEL", F_PCT, 0, 127, 20, 0, 0},
        {"SHRILL", F_PCT, 0, 127, 80, 0, 0},
        {"RASP", F_PCT, 0, 127, 40, 0, 0},
        {"CROWD", F_INT, 1, SC_NC, 1, 0, 0},
    },
    .presets = SCREAM_PRESETS,
    .npresets = NELEM(SCREAM_PRESETS),
    .poly = 1,
    .note_on = scream_note_on,
    .render = scream_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},
};
