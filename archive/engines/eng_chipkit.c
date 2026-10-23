/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* CHIPKIT (1.5, CHIP): the drums of the 8-bit consoles, made as their sound chips made them (Blue Hare's own: no
 * chip emulated, no samples). Every key from C2 another drum, made anew at each hit (DRUM's map; the octave above the
 * same twelve again): KICK, SNARE, HAT, OPEN (an open hat), TOM L, TOM M, TOM H, CRASH, CLAP, ZAP (a laser), COIN (the
 * two-note pickup), NOISE (a metallic buzz). A drum is the chips' three kinds of channel at once: a 4-bit stepped
 * triangle (a 32-step ramp up and down: kicks and toms, its pitch falling), a pulse (12.5, 25 or 50 %: the snare's
 * body, ZAP, COIN) and the noise channel, a shift register clocked at one of 16 rates, in its long mode (hiss) or its
 * short one (a 93-step loop: metallic). Nothing moves smoothly: the chips' software changed their registers once a
 * frame, so the pitch, the volume (16 steps) and the noise's rate change in steps, RATE times a second.
 * (The register: one bit a step, the feedback of two of its bits; the short mode's taps make it loop sooner.)
 * CHIP picks the console: NES (the 2A03: the stepped triangle, 15-bit noise, 60 frames a second), GB (the Game Boy:
 * its 4-bit wave channel as the triangle, 7-bit short noise, 64 frames), SMS (the Master System's SN76489: squares
 * only, the triangle a square, its volume in 2 dB steps, three noise rates and its periodic noise, tonal, as the
 * short mode) and C64 (the SID's way: a smooth
 * triangle, its 23-bit noise, smooth envelopes, 50 frames). TUNE moves the kit (the noise's rate a step every 3
 * semitones), DECAY shortens or lengthens every drum (a quarter .. four times), SWEEP the pitch falls (0 none, 64 as
 * designed, 127 twice), NOISE how much noise each drum has, RATE the frames (a quarter .. four times the console's),
 * VARY gives every hit its own small differences (pitch, length), VEL how much the velocity counts.
 * Its own envelope (engine_t.ownenv: the velocity the level), the voice ending with the hit.
 * Voice state: ph[0] the tone (the triangle's and the pulse's phase), ph[1] the noise's clock (Q16 steps), ph[2] its
 * register; s[0] samples since the hit, s[1] the drum | frames << 4, s[2] samples to the next frame, s[3] the tone's
 * pitch (1/16 semitone), s[4] the pulse's volume (Q8) | the noise's << 16, s[5] the noise's rate (index, Q4), s[6] the
 * level (Q15), s[7] the hit's pitch offset (1/16 semitone, int8) | its length's (int8) << 8 | the frame length << 16. */
typedef struct {
    int8_t note;                 /* the triangle's / the pulse's note at the hit */
    uint8_t sweep;               /* its fall, 1/4 semitone a frame */
    uint8_t tri;                 /* frames the triangle sounds (0: none) */
    uint8_t pvol, pdec, duty;    /* the pulse: volume 0..15, its fall (1/4 step a frame), duty 0 12.5 % 1 25 % 2 50 % */
    uint8_t nvol, ndec;          /* the noise: volume 0..15, its fall */
    uint8_t nrate, ndrift;       /* its rate (0 highest .. 15 lowest), its drift down (1/16 index a frame) */
    uint8_t nshort;              /* the short mode (metallic) */
    int8_t arp;                  /* COIN: the second note (semitones), from frame 4 */
    uint8_t bursts;              /* CLAP: the noise restarted this many times, a frame apart */
} ck_drum_t;
static const ck_drum_t CK_D[12] = {
    /* note sw tri pvol pdec duty nvol ndec nrate drift short arp bursts */
    {57, 10, 8, 0, 0, 0, 6, 40, 6, 0, 0, 0, 0},     /* KICK: the triangle falling, a click of noise */
    {50, 4, 3, 0, 0, 0, 15, 3, 5, 2, 0, 0, 0},      /* SNARE: a body, then long noise */
    {0, 0, 0, 0, 0, 0, 12, 12, 1, 0, 0, 0, 0},      /* HAT */
    {0, 0, 0, 0, 0, 0, 12, 2, 1, 0, 0, 0, 0},       /* OPEN */
    {45, 3, 14, 0, 0, 0, 4, 16, 6, 0, 0, 0, 0},     /* TOM L */
    {50, 3, 12, 0, 0, 0, 4, 16, 6, 0, 0, 0, 0},     /* TOM M */
    {55, 3, 10, 0, 0, 0, 4, 16, 5, 0, 0, 0, 0},     /* TOM H */
    {0, 0, 0, 0, 0, 0, 15, 1, 3, 2, 0, 0, 0},       /* CRASH: long noise, sinking */
    {0, 0, 0, 0, 0, 0, 14, 6, 4, 0, 0, 0, 3},       /* CLAP: three bursts and the tail */
    {96, 24, 0, 15, 2, 2, 0, 0, 0, 0, 0, 0, 0},     /* ZAP: a pulse diving */
    {83, 0, 0, 15, 1, 1, 0, 0, 0, 0, 0, 5, 0},      /* COIN: B5, then E6 */
    {0, 0, 0, 0, 0, 0, 15, 2, 8, 0, 1, 0, 0},       /* NOISE: the short mode's buzz */
};
static const char *const N_CK_CHIP[] = {"NES", "GB", "SMS", "C64", 0};
enum { CK_NES, CK_GB, CK_SMS, CK_C64 };
static const uint8_t CK_FPS[4] = {60, 64, 60, 50};
/* the noise's 16 rates: steps of the register a second (the 2A03's periods at its 1.79 MHz), Q16 a sample at 44.1
 * kHz: 1789773 / period / 44100 * 65536 */
static const uint32_t CK_NRATE[16] = {664999, 332499, 166249, 83125, 41562, 27708, 20781, 16625,
                                      13167, 10470, 6998, 5235, 3490, 2617, 1307, 653};
static const uint16_t CK_LOG[16] = {0, 1030, 1297, 1633, 2056, 2588, 3258, 4102,   /* SMS: 2 dB a step (Q15) */
                                    5164, 6501, 8184, 10303, 12971, 16330, 20558, 25881};

/* the noise's rate as each chip has it: the NES's 16; the Game Boy's a step darker; the SN76489's three, by the
 * nearest (high, middle, low: its N/512, N/1024, N/2048); the SID's as the NES's */
static int32_t ck_rate(int32_t chip, int32_t r)
{
    r = r < 0 ? 0 : r > 15 ? 15 : r;
    if (chip == CK_GB)
        return r < 15 ? r + 1 : 15;
    if (chip == CK_SMS)
        return r < 4 ? 3 : r < 8 ? 6 : 9;
    return r;
}

static uint32_t ck_rnd(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static void chipkit_note_on(track_t *t, voice_t *v)
{
    int32_t k = ((int32_t)v->note - 36) % 12, chip = t->p[P_E2] & 3, fps, frame, vary = t->p[P_E6];
    uint32_t r = ck_rnd(0x9E3779B9u ^ (v->age * 2654435761u) ^ (uint32_t)v->note);
    const ck_drum_t *d;
    int32_t dp, dl;
    k = k < 0 ? k + 12 : k;
    d = &CK_D[k];
    fps = CK_FPS[chip] * 64;                          /* RATE: a quarter .. four times, the console's at 64 */
    fps = t->p[P_E5] >= 64 ? fps + (fps * 3 * (t->p[P_E5] - 64)) / 63 : fps - (fps * 3 * (64 - t->p[P_E5])) / 256;
    frame = (int32_t)FS * 64 / (fps ? fps : 1);
    frame = clamp(frame, 64, 4096);
    dp = (int32_t)((r & 255u) - 128u) * vary / 512;   /* VARY: the pitch +- a semitone, the length +- a quarter */
    dl = (int32_t)(((r >> 8) & 255u) - 128u) * vary / 512;
    v->s[0] = 0;
    v->s[1] = k;
    v->s[2] = 0;                                      /* (a frame starts with the hit) */
    v->s[3] = (d->note + t->p[P_E0]) * 16 + dp;
    v->s[4] = (int32_t)(d->pvol * 256) | (int32_t)(d->nvol * 256) << 16;
    v->s[5] = clamp(d->nrate * 16 - t->p[P_E0] * 16 / 3, 0, 15 * 16);
    v->s[6] = 32767 - ((127 - v->vel) * t->p[P_E7] * 2);
    v->s[6] = v->s[6] < 2048 ? 2048 : v->s[6];
    v->s[7] = (dp & 255) | (dl & 255) << 8 | frame << 16;
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->ph[2] = 1u;                                    /* (the register: never all zeros) */
}

/* the triangle's frames, DECAY's (a quarter .. four times the drum's) */
static int32_t ck_tri_frames(const track_t *t, const ck_drum_t *d)
{
    int32_t e = t->p[P_E1];
    return d->tri ? (d->tri * (e >= 64 ? 64 + (e - 64) * 3 : 16 + e * 3 / 4)) / 64 : 0;
}

static int chipkit_done(track_t *t, voice_t *v)
{
    return !(v->s[4] & 0xFFFF) && !(v->s[4] >> 16) && (v->s[1] >> 4) > ck_tri_frames(t, &CK_D[v->s[1] & 15]) &&
           v->s[0] > 64;
}

/* the volume a step: 16 steps (SMS: 2 dB apart; C64: smooth) */
static int32_t ck_amp(int32_t chip, int32_t vq8)
{
    int32_t s = vq8 >> 8;
    if (vq8 <= 0)
        return 0;
    if (chip == CK_SMS)
        return CK_LOG[s > 15 ? 15 : s];
    if (chip == CK_C64)
        return vq8 * 2184 >> 8 > 32760 ? 32760 : vq8 * 2184 >> 8;
    return (s > 15 ? 15 : s) * 2184;
}

/* one frame: the drum's registers as its software would set them */
static void ck_frame(const track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    const ck_drum_t *d = &CK_D[v->s[1] & 15];
    int32_t fr = v->s[1] >> 4, dl = (int8_t)(v->s[7] >> 8 & 255);
    /* DECAY: a quarter .. four times as long (its falls divided by up to 4, multiplied by up to 4) */
    int32_t dk = p[P_E1] >= 64 ? 256 - (192 * (p[P_E1] - 64)) / 63 : 256 + (768 * (64 - p[P_E1])) / 64;
    int32_t pv = v->s[4] & 0xFFFF, nv = v->s[4] >> 16, sw = d->sweep * p[P_E3] / 64;
    dk = dk * (256 - dl) >> 8;
    if (fr)                                           /* (frame 0: as struck) */
        v->s[3] -= sw * 4;                            /* 1/4 semitone = 4 sixteenths */
    if (d->arp && fr == 4)
        v->s[3] += d->arp * 16;
    if (fr) {
        pv -= d->pdec * 64 * dk >> 8;                 /* (pdec quarters of a step: 64 in Q8) */
        nv -= d->ndec * 64 * dk >> 8;
    }
    if (d->bursts && fr < d->bursts)                  /* CLAP: the noise struck again */
        nv = d->nvol * 256;
    if (fr && d->ndrift)
        v->s[5] = clamp(v->s[5] + d->ndrift, 0, 15 * 16);
    pv = pv < 0 ? 0 : pv;
    nv = nv < 0 ? 0 : nv;
    v->s[4] = pv | nv << 16;
    v->s[1] = (v->s[1] & 15) | (fr + 1) << 4;
}

static void chipkit_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const ck_drum_t *d = &CK_D[v->s[1] & 15];
    int32_t chip = p[P_E2] & 3, frame = v->s[7] >> 16 & 0xFFFF, left = v->s[2], amp = v->s[6], age = v->s[0];
    /* NOISE: each drum's noise up or down (64 as designed), tone drums get some from 64 up */
    int32_t nmix = p[P_E4], ns = nmix >= 64 ? 32768 + (nmix - 64) * 512 : nmix * 512;
    int32_t tri_fr = ck_tri_frames(t, d);
    uint32_t ph = v->ph[0], nph = v->ph[1], reg = v->ph[2], i;
    uint32_t taps = chip == CK_C64 ? 22u : chip == CK_SMS && d->nshort ? 15u : chip == CK_SMS ? 15u : 14u;
    uint32_t sbit = d->nshort ? (chip == CK_GB ? 6u : chip == CK_SMS ? 3u : 6u) : (chip == CK_SMS ? 3u : 1u);
    for (i = 0; i < n; i++) {
        int32_t x = 0, pv, nv, fr;
        uint32_t inc, steps, ones = 0, k;
        if (left <= 0) {
            ck_frame(t, v);
            left = frame;
        }
        left--;
        fr = (v->s[1] >> 4) - 1;
        pv = v->s[4] & 0xFFFF;
        nv = v->s[4] >> 16;
        inc = pitch_inc((uint32_t)clamp(v->s[3], 0, 2047));
        if (fr < tri_fr) {                            /* the triangle (SMS: a square) */
            int32_t tv;
            if (chip == CK_SMS)
                tv = osc_pulse(ph, inc, 0x80000000u);
            else if (chip == CK_C64)
                tv = osc_tri(ph);
            else {                                    /* 32 steps of 4 bits, up and down */
                uint32_t st = ph >> 27;
                tv = (int32_t)(st < 16u ? st : 31u - st) * 4369 - 32767;
            }
            x += tv >> 1;
        }
        if (pv) {                                     /* the pulse */
            static const uint32_t DUTY[3] = {0x20000000u, 0x40000000u, 0x80000000u};
            x += (int32_t)(((int64_t)osc_pulse(ph, inc, DUTY[d->duty % 3]) * ck_amp(chip, pv)) >> 16);
        }
        ph += inc;
        if (nv) {                                     /* the noise: its register stepped, the bits it gave averaged */
            int32_t rate = ck_rate(chip, v->s[5] >> 4);
            nph += CK_NRATE[rate];
            steps = nph >> 16;
            nph &= 0xFFFFu;
            for (k = 0; k < steps; k++) {
                uint32_t fb = chip == CK_SMS && d->nshort ? reg & 1u : (reg ^ (reg >> sbit)) & 1u;   /* (SMS: its
                                                                         * periodic noise, a loop of 16: tonal) */
                reg = (reg >> 1) | fb << taps;
                ones += reg & 1u;
            }
            {                                         /* (a slow rate: the register's bit held between its steps) */
                int32_t b = steps ? (int32_t)(ones * 2u * 32767u / steps) - 32767 : (reg & 1u) ? 32767 : -32767;
                x += (int32_t)(((int64_t)b * ck_amp(chip, nv)) >> 16) * ns >> 15;
            }
        }
        x = (x * amp) >> 15;
        out[i] += voice_amp(clamp(x, -32767, 32767), m, i) << 1;
        age++;
    }
    v->s[0] = age;
    v->s[2] = left;
    v->ph[0] = ph;
    v->ph[1] = nph;
    v->ph[2] = reg;
}

static int32_t chipkit_keys(const track_t *t, uint32_t k) { return drum_keys(t, k); }

/* {TUNE, DECAY, CHIP, SWEEP, NOISE, RATE, VARY, VEL}. The keys from C2: KICK SNARE HAT OPEN TOM-L TOM-M TOM-H CRASH CLAP
 * ZAP COIN NOISE */
static const preset_t CHIPKIT_PRESETS[] = {
    {"BOSS FIGHT", {-3, 80, CK_NES, 80, 70, 64, 10, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 15, 25)},
    {"DUNGEON", {-5, 90, CK_NES, 50, 72, 48, 10, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 20, 45)},
    {"LONG TAILS", {0, 110, CK_NES, 64, 64, 64, 0, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 25)},
    {"PITCHED DOWN", {-7, 70, CK_NES, 64, 64, 64, 0, 64}, {0, 0, 127, 20}, 0, 0, FX(0, 0, 10, 15)},
};

static const engine_t ENG_CHIPKIT = {
    .name = "CHIPKIT",
    .id = EID_CHIPKIT,
    .cc = 0x25,                  /* CC74 DECAY, CC71 NOISE */
    .cats = "DDDD",
    .page_title = {"KIT", "CHIP"},
    .edit = {
        {"TUNE", F_INT, -24, 24, 0, 0, "st"},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"CHIP", F_ENUM, 0, 3, 0, N_CK_CHIP, 0},
        {"SWEEP", F_PCT, 0, 127, 64, 0, 0},
        {"NOISE", F_PCT, 0, 127, 64, 0, 0},
        {"RATE", F_PCT, 0, 127, 64, 0, 0},
        {"VARY", F_PCT, 0, 127, 0, 0, 0},
        {"VEL", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = CHIPKIT_PRESETS,
    .npresets = NELEM(CHIPKIT_PRESETS),
    .poly = 8,
    .note_on = chipkit_note_on,
    .render = chipkit_render,
    .keys = chipkit_keys,
    .knob = {P_E1, P_E3, P_E4, P_E5},
    .ownenv = 1,
    .done = chipkit_done,
};
