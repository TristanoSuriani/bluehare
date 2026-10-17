/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* MODAL (1.5): a modal resonator (Blue Hare's own voice; its resonators are PHYS's, DaisySP's ResonatorSvf in fixed
 * point: phys_dsp.c, MIT). An exciter strikes a bank of MD_NM tuned resonators, the modes of a material: MATRL BAR (a
 * free bar), MRMBA (a tuned marimba bar), BELL (a church bell's partials, the hum an octave down), GLASS, PLATE, DRUM (a
 * membrane), TUBE (odd harmonics, a closed pipe), STRING (harmonics, a little stiff). EXCIT: MALLET (a soft pulse, its
 * width by BRIGHT), PLUCK (a noise burst), BOW (noise while the key is held), CLICK (a single sample: the hardest).
 * STRUCT stretches the material's modes (64: as it is; less: closer together, more: further apart, inharmonic); POS
 * the strike's place (which modes it excites: the middle, the edge); DECAY the fundamental's ring (50 ms .. 8 s); DAMP
 * how much sooner the high modes die; BRIGHT the exciter's hardness and the high modes' level; BEAT detunes the modes
 * a little each (the beating of a bell, a gamelan's shimmer). Modes past ~19 kHz are left out. The resonators'
 * coefficients cost divides: worked out when the pitch or a parameter moves, not every block.
 * (2026-10-17) Played: velocity is the strike's hardness (BRIGHT moves with it, -60 .. +20 around a velocity of 100:
 * a soft touch rings dark, a hard one bright, as a mallet does), and the SHP routings (ENV / LFO DEST SHP, the
 * matrix's) move POS, the place struck (an LFO wandering over the bar). The plain page (MENU > EXPERT OFF): MATRL
 * EXCIT DECAY BRIGHT; STRUCT POS DAMP BEAT on EXPERT's.
 * Voice state: the track's block (engines.c eng_slab, MD_POLY voices a track): the modes, the exciter. */
#define MD_NM 12
#define MD_POLY 4
#define MD_FMAX 1846000000u      /* a mode's highest frequency, cycles a sample Q32 (~0.43: ~19 kHz) */
static const char *const N_MD_MATRL[] = {"BAR", "MRMBA", "BELL", "GLASS", "PLATE", "DRUM", "TUBE", "STRING", 0};
static const char *const N_MD_EXCIT[] = {"MALLET", "PLUCK", "BOW", "CLICK", 0};
static const uint16_t MD_RATIO[8][MD_NM] = {   /* the materials' modes over the fundamental, Q8 */
    {256, 706, 1383, 2287, 3416, 4772, 6351, 8159, 10191, 12449, 14932, 17644},   /* BAR */
    {256, 1021, 2560, 4506, 6938, 9830, 13210, 17050, 21350, 26112, 31309, 36966},   /* MARIMBA */
    {128, 256, 303, 386, 512, 644, 681, 771, 1066, 1391, 1740, 2103},   /* BELL */
    {256, 594, 1088, 1697, 2401, 3200, 4096, 5069, 6118, 7270, 8499, 9805},   /* GLASS */
    {256, 361, 573, 640, 724, 809, 924, 1055, 1144, 1280, 1306, 1449},   /* PLATE */
    {256, 408, 547, 588, 679, 747, 808, 896, 922, 935, 1039, 1063},   /* DRUM */
    {256, 768, 1280, 1792, 2304, 2816, 3328, 3840, 4352, 4864, 5376, 5888},   /* TUBE */
    {256, 512, 769, 1027, 1286, 1547, 1809, 2074, 2341, 2611, 2883, 3159},   /* STRING */
};
static const uint16_t MD_DEC_MS[128] = {   /* DECAY: the fundamental's ring, ms (50 .. 8000, exponential) */
    50, 52, 54, 56, 59, 61, 64, 66, 69, 72, 75, 78, 81, 84, 87, 91,
    95, 99, 103, 107, 111, 116, 120, 125, 130, 136, 141, 147, 153, 159, 166, 173,
    180, 187, 195, 202, 211, 219, 228, 238, 247, 257, 268, 279, 290, 302, 314, 327,
    340, 354, 369, 384, 399, 416, 433, 450, 469, 488, 508, 528, 550, 572, 596, 620,
    645, 672, 699, 727, 757, 788, 820, 853, 888, 924, 962, 1001, 1042, 1085, 1129, 1175,
    1223, 1273, 1325, 1379, 1435, 1493, 1554, 1618, 1684, 1752, 1824, 1898, 1975, 2056, 2140, 2227,
    2318, 2412, 2511, 2613, 2720, 2830, 2946, 3066, 3191, 3321, 3456, 3597, 3744, 3897, 4056, 4221,
    4393, 4572, 4759, 4953, 5154, 5365, 5583, 5811, 6048, 6294, 6551, 6818, 7096, 7385, 7687, 8000,
};
typedef struct {
    int32_t s[MD_NM][2];         /* the resonators' states */
    px_mode_t m[MD_NM];          /* their coefficients and gains */
    uint32_t n, key;             /* the modes sounding; what the coefficients were worked out for */
    uint32_t age, rng;           /* samples since the strike; the noise */
    int32_t lp;                  /* the exciter's low-pass */
} md_voice_t;

static inline __attribute__((always_inline)) md_voice_t *md_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NPART])
        return 0;
    i = (uint32_t)(v - t->v);
    {
        md_voice_t *a = eng_slab(t, SLAB_MODAL, sizeof(md_voice_t) * MD_POLY);   /* (engines.c; ANVIL's too) */
        return a && i < MD_POLY ? &a[i] : 0;
    }
}

static void modal_note_on(track_t *t, voice_t *v)
{
    md_voice_t *d = md_of(t, v);
    if (!d)
        return;
    d->age = 0;                                       /* (a retrigger strikes the ringing modes again: no reset) */
    if (!d->rng)
        d->rng = 0x9E3779B9u ^ (v->age * 2654435761u);
    if (!v->env && !v->env_out)                       /* (a voice not sounding: voice.c set its level to 0; the
                                                       * modes from rest, not what rang when it last played) */
        memset(d->s, 0, sizeof d->s);
    d->key = 0;
}

/* the coefficients for the pitch inc and the track's parameters, the materials' table tbl; POS and BRIGHT as played
 * (posk, br: the knobs with SHP and the velocity) */
static void md_coef(md_voice_t *d, const int16_t *p, uint32_t inc, const uint16_t (*tbl)[MD_NM], int32_t posk, int32_t br)
{
    const uint16_t *r = tbl[(uint32_t)p[P_E0] & 7u];
    uint32_t k, n = 0, tms = MD_DEC_MS[p[P_E4] & 127];
    int32_t st = 128 + p[P_E2] * 2, pos = 40 + posk * 3, damp = p[P_E5], beat = p[P_E7];
    for (k = 0; k < MD_NM; k++) {
        int32_t ratio = 256 + (((int32_t)r[k] - 256) * st >> 8), h = (int32_t)((k * 2654435761u) >> 24) - 128, a, w;
        uint64_t mf;
        uint32_t tk, q8;
        px_svf_t c;
        ratio += (ratio * h * beat) >> 21;            /* BEAT: up to ~0.8 % a mode */
        if (ratio < 64)
            ratio = 64;
        mf = ((uint64_t)inc * (uint32_t)ratio) >> 8;
        if (mf >= MD_FMAX)
            break;
        tk = tms * 256u / (uint32_t)(256 + ((damp * (ratio > 256 ? ratio - 256 : 0)) >> 6));
        q8 = (uint32_t)((mf * tk * 35467u) >> 32);    /* Q = pi f T (the ring's e-folding T): f Hz x T ms x 0.804, Q8 */
        q8 = q8 < 256u ? 256u : q8;
        px_svf_coef(&c, (uint32_t)mf, q8);
        d->m[n].g = c.g;
        d->m[n].rpg = c.rpg;
        d->m[n].gh = c.gh;
        w = sine_i((uint32_t)((k + 1u) * (uint32_t)pos) << 22);   /* POS: the strike's place, sin((k + 1) pi x) */
        w = w < 0 ? -w : w;
        a = ((w + 2048) << 13) / ratio;               /* the strike's level falls as 1 / ratio (32-bit: the device */
        a += (a * (br - 64)) >> 6 >> (k < 2u ? 2 : 0);   /* has no signed 64-bit divide); BRIGHT tilts the high ones */
        d->m[n].a = a < 0 ? 0 : a;
        n++;
    }
    for (k = n; k < MD_NM; k++)
        d->s[k][0] = d->s[k][1] = 0;
    d->n = n;
}

static void md_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, const uint16_t (*tbl)[MD_NM],
                      uint32_t sh)
{
    md_voice_t *d = md_of(t, v);
    const int16_t *p = t->p;
    uint32_t key, i, k, ex = (uint32_t)p[P_E1] & 3u, w, rs, pinc;
    int32_t x[CTL], lp, kl;
    int32_t br = clamp(p[P_E6] + ((int32_t)v->vel - 100) * 3 / 4, 0, 127);    /* (the touch: harder, brighter) */
    int32_t posk = clamp(p[P_E3] + ((m->shape - (64 << 8)) >> 8), 0, 127);   /* (SHP: the place struck) */
    if (!d || n > CTL)
        return;
    key = m->inc ^ ((uint32_t)p[P_E0] << 24) ^ ((uint32_t)p[P_E2] << 17) ^ ((uint32_t)posk << 10) ^
          ((uint32_t)p[P_E4] << 3) ^ ((uint32_t)p[P_E5] * 2654435761u) ^ ((uint32_t)br * 40503u) ^ ((uint32_t)p[P_E7] << 20);
    if (key != d->key || !d->key) {
        md_coef(d, p, m->inc, tbl, posk, br);
        d->key = key | 1u;
    }
    /* the exciter, Q22 (1.0 = 2^22): MALLET a raised-cosine pulse (BRIGHT: 4 .. ~5 ms wide), PLUCK a noise burst
     * (~10 ms), BOW noise while the key is held, CLICK one sample; PLUCK and BOW low-passed by BRIGHT */
    w = 4u + (uint32_t)(127 - br) * (uint32_t)(127 - br) / 70u;
    pinc = 0xFFFFFFFFu / w;                           /* (the pulse's phase a sample) */
    kl = 2000 + br * 240;
    lp = d->lp;
    rs = d->rng;
    for (i = 0; i < n; i++, d->age++) {
        int32_t e = 0;
        if (ex == 0u) {
            if (d->age < w)
                e = (32767 - sine_i(d->age * pinc + 0x40000000u)) << 6;
        } else if (ex == 3u) {
            e = d->age == 0u ? 1 << 24 : 0;
        } else if ((ex == 1u && d->age < 441u) || (ex == 2u && v->gate)) {
            int32_t nz = (int32_t)(int16_t)(px_rand(&rs) >> 16) << 7;
            if (ex == 1u)
                nz = ((nz >> 2) * (441 - (int32_t)d->age) / 441) << 2;
            else
                nz >>= 3;
            lp += (int32_t)(((int64_t)(nz - lp) * kl) >> 15);
            e = lp;
        }
        x[i] = e;
    }
    d->lp = lp;
    d->rng = rs;
    for (i = 0; i < n; i++) {                         /* the modes, band-pass (phys_dsp.c px_modal_run) */
        int64_t acc = 0;
        const px_mode_t *c = d->m;
        int32_t (*sp)[2] = d->s, xi = x[i], y;
        for (k = 0; k < d->n; k++, c++, sp++) {
            int32_t s1 = sp[0][0], s2 = sp[0][1], tt, vv;
            tt = px_m(c->gh, xi - px_m(c->rpg, s1, 28) - s2, 30);
            vv = s1 + tt;
            sp[0][0] = vv + tt;
            tt = px_m(c->g, vv, 28);
            sp[0][1] = s2 + tt + tt;
            acc += (int64_t)c->a * vv;
        }
        y = (int32_t)(acc >> sh);                     /* (Q28 x Q22 -> Q15 and +30 dB at 28: the modes ring far below the
                                                       * strike) */
        y = soft_knee(clamp(y, -98304, 98304), 22000);
        out[i] += voice_amp(clamp(y, -32767, 32767), m, i) << 1;
    }
    for (k = 0; k < d->n; k++) {
        d->s[k][0] = px_clamp(d->s[k][0], -(1 << 28), 1 << 28);
        d->s[k][1] = px_clamp(d->s[k][1], -(1 << 28), 1 << 28);
    }
}

static void modal_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    md_render(t, v, out, n, m, MD_RATIO, 28u);
}

/* {MATRL, EXCIT, STRUCT, POS, DECAY, DAMP, BRIGHT, BEAT}; MATRL 0 BAR 1 MRMBA 2 BELL 3 GLASS 4 PLATE 5 DRUM 6 TUBE
 * 7 STRING; EXCIT 0 MALLET 1 PLUCK 2 BOW 3 CLICK. The amplitude: a long release, the modes ring as they will */
static const preset_t MODAL_PRESETS[] = {
    {"MARIMBA", {1, 0, 64, 20, 60, 70, 60, 0}, {0, 0, 127, 70}, 0, 0, FX(0, 0, 20, 25)},
    {"VIBES", {1, 0, 64, 30, 95, 40, 70, 12}, {0, 0, 127, 90}, 0, 0, FX(0, 30, 30, 40)},
    {"BELL", {2, 0, 64, 30, 110, 30, 75, 20}, {0, 0, 127, 100}, 0, 0, FX(0, 10, 30, 50)},
    {"TUBULAR", {6, 0, 70, 25, 105, 35, 80, 6}, {0, 0, 127, 100}, 0, 0, FX(0, 10, 35, 55)},
    {"GLASS", {3, 0, 64, 40, 100, 25, 90, 8}, {0, 0, 127, 95}, 0, 0, FX(0, 30, 35, 60)},
    {"STEEL BAR", {0, 3, 64, 35, 90, 30, 100, 0}, {0, 0, 127, 85}, 0, 0, FX(0, 0, 30, 40)},
    {"PLATE", {4, 0, 64, 50, 95, 45, 70, 30}, {0, 0, 127, 95}, 0, 0, FX(0, 20, 30, 55)},
    {"WOOD DRUM", {5, 0, 64, 15, 40, 90, 40, 0}, {0, 0, 127, 50}, 0, 0, FX(0, 0, 15, 20)},
    {"PLUCK BAR", {0, 1, 64, 30, 70, 60, 70, 4}, {0, 0, 127, 70}, 0, 0, FX(0, 0, 30, 30)},
    {"HARP", {7, 1, 64, 25, 90, 50, 80, 0}, {0, 0, 127, 85}, 0, 0, FX(0, 20, 30, 45)},
    {"KALIMBA", {0, 0, 40, 20, 65, 80, 85, 0}, {0, 0, 127, 70}, 0, 0, FX(0, 0, 25, 30)},
    {"GAMELAN", {2, 0, 80, 40, 100, 40, 90, 70}, {0, 0, 127, 100}, 0, 0, FX(0, 20, 35, 55)},
    /* bowed: noise while the key is held, the modes singing */
    {"BOWED GLASS", {3, 2, 64, 40, 110, 20, 60, 10}, {30, 0, 127, 90}, 0, 0, FX(0, 40, 30, 70)},
    {"BOWED BELL", {2, 2, 64, 30, 115, 25, 50, 20}, {50, 0, 127, 100}, 0, 0, FX(0, 40, 30, 75)},
    {"DRONE PIPE", {6, 2, 64, 20, 120, 50, 40, 6}, {60, 0, 127, 100}, 0, 0, FX(0, 50, 25, 80)},
    /* IDM: clicks, stretched metals, clanks */
    {"CLANK", {4, 3, 100, 60, 60, 30, 110, 50}, {0, 0, 127, 50}, 0, 0, FX(20, 0, 30, 30)},
    {"TIN CAN", {0, 3, 120, 70, 45, 20, 120, 80}, {0, 0, 127, 40}, 0, 0, FX(10, 0, 30, 25)},
    {"METAL HIT", {4, 0, 30, 80, 75, 10, 115, 40}, {0, 0, 127, 70}, 0, 0, FX(20, 0, 35, 45)},
    {"GLITCH BELL", {2, 3, 110, 90, 85, 20, 127, 127}, {0, 0, 127, 80}, 0, 0, FX(10, 20, 40, 50)},
    {"SUB DRUM", {5, 0, 50, 10, 55, 100, 20, 0}, {0, 0, 127, 50}, 0, 0, FX(0, 0, 10, 15), .oct = -1},
    /* (FOUNDRY's clean side: chimes and a bowed string) */
    {"CHIMES", {2, 0, 64, 25, 115, 20, 85, 10}, {0, 0, 127, 110}, 0, 0, FX(0, 20, 35, 65)},
    {"GLOCKEN", {0, 0, 64, 25, 95, 25, 110, 0}, {0, 0, 127, 90}, 0, 0, FX(0, 10, 30, 55)},
    {"SOFT MARIMBA", {1, 0, 64, 15, 70, 80, 45, 0}, {0, 0, 127, 75}, 0, 0, FX(0, 10, 20, 35)},
    {"BOWED STRING", {7, 2, 64, 25, 115, 40, 55, 4}, {50, 0, 127, 100}, 0, 0, FX(0, 40, 30, 75)},
};

static const engine_t ENG_MODAL = {
    .name = "MODAL",
    .id = EID_MODAL,
    .cc = 0x57,                  /* CC74 DECAY, CC71 BRIGHT */
    .cats = "KKKKKKKDUUKKPPPFFFFDKKKP",
    .page_title = {"BODY", "STRIKE"},
    .edit = {
        {"MATRL", F_ENUM, 0, 7, 1, N_MD_MATRL, 0},
        {"EXCIT", F_ENUM, 0, 3, 0, N_MD_EXCIT, 0},
        {"STRUCT", F_PCT, 0, 127, 64, 0, 0},
        {"POS", F_PCT, 0, 127, 20, 0, 0},
        {"DECAY", F_PCT, 0, 127, 60, 0, 0},
        {"DAMP", F_PCT, 0, 127, 70, 0, 0},
        {"BRIGHT", F_PCT, 0, 127, 60, 0, 0},
        {"BEAT", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = MODAL_PRESETS,
    .npresets = NELEM(MODAL_PRESETS),
    .poly = MD_POLY,
    .note_on = modal_note_on,
    .render = modal_render,
    .knob = {P_E0, P_E4, P_E6, P_E3},
    .plain = {P_E0, P_E1, P_E4, P_E6},
};
