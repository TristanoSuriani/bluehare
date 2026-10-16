/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* CHAOS (1.5): chaotic systems as oscillators (Blue Hare's own). MODEL: two flows, integrated a step a sample at the
 * note's speed (LRNZ the Lorenz attractor, ROSS Rossler's): never quite periodic, a tone that wanders; and two maps,
 * one new value each period of the note (LOGI the logistic map, HENO Henon's): pitched buzz whose waveform changes
 * every cycle. CHAOS is the system's parameter (Lorenz rho 24 .. 56, Rossler c 2 .. 18, logistic r 3.5 .. 4, Henon
 * a 1 .. 1.4): low, near order (the maps: cycles of 2, 4, 8 ..: tones), high, wild. OUT picks the variable heard
 * (the flows x .. y .. z, the maps the value or the one before); SMTH smooths it (the maps: from steps to lines, the
 * flows a low-pass); FOLD folds it (a sine folder); SINE mixes in a sine at the note (a pitch to hold on to); SEED
 * NOTE starts every note from the same place (the same attack each time), FREE from somewhere new; TONE a low-pass.
 * Fixed point: Q16 states, 64-bit products; clamped (a state flung out starts again).
 * (Blue Hare) A map's new value is a step (SMTH short of 127): band-limited, a polyBLEP over the samples either side
 * of the period's start, where between samples it fell (the maps' signal runs a sample late, so both are corrected
 * when the period starts); unsmoothed the steps aliased, a grain between the harmonics.
 * Voice state: ph[0] the period (the maps, the sine), ph[1] the DC blocker's last input, ph[2] the maps' sample not
 * yet out, s[0..2] x y z (Q16), s[3] / s[4] the maps' last two outputs (Q15), s[5] SMTH's filter, s[6] TONE's, s[7]
 * the DC blocker. */
static const char *const N_CH_MODEL[] = {"LRNZ", "ROSS", "LOGI", "HENO", 0};
static const char *const N_CH_SEED[] = {"NOTE", "FREE", 0};

static void chaos_seed(track_t *t, voice_t *v)
{
    uint32_t model = (uint32_t)t->p[P_E0] & 3u;
    int32_t jit = t->p[P_E6] ? (int32_t)((v->age * 2654435761u) >> 20) - 2048 : 0;   /* FREE: a little elsewhere */
    v->s[0] = (model >= 2u ? 19661 : 65536) + jit;    /* (0.3 for the maps, 1 for the flows) */
    v->s[1] = model == 3u ? 0 : 65536;
    v->s[2] = 65536;
}

static void chaos_note_on(track_t *t, voice_t *v)
{
    chaos_seed(t, v);
    v->ph[0] = 0xFFFFFFFFu;                           /* (the first sample: a new period) */
    v->ph[1] = v->ph[2] = 0;
    v->s[3] = v->s[4] = v->s[5] = v->s[6] = v->s[7] = 0;
}

static int32_t ch_mul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

/* a map's next value: s[0], s[1] stepped; -> its two outputs blended by OUT, Q15 */
static int32_t ch_map(track_t *t, voice_t *v, uint32_t model, int32_t chaos, int32_t out)
{
    int32_t x = v->s[0], y = v->s[1], a, b;
    if (model == 2u) {                                /* logistic: x' = r x (1 - x) */
        int32_t r = 229376 + chaos * 258;
        v->s[1] = x;
        x = ch_mul(r, ch_mul(x, 65536 - x));
        if (x <= 16 || x >= 65520)                    /* (stuck at a fixed point of the arithmetic: start again) */
            x = 19661 + (int32_t)(v->age & 1023u);
        v->s[0] = x;
        a = x - 32768;
        b = v->s[1] - 32768;
    } else {                                          /* Henon: x' = 1 - a x^2 + y, y' = 0.3 x */
        int32_t ah = 65536 + chaos * 206, nx = 65536 - ch_mul(ah, ch_mul(x, x)) + y;
        v->s[1] = ch_mul(19661, x);
        if (nx > 2 * 65536 || nx < -2 * 65536) {      /* (flung out: start again) */
            nx = 6554;
            v->s[1] = 0;
        }
        v->s[0] = nx;
        a = (int32_t)(((int64_t)nx * 25205) >> 16);   /* x / 1.3 */
        b = v->s[1] + (v->s[1] >> 2);                 /* y / 0.4 */
    }
    (void)t;
    a = clamp(a, -32767, 32767);
    b = clamp(b, -32767, 32767);
    return a + (((b - a) * out) >> 7);
}

static void chaos_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t model = (uint32_t)p[P_E0] & 3u, i;
    int32_t chaos = p[P_E1], o = p[P_E2], sm = p[P_E3], fold = p[P_E4], sn = p[P_E5], tone = p[P_E7];
    int32_t x = v->s[0], y = v->s[1], z = v->s[2], h, sl = v->s[5], lp = v->s[6], dc = v->s[7], dc1 = (int32_t)v->ph[1];
    int32_t ks = 32767 - sm * 240, kt = 1500 + tone * 245, rho = (24 << 16) + chaos * 16513, rc = (2 << 16) + chaos * 8256;
    kt = kt > 32767 ? 32767 : kt;
    if (model == 0u) {                                /* the time step a sample: the attractor's turn at the note */
        h = (int32_t)(m->inc >> 16) * 3 >> 2;
        h = h > 1311 ? 1311 : h;                      /* (Euler stays stable: at most 0.02) */
    } else {
        h = (int32_t)(m->inc >> 16) * 6;
        h = h > 3277 ? 3277 : h;
    }
    for (i = 0; i < n; i++) {
        uint32_t o0 = v->ph[0];
        int32_t c, f;
        v->ph[0] += m->inc;
        if (model == 0u) {                            /* Lorenz: sigma 10, beta 8/3 */
            int32_t dx = 10 * (y - x), dy = ch_mul(x, rho - z) - y, dz = ch_mul(x, y) - ch_mul(z, 174763);   /* (8 / 3, Q16) */
            x += ch_mul(dx, h);
            y += ch_mul(dy, h);
            z += ch_mul(dz, h);
            if (x > (60 << 16) || x < -(60 << 16) || y > (60 << 16) || y < -(60 << 16) || z > (120 << 16) ||
                z < -(10 << 16) || (x == 0 && y == 0)) {
                x = y = z = 65536;
            }
            {
                int32_t ox = ch_mul(x, 1311), oy = ch_mul(y, 1092), oz = ch_mul(z - (25 << 16), 1092);   /* /50 /60 */
                c = o < 64 ? ox + (((oy - ox) * o) >> 6) : oy + (((oz - oy) * (o - 64)) >> 6);
            }
        } else if (model == 1u) {                     /* Rossler: a = b = 0.2 */
            int32_t dx = -y - z, dy = x + ch_mul(13107, y), dz = 13107 + ch_mul(z, x - rc);
            x += ch_mul(dx, h);
            y += ch_mul(dy, h);
            z += ch_mul(dz, h);
            if (x > (40 << 16) || x < -(40 << 16) || y > (40 << 16) || y < -(40 << 16) || z > (80 << 16) ||
                z < -(10 << 16)) {
                x = y = z = 65536;
            }
            {
                int32_t ox = ch_mul(x, 2731), oy = ch_mul(y, 2731), oz = (z - (2 << 16)) >> 5;   /* /24 /32 */
                c = o < 64 ? ox + (((oy - ox) * o) >> 6) : oy + (((oz - oy) * (o - 64)) >> 6);
            }
        } else {                                      /* the maps: a new value a period, SMTH from steps to lines */
            int32_t late = (int32_t)v->ph[2], bl = 0;
            if (v->ph[0] < o0) {
                int32_t h;
                uint32_t tq, u;
                v->s[4] = v->s[3];
                v->s[0] = x;
                v->s[1] = y;
                v->s[3] = ch_map(t, v, model, chaos, o * 2 > 127 ? 127 : o * 2);
                x = v->s[0];
                y = v->s[1];
                /* the step: the old value (the line's end) to the line's start, (old - new) (1 - SMTH); t the time
                 * since it (Q8): the sample after raised by h / 2 (1 - t)^2, the one before lowered by h / 2 t^2 */
                h = ((v->s[4] - v->s[3]) * (128 - sm)) >> 7;
                tq = (v->ph[0] >> 8 << 8) / (m->inc >> 8 | 1u);
                tq = tq > 256u ? 256u : tq;
                u = 256u - tq;
                bl = (h * (int32_t)(u * u >> 2)) >> 15;
                late -= (h * (int32_t)(tq * tq >> 2)) >> 15;
            }
            {
                int32_t lin = v->s[4] + (int32_t)(((int64_t)(v->s[3] - v->s[4]) * (v->ph[0] >> 16)) >> 16);
                v->ph[2] = (uint32_t)(v->s[3] + (((lin - v->s[3]) * sm) >> 7) + bl);
                c = late;                             /* (a sample late: the polyBLEP's before-sample) */
            }
        }
        c = clamp(c, -32767, 32767);
        if (model < 2u) {                             /* the flows: SMTH a low-pass */
            sl += ((c - sl) * ks) >> 15;
            c = sl;
        }
        if (fold)                                     /* FOLD: a sine folder (16: x = 1 a quarter turn) */
            c = sine_i((uint32_t)(c * (16 + fold)) << 11);
        c = (c * (128 - sn) + sine_i(v->ph[0]) * sn) >> 7;   /* SINE */
        f = c - dc1 + dc - (dc >> 9);                 /* (the DC blocked) */
        dc1 = c;
        dc = f;
        lp += ((f - lp) * kt) >> 15;
        out[i] += voice_amp(clamp(lp, -32767, 32767), m, i) << 1;
    }
    if (model < 2u) {
        v->s[0] = x;
        v->s[1] = y;
        v->s[2] = z;
    }
    v->s[5] = sl;
    v->s[6] = lp;
    v->s[7] = dc;
    v->ph[1] = (uint32_t)dc1;
}

/* {MODEL, CHAOS, OUT, SMTH, FOLD, SINE, SEED, TONE}. MODEL 0 LRNZ 1 ROSS 2 LOGI 3 HENO; SEED 0 NOTE 1 FREE */
static const preset_t CHAOS_PRESETS[] = {
    {"BUTTERFLY", {0, 20, 0, 20, 0, 0, 0, 100}, {10, 0, 127, 50}, 0, 0, FX(0, 20, 20, 40)},
    {"STRANGE BASS", {0, 40, 20, 40, 0, 50, 0, 60}, {0, 60, 90, 20}, 0, 1, FX(30, 0, 0, 5)},
    {"LORENZ DRONE", {0, 70, 100, 30, 20, 20, 1, 90}, {60, 0, 127, 90}, 0, 0, FX(0, 60, 30, 70)},
    {"ROSSLER LEAD", {1, 50, 10, 10, 0, 30, 0, 110}, {0, 0, 127, 30}, 0, 1, FX(10, 0, 30, 20)},
    {"ROSSLER SPIKE", {1, 110, 127, 0, 0, 0, 0, 120}, {0, 40, 60, 20}, 0, 0, FX(20, 0, 20, 10)},
    {"BIFURCATE", {2, 30, 0, 0, 0, 0, 0, 110}, {0, 0, 127, 30}, 0, 0, FX(0, 0, 30, 20)},
    {"LOGISTIC BUZZ", {2, 110, 30, 40, 0, 20, 0, 100}, {0, 0, 127, 30}, 0, 1, FX(20, 0, 20, 10)},
    {"EDGE OF CHAOS", {2, 90, 0, 90, 10, 40, 0, 90}, {20, 0, 127, 60}, 0, 0, FX(0, 40, 30, 50)},
    {"HENON BITS", {3, 100, 0, 0, 0, 0, 1, 127}, {0, 50, 60, 20}, 0, 0, FX(0, 0, 30, 10)},
    {"HENON GROWL", {3, 70, 60, 60, 40, 40, 0, 70}, {0, 0, 127, 20}, 0, 1, FX(50, 0, 0, 5)},
    {"FOLDED CHAOS", {0, 50, 40, 20, 90, 20, 0, 100}, {0, 0, 127, 40}, 0, 0, FX(20, 20, 20, 30)},
    {"CHAOS PAD", {1, 80, 60, 60, 30, 40, 1, 70}, {80, 0, 127, 100}, 0, 0, FX(0, 80, 30, 80)},
};

static const engine_t ENG_CHAOS = {
    .name = "CHAOS",
    .id = EID_CHAOS,
    .cc = 0x25,                  /* CC74 CHAOS, CC71 FOLD */
    .cats = "LBPLFLLPFBLP",
    .page_title = {"SYSTEM", "SHAPE"},
    .edit = {
        {"MODEL", F_ENUM, 0, 3, 0, N_CH_MODEL, 0},
        {"CHAOS", F_PCT, 0, 127, 50, 0, 0},
        {"OUT", F_PCT, 0, 127, 0, 0, 0},
        {"SMTH", F_PCT, 0, 127, 20, 0, 0},
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},
        {"SINE", F_PCT, 0, 127, 0, 0, 0},
        {"SEED", F_ENUM, 0, 1, 0, N_CH_SEED, 0},
        {"TONE", F_PCT, 0, 127, 110, 0, 0},
    },
    .presets = CHAOS_PRESETS,
    .npresets = NELEM(CHAOS_PRESETS),
    .note_on = chaos_note_on,
    .render = chaos_render,
    .knob = {P_E1, P_E2, P_E4, P_E5},
};
