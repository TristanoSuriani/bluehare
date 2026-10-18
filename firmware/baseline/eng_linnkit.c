/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* LINNKIT (1.5, WAREHOUSE): the 80s sampled drum machine, after the LinnDrum (Blue Hare's own, synthesized: no
 * samples). Every key from C2 another drum, made anew at each hit (DRUM's map; the octave above the same twelve again):
 * KICK, SNARE, RIM, CLAP, HAT, OPEN (an open hat), TOM L, TOM M, TOM H, CONGA, COWBELL, TAMB. Each is a body (a sine,
 * or a triangle for the snare and the rim, two squares for the cowbell) whose pitch falls from BODY semitones above
 * over its first ~20 ms, and noise (coloured; the hats' and the tambourine's high-passed), mixed by SNAP; DECAY cuts it
 * short or lets it ring, GATE holds it up and cuts it at the end (the 80s gated snare), TONE a low-pass, CHAR the
 * sampler's grit: the rate down towards ~12 kHz and the bits towards 8 (the machine's own were 8-bit samples at ~28
 * kHz), VEL how much the velocity counts. TUNE moves the kit.
 * Its own envelope (engine_t.ownenv: the velocity the level), the voice ending with the hit.
 * Voice state: ph[0] the body, ph[1] the cowbell's second square, ph[2] TONE's low-pass; s[0] samples since the hit,
 * s[1] its length, s[2] the noise, s[3] the noise's colour (a low-pass), s[4] the drum, s[5] the level (Q15), s[6]
 * CHAR's held sample, s[7] its rate's phase (Q16). */
typedef struct { int8_t note, sweep; uint8_t body, noise, nlp, hp; uint16_t ms; } ln_drum_t;
enum { LN_SINE, LN_TRI, LN_BELL, LN_NONE };
static const ln_drum_t LN_D[12] = {     /* {note, the pitch's fall (st), body, noise 0..127, colour, high-passed, ms} */
    {33, 14, LN_SINE, 6, 20, 0, 380},   /* KICK */
    {52, 5, LN_TRI, 90, 70, 0, 260},    /* SNARE */
    {77, 0, LN_TRI, 30, 100, 0, 45},    /* RIM */
    {60, 0, LN_NONE, 127, 60, 0, 300},  /* CLAP */
    {0, 0, LN_NONE, 127, 120, 1, 60},   /* HAT */
    {0, 0, LN_NONE, 127, 115, 1, 420},  /* OPEN */
    {43, 6, LN_SINE, 10, 30, 0, 420},   /* TOM L */
    {48, 6, LN_SINE, 10, 30, 0, 380},   /* TOM M */
    {53, 6, LN_SINE, 10, 30, 0, 340},   /* TOM H */
    {62, 3, LN_SINE, 4, 40, 0, 220},    /* CONGA */
    {73, 0, LN_BELL, 0, 0, 0, 300},     /* COWBELL (~540 and ~800 Hz) */
    {0, 0, LN_NONE, 127, 127, 1, 160},  /* TAMB */
};

static void linnkit_note_on(track_t *t, voice_t *v)
{
    int32_t k = ((int32_t)v->note - 36) % 12, len;
    k = k < 0 ? k + 12 : k;
    len = (int32_t)LN_D[k].ms * (int32_t)FS / 1000;
    v->s[1] = (len * (16 + t->p[P_E1] * 2)) >> 6;     /* DECAY: a quarter .. four times */
    v->s[1] = v->s[1] < 64 ? 64 : v->s[1];
    v->s[0] = 0;
    v->s[4] = k;
    v->s[5] = 32767 - ((127 - v->vel) * t->p[P_E7] * 2);   /* VEL: how much the velocity takes off */
    v->s[5] = v->s[5] < 2048 ? 2048 : v->s[5];
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[3] = v->s[6] = v->s[7] = 0;
    if (!v->s[2])
        v->s[2] = (int32_t)(0x6A09E667u ^ (v->age * 2654435761u));
}

static int linnkit_done(track_t *t, voice_t *v) { (void)t; return v->s[0] >= v->s[1]; }

static void linnkit_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t k = v->s[4], age = v->s[0], len = v->s[1], lp = v->s[3], amp = v->s[5], hold = v->s[6], acc = v->s[7];
    const ln_drum_t *d = &LN_D[k % 12];
    uint32_t i, nz = (uint32_t)v->s[2], ph = v->ph[0], ph2 = v->ph[1], inv = (1u << 29) / (uint32_t)len;
    int32_t base16 = (d->note + p[P_E0]) * 16, sweep = d->sweep * (8 + p[P_E4]) / 72, snap = p[P_E3];
    int32_t noise = clamp(d->noise + (snap - 64), 0, 127), nk = 1500 + d->nlp * 240, gate = p[P_E5];
    int32_t kt = clamp(2500 + p[P_E2] * 236, 2500, 32767), tl = (int32_t)v->ph[2], ch = p[P_E6];
    int32_t step = 65536 - ch * 320, mask = ch ? ~((1 << (ch / 16)) - 1) : -1;   /* CHAR: ~12 kHz, 8 bits at 127 */
    int32_t pe0, pe1, pd, gl = len - (len >> 3);
    uint32_t ginv = 65536u / (uint32_t)(len - gl + 1);   /* (GATE's cut: (len - age) / (len - gl), Q16) */
    pe0 = (int32_t)((32767u * 900u) / (900u + (uint32_t)age));   /* the pitch's fall: ~20 ms, hyperbolic (once a block, */
    pe1 = (int32_t)((32767u * 900u) / (900u + (uint32_t)age + n));   /* between the block's ends) */
    pd = (pe1 - pe0) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t w, e, ef, x = 0, nzv;
        uint32_t inc;
        if (age >= len)
            break;
        w = (int32_t)(((uint32_t)(len - age) * inv) >> 14);   /* 1 .. 0 over the hit, Q15 */
        e = (w * w) >> 15;
        if (gate) {                                   /* GATE: held up towards full, then cut over the last eighth */
            e += ((32767 - e) * gate) >> 7;
            if (age >= gl)
                e = (e * (int32_t)(((uint32_t)(len - age) * ginv) >> 8)) >> 8;
        }
        if (age < 24)
            e = (e * age) / 24;
        ef = (pe0 * pe0) >> 15;
        pe0 += pd;
        if (d->body != LN_NONE) {
            inc = pitch_inc((uint32_t)clamp(base16 + ((sweep * 16 * ef) >> 15), 0, 2047));
            if (d->body == LN_SINE)
                x = sine_i(ph);
            else if (d->body == LN_TRI)
                x = (int32_t)((ph >> 16) < 32768u ? (ph >> 15) : 131071u - (ph >> 15)) - 32768;
            else {                                    /* two squares, ~1.48 apart */
                x = ((ph & 0x80000000u) ? 12000 : -12000) + ((ph2 & 0x80000000u) ? 12000 : -12000);
                ph2 += (uint32_t)(((uint64_t)inc * 379u) >> 8);
            }
            ph += inc;
        }
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        nzv = (int32_t)(int16_t)(nz >> 16);
        if (k % 12 == 3) {                            /* CLAP: three bursts, then the tail */
            int32_t b = age / 441;
            if (b < 6 && (b & 1))
                nzv = 0;
        }
        lp += ((nzv - lp) * nk) >> 15;
        nzv = d->hp ? nzv - lp : lp;                  /* (the hats, the tambourine: what the colour leaves out) */
        x = (x * (127 - noise) + nzv * noise) >> 7;
        x = (x * e) >> 15;
        if (ch) {                                     /* CHAR: the rate down, the bits down */
            acc += step;
            if (acc >= 65536) {
                acc -= 65536;
                hold = x & mask;
            }
            x = hold;
        }
        tl += ((x - tl) * kt) >> 15;                  /* TONE */
        out[i] += voice_amp(clamp((tl * amp) >> 15, -32767, 32767), m, i) << 1;
        age++;
    }
    v->s[0] = age;
    v->s[2] = (int32_t)nz;
    v->s[3] = lp;
    v->s[6] = hold;
    v->s[7] = acc;
    v->ph[0] = ph;
    v->ph[1] = ph2;
    v->ph[2] = (uint32_t)tl;
}

static int32_t linnkit_keys(const track_t *t, uint32_t k) { return drum_keys(t, k); }

/* {TUNE, DECAY, TONE, SNAP, BODY, GATE, CHAR, VEL}. The keys from C2: KICK SNARE RIM CLAP HAT OPEN TOM-L TOM-M TOM-H
 * CONGA COWBELL TAMB */
static const preset_t LINNKIT_PRESETS[] = {
    {"LINN KIT", {0, 64, 100, 64, 64, 0, 64, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 25)},
    {"GATED 80S", {0, 90, 100, 70, 70, 100, 60, 60}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 70)},
    {"ELECTRO", {0, 40, 115, 60, 80, 0, 56, 50}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 15, 20)},
    {"DEEP LINN", {-4, 85, 85, 56, 80, 0, 64, 60}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 30)},
    {"BIG ROOM 84", {-1, 80, 100, 66, 70, 60, 60, 60}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 15, 85)},
    {"ITALO", {0, 60, 110, 64, 72, 20, 60, 50}, {0, 0, 127, 20}, 0, 0, FX(0, 10, 25, 40)},
    {"CRUSHED LINN", {0, 55, 90, 70, 64, 0, 127, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
    {"PITCHED DOWN", {-6, 75, 90, 60, 72, 0, 64, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 25)},
};

static const engine_t ENG_LINNKIT = {
    .name = "LINNKIT",
    .id = EID_LINNKIT,
    .cc = 0x26,                  /* CC74 DECAY, CC71 GATE */
    .cats = "DDDDDDDD",
    .page_title = {"KIT", "LINN"},
    .edit = {
        {"TUNE", F_INT, -24, 24, 0, 0, "st"},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 100, 0, 0},
        {"SNAP", F_PCT, 0, 127, 64, 0, 0},
        {"BODY", F_PCT, 0, 127, 64, 0, 0},
        {"GATE", F_PCT, 0, 127, 0, 0, 0},
        {"CHAR", F_PCT, 0, 127, 64, 0, 0},
        {"VEL", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = LINNKIT_PRESETS,
    .npresets = NELEM(LINNKIT_PRESETS),
    .poly = 8,
    .note_on = linnkit_note_on,
    .render = linnkit_render,
    .keys = linnkit_keys,
    .knob = {P_E1, P_E5, P_E3, P_E6},
    .ownenv = 1,
    .done = linnkit_done,
};
