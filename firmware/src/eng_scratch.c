/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SCRATCH (1.5, FOUNDRY): a turntablist's hands (Fel-Tracker's own). The record is a sound worked out from where the
 * needle is, not a recording: SRC a STAB (a saw and its fifth at the key, decaying along the groove), VOX (an "aah",
 * three sines in a vowel's place), KICK (a falling sine) or NOISE (a burst, its grain fixed to the groove: it scrubs as
 * a sample would). MOTION moves the needle at the song's tempo (RATE: a beat .. 1/16): BABY (forward, back), FLARE
 * (forward, back, the fader clicking it), CHIRP (a push forward, cut), TEAR (back in two stutters), REWIND (a fast spin
 * back), STOP (the platter braking), SCRUB (a random hand); DEPTH how far the hand goes, FADER how hard the fader cuts,
 * CRACKLE the vinyl, TONE a low-pass, DRIVE last.
 * Voice state: s[0] the needle (samples into the groove, Q8), s[1] MOTION's phase (Q16 of a step), s[2] the last
 * noise grain, s[3] TONE's filter, s[4] the crackle, s[5] the samples since the note; ph[0] MOTION's phase (a step
 * 2^32). */
static uint32_t beat_samples(void);
static const char *const N_SX_SRC[] = {"STAB", "VOX", "KICK", "NOISE", 0};
static const char *const N_SX_MOTION[] = {"BABY", "FLARE", "CHIRP", "TEAR", "REWIND", "STOP", "SCRUB", 0};
static const char *const N_SX_RATE[] = {"1/4", "1/8", "1/16", "1/2", 0};
static const uint8_t SX_DIV[4] = {1, 2, 4, 0};       /* steps a beat (0: two beats) */
#define SX_LEN (FS / 2)                               /* the groove: half a second of sound */

static void scratch_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = 0;
    v->s[5] = 0;
    v->ph[0] = 0;
    if (!v->s[4])
        v->s[4] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

/* the record at needle pos (samples, Q8): the source's value there; pinc the note's phase a sample */
static int32_t sx_record(uint32_t src, int32_t pos, uint32_t pinc, int32_t *grain)
{
    int32_t s = pos >> 8, e, x;
    uint32_t ph;
    if (s < 0 || s >= SX_LEN)
        return 0;
    e = 32767 - s * 32767 / SX_LEN;                   /* (decaying along the groove; 32 bits: s < 22050) */
    ph = (uint32_t)((int64_t)pos * pinc >> 8);        /* the phase where the needle is: the pitch with the speed */
    switch (src) {
    case 0u:
        x = (osc_saw(ph, pinc) + osc_saw(ph + (ph >> 1), pinc + (pinc >> 1))) >> 1;
        break;
    case 1u:
        x = (sine_i(ph) + (sine_i(ph * 3u) >> 1) + (sine_i(ph * 5u) >> 2)) >> 1;
        break;
    case 2u:
        e = (e * e) >> 15;
        e = (e * e) >> 15;
        x = sine_i((uint32_t)((int64_t)pos * (pinc >> 2) >> 8) +       /* (two octaves down, falling from 3 times */
                   (uint32_t)((uint64_t)(pinc >> 2) * (uint32_t)((s * (32767 - e)) >> 15)));   /* that at the start) */
        break;
    default:                                          /* NOISE: a grain each 16 samples of the groove, interpolated */
        {
            uint32_t g0 = (uint32_t)(s >> 4) * 2654435761u, g1 = (uint32_t)((s >> 4) + 1) * 2654435761u;
            int32_t a = (int32_t)(int16_t)(g0 >> 16), b = (int32_t)(int16_t)(g1 >> 16), f = pos & 4095;
            x = a + (((b - a) * f) >> 12);
            *grain = x;
        }
        break;
    }
    return (x * e) >> 15;
}

static void scratch_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t src = (uint32_t)p[P_E0] & 3u, mot = (uint32_t)p[P_E1] % 7u, i, nz = (uint32_t)v->s[4];
    uint32_t div = SX_DIV[(uint32_t)p[P_E2] & 3u], step = div ? beat_samples() / div : beat_samples() * 2u;
    int32_t pos = v->s[0], lp = v->s[3], grain = v->s[2], depth = 64 + p[P_E3] * 3, fader = p[P_E4];
    int32_t crack = p[P_E5], kt = clamp(1500 + p[P_E6] * 250, 1500, 32767), since = v->s[5], drive = 256 + p[P_E7] * 12;
    uint32_t mph = v->ph[0], dph = 0xFFFFFFFFu / (step | 1u);   /* MOTION's phase a sample (a step: 2^32) */
    for (i = 0; i < n; i++) {
        int32_t sp, x, g = 32767, u = (int32_t)(mph >> 16);   /* u: 0 .. 65535 through the step */
        switch (mot) {                                /* the needle's speed (Q8: 256 the record's own) */
        case 0u: sp = sine_i((uint32_t)u << 16) * depth >> 15; break;          /* BABY */
        case 1u: sp = sine_i((uint32_t)u << 16) * depth >> 15;                 /* FLARE: the fader clicks it */
            g = ((u >> 12) & 3u) == 1u ? 32767 - fader * 258 : 32767; break;
        case 2u: sp = u < 24576 ? depth : 0; g = u < 24576 ? 32767 : 32767 - fader * 258; break;   /* CHIRP */
        case 3u: sp = u < 32768 ? depth : -(depth / 2) * ((u >> 13) & 1 ? 1 : 0); break;   /* TEAR */
        case 4u: sp = u < 32768 ? 256 : -(depth * 3) * (65535 - u) / 32768; break;   /* REWIND: played, then spun back */
        case 5u: sp = 256 - (int32_t)((256u * (uint32_t)clamp(since, 0, (int32_t)step * 2)) / (step * 2u | 1u)); break;   /* STOP */
        default: sp = (int32_t)(((mph >> 28) * 2654435761u) >> 23) - 256; sp = sp * depth >> 8; break;   /* SCRUB */
        }
        pos += sp;
        pos = clamp(pos, 0, (SX_LEN - 1) << 8);
        x = sx_record(src, pos, m->inc, &grain);
        x = (x * g) >> 15;
        if (crack) {                                  /* CRACKLE */
            nz ^= nz << 13;
            nz ^= nz >> 17;
            nz ^= nz << 5;
            if ((nz >> 22) < (uint32_t)crack / 16u)
                x += (int32_t)(int16_t)(nz >> 8) >> 2;
        }
        lp += ((x - lp) * kt) >> 15;
        out[i] += voice_amp(softclip((lp * drive) >> 8), m, i) << 1;   /* DRIVE */
        if (mph + dph < mph && (mot == 2u || mot == 3u || mot == 4u))
            pos = 0;                                  /* (CHIRP, TEAR, REWIND: the hand back on the cue each step) */
        mph += dph;
        since++;
    }
    v->s[0] = pos;
    v->ph[0] = mph;
    v->s[2] = grain;
    v->s[3] = lp;
    v->s[4] = (int32_t)nz;
    v->s[5] = since;
}

/* {SRC, MOTION, RATE, DEPTH, FADER, CRACKLE, TONE, DRIVE}; SRC 0 STAB 1 VOX 2 KICK 3 NOISE; MOTION 0 BABY 1 FLARE 2 CHIRP
 * 3 TEAR 4 REWIND 5 STOP 6 SCRUB; RATE 0 1/4 1 1/8 2 1/16 3 1/2 */
static const preset_t SCRATCH_PRESETS[] = {
    {"BABY SCRATCH", {0, 0, 1, 64, 0, 20, 110, 0}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 15)},
    {"FLARE", {0, 1, 1, 70, 120, 20, 110, 0}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 15)},
    {"CHIRP", {1, 2, 2, 60, 110, 10, 110, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 25, 15)},
    {"TEAR", {0, 3, 1, 70, 60, 20, 110, 0}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 15)},
    {"REWIND", {0, 4, 3, 90, 0, 30, 110, 0}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 30, 20)},
    {"TAPE STOP", {1, 5, 3, 64, 0, 20, 100, 0}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 25, 20)},
    {"NOISE SCRUB", {3, 6, 2, 80, 40, 40, 100, 0}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 15)},
    {"KICK SCRATCH", {2, 0, 2, 60, 80, 10, 90, 0}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 15, 10)},
    {"VOX TEAR", {1, 3, 2, 80, 90, 20, 110, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 30, 20)},
    {"FRESH", {3, 0, 1, 70, 0, 0, 120, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 20, 15)},
    {"TRANSFORMER", {0, 1, 2, 70, 127, 10, 110, 20}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 15)},
    {"CRAB", {1, 1, 2, 60, 127, 10, 110, 0}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 20, 15)},
    {"ORBIT", {0, 0, 2, 90, 40, 20, 110, 20}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 15)},
    {"DIRTY VINYL", {0, 6, 1, 70, 30, 127, 90, 60}, {0, 0, 127, 20}, 0, 1, FX(20, 0, 20, 15)},
    {"SLOW REWIND", {1, 4, 3, 50, 0, 40, 100, 0}, {0, 0, 127, 30}, 0, 1, FX(0, 0, 30, 25)},
    {"KICK CHIRP", {2, 2, 2, 70, 110, 0, 90, 40}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 15, 10)},
};

static const engine_t ENG_SCRATCH = {
    .name = "SCRATCH",
    .id = EID_SCRATCH,
    .cc = 0x47,                  /* CC74 DEPTH .. FADER, CC71 TONE */
    .cats = "FFFFFFFDFFFFFFFF",
    .page_title = {"RECORD", "HANDS"},
    .edit = {
        {"SRC", F_ENUM, 0, 3, 0, N_SX_SRC, 0},
        {"MOTION", F_ENUM, 0, 6, 0, N_SX_MOTION, 0},
        {"RATE", F_ENUM, 0, 3, 1, N_SX_RATE, 0},
        {"DEPTH", F_PCT, 0, 127, 64, 0, 0},
        {"FADER", F_PCT, 0, 127, 0, 0, 0},
        {"CRACKLE", F_PCT, 0, 127, 20, 0, 0},
        {"TONE", F_PCT, 0, 127, 110, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = SCRATCH_PRESETS,
    .npresets = NELEM(SCRATCH_PRESETS),
    .poly = 2,
    .note_on = scratch_note_on,
    .render = scratch_render,
    .knob = {P_E1, P_E3, P_E4, P_E2},
};
