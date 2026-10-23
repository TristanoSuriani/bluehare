/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* RHYTHM (2026-10-23, the user's "add 808 and 909 kits"): the two analog drum machines of the 80s, after the TR-808
 * of 1980 and the TR-909 of 1983, synthesized the way their circuits made their drums (Blue Hare's own design, in
 * float on the FPU: no samples, no circuit modelled part for part). MODEL picks the machine's voicing; every drum
 * is made anew at each hit:
 *   KICK   808: a ringing sine (the bridged-T's), a small fall in pitch, KICK its length (~0.1 .. 1.5 s: the boom).
 *          909: a sine swept down from ~4x its pitch over ~15 ms, a click (a pulse and a burst of noise), saturated.
 *   SNARE  two ringing sines (~180 and ~330 Hz; 909: a little fall in pitch) and high-passed noise, SNAP its amount.
 *   CLAP   noise band-passed around ~1 kHz, four bursts ~10 ms apart, then the reverb's tail.
 *   RIM    two short sines (~455 and ~1.7 kHz), high-passed. CLAVE: one at ~2.5 kHz.
 *   COWBL  two squares (540 and 800 Hz) band-passed, a fast fall then a slower one (808's; the 909 had none).
 *   TOMS   sines with a fall in pitch (909: more, and a little noise).
 *   HATS   808: six squares at the circuit's metallic pitches, high-passed; 909: noise and the squares, brighter.
 *          CLOSED chokes a sounding OPEN. CYMBL the squares and noise in two bands, ~1 s.
 * The GM drum notes on every octave of 36 .. 47 (as KIT): C KICK, C# RIM, D SNARE, D# CLAP, E COWBL, F TOM L,
 * F# HAT CL, G TOM M, G# CLAVE, A TOM H, A# HAT OP, B CYMBL. TUNE moves the kit, DECAY every length, TONE the
 * noise's brightness, DRIVE the saturation after, ACC how much the velocity counts (the machines' accent).
 * Its own envelope (engine_t.ownenv), the voice ending with the hit.
 * Voice state: rk_v (the oscillators' phases, the filters, the envelopes), s[0] samples since the hit, s[1] its
 * length, s[4] the drum. */
enum { RK_KICK, RK_RIM, RK_SNARE, RK_CLAP, RK_COWBL, RK_TOML, RK_HATC, RK_TOMM, RK_CLAVE, RK_TOMH, RK_HATO, RK_CYMBL };
static const char *const N_RK_MODEL[] = {"808", "909", 0};
static const float RK_METAL[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};   /* the hats' squares */
typedef struct {
    float ph[6];                 /* the oscillators' phases (turns) */
    float f1, f2, g1, g2;        /* two state-variable filters' states */
    float hp;                    /* the one-pole high-pass's */
    float ea, eb, en;            /* the envelopes: body, second body, noise */
    float da, db, dn;            /* their decay a sample */
    float choke;                 /* 1 .. 0: a CLOSED hat's choke of an OPEN */
    uint32_t nz;                 /* the noise */
} rk_voice_t;
static rk_voice_t rk_v[NPART][NVOICE];
static rk_voice_t *rk_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < NVOICE ? &rk_v[t - trk][i] : 0;
}
static inline float rk_decay(float sec) { return fm_expf(-1.0f / (fm_maxf(sec, 0.002f) * (float)FS)); }
static inline float rk_noise(rk_voice_t *s)
{
    s->nz ^= s->nz << 13;
    s->nz ^= s->nz >> 17;
    s->nz ^= s->nz << 5;
    return (float)(int32_t)s->nz * (1.0f / 2147483648.0f);
}
/* a state-variable filter (zero-delay): its band and its low (the high: x - k band - low) */
static inline float rk_svf(float x, float g, float k, float *z1, float *z2, float *lo)
{
    float a1 = 1.0f / (1.0f + g * (g + k)), v3 = x - *z2, v1 = a1 * (*z1 + g * v3), v2 = *z2 + g * v1;
    *z1 = 2.0f * v1 - *z1;
    *z2 = 2.0f * v2 - *z2;
    *lo = v2;
    return v1;
}
static inline float rk_g(float hz) { return fm_tanf(3.14159265f * fm_minf(hz, 0.45f * (float)FS) / (float)FS); }
static inline float rk_sq(float ph) { return ph - fm_floorf(ph) < 0.5f ? 1.0f : -1.0f; }

static void rhythm_note_on(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    rk_voice_t *s = rk_of(t, v);
    int32_t k = ((int32_t)v->note - 36) % 12;
    float dec = 0.25f * fm_exp2f((float)p[P_E2] * (4.0f / 127.0f)), kick = 0.08f + (float)p[P_E3] * (1.4f / 127.0f);
    uint32_t i, m909 = p[P_E0] == 1;
    k = k < 0 ? k + 12 : k;
    v->s[0] = 0;
    v->s[4] = k;
    if (!s)
        return;
    memset(s, 0, sizeof *s - sizeof s->nz);
    s->nz = s->nz ? s->nz : 0x9E3779B9u ^ (v->age * 2654435761u) ^ (uint32_t)(v - t->v);
    s->ea = s->eb = s->en = 1.0f;
    s->choke = 1.0f;
    for (i = 0; i < 6u; i++)                          /* (the squares anywhere in their cycles) */
        s->ph[i] = (float)((s->nz >> (i * 5u)) & 31u) * (1.0f / 32.0f);
    switch (k) {
    case RK_KICK:
        s->da = rk_decay(m909 ? 0.3f * kick + 0.05f : kick);
        s->dn = rk_decay(0.003f);
        s->db = rk_decay(m909 ? 0.015f : 0.006f);     /* (the pitch's fall) */
        break;
    case RK_SNARE:
        s->da = rk_decay(0.06f * dec);
        s->db = rk_decay(0.04f * dec);
        s->dn = rk_decay((m909 ? 0.16f : 0.11f) * dec);
        break;
    case RK_CLAP:
        s->dn = rk_decay(0.012f);
        s->da = rk_decay(0.13f * dec);
        break;
    case RK_RIM:
    case RK_CLAVE:
        s->da = rk_decay(k == RK_RIM ? 0.008f : 0.022f);
        break;
    case RK_COWBL:
        s->da = rk_decay(0.012f);
        s->db = rk_decay(0.16f * dec);
        break;
    case RK_TOML:
    case RK_TOMM:
    case RK_TOMH:
        s->da = rk_decay((m909 ? 0.22f : 0.32f) * dec);
        s->db = rk_decay(m909 ? 0.03f : 0.05f);
        s->dn = rk_decay(0.02f);
        break;
    case RK_HATC:
        s->da = rk_decay((m909 ? 0.05f : 0.04f) * dec);
        for (i = 0; i < NVOICE; i++)                  /* CLOSED chokes a sounding OPEN */
            if (&t->v[i] != v && t->v[i].active && t->v[i].s[4] == RK_HATO) {
                rk_voice_t *o = rk_of(t, &t->v[i]);
                if (o)
                    o->choke = 0.999f;
            }
        break;
    case RK_HATO:
        s->da = rk_decay((m909 ? 0.35f : 0.28f) * dec);
        break;
    default:                                          /* CYMBL */
        s->da = rk_decay((m909 ? 0.9f : 1.1f) * dec);
        s->db = rk_decay(0.15f * dec);
        break;
    }
    v->s[1] = (int32_t)((float)FS * (k == RK_KICK ? (m909 ? 0.3f * kick + 0.05f : kick) * 6.0f :
                                     k == RK_CYMBL ? 6.5f * dec : 2.5f * dec)) + 256;
    v->s[1] = v->s[1] > 4 * (int32_t)FS ? 4 * (int32_t)FS : v->s[1];
}

static int rhythm_done(track_t *t, voice_t *v)
{
    rk_voice_t *s = rk_of(t, v);
    (void)t;
    return v->s[0] >= v->s[1] || (s && s->choke < 0.001f);
}

static void rhythm_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    rk_voice_t *s = rk_of(t, v);
    int32_t k = v->s[4], age = v->s[0];
    uint32_t i, j, m909 = p[P_E0] == 1;
    float tune = fm_exp2f((float)p[P_E1] * (1.0f / 12.0f)), tone = (float)p[P_E5] * (1.0f / 127.0f);
    float snap = (float)p[P_E4] * (1.0f / 127.0f), drive = 1.0f + (float)p[P_E6] * (3.0f / 127.0f);
    float acc = 1.0f - (float)p[P_E7] * (1.0f / 127.0f) * (127.0f - (float)v->vel) * (1.0f / 127.0f);
    float rfs = 1.0f / (float)FS, gain = 0.5f * acc, lo, ga, gb, gk;
    if (!s)
        return;
    if (m909 && (k == RK_HATC || k == RK_HATO || k == RK_CYMBL))
        ga = rk_g(9000.0f + 4000.0f * tone);
    else
        ga = rk_g(k == RK_CLAP ? 1000.0f + 600.0f * tone : k == RK_COWBL ? 2600.0f : 7000.0f + 5000.0f * tone);
    gb = rk_g(k == RK_SNARE ? 2500.0f + 5000.0f * tone : 3500.0f);
    gk = rk_g(150.0f * tune);
    for (i = 0; i < n; i++) {
        float x = 0.0f, nz = rk_noise(s), f;
        switch (k) {
        case RK_KICK:
            f = 49.0f * tune * (1.0f + (m909 ? 3.0f : 0.25f) * s->eb);
            s->ph[0] += f * rfs;
            x = fm_sin_turns(s->ph[0]) * s->ea;
            if (m909) {                               /* the click: a pulse and a burst of noise, high-passed */
                float c = (age < 40 ? 0.6f : 0.0f) + nz * s->en * 0.5f;
                s->hp += 0.15f * (c - s->hp);
                x = po_sat(1.6f * (x + 0.5f * (c - s->hp)));
            } else
                x += (age < 24 ? 0.25f : 0.0f);
            s->ea *= s->da;
            s->eb *= s->db;
            s->en *= s->dn;
            break;
        case RK_SNARE:
            f = tune * (1.0f + (m909 ? 0.15f * s->eb : 0.0f));
            s->ph[0] += 180.0f * f * rfs;
            s->ph[1] += 330.0f * f * rfs;
            x = (0.6f * fm_sin_turns(s->ph[0]) * s->ea + 0.4f * fm_sin_turns(s->ph[1]) * s->eb) * (1.1f - 0.6f * snap);
            rk_svf(nz, gb, 1.4f, &s->f1, &s->f2, &lo);
            x += (nz - lo) * s->en * (0.25f + 0.9f * snap);
            s->ea *= s->da;
            s->eb *= s->db;
            s->en *= s->dn;
            break;
        case RK_CLAP: {                               /* four bursts ~10 ms apart, then the tail */
            int32_t b = age / (int32_t)(FS / 100u);
            float e = b < 4 ? fm_expf(-(float)(age - b * (int32_t)(FS / 100u)) * (1.0f / (0.004f * (float)FS))) : 0.0f;
            x = rk_svf(nz, ga, 0.8f, &s->f1, &s->f2, &lo) * (1.6f * e + (b >= 3 ? 1.2f * s->ea : 0.0f));
            if (b >= 3)
                s->ea *= s->da;
            break;
        }
        case RK_RIM:
            s->ph[0] += 455.0f * tune * rfs;
            s->ph[1] += 1667.0f * tune * rfs;
            x = (fm_sin_turns(s->ph[0]) + fm_sin_turns(s->ph[1])) * s->ea;
            s->hp += 0.2f * (x - s->hp);
            x = 1.4f * (x - s->hp);
            s->ea *= s->da;
            break;
        case RK_CLAVE:
            s->ph[0] += 2500.0f * tune * rfs;
            x = fm_sin_turns(s->ph[0]) * s->ea * 0.9f;
            s->ea *= s->da;
            break;
        case RK_COWBL:
            s->ph[0] += 540.0f * tune * rfs;
            s->ph[1] += 800.0f * tune * rfs;
            x = rk_svf(rk_sq(s->ph[0]) + rk_sq(s->ph[1]), ga, 1.2f, &s->f1, &s->f2, &lo) *
                (0.35f * s->ea + 0.4f * s->eb);
            s->ea *= s->da;
            s->eb *= s->db;
            break;
        case RK_TOML:
        case RK_TOMM:
        case RK_TOMH:
            f = (k == RK_TOML ? 95.0f : k == RK_TOMM ? 135.0f : 185.0f) * tune * (1.0f + (m909 ? 0.6f : 0.3f) * s->eb);
            s->ph[0] += f * rfs;
            x = fm_sin_turns(s->ph[0]) * s->ea;
            if (m909)
                x += rk_svf(nz, gk, 1.0f, &s->f1, &s->f2, &lo) * s->en * 0.3f;
            s->ea *= s->da;
            s->eb *= s->db;
            s->en *= s->dn;
            break;
        default: {                                    /* the hats, the cymbal: the six squares, the noise */
            float mt = 0.0f, bright;
            for (j = 0; j < 6u; j++) {
                s->ph[j] += RK_METAL[j] * tune * rfs;
                mt += rk_sq(s->ph[j]);
            }
            mt *= 1.0f / 6.0f;
            if (m909)
                mt = 0.45f * mt + 0.8f * nz;
            else if (k == RK_CYMBL)
                mt = 0.85f * mt + 0.25f * nz;
            bright = rk_svf(mt, ga, 0.9f, &s->f1, &s->f2, &lo);
            s->hp += 0.35f * (bright - s->hp);
            x = (bright - s->hp) * (m909 ? 2.2f : 4.0f);   /* (808's squares: quieter through the bands) */
            if (k == RK_CYMBL) {
                rk_svf(mt, gb, 1.0f, &s->g1, &s->g2, &lo);
                x = x * s->ea + (mt - lo) * s->eb * 0.6f;
                s->eb *= s->db;
            } else
                x *= s->ea;
            s->ea *= s->da;
            if (s->choke < 1.0f)
                s->choke *= 0.995f;                   /* (~5 ms) */
            x *= s->choke;
            break;
        }
        }
        if (age < 8)                                  /* (no click at the start but the drum's own) */
            x *= (float)(age + 1) * 0.125f;
        if (p[P_E6])
            x = po_sat(x * drive) * (2.0f / (1.0f + drive));   /* (louder as it drives, a little) */
        out[i] += voice_amp((int32_t)fm_clampf(x * gain * 32767.0f, -32767.0f, 32767.0f), m, i) << 1;
        age++;
    }
    for (j = 0; j < 6u; j++)
        s->ph[j] -= fm_floorf(s->ph[j]);
    s->f1 = fm_flush(s->f1);
    s->f2 = fm_flush(s->f2);
    s->g1 = fm_flush(s->g1);
    s->g2 = fm_flush(s->g2);
    s->hp = fm_flush(s->hp);
    v->s[0] = age;
}

static int32_t rhythm_keys(const track_t *t, uint32_t k) { return drum_keys(t, k); }

/* {MODEL, TUNE, DECAY, KICK, SNAP, TONE, DRIVE, ACC}. The GM notes on every octave of 36 .. 47: KICK RIM SNARE CLAP
 * COWBL TOM-L HAT-CL TOM-M CLAVE TOM-H HAT-OP CYMBL */
static const preset_t RHYTHM_PRESETS[] = {
    {"808 KIT", {0, 0, 64, 70, 60, 64, 0, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 20)},
    {"808 BOOM", {0, -2, 70, 127, 50, 50, 20, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
    {"808 TIGHT", {0, 1, 35, 30, 80, 80, 0, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 10)},
    {"ELECTRO 808", {0, 0, 55, 90, 70, 90, 30, 80}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 20, 25)},
    {"MIAMI BASS", {0, -4, 75, 127, 60, 60, 45, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 15, 20)},
    {"909 KIT", {1, 0, 64, 60, 70, 70, 20, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 20)},
    {"909 HARD", {1, 0, 50, 55, 85, 85, 90, 80}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
    {"909 SHORT", {1, 2, 30, 30, 75, 90, 30, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 10)},
    {"ACID HOUSE", {1, -1, 60, 75, 60, 60, 40, 90}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 20, 30)},
    {"DETROIT", {1, -3, 75, 90, 55, 55, 25, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 25, 35)},
    {"WAREHOUSE", {1, 0, 70, 80, 90, 75, 110, 70}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 15, 50)},
};

static const engine_t ENG_RHYTHM = {
    .name = "RHYTHM",
    .id = EID_RHYTHM,
    .cc = 0x34,                  /* CC74 DECAY, CC71 KICK */
    .cats = "DDDDDDDDDDD",
    .page_title = {"KIT", "ANALOG"},
    .edit = {
        {"MODEL", F_ENUM, 0, 1, 0, N_RK_MODEL, 0},
        {"TUNE", F_INT, -12, 12, 0, 0, "st"},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"KICK", F_PCT, 0, 127, 70, 0, 0},
        {"SNAP", F_PCT, 0, 127, 60, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
        {"ACC", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = RHYTHM_PRESETS,
    .npresets = NELEM(RHYTHM_PRESETS),
    .poly = 8,
    .note_on = rhythm_note_on,
    .render = rhythm_render,
    .keys = rhythm_keys,
    .knob = {P_E2, P_E3, P_E4, P_E5},
    .ownenv = 1,
    .oneshot = 1,                /* (a note-off leaves the hit alone: a sequencer's short notes) */
    .done = rhythm_done,
};
