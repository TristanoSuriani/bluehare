/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* VAKIT (2026-10-23, the user's "can we use the VA engine to make a derivative drumkit engine? ... macro"): synth
 * drums, each hit a short voice of VA's core (eng_va.c): its band-limited oscillators and noises, its filters, its
 * saturation, at twice the rate. Not a machine imitated (RHYTHM is the 808 and the 909): the analog synth's own drums.
 * The GM drum notes on every octave of 36 .. 47 (as KIT and RHYTHM): C KICK, C# RIM, D SNARE, D# CLAP, E ZAP, F TOM L,
 * F# HAT CL, G TOM M, G# BLIP, A TOM H, A# HAT OP, B CYMBL. Each drum is a recipe (VK_D): a body (WAVE's, or its own),
 * a second tone, a noise, a filter (the ladder, or the 12 dB filter's low, band or high pass), a pitch envelope, a
 * filter envelope, its length. The eight knobs shape the whole kit: TUNE, DECAY (every length, a quarter .. four
 * times), PENV (the pitch envelopes, none .. twice), CUT and RES (every filter), NOISE (every noise, none .. twice),
 * DRIVE (into the core's saturation), WAVE (the body of the kick, the toms and the zap: SINE TRI SAW PULSE).
 * CLOSED chokes a sounding OPEN. Six voices (a hit is short: the oldest gives way). Its own envelope (engine_t.ownenv,
 * the velocity the level), the hit plays to its end past its note-off (engine_t.oneshot).
 * Voice state: VA's core's (va_v: the oscillators, the filter); s[0] samples since the hit, s[1] its length, s[4] the
 * drum, s[5] the choke (Q15, 32767 none). */
#define VK_POLY 6
static const char *const N_VK_WAVE[] = {"SINE", "TRI", "SAW", "PULSE", 0};
enum { VK_KICK, VK_RIM, VK_SNARE, VK_CLAP, VK_ZAP, VK_TOML, VK_HATC, VK_TOMM, VK_BLIP, VK_TOMH, VK_HATO, VK_CYMBL };
#define VK_W 255u                /* (the body: WAVE's) */
typedef struct {
    float hz;                    /* the body's pitch */
    uint8_t w1;                  /* its wave (VA_*, VK_W: WAVE's) */
    uint8_t w2;                  /* the second tone's wave */
    float r2, g2;                /* its ratio to the body, its level (0: none) */
    uint8_t nw;                  /* the noise (VA_WHITE, VA_PINK) */
    float gn;                    /* its level */
    uint8_t filt;                /* VA_LP12, VA_LADDER, VA_BP, VA_HP */
    float cut;                   /* the cutoff (0 .. 127) */
    float fenv, fms;             /* the filter envelope: its depth (cutoff units), its time (ms) */
    float pst, pms;              /* the pitch envelope: semitones from above, its time (ms) */
    float ms, nms;               /* the level's decay, the noise's (ms) */
} vk_drum_t;
static const vk_drum_t VK_D[12] = {
    /* hz     w1        w2        r2     g2    noise     gn    filter     cut   fenv  fms   pst   pms   ms    nms */
    {48.0f, VK_W, VA_SINE, 1.0f, 0.0f, VA_WHITE, 0.25f, VA_LADDER, 62.0f, 40.0f, 25.0f, 26.0f, 22.0f, 420.0f, 3.0f},   /* KICK */
    {470.0f, VA_TRI, VA_TRI, 3.47f, 0.5f, VA_WHITE, 0.1f, VA_BP, 98.0f, 0.0f, 1.0f, 2.0f, 4.0f, 28.0f, 10.0f},          /* RIM */
    {185.0f, VA_TRI, VA_SINE, 1.78f, 0.45f, VA_WHITE, 0.9f, VA_LP12, 108.0f, 10.0f, 30.0f, 8.0f, 14.0f, 110.0f, 170.0f}, /* SNARE */
    {1.0f, VA_SINE, VA_SINE, 1.0f, 0.0f, VA_PINK, 1.6f, VA_BP, 86.0f, 6.0f, 40.0f, 0.0f, 1.0f, 190.0f, 190.0f},          /* CLAP */
    {110.0f, VK_W, VA_SAW, 1.01f, 0.5f, VA_WHITE, 0.0f, VA_LADDER, 70.0f, 55.0f, 110.0f, 36.0f, 45.0f, 220.0f, 5.0f},    /* ZAP */
    {88.0f, VK_W, VA_SINE, 1.5f, 0.15f, VA_WHITE, 0.08f, VA_LADDER, 78.0f, 22.0f, 70.0f, 11.0f, 60.0f, 300.0f, 25.0f},   /* TOM L */
    {3150.0f, VA_PULSE, VA_PULSE, 1.4471f, 0.8f, VA_WHITE, 0.7f, VA_HP, 104.0f, 0.0f, 1.0f, 0.0f, 1.0f, 45.0f, 40.0f},   /* HAT CL */
    {126.0f, VK_W, VA_SINE, 1.5f, 0.15f, VA_WHITE, 0.08f, VA_LADDER, 82.0f, 22.0f, 65.0f, 10.0f, 55.0f, 260.0f, 25.0f},  /* TOM M */
    {820.0f, VA_SINE, VA_SINE, 2.51f, 0.3f, VA_WHITE, 0.0f, VA_LP12, 110.0f, 0.0f, 1.0f, 12.0f, 7.0f, 70.0f, 5.0f},      /* BLIP */
    {176.0f, VK_W, VA_SINE, 1.5f, 0.15f, VA_WHITE, 0.08f, VA_LADDER, 86.0f, 22.0f, 60.0f, 9.0f, 50.0f, 220.0f, 25.0f},   /* TOM H */
    {3150.0f, VA_PULSE, VA_PULSE, 1.4471f, 0.8f, VA_WHITE, 0.7f, VA_HP, 102.0f, 0.0f, 1.0f, 0.0f, 1.0f, 340.0f, 300.0f}, /* HAT OP */
    {2380.0f, VA_PULSE, VA_PULSE, 1.6818f, 0.8f, VA_WHITE, 0.6f, VA_HP, 96.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1100.0f, 700.0f}, /* CYMBL */
};
static const uint8_t VK_WAVES[4] = {VA_SINE, VA_TRI, VA_SAW, VA_PULSE};
static inline float vk_dec(const int16_t *p) { return 0.25f * fm_exp2f((float)p[P_E1] * (4.0f / 127.0f)); }
static inline float vk_env(float age, float ms) { return fm_expf(-age / (ms * (float)FS * 0.001f)); }   /* (age: samples) */

static void vakit_note_on(track_t *t, voice_t *v)
{
    const vk_drum_t *d;
    int32_t k = ((int32_t)v->note - 36) % 12;
    uint32_t i;
    float len;
    k = k < 0 ? k + 12 : k;
    d = &VK_D[k];
    v->env = 0;                                       /* (a fresh hit: the core's state cleared, va_start) */
    va_start(t, v);
    v->s[0] = 0;
    v->s[4] = k;
    v->s[5] = 32767;
    len = 6.5f * fm_maxf(d->ms, d->nms) * vk_dec(t->p) * (float)FS * 0.001f;
    v->s[1] = (int32_t)fm_minf(len, 4.0f * (float)FS) + 256;
    if (k == VK_HATC)                                 /* CLOSED chokes a sounding OPEN */
        for (i = 0; i < NVOICE; i++)
            if (&t->v[i] != v && t->v[i].active && t->v[i].s[4] == VK_HATO && t->v[i].s[5] == 32767)
                t->v[i].s[5] = 32766;
}

static int vakit_done(track_t *t, voice_t *v)
{
    (void)t;
    return v->s[0] >= v->s[1] || v->s[5] < 64;
}

static void vakit_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    va_voice_t *s = va_of(t, v);
    int32_t k = v->s[4], age = v->s[0], ch = v->s[5];
    const vk_drum_t *d = &VK_D[k % 12];
    float dec = vk_dec(p), a0, a1, am, pe, fe, ne, hz, y[CTL], vel, da;
    float tune = (float)p[P_E0], penv = (float)p[P_E2] * (2.0f / 127.0f);
    va_cfg_t c;
    uint32_t i, inc;
    if (!s || n > CTL)
        return;
    a0 = vk_env((float)age, d->ms * dec);             /* the level: from the block's start to its end (per sample) */
    a1 = vk_env((float)(age + (int32_t)n), d->ms * dec);
    am = fm_maxf(0.5f * (a0 + a1), 1e-4f);
    pe = d->pst * penv * vk_env((float)age + 0.5f * (float)n, d->pms);
    fe = d->fenv * vk_env((float)age + 0.5f * (float)n, d->fms * dec);
    ne = vk_env((float)age + 0.5f * (float)n, d->nms * dec);
    hz = d->hz * fm_exp2f((tune + pe) * (1.0f / 12.0f));
    inc = (uint32_t)fm_minf(hz / (float)FS * 4294967296.0f, 0.45f * 4294967296.0f);
    c.nosc = 2;
    c.sync = 0;
    c.w[0] = d->w1 == VK_W ? VK_WAVES[p[P_E7] & 3] : d->w1;
    c.w[1] = d->w2;
    c.inc[0] = inc;
    c.inc[1] = (uint32_t)fm_minf((float)inc * d->r2, 0.45f * 4294967296.0f);
    c.g[0] = d->hz > 2.0f ? 0.6f : 0.0f;              /* (the clap: noise alone) */
    c.g[1] = 0.6f * d->g2;
    c.ws = d->nw;                                     /* the noise in the sub's place (its step unused) */
    c.incs = 0;
    c.gs = fm_minf(d->gn * (float)p[P_E5] * (2.0f / 127.0f) * ne / am, 4.0f);   /* (relative to the level: applied after) */
    c.pw = 0.5f;
    c.cut = d->cut + fe + (float)(p[P_E3] - 64) * 0.6f;
    c.res = (float)p[P_E4] * (0.9f / 127.0f);
    c.ladder = d->filt;
    c.drive = 1.0f + (float)p[P_E6] * (3.0f / 127.0f);
    va_run(s, y, n, &c);
    vel = (0.35f + 0.65f * (float)v->vel * (1.0f / 127.0f)) * (2.0f / (1.0f + c.drive * 0.5f));
    vel *= k == VK_RIM ? 1.8f : 1.0f;                 /* (the rim's narrow band: as loud as the rest) */
    da = (a1 - a0) / (float)n;
    for (i = 0; i < n; i++) {
        float e = a0 + da * (float)i, x;
        if (k == VK_CLAP) {                           /* four bursts ~10 ms apart, then the tail */
            int32_t b = (age + (int32_t)i) / (int32_t)(FS / 100u);
            e = b < 3 ? fm_expf(-(float)(age + (int32_t)i - b * (int32_t)(FS / 100u)) * (1.0f / (0.003f * (float)FS))) : e;
        }
        if (age + (int32_t)i < 16)                    /* (no click but the drum's own) */
            e *= (float)(age + (int32_t)i + 1) * (1.0f / 16.0f);
        if (ch < 32767) {                             /* the choke: ~7 ms */
            ch -= ch >> 5;
            ch = ch < 1 ? 0 : ch;
            e *= (float)ch * (1.0f / 32767.0f);
        }
        x = y[i] * e * vel;
        out[i] += voice_amp((int32_t)fm_clampf(x * 30000.0f, -32767.0f, 32767.0f), m, i) << 1;
    }
    v->s[0] = age + (int32_t)n;
    v->s[5] = ch;
}

static int32_t vakit_keys(const track_t *t, uint32_t k) { return drum_keys(t, k); }

/* {TUNE, DECAY, PENV, CUT, RES, NOISE, DRIVE, WAVE}. The GM notes on every octave of 36 .. 47: KICK RIM SNARE CLAP ZAP
 * TOM-L HAT-CL TOM-M BLIP TOM-H HAT-OP CYMBL */
static const preset_t VAKIT_PRESETS[] = {
    {"SYNTH KIT", {0, 64, 64, 64, 30, 64, 20, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 20)},
    {"SYNTHWAVE", {-2, 80, 80, 60, 40, 60, 30, 1}, {0, 0, 127, 20}, 0, 0, FX(0, 30, 20, 60)},
    {"ELECTRO", {0, 50, 100, 70, 50, 70, 40, 3}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 25, 20)},
    {"ACID PERC", {0, 45, 90, 50, 105, 50, 60, 2}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 30, 25)},
    {"ZAP ATTACK", {3, 55, 127, 75, 80, 40, 50, 2}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 25, 25)},
    {"DEEP BOOM", {-5, 100, 60, 45, 20, 40, 35, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 30)},
    {"MINIMAL", {1, 30, 50, 70, 25, 55, 10, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 15, 15)},
    {"INDUSTRIAL", {-1, 70, 70, 55, 70, 100, 127, 3}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 15, 45)},
    {"LOFI SYNTH", {-3, 60, 50, 35, 35, 80, 70, 1}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 25)},
    {"RAVE TOMS", {2, 75, 110, 80, 60, 50, 45, 3}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 30, 45)},
};

static const engine_t ENG_VAKIT = {
    .name = "VAKIT",
    .id = EID_VAKIT,
    .cc = 0x24,                  /* CC74 DECAY, CC71 CUT */
    .cats = "DDDDDDDDDD",
    .page_title = {"KIT", "SYNTH"},
    .edit = {
        {"TUNE", F_INT, -12, 12, 0, 0, "st"},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"PENV", F_PCT, 0, 127, 64, 0, 0},
        {"CUT", F_PCT, 0, 127, 64, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"NOISE", F_PCT, 0, 127, 64, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 20, 0, 0},
        {"WAVE", F_ENUM, 0, 3, 0, N_VK_WAVE, 0},
    },
    .presets = VAKIT_PRESETS,
    .npresets = NELEM(VAKIT_PRESETS),
    .poly = VK_POLY,
    .note_on = vakit_note_on,
    .render = vakit_render,
    .keys = vakit_keys,
    .knob = {P_E1, P_E2, P_E3, P_E6},
    .ownenv = 1,
    .oneshot = 1,                /* (a note-off leaves the hit alone: a sequencer's short notes) */
    .done = vakit_done,
};
