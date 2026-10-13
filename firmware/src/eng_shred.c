/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* SHRED (1.5, FOUNDRY): a sound torn apart as it plays (Fel-Tracker's own). One voice a track plays SRC (a SAW, SQR,
 * an FM tone at the key, or NOISE) into a short ring of its own (35 ms), and two windowed grains read it back (GRAIN
 * long, 1 .. 35 ms, overlapping): STRETCH how fast they walk along it (0: frozen, 64: in time, 127: twice), PITCH
 * their speed (+-12 semitones: the tears up or down), STUTTER how often a grain repeats instead of moving on, TEAR
 * how often one jumps somewhere else or plays backwards, at the song's tempo with SYNC (OFF, 1/16, 1/32, 1/64: the
 * stutters land on the grid); MIX the tear against the source.
 * State: the ring sh_buf[track] (in the pool, SH_LEN samples), the voice's ph[0] the source, ph[1] / ph[2] the grains'
 * read places (Q16 of a sample), s[0] / s[1] the grains' ages, s[2] the write place, s[3] the walk (Q16), s[4] the
 * random, s[5] the grains' direction bits (and bit 8: the ring filled once), s[6] the samples into SYNC's step, s[7] FM's modulator. */
#define SH_LEN 1536
static int16_t sh_buf[NPART][SH_LEN] __attribute__((section(".pool")));
static uint32_t beat_samples(void);
static const char *const N_SH_SRC[] = {"SAW", "SQR", "FM", "NOISE", 0};
static const char *const N_SH_SYNC[] = {"OFF", "1/16", "1/32", "1/64", 0};
static const uint8_t SH_DIV[4] = {0, 4, 8, 16};

static int16_t *sh_of(track_t *t) { return t >= &trk[0] && t < &trk[NPART] ? sh_buf[t - trk] : 0; }

static void shred_note_on(track_t *t, voice_t *v)
{
    int16_t *b = sh_of(t);
    if (b && !v->env && !v->env_out)
        memset(b, 0, SH_LEN * sizeof *b);
    int32_t glen = clamp(44 + t->p[P_E1] * 12, 44, SH_LEN - 4);
    v->ph[0] = 0;
    v->s[2] = 0;
    v->s[0] = glen;                                   /* (the first grain at once, the second half a grain on) */
    v->s[1] = glen / 2;
    v->s[3] = (int32_t)(((uint32_t)v->s[2] + SH_LEN * 2u - (uint32_t)glen * 2u) % SH_LEN) << 16;   /* the walk: behind
                                                       * the write place */
    v->s[5] = 0;
    v->s[6] = 0;
    if (!v->s[4])
        v->s[4] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

/* a grain's next start: STUTTER (the same again), TEAR (anywhere, maybe backwards), else where the walk is */
static void sh_start(const int16_t *p, voice_t *v, uint32_t *rp, uint32_t g, int32_t walk)
{
    uint32_t r = (uint32_t)v->s[4];
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    v->s[4] = (int32_t)r;
    if ((r & 127u) < (uint32_t)p[P_E4])
        return;                                       /* STUTTER: from where it began again */
    if (((r >> 8) & 127u) < (uint32_t)p[P_E5]) {      /* TEAR: elsewhere, perhaps backwards */
        *rp = ((r >> 16) % SH_LEN) << 16;
        v->s[5] ^= (int32_t)(((r >> 30) & 1u) << g);
        return;
    }
    *rp = (uint32_t)walk;
    v->s[5] &= ~(1 << g);
}

static void shred_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int16_t *b = sh_of(t);
    const int16_t *p = t->p;
    uint32_t src = (uint32_t)p[P_E0] & 3u, i, rp[2], w = (uint32_t)v->s[2], ph = v->ph[0], div = SH_DIV[(uint32_t)p[P_E6] & 3u];
    int32_t glen = 44 + p[P_E1] * 12, ag[2], walk = v->s[3], wstep = p[P_E2] * 1024, mix = p[P_E7];
    int32_t pr, step = div ? (int32_t)(beat_samples() / div) : 0, sc = v->s[6], fmph = v->s[7];
    uint32_t r = (uint32_t)v->s[4];
    if (!b)
        return;
    pr = (int32_t)(sp_pow2(p[P_E3] * 16) >> 0);      /* PITCH: the grains' speed (Q16) */
    glen = glen > SH_LEN - 4 ? SH_LEN - 4 : glen;
    rp[0] = v->ph[1];
    rp[1] = v->ph[2];
    ag[0] = v->s[0];
    ag[1] = v->s[1];
    for (i = 0; i < n; i++) {
        int32_t x, y = 0;
        uint32_t g;
        switch (src) {                                /* SRC into the ring */
        case 0u: x = osc_saw(ph, m->inc); break;
        case 1u: x = osc_pulse(ph, m->inc, 0x80000000u); break;
        case 2u: x = sine_i(ph + (uint32_t)(sine_i((uint32_t)fmph) * 12000)); fmph += (int32_t)(m->inc * 2u); break;
        default: r ^= r << 13; r ^= r >> 17; r ^= r << 5; x = (int32_t)(int16_t)(r >> 16); break;
        }
        ph += m->inc;
        x >>= 1;
        if (wstep || !(v->s[5] & 256)) {              /* (STRETCH 0: the ring frozen once it is full) */
            b[w] = (int16_t)x;
            w = w + 1u == SH_LEN ? 0u : w + 1u;
            if (!w)
                v->s[5] |= 256;
        }
        walk += wstep;                                /* STRETCH: the walk along the ring */
        while (walk >= (SH_LEN << 16))
            walk -= SH_LEN << 16;
        if (step && ++sc >= step) {                   /* SYNC: the grains restart on the grid, still half apart */
            sc = 0;
            ag[0] = glen;
            ag[1] = glen / 2;
        }
        for (g = 0; g < 2u; g++) {                    /* two grains, half a grain apart */
            int32_t e, a, k, aa;
            uint32_t q;
            if (ag[g] >= glen) {
                ag[g] = 0;
                sh_start(p, v, &rp[g], g, walk);
            }
            aa = ag[g];
            e = (32767 - sine_i((uint32_t)aa * (0xFFFFFFFFu / (uint32_t)glen) + 0x40000000u)) >> 1;   /* a Hann window */
            q = rp[g] >> 16;
            a = b[q];
            k = b[q + 1u == SH_LEN ? 0u : q + 1u];
            a += ((k - a) * (int32_t)((rp[g] >> 1) & 0x7FFF)) >> 15;
            y += (a * e) >> 15;
            if ((v->s[5] >> g) & 1) {                 /* (backwards, round the ring) */
                rp[g] -= (uint32_t)pr;
                if (rp[g] >= (uint32_t)SH_LEN << 16)
                    rp[g] += (uint32_t)SH_LEN << 16;
            } else {
                rp[g] += (uint32_t)pr;
                if (rp[g] >= (uint32_t)SH_LEN << 16)
                    rp[g] -= (uint32_t)SH_LEN << 16;
            }
            ag[g]++;
        }
        y = (x * (127 - mix) + y * mix) >> 7;
        out[i] += voice_amp(soft_knee(clamp(y << 1, -65535, 65535), 24000), m, i) << 1;
    }
    v->ph[0] = ph;
    v->ph[1] = rp[0];
    v->ph[2] = rp[1];
    v->s[0] = ag[0];
    v->s[1] = ag[1];
    v->s[2] = (int32_t)w;
    v->s[3] = walk;
    v->s[4] = (int32_t)r;
    v->s[6] = sc;
    v->s[7] = fmph;
}

/* {SRC, GRAIN, STRETCH, PITCH, STUTTER, TEAR, SYNC, MIX}; SRC 0 SAW 1 SQR 2 FM 3 NOISE; SYNC 0 OFF 1 1/16 2 1/32 3 1/64 */
static const preset_t SHRED_PRESETS[] = {
    {"SHRED", {0, 40, 64, 0, 40, 30, 2, 110}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 20)},
    {"TIME TEAR", {1, 60, 20, 0, 20, 70, 0, 120}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 25, 25)},
    {"FREEZE", {2, 100, 0, 0, 0, 10, 0, 127}, {10, 0, 127, 40}, 0, 1, FX(0, 20, 20, 40)},
    {"STUTTER", {0, 30, 64, 0, 100, 0, 3, 120}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 20, 15)},
    {"UP TEARS", {2, 30, 64, 12, 30, 50, 2, 110}, {0, 0, 127, 20}, 0, 1, FX(0, 0, 30, 25)},
    {"DOWN TEARS", {1, 50, 64, -12, 30, 50, 1, 110}, {0, 0, 127, 20}, 0, 1, FX(10, 0, 25, 25)},
    {"NOISE SHRED", {3, 20, 90, 0, 50, 90, 3, 120}, {0, 0, 127, 15}, 0, 1, FX(10, 0, 20, 20)},
    {"STRETCH PAD", {2, 127, 10, 0, 10, 20, 0, 127}, {60, 0, 127, 90}, 0, 1, FX(0, 40, 25, 70)},
};

static const engine_t ENG_SHRED = {
    .name = "SHRED",
    .id = EID_SHRED,
    .cc = 0x45,                  /* CC74 STUTTER .. GRAIN, CC71 TEAR */
    .cats = "FFPFFFFP",
    .page_title = {"GRAIN", "TEAR"},
    .edit = {
        {"SRC", F_ENUM, 0, 3, 0, N_SH_SRC, 0},
        {"GRAIN", F_PCT, 0, 127, 40, 0, 0},
        {"STRETCH", F_PCT, 0, 127, 64, 0, 0},
        {"PITCH", F_INT, -12, 12, 0, 0, "st"},
        {"STUTTER", F_PCT, 0, 127, 40, 0, 0},
        {"TEAR", F_PCT, 0, 127, 30, 0, 0},
        {"SYNC", F_ENUM, 0, 3, 2, N_SH_SYNC, 0},
        {"MIX", F_PCT, 0, 127, 110, 0, 0},
    },
    .presets = SHRED_PRESETS,
    .npresets = NELEM(SHRED_PRESETS),
    .poly = 1,
    .note_on = shred_note_on,
    .render = shred_render,
    .knob = {P_E1, P_E2, P_E4, P_E5},
};
