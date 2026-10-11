/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DUBSIREN (1.5): the sound-system siren box (Felucca's own, in the manner of the boxes): an oscillator (WAVE: SQR,
 * TRI, SAW, SINE) whose pitch an LFO swings DEPTH semitones up at RATE (~0.1 .. 20 Hz) in SHAPE (SQR the two-tone
 * "nee-naw", UP a whoop up, DOWN a whoop down, TRI a wail); SWEEP slides the whole pitch up or down from the note on
 * (+-32 semitones a second, on through the release: the dive), and the box's own tape echo: TIME (20 .. 460 ms; turned
 * while it sounds, the echoes bend in pitch as tape does), FDBK (past 100 or so it runs away while the key is held;
 * let go, it settles back, so the tail always ends), MIX the echoes' level. The echo's loop is darkened and
 * saturated as a tape loop is.
 * Its own envelope: ATK and REL of the ADSR (DEC, SUS are not used: a siren is on or off); the voice lasts until
 * the echoes have died away (ownenv). One voice a part (the echo is the part's).
 * Voice state: ph[0] the oscillator, ph[1] the LFO, s[0] the envelope (Q15), s[1] the sweep (1/16 semitone, Q8),
 * s[2] the last block's echo level. The echo: ds_echo[part] (in the pool), 11025 samples a second. */
static const char *const N_DS_WAVE[] = {"SQR", "TRI", "SAW", "SINE", 0};
static const char *const N_DS_SHAPE[] = {"SQR", "UP", "DOWN", "TRI", 0};
#define DS_LEN 5120u             /* the echo line: 464 ms at 11025 a second */
typedef struct {
    int16_t buf[DS_LEN];
    uint32_t w, cnt;             /* the next write; the input samples summed so far (4 make one) */
    int32_t acc, lp, d8;         /* their sum; the loop's low-pass; the delay (samples, Q8: glides to TIME) */
} ds_echo_t;
static ds_echo_t ds_echo[NPART] __attribute__((section(".pool")));

static ds_echo_t *ds_of(track_t *t) { return t >= &trk[0] && t < &trk[NPART] ? &ds_echo[t - trk] : 0; }
static int32_t ds_target(const track_t *t) { return (220 + t->p[P_E5] * 38) << 8; }   /* TIME: 220 .. 5046 samples */

static void dubsiren_note_on(track_t *t, voice_t *v)
{
    ds_echo_t *e = ds_of(t);
    v->ph[0] = v->ph[1] = 0;
    v->s[1] = 0;                                      /* (the envelope and the echo go on: a retrigger, no click) */
    if (!v->s[0])
        v->s[2] = 0;
    if (e && !e->d8)
        e->d8 = ds_target(t);
}

static int ds_done(track_t *t, voice_t *v)
{
    (void)t;
    return !v->gate && v->s[0] == 0 && v->s[2] < 24;  /* let go, silent, the echoes gone */
}

static int32_t ds_step(int32_t env, int32_t k, int up)   /* the envelope a block: ATK / REL 0 .. 127 (~2 s) */
{
    int32_t tau = 1 + k * k / 6;
    if (up)
        return env + (32767 - env) / tau + 1 > 32767 ? 32767 : env + (32767 - env) / tau + 1;
    return env - env / tau - 1 < 0 ? 0 : env - env / tau - 1;
}

static void dubsiren_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    ds_echo_t *e = ds_of(t);
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0] & 3u, shape = (uint32_t)p[P_E3] & 3u, i, inc, lph;
    int32_t env = v->s[0], sw = v->s[1], lfo, p16, fb = p[P_E6] * 270, dry = 32767 - p[P_E7] * 128, wet = p[P_E7] * 258;
    int32_t level = 0;
    if (!e)
        return;
    env = ds_step(env, v->gate ? p[P_ATK] : p[P_REL], v->gate);
    if (!v->gate && fb > 26000)                       /* (let go: the runaway settles, the tail ends in seconds) */
        fb = 26000;
    sw = clamp(sw + p[P_E4] * 3 / 2, -196608, 196608);   /* SWEEP: +-32 semitones a second, +-48 at most */
    lph = v->ph[1] + (pitch_inc((uint32_t)(p[P_E1] * 11)) >> 1);   /* RATE: ~0.13 .. 20 Hz, a block at a time */
    v->ph[1] = lph;
    switch (shape) {                                  /* the LFO, 0 .. 32767: up from the note */
    case 0u: lfo = lph < 0x80000000u ? 32767 : 0; break;
    case 1u: lfo = (int32_t)(lph >> 17); break;
    case 2u: lfo = 32767 - (int32_t)(lph >> 17); break;
    default: lfo = (osc_tri(lph) + 32768) >> 1; break;
    }
    p16 = m->pitch16 + (sw >> 8) + ((lfo * p[P_E2] * 16) >> 15);
    inc = pitch_inc((uint32_t)clamp(p16, 0, 2047));
    e->d8 += (ds_target(t) - e->d8) >> 6;             /* TIME glides (the tape's pitch bend) */
    for (i = 0; i < n; i++) {
        int32_t s, rp, a, b, y, x;
        uint32_t r0;
        switch (wave) {
        case 0u: s = osc_pulse(v->ph[0], inc, 0x80000000u); break;
        case 1u: s = osc_tri(v->ph[0]); break;
        case 2u: s = osc_saw(v->ph[0], inc); break;
        default: s = sine_i(v->ph[0]); break;
        }
        v->ph[0] += inc;
        s = (s >> 1) * env >> 15;                     /* the dry siren, half scale */
        /* the echo, read between its samples: (w + cnt / 4) - d samples back */
        rp = (int32_t)(e->w << 8) + (int32_t)(e->cnt << 6) - e->d8;
        r0 = (uint32_t)(rp + (int32_t)(DS_LEN << 8) * 2) >> 8;   /* (0 .. 3 lines: wrapped by compares) */
        r0 = r0 >= DS_LEN ? r0 - DS_LEN : r0;
        r0 = r0 >= DS_LEN ? r0 - DS_LEN : r0;
        a = e->buf[r0];
        b = e->buf[r0 + 1u == DS_LEN ? 0u : r0 + 1u];
        y = a + (((b - a) * (rp & 255)) >> 8);
        level = (y < 0 ? -y : y) > level ? (y < 0 ? -y : y) : level;   /* (no libc abs on the device) */
        e->acc += s;
        if (++e->cnt == 4u) {                         /* a sample into the line: the input and the loop fed back */
            e->lp += ((y - e->lp) * 14000) >> 15;     /* (the loop darkened: ~2.4 kHz) */
            x = (e->acc >> 2) + ((e->lp * fb) >> 15);
            e->buf[e->w] = (int16_t)clamp(soft_knee(x, 20000), -32767, 32767);   /* (and saturated: a tape) */
            e->w = e->w + 1u == DS_LEN ? 0u : e->w + 1u;
            e->acc = 0;
            e->cnt = 0;
        }
        out[i] += voice_amp(clamp(((s * dry) >> 15) + ((y * wet) >> 15), -32767, 32767), m, i) << 1;
    }
    v->s[0] = env;
    v->s[1] = sw;
    v->s[2] = level;
}

/* {WAVE, RATE, DEPTH, SHAPE, SWEEP, TIME, FDBK, MIX}; WAVE 0 SQR 1 TRI 2 SAW 3 SINE; SHAPE 0 SQR 1 UP 2 DOWN 3 TRI.
 * ADSR: ATK and REL only */
static const preset_t DUBSIREN_PRESETS[] = {
    {"CLASSIC SIREN", {0, 60, 12, 2, 0, 80, 90, 70}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 0, 20)},
    {"NEE NAW", {0, 40, 5, 0, 0, 70, 70, 60}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 0, 20)},
    {"WHOOP", {0, 30, 24, 1, 0, 90, 80, 60}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 0, 20)},
    {"WAIL", {1, 20, 9, 3, 0, 100, 85, 60}, {10, 0, 127, 40}, 0, 1, FX(0, 0, 0, 30)},
    {"LASER", {2, 110, 24, 2, -40, 60, 95, 70}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 0, 10)},
    {"ALARM", {0, 90, 7, 0, 0, 50, 60, 50}, {0, 0, 127, 15}, 0, 1, FX(0, 0, 0, 10)},
    {"DIVE", {0, 0, 0, 0, -50, 110, 100, 70}, {0, 0, 127, 50}, 0, 1, FX(0, 0, 0, 30)},
    {"RISER", {2, 70, 3, 3, 45, 80, 90, 60}, {40, 0, 127, 40}, 0, 1, FX(0, 20, 0, 30)},
    {"RUNAWAY ECHO", {0, 50, 12, 2, 0, 90, 118, 90}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 0, 20)},
    {"TRILL", {3, 120, 2, 0, 0, 70, 70, 50}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 0, 20)},
    {"UFO", {3, 85, 14, 3, 10, 100, 95, 70}, {20, 0, 127, 60}, 0, 1, FX(0, 30, 0, 40)},
    {"DUB HORN", {2, 0, 0, 0, 0, 85, 80, 55}, {0, 0, 127, 20}, 0, 1, FX(20, 0, 0, 20)},
};

static const engine_t ENG_DUBSIREN = {
    .name = "DUBSIREN",
    .id = EID_DUBSIREN,
    .cc = 0x27,                  /* CC74 RATE, CC71 FDBK */
    .cats = "FFFFFFFFFLFL",
    .page_title = {"SIREN", "ECHO"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 0, N_DS_WAVE, 0},
        {"RATE", F_PCT, 0, 127, 60, 0, 0},
        {"DEPTH", F_INT, 0, 36, 12, 0, "st"},
        {"SHAPE", F_ENUM, 0, 3, 2, N_DS_SHAPE, 0},
        {"SWEEP", F_BIPCT, -64, 63, 0, 0, 0},
        {"TIME", F_PCT, 0, 127, 80, 0, 0},
        {"FDBK", F_PCT, 0, 127, 90, 0, 0},
        {"MIX", F_PCT, 0, 127, 70, 0, 0},
    },
    .presets = DUBSIREN_PRESETS,
    .npresets = NELEM(DUBSIREN_PRESETS),
    .poly = 1,
    .note_on = dubsiren_note_on,
    .render = dubsiren_render,
    .knob = {P_E1, P_E2, P_E5, P_E6},
    .ownenv = 1,
    .done = ds_done,
};
