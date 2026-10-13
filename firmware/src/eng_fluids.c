/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* FLUIDS (1.5, FOUNDRY): liquid, viscous sound (Fel-Tracker's own). WAVE morphs the oscillator from a sine (0) through
 * a saw (64) to a square (127); then STAGES (2 .. 8) first-order all-passes in a chain, all at DISP: the wave's
 * harmonics smeared in time, half of it mixed back with what went in (its notches: the liquid sheen), MOTION an LFO
 * (~0.05 .. ~8 Hz) moving DISP by DEPTH (the flow); a resonant low-pass (CUT, RES, the key half tracked, ENV ->
 * FILTER) and SUB a sine an octave down last.
 * Voice state: ph[0] the oscillator, ph[1] MOTION's, ph[2] SUB's, s[0] s[1] the filter; the all-passes fl_ap[track]
 * [voice][] (in the pool, FL_POLY voices a track). */
#define FL_POLY 6
#define FL_NAP 8
static int32_t fl_ap[NPART][FL_POLY][FL_NAP] __attribute__((section(".pool")));

static int32_t *fl_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < FL_POLY ? fl_ap[t - trk][i] : 0;
}

static void fluids_note_on(track_t *t, voice_t *v)
{
    int32_t *a = fl_of(t, v);
    if (a && !v->env && !v->env_out)                  /* (a voice not sounding: the chain from rest) */
        memset(a, 0, FL_NAP * sizeof *a);
    v->ph[0] = v->ph[2] = 0;
}

static void fluids_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int32_t *ap = fl_of(t, v);
    const int16_t *p = t->p;
    uint32_t i, k, ph = v->ph[0], sph = v->ph[2], inc = m->inc, ns = (uint32_t)clamp(p[P_E2], 2, FL_NAP);
    int32_t wm = p[P_E0], c, sub = p[P_E7] * 200, ic1 = v->s[0], ic2 = v->s[1];
    tsvf_t flt;
    if (!ap)
        return;
    v->ph[1] += (uint32_t)(4000u + (uint32_t)p[P_E4] * p[P_E4] * 45u) * 32u;   /* MOTION: ~0.05 .. ~8 Hz */
    c = clamp(-26000 + p[P_E1] * 400 + ((sine_i(v->ph[1]) * p[P_E5]) >> 7) * 2, -31000, 31000);   /* DISP + DEPTH */
    tsvf_coef(&flt, (p[P_E3] << 8) + m->cutoff + (m->pitch16 - 60 * 16) * 8, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t x, y;
        if (wm < 64)                                  /* WAVE: sine .. saw .. square */
            x = sine_i(ph) + (((osc_saw(ph, inc) - sine_i(ph)) * wm) >> 6);
        else
            x = osc_saw(ph, inc) + (((osc_pulse(ph, inc, 0x80000000u) - osc_saw(ph, inc)) * (wm - 64)) >> 6);
        x >>= 1;
        y = x;
        for (k = 0; k < ns; k++) {                    /* the all-passes: y = c x + s, s = x - c y */
            int32_t o = ((c * y) >> 15) + ap[k];
            ap[k] = clamp(y - ((c * o) >> 15), -65535, 65535);
            y = o;
        }
        y = (x + y) >> 1;
        y = tsvf_lp(&flt, y, &ic1, &ic2);
        if (sub)
            y += (sine_i(sph) * sub) >> 16;
        out[i] += voice_amp(soft_knee(clamp(y << 1, -98304, 98304), 24000), m, i) << 1;
        ph += inc;
        sph += inc >> 1;
    }
    v->ph[0] = ph;
    v->ph[2] = sph;
    v->s[0] = ic1;
    v->s[1] = ic2;
}

/* {WAVE, DISP, STAGES, CUT, MOTION, DEPTH, RES, SUB} */
static const preset_t FLUIDS_PRESETS[] = {
    {"LIQUID BASS", {64, 60, 6, 50, 30, 40, 40, 60}, {0, 50, 100, 20}, 20, 1, FX(10, 0, 0, 5)},
    {"VISCOUS", {90, 80, 8, 60, 20, 60, 30, 40}, {0, 60, 110, 25}, 10, 1, FX(10, 10, 10, 10)},
    {"MERCURY", {40, 50, 8, 80, 50, 80, 20, 0}, {30, 0, 127, 70}, 0, 0, FX(0, 50, 25, 60)},
    {"OIL PAD", {64, 70, 6, 70, 15, 90, 20, 30}, {90, 90, 115, 100}, 0, 0, FX(0, 70, 25, 80)},
    {"WATER LEAD", {30, 40, 4, 90, 60, 50, 30, 0}, {5, 40, 110, 30}, 10, 1, FX(0, 20, 35, 30)},
    {"MOLASSES", {100, 90, 8, 40, 8, 100, 50, 80}, {40, 0, 127, 80}, 0, 1, FX(10, 20, 10, 40)},
    {"DRIP BASS", {64, 30, 5, 45, 90, 70, 60, 50}, {0, 40, 60, 15}, 30, 1, FX(10, 0, 10, 10)},
    {"FLOW", {50, 60, 7, 75, 25, 110, 25, 20}, {80, 90, 120, 100}, 0, 0, FX(0, 70, 30, 85)},
    {"GEL KEYS", {80, 50, 4, 70, 40, 40, 30, 0}, {0, 60, 50, 40}, 20, 0, FX(0, 30, 30, 40)},
    {"SLUDGE", {127, 100, 8, 35, 12, 80, 70, 100}, {0, 40, 110, 20}, 20, 1, FX(30, 0, 10, 10)},
    {"PLASMA", {90, 90, 8, 70, 70, 100, 40, 30}, {5, 40, 110, 30}, 10, 1, FX(10, 20, 35, 30)},
    {"LIQUID REESE", {64, 70, 8, 55, 10, 40, 50, 70}, {0, 0, 127, 20}, 10, 1, FX(20, 10, 0, 10)},
    {"ACID POOL", {110, 60, 6, 40, 40, 60, 90, 40}, {0, 40, 80, 15}, 30, 1, FX(20, 0, 20, 10)},
    {"TIDE PAD", {64, 60, 8, 80, 5, 110, 20, 30}, {100, 90, 120, 110}, 0, 0, FX(0, 70, 30, 90)},
    {"INK", {80, 100, 8, 45, 20, 80, 40, 60}, {0, 50, 100, 20}, 20, 1, FX(10, 0, 0, 10)},
    {"QUICKSILVER", {60, 40, 7, 95, 100, 90, 30, 0}, {5, 40, 110, 30}, 10, 1, FX(0, 30, 35, 40)},
    {"LAVA LAMP", {70, 70, 8, 60, 10, 110, 30, 40}, {80, 90, 120, 100}, 0, 0, FX(0, 70, 30, 90)},
    {"BUBBLE", {64, 50, 6, 95, 110, 80, 50, 20}, {0, 50, 80, 30}, 20, 0, FX(0, 30, 30, 40)},
    {"SYRUP BASS", {90, 90, 8, 40, 20, 50, 60, 90}, {0, 50, 100, 20}, 20, 1, FX(10, 0, 0, 10)},
    {"SPLASH", {127, 60, 6, 90, 90, 90, 40, 0}, {5, 40, 110, 30}, 10, 1, FX(10, 20, 35, 30)},
    {"DEEP WATER", {50, 80, 8, 35, 15, 100, 40, 80}, {0, 50, 100, 20}, 20, 1, FX(10, 0, 0, 10)},
    {"OOZE", {100, 110, 8, 50, 30, 90, 70, 60}, {0, 50, 100, 20}, 20, 1, FX(20, 0, 0, 10)},
    {"GLASS LIQUID", {20, 30, 4, 110, 60, 60, 20, 0}, {0, 60, 50, 40}, 20, 0, FX(0, 30, 30, 40)},
    {"WAVE POOL", {64, 60, 7, 70, 40, 70, 30, 30}, {60, 90, 120, 100}, 0, 0, FX(0, 60, 30, 80)},
};

static const engine_t ENG_FLUIDS = {
    .name = "FLUIDS",
    .id = EID_FLUIDS,
    .cc = 0x27,                  /* CC74 DISP, CC71 RES */
    .cats = "BBPPLBBPKBLBBPBLPKBLBBKP",
    .page_title = {"WAVE", "FLOW"},
    .edit = {
        {"WAVE", F_PCT, 0, 127, 64, 0, 0},
        {"DISP", F_PCT, 0, 127, 60, 0, 0},
        {"STAGES", F_INT, 2, 8, 6, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 70, 0, 0},
        {"MOTION", F_PCT, 0, 127, 30, 0, 0},
        {"DEPTH", F_PCT, 0, 127, 50, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = FLUIDS_PRESETS,
    .npresets = NELEM(FLUIDS_PRESETS),
    .poly = FL_POLY,
    .note_on = fluids_note_on,
    .render = fluids_render,
    .knob = {P_E1, P_E3, P_E5, P_E0},
    .keep = 0x03,                /* the filter (a slide: no click) */
};
