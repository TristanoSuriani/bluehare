/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* NOISE (2026-10-24, track 3: the user's "a noise engine based on VA"): noise played as an instrument, through VA's
 * core (eng_va.c). COLOR the noise (WHITE, PINK, BROWN), FILT the core's filter (LP24 the ladder, LP12, BP, HP), CUT
 * and RES; TRACK how far the cutoff follows the key (127: an octave up the keys an octave up the filter: with RES
 * high, the noise sings the notes), ENV and DEC a filter envelope each note (a sweep down from ENV above CUT over DEC),
 * TONE a saw at the note under the noise (none .. as loud). Wind, surf, static, breath, whistles, the hiss of tape.
 * Its id is CHAOS's (23, archived 2026-10-16: the favorites' layout has no room for a new one).
 * Voice state: VA's core's (va_v: the oscillators, the filter, the noises); s[0] samples since the note. */
static const char *const N_NZ_COLOR[] = {"WHITE", "PINK", "BROWN", 0};
static const char *const N_NZ_FILT[] = {"LP24", "LP12", "BP", "HP", 0};
static const uint8_t NZ_MODE[4] = {VA_LADDER, VA_LP12, VA_BP, VA_HP};

static void noise_note_on(track_t *t, voice_t *v)
{
    va_start(t, v);
    v->s[0] = 0;
}

static void noise_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    va_voice_t *s = va_of(t, v);
    int32_t age = v->s[0];
    float y[CTL], dec = 0.01f * fm_exp2f((float)p[P_E6] * (8.0f / 127.0f)), fe;   /* DEC: 10 ms .. 2.5 s */
    uint32_t i;
    va_cfg_t c;
    if (!s || n > CTL)
        return;
    fe = (float)p[P_E5] * (60.0f / 127.0f) * fm_expf(-(float)age / (dec * (float)FS));
    c.nosc = 2;
    c.sync = 0;
    c.w[0] = c.w[1] = VA_SAW;
    c.inc[0] = m->inc;                                /* TONE: the saw at the note */
    c.inc[1] = m->inc;
    c.g[0] = (float)p[P_E7] * (0.45f / 127.0f);
    c.g[1] = 0.0f;
    c.ws = (uint8_t)(VA_WHITE + clamp(p[P_E0], 0, 2));
    c.incs = 0;
    c.gs = p[P_E1] == 2 ? 1.4f + (float)p[P_E3] * (2.6f / 127.0f) : 0.9f;   /* (the band-pass: what it lets through,
                                                       * narrower with RES, made up) */
    c.pw = 0.5f;
    c.ladder = NZ_MODE[p[P_E1] & 3];
    c.cut = (float)p[P_E2] + fe + (float)m->cutoff * (1.0f / 256.0f) +
            ((float)m->pitch16 * (1.0f / 16.0f) - 60.0f) * (127.0f / (9.8f * 12.0f)) * (float)p[P_E4] * (1.0f / 127.0f);
    c.res = (float)p[P_E3] * (0.95f / 127.0f);
    c.drive = 1.0f;
    va_run(s, y, n, &c);
    for (i = 0; i < n; i++)
        out[i] += voice_amp((int32_t)fm_clampf(y[i] * 34000.0f, -32767.0f, 32767.0f), m, i) << 1;
    v->s[0] = age + (int32_t)n;
}

/* {COLOR, FILT, CUT, RES, TRACK, ENV, DEC, TONE} */
static const preset_t NOISE_PRESETS[] = {
    {"WIND", {1, 2, 60, 70, 0, 0, 60, 0}, {110, 90, 120, 110}, 0, 0, FX(0, 40, 20, 80)},
    {"SURF", {1, 1, 70, 20, 0, 40, 100, 0}, {100, 100, 90, 115}, 0, 0, FX(0, 30, 10, 90)},
    {"STATIC", {0, 3, 100, 10, 0, 0, 40, 0}, {0, 0, 127, 20}, 0, 0, FX(20, 0, 10, 20)},
    {"BREATH", {1, 2, 85, 30, 60, 20, 50, 15}, {45, 80, 100, 50}, 0, 0, FX(0, 20, 20, 60)},
    {"WHISTLE", {0, 2, 64, 120, 127, 0, 40, 0}, {40, 60, 110, 50}, 0, 1, FX(0, 10, 30, 60), .glide = 20},
    {"SINGING NOISE", {0, 0, 60, 118, 127, 10, 60, 10}, {20, 80, 110, 60}, 0, 0, FX(0, 40, 25, 70)},
    {"TAPE HISS", {0, 3, 105, 0, 0, 0, 40, 0}, {60, 0, 127, 60}, 0, 0, FX(0, 0, 0, 20)},
    {"RUMBLE", {2, 0, 30, 40, 0, 20, 90, 0}, {80, 90, 120, 100}, 0, 0, FX(15, 0, 10, 50)},
    {"NOISE SWEEP", {0, 0, 25, 90, 30, 120, 110, 0}, {0, 100, 60, 80}, 0, 0, FX(0, 20, 40, 60)},
    {"SNAP", {0, 2, 70, 40, 40, 90, 15, 0}, {0, 25, 0, 25}, 0, 0, FX(0, 0, 25, 30)},
};

static const engine_t ENG_NOISE = {
    .name = "NOISE",
    .id = EID_NOISE,
    .cc = 0x34,                  /* CC74 CUT, CC71 RES */
    .cats = "FFFFLPFFFU",
    .page_title = {"NOISE", "FILTER"},
    .edit = {
        {"COLOR", F_ENUM, 0, 2, 0, N_NZ_COLOR, 0},
        {"FILT", F_ENUM, 0, 3, 2, N_NZ_FILT, 0},
        {"CUT", F_CUTOFF, 0, 127, 70, 0, 0},
        {"RES", F_PCT, 0, 127, 40, 0, 0},
        {"TRACK", F_PCT, 0, 127, 0, 0, 0},
        {"ENV", F_PCT, 0, 127, 0, 0, 0},
        {"DEC", F_PCT, 0, 127, 60, 0, 0},
        {"TONE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = NOISE_PRESETS,
    .npresets = NELEM(NOISE_PRESETS),
    .poly = VA_POLY,
    .note_on = noise_note_on,
    .render = noise_render,
    .knob = {P_E2, P_E3, P_E4, P_E5},
};
