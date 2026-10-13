/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* DRILL (1.5, FOUNDRY): power tools (Fel-Tracker's own). A sine carrier at the key, its pitch ramped at audio rate
 * (RATE ~1 .. ~390 Hz: pitch_inc(0) is ~8.2 Hz a sample, / 8 ~1 Hz; DEPTH up to +-2 octaves, SHAPE UP, DOWN or TRI: a drill's whine, a grinder's buzz), phase-
 * modulated by a sine RATIO times it (FM the index); ROLL cuts it into hits at the song's tempo while held (1/32 ..
 * 1/128: hyper-speed rolls), DRIVE, TONE a low-pass. The ADSR shapes it.
 * Voice state: ph[0] the carrier, ph[1] the modulator, ph[2] the ramp, s[0] samples into ROLL's step, s[1] TONE's
 * filter. */
static uint32_t beat_samples(void);
static const char *const N_DR_SHAPE[] = {"UP", "DOWN", "TRI", 0};
static const char *const N_DR_RATIO[] = {"1/2", "1", "3/2", "2", "3", "4", "5", "7", 0};
static const uint16_t DR_RATIO[8] = {128, 256, 384, 512, 768, 1024, 1280, 1792};
static const char *const N_DR_ROLL[] = {"OFF", "1/32", "1/48", "1/64", "1/96", "1/128", 0};
static const uint8_t DR_ROLL[6] = {0, 8, 12, 16, 24, 32};

static void drill_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[0] = 0;
}

static void drill_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t i, ph = v->ph[0], mph = v->ph[1], rph = v->ph[2], shape = (uint32_t)p[P_E2] % 3u, ratio = DR_RATIO[p[P_E4] & 7];
    uint32_t rinc = pitch_inc((uint32_t)p[P_E0] * 13u) / 8u, roll = DR_ROLL[(uint32_t)p[P_E5] % 6u], step = 0;
    int32_t depth = p[P_E1] * 3, fm = p[P_E3] * 400, drive = 256 + p[P_E6] * 12, lp = v->s[1], cnt = v->s[0];
    int32_t kt = clamp(1500 + p[P_E7] * 250, 1500, 32767);
    if (roll)
        step = beat_samples() / roll;
    for (i = 0; i < n; i++) {
        int32_t r = (int32_t)(rph >> 17) - 16384, x, g = 32767;   /* the ramp, -1 .. 1 (Q14) */
        uint32_t inc;
        if (shape == 1u)
            r = -r;
        else if (shape == 2u)
            r = (r < 0 ? -r : r) * 2 - 16384;
        inc = pitch_inc((uint32_t)clamp(m->pitch16 + ((r * depth) >> 10), 0, 2047));   /* DEPTH: up to +-24 st */
        x = sine_i(ph + (uint32_t)(sine_i(mph) * fm));
        ph += inc;
        mph += (uint32_t)(((uint64_t)inc * ratio) >> 8);
        rph += rinc;
        if (step && v->gate) {                        /* ROLL: on 2/3 of each step */
            if (++cnt >= (int32_t)step)
                cnt = 0;
            g = cnt < (int32_t)(step * 2u / 3u) ? 32767 : 0;
            if (cnt < 32)                             /* (a declick in) */
                g = (g >> 5) * cnt;
        }
        x = softclip((((x * g) >> 15) * drive) >> 8);
        lp += ((x - lp) * kt) >> 15;
        out[i] += voice_amp(lp, m, i) << 1;
    }
    v->ph[0] = ph;
    v->ph[1] = mph;
    v->ph[2] = rph;
    v->s[0] = cnt;
    v->s[1] = lp;
}

/* {RATE, DEPTH, SHAPE, FM, RATIO, ROLL, DRIVE, TONE}; SHAPE 0 UP 1 DOWN 2 TRI; RATIO 0 1/2 .. 7 7; ROLL 0 OFF 1 1/32 ..
 * 5 1/128 */
static const preset_t DRILL_PRESETS[] = {
    {"POWER DRILL", {100, 20, 0, 30, 3, 0, 60, 100}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 10, 15)},
    {"GRINDER", {110, 40, 2, 60, 5, 0, 90, 110}, {0, 0, 127, 20}, 0, 1, FX(20, 0, 10, 15)},
    {"ZAP GUN", {60, 127, 1, 20, 1, 0, 40, 100}, {0, 40, 0, 20}, 0, 1, FX(0, 0, 30, 20)},
    {"HYPER ROLL", {80, 30, 0, 40, 3, 5, 60, 100}, {0, 0, 127, 10}, 0, 1, FX(10, 0, 20, 15)},
    {"RIVET ROLL", {50, 60, 1, 50, 4, 3, 80, 90}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 20, 15)},
    {"BUZZSAW", {120, 15, 0, 90, 7, 0, 110, 120}, {0, 0, 127, 20}, 0, 1, FX(30, 0, 10, 10)},
    {"DENTIST", {127, 10, 2, 20, 1, 0, 30, 127}, {0, 0, 127, 20}, 0, 1, FX(0, 10, 20, 25)},
    {"MACHINE GUN", {40, 80, 1, 60, 2, 4, 100, 90}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 15, 15)},
    {"JACKHAMMER", {30, 90, 1, 70, 2, 2, 110, 90}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 10, 15)},
    {"ROUTER", {115, 30, 0, 50, 5, 0, 80, 110}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 10, 15)},
    {"IMPACT WRENCH", {20, 100, 2, 60, 3, 3, 120, 80}, {0, 0, 127, 10}, 0, 1, FX(30, 0, 15, 15)},
    {"LASER DRILL", {127, 60, 1, 30, 6, 0, 50, 127}, {0, 0, 127, 20}, 0, 1, FX(0, 10, 30, 25)},
    {"SANDER", {90, 10, 2, 100, 4, 0, 70, 90}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 10, 15)},
    {"NAIL GUN", {50, 110, 1, 80, 1, 4, 100, 100}, {0, 0, 127, 10}, 0, 1, FX(20, 0, 15, 15)},
    {"TURBINE", {100, 5, 0, 40, 0, 0, 40, 100}, {30, 0, 127, 60}, 0, 1, FX(0, 20, 20, 40)},
    {"DRILL BASS", {20, 30, 0, 40, 0, 0, 80, 60}, {0, 50, 100, 20}, 0, 1, FX(20, 0, 0, 5)},
};

static const engine_t ENG_DRILL = {
    .name = "DRILL",
    .id = EID_DRILL,
    .cc = 0x17,                  /* CC74 RATE, CC71 DRIVE */
    .cats = "FFFFFFFFFFFFFFFB",
    .page_title = {"RAMP", "TOOL"},
    .edit = {
        {"RATE", F_PCT, 0, 127, 100, 0, 0},
        {"DEPTH", F_PCT, 0, 127, 20, 0, 0},
        {"SHAPE", F_ENUM, 0, 2, 0, N_DR_SHAPE, 0},
        {"FM", F_PCT, 0, 127, 30, 0, 0},
        {"RATIO", F_ENUM, 0, 7, 3, N_DR_RATIO, 0},
        {"ROLL", F_ENUM, 0, 5, 0, N_DR_ROLL, 0},
        {"DRIVE", F_PCT, 0, 127, 60, 0, 0},
        {"TONE", F_PCT, 0, 127, 100, 0, 0},
    },
    .presets = DRILL_PRESETS,
    .npresets = NELEM(DRILL_PRESETS),
    .note_on = drill_note_on,
    .render = drill_render,
    .knob = {P_E0, P_E1, P_E3, P_E6},
};
