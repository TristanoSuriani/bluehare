/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SLAG (1.5, FOUNDRY): molten walls and drones (Fel-Tracker's own). OSCS oscillators (2 .. 4, WAVE SAW or PULSE) each
 * wander in tune on its own (DRIFT: a slow random walk, a hot circuit's), into a saturation HEAT hard (from warm to a
 * wall of distortion), a resonant low-pass (CUT, RES) that MELT lets sink over each held note (the metal cooling) and
 * a crackle of NOISE (sparks off the pour).
 * Voice state: ph[0 .. 2], s[0] the oscillators' phases; s[1 .. 4] their drift (1/256 cent); s[5] s[6] the filter;
 * s[7] MELT (Q15, 1 at the note). */
static const char *const N_SLAG_WAVE[] = {"SAW", "PULSE", 0};

static void slag_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;
    v->ph[1] = 0x55555555u;
    v->ph[2] = 0xAAAAAAAAu;
    v->s[0] = 0x2AAAAAAA;
    v->s[7] = 32767;
}

static void slag_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t no = (uint32_t)clamp(p[P_E0], 2, 4), pulse = p[P_E1] != 0, inc[4], ph[4], i, k, h;
    int32_t heat = 256 + p[P_E3] * 24, melt = v->s[7], ic1 = v->s[5], ic2 = v->s[6], crack = p[P_E7], cut;
    tsvf_t flt;
    for (k = 0; k < no; k++) {                        /* DRIFT: each its own random walk, pulled back */
        h = (v->age + k * 7919u + (uint32_t)v->s[1 + k]) * 2654435761u;
        v->s[1 + k] += (((int32_t)(h >> 22) - 512) * p[P_E2]) >> 6;
        v->s[1 + k] -= v->s[1 + k] >> 7;
        inc[k] = cents_inc(m->pitch16, ((int32_t)k - (int32_t)no / 2) * 6 + (v->s[1 + k] >> 8), m->fine);
        ph[k] = k < 3u ? v->ph[k] : (uint32_t)v->s[0];
    }
    melt -= (melt * (1 + p[P_E6] / 8)) >> 12;         /* MELT: the cutoff sinks over the held note */
    cut = (p[P_E4] << 8) + m->cutoff - (((32767 - melt) * p[P_E6] * 4) >> 13) + (m->pitch16 - 60 * 16) * 4;
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t s = 0, y;
        for (k = 0; k < no; k++) {
            s += pulse ? osc_pulse(ph[k], inc[k], 0x60000000u) : osc_saw(ph[k], inc[k]);
            ph[k] += inc[k];
        }
        s /= (int32_t)no;
        s = softclip(((s >> 1) * heat) >> 8);         /* HEAT */
        if (crack && ((ph[0] ^ (ph[1] >> 7)) * 2246822519u >> 24) < (uint32_t)crack / 8u)   /* NOISE: sparks, now */
            s += (int32_t)(((ph[1] + i) * 2654435761u) >> 17) - 16384;                      /* and then */
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);
        out[i] += voice_amp(soft_knee(y << 1, 24000), m, i) << 1;
    }
    for (k = 0; k < no; k++) {
        if (k < 3u)
            v->ph[k] = ph[k];
        else
            v->s[0] = (int32_t)ph[k];
    }
    v->s[5] = ic1;
    v->s[6] = ic2;
    v->s[7] = melt;
}

/* {OSCS, WAVE, DRIFT, HEAT, CUT, RES, MELT, NOISE}; WAVE 0 SAW 1 PULSE */
static const preset_t SLAG_PRESETS[] = {
    {"MOLTEN WALL", {4, 0, 40, 80, 80, 20, 20, 10}, {40, 0, 127, 80}, 0, 0, FX(0, 30, 20, 50)},
    {"HOT DRONE", {3, 0, 60, 60, 60, 30, 40, 20}, {80, 0, 127, 100}, 0, 0, FX(0, 40, 20, 70)},
    {"SLAG HEAP", {4, 1, 50, 110, 70, 40, 30, 30}, {20, 0, 127, 80}, 0, 0, FX(20, 20, 20, 50)},
    {"COOLING", {3, 0, 30, 50, 100, 30, 100, 0}, {30, 0, 127, 90}, 0, 0, FX(0, 40, 30, 70)},
    {"FURNACE", {4, 0, 80, 127, 90, 50, 10, 40}, {50, 0, 127, 90}, 0, 0, FX(30, 20, 20, 60)},
    {"IRON BASS", {2, 1, 20, 70, 50, 30, 30, 0}, {0, 60, 100, 20}, 10, 1, FX(20, 0, 0, 5)},
    {"WARPED LEAD", {3, 0, 90, 70, 90, 40, 20, 0}, {5, 40, 110, 30}, 10, 1, FX(20, 10, 35, 25)},
    {"EMBER PAD", {4, 1, 40, 40, 70, 20, 50, 20}, {90, 90, 115, 100}, 0, 0, FX(0, 60, 25, 80)},
    {"CRUCIBLE", {4, 0, 70, 100, 60, 60, 60, 60}, {60, 0, 127, 100}, 0, 0, FX(20, 30, 30, 70)},
    {"SMELT STAB", {3, 1, 30, 90, 85, 40, 0, 10}, {0, 40, 0, 30}, 30, 0, FX(20, 10, 40, 30)},
    {"MAGMA", {4, 0, 60, 110, 50, 40, 40, 40}, {50, 0, 127, 90}, 0, 0, FX(20, 30, 20, 60)},
    {"FORGE BASS", {2, 0, 15, 90, 45, 40, 20, 10}, {0, 60, 100, 20}, 20, 1, FX(20, 0, 0, 5)},
    {"ASH CLOUD", {4, 1, 90, 50, 60, 20, 70, 80}, {80, 0, 127, 100}, 0, 0, FX(0, 50, 25, 80)},
    {"BLAST FURNACE", {4, 0, 100, 127, 100, 60, 10, 70}, {30, 0, 127, 90}, 0, 0, FX(40, 20, 20, 60)},
    {"LAVA LEAD", {3, 1, 50, 100, 95, 50, 30, 10}, {5, 40, 110, 30}, 10, 1, FX(20, 10, 35, 25)},
    {"IRON SLUDGE", {3, 0, 40, 80, 40, 50, 90, 20}, {0, 60, 100, 20}, 10, 1, FX(30, 0, 0, 5)},
};

static const engine_t ENG_SLAG = {
    .name = "SLAG",
    .id = EID_SLAG,
    .cc = 0x46,                  /* CC74 HEAT .. CUT, CC71 RES */
    .cats = "PPPPPBLPPKPBPPLB",
    .page_title = {"HEAT", "MELT"},
    .edit = {
        {"OSCS", F_INT, 2, 4, 3, 0, 0},
        {"WAVE", F_ENUM, 0, 1, 0, N_SLAG_WAVE, 0},
        {"DRIFT", F_PCT, 0, 127, 40, 0, 0},
        {"HEAT", F_PCT, 0, 127, 60, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"MELT", F_PCT, 0, 127, 30, 0, 0},
        {"NOISE", F_PCT, 0, 127, 10, 0, 0},
    },
    .presets = SLAG_PRESETS,
    .npresets = NELEM(SLAG_PRESETS),
    .poly = 6,
    .note_on = slag_note_on,
    .render = slag_render,
    .knob = {P_E3, P_E4, P_E6, P_E2},
    .keep = 0x60,                /* the filter (a retrigger: no click) */
};
