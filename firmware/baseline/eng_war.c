/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* WAR (1.5, FOUNDRY): an industrial kit (Blue Hare's own). Every key from C2 another drum, made anew at each hit
 * (DRUM's map; the octave above the same twelve again): KICK, HARD (a harder kick), SNARE, CLAP, HAT, OPEN
 * (an open hat), TOM L, TOM H, METAL, ANVIL, CRASH, PEDAL. Each is a pair of operators (a carrier, a modulator RATIO
 * times it: FM, METAL its index), a pitch falling fast (~25 ms, whatever DECAY) from PUNCH semitones above, and noise
 * (NOISE), cut short or rung long by DECAY, through DIST (a folder pushed into clipping: the war), TONE (a low-pass)
 * and CRUSH last. TUNE moves the kit.
 * Its own envelope (engine_t.ownenv: the velocity the level), the voice ending with the hit.
 * Voice state: ph[0] the carrier, ph[1] the modulator, s[0] samples since the hit, s[1] its length, s[2] the noise,
 * s[3] the noise's colour, s[4] the drum, s[5] the level (Q15), s[6] CRUSH's held sample, s[7] its count; ph[2] TONE's
 * low-pass. */
typedef struct { int8_t note, sweep; uint16_t ratio, index; uint8_t noise; uint16_t ms; uint8_t nlp; } war_drum_t;
static const war_drum_t WAR_D[12] = {   /* {note, the pitch's fall (st), RATIO Q8, index, noise 0..127, ms, noise colour} */
    {29, 24, 256, 1, 4, 420, 4}, {31, 28, 358, 4, 10, 340, 6}, {45, 7, 591, 2, 80, 220, 55}, {57, 0, 256, 0, 127, 260, 45},
    {88, 0, 361, 8, 80, 70, 100}, {88, 0, 361, 8, 80, 380, 95}, {38, 9, 256, 1, 8, 340, 15}, {45, 9, 256, 1, 8, 280, 15},
    {60, 0, 909, 7, 14, 480, 40}, {64, 3, 707, 8, 20, 700, 35}, {81, 0, 443, 10, 100, 1100, 80}, {85, 0, 395, 9, 85, 130, 85},
};

static void war_note_on(track_t *t, voice_t *v)
{
    int32_t k = ((int32_t)v->note - 36) % 12, len;
    const war_drum_t *d;
    k = k < 0 ? k + 12 : k;
    d = &WAR_D[k];
    len = (int32_t)d->ms * (int32_t)FS / 1000;
    v->s[1] = (len * (16 + t->p[P_E1] * 2)) >> 6;     /* DECAY: a quarter .. four times */
    v->s[1] = v->s[1] < 64 ? 64 : v->s[1];
    v->s[0] = 0;
    v->s[4] = k;
    v->s[5] = 4096 + v->vel * 225;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[3] = v->s[6] = v->s[7] = 0;
    if (!v->s[2])
        v->s[2] = (int32_t)(0x2545F491u ^ (v->age * 2654435761u));
}

static int war_done(track_t *t, voice_t *v) { (void)t; return v->s[0] >= v->s[1]; }

static void war_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t k = v->s[4], age = v->s[0], len = v->s[1], lp = v->s[3], amp = v->s[5], hold = v->s[6], hc = v->s[7];
    const war_drum_t *d = &WAR_D[k % 12];
    uint32_t i, nz = (uint32_t)v->s[2], ph = v->ph[0], mph = v->ph[1], inv = (1u << 29) / (uint32_t)len;
    int32_t base16 = (d->note + p[P_E0]) * 16, idx = d->index * (16 + p[P_E3]) / 2;
    int32_t noise = d->noise * (32 + p[P_E4]) / 96, sweep = d->sweep * (16 + p[P_E5]) / 80, dist = 256 + p[P_E6] * 24;
    int32_t kt = clamp(2500 + p[P_E2] * 236, 2500, 32767), tl = (int32_t)v->ph[2], pe0, pe1, pd, nk = 1500 + d->nlp * 240, crush = p[P_E7];
    int32_t mask = crush ? ~((1 << (crush / 12)) - 1) : -1, hold_n = 1 + crush / 20;
    pe0 = (int32_t)((32767u * 1100u) / (1100u + (uint32_t)age));   /* the pitch's fall: ~25 ms, hyperbolic (once a block, */
    pe1 = (int32_t)((32767u * 1100u) / (1100u + (uint32_t)age + n));   /* between the block's ends) */
    pd = (pe1 - pe0) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t w, e, ef, x, nzv, cm;
        uint32_t inc, minc;
        if (age >= len)
            break;
        w = (int32_t)(((uint32_t)(len - age) * inv) >> 14);   /* 1 .. 0 over the hit, Q15 */
        e = (w * w) >> 15;
        ef = (pe0 * pe0) >> 15;
        pe0 += pd;
        if (age < 32)
            e = (e * age) >> 5;
        inc = pitch_inc((uint32_t)clamp(base16 + ((sweep * 16 * ef) >> 15), 0, 2047));
        minc = (uint32_t)(((uint64_t)inc * d->ratio) >> 8);
        cm = sine_i(mph);
        x = sine_i(ph + (uint32_t)(cm * idx * 4));
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
        x = (x * (127 - noise) + lp * noise) >> 7;
        x = (x * e) >> 15;
        x = softclip((x * dist) >> 8);                /* DIST */
        if (crush) {                                  /* CRUSH */
            if (--hc <= 0) {
                hc = hold_n;
                hold = x & mask;
            }
            x = hold;
        }
        tl += ((x - tl) * kt) >> 15;                  /* TONE */
        x = tl;
        out[i] += voice_amp(clamp((x * amp) >> 15, -32767, 32767), m, i) << 1;
        ph += inc;
        mph += minc;
        age++;
    }
    v->s[0] = age;
    v->s[2] = (int32_t)nz;
    v->s[3] = lp;
    v->s[6] = hold;
    v->s[7] = hc;
    v->ph[0] = ph;
    v->ph[1] = mph;
    v->ph[2] = (uint32_t)tl;
}

static int32_t war_keys(const track_t *t, uint32_t k) { return drum_keys(t, k); }

/* {TUNE, DECAY, TONE, METAL, NOISE, PUNCH, DIST, CRUSH}. The keys from C2: KICK HARD SNARE CLAP HAT OPEN TOM-L TOM-H
 * METAL ANVIL CRASH PEDAL */
static const preset_t WAR_PRESETS[] = {
    {"WAR KIT", {0, 64, 100, 64, 64, 64, 40, 0}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 20)},
    {"INDUSTRIAL", {0, 70, 110, 90, 70, 70, 80, 0}, {0, 0, 127, 20}, 0, 0, FX(20, 0, 10, 25)},
    {"BUNKER", {-3, 80, 70, 50, 50, 80, 60, 0}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 10, 40)},
    {"HEAVY", {-5, 90, 80, 60, 50, 100, 110, 0}, {0, 0, 127, 20}, 0, 0, FX(30, 0, 10, 30)},
    {"WARZONE", {0, 100, 90, 100, 90, 90, 127, 30}, {0, 0, 127, 20}, 0, 0, FX(40, 0, 20, 40)},
    {"DEATH KIT", {-4, 90, 70, 50, 60, 110, 120, 0}, {0, 0, 127, 20}, 0, 0, FX(40, 0, 10, 30)},
    {"EBM KIT", {0, 40, 110, 40, 50, 70, 30, 0}, {0, 0, 127, 20}, 0, 0, FX(10, 0, 15, 15)},
    {"DEMOLITION", {-2, 100, 90, 90, 100, 120, 127, 20}, {0, 0, 127, 20}, 0, 0, FX(50, 0, 10, 40)},
    {"IRON FIST", {-1, 70, 90, 70, 60, 100, 90, 0}, {0, 0, 127, 20}, 0, 0, FX(30, 0, 10, 25)},
};

static const engine_t ENG_WAR = {
    .name = "WAR",
    .id = EID_WAR,
    .cc = 0x27,                  /* CC74 DECAY, CC71 DIST */
    .cats = "DDDDDDDDD",
    .page_title = {"KIT", "WAR"},
    .edit = {
        {"TUNE", F_INT, -24, 24, 0, 0, "st"},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 100, 0, 0},
        {"METAL", F_PCT, 0, 127, 64, 0, 0},
        {"NOISE", F_PCT, 0, 127, 64, 0, 0},
        {"PUNCH", F_PCT, 0, 127, 64, 0, 0},
        {"DIST", F_PCT, 0, 127, 40, 0, 0},
        {"CRUSH", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = WAR_PRESETS,
    .npresets = NELEM(WAR_PRESETS),
    .poly = 8,
    .note_on = war_note_on,
    .render = war_render,
    .keys = war_keys,
    .knob = {P_E1, P_E6, P_E3, P_E5},
    .ownenv = 1,
    .done = war_done,
};
