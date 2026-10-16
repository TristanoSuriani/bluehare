/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* KIT (1.5): a whole drum kit on one part, of FMPERC's voices (eng_fmperc.c fmp_run). DRUM's 8 lanes and its key and
 * note map (shared/drum_map.c drum_lane, DRUM_GM: the General MIDI drums, the kick on the first C), its lane LEVELs (EDIT >
 * LANES) and its lane MUTEs (the EDIT layer's black keys): KICK SNARE CLAP HAT CL HAT OP TOM RIM BELL (a cowbell).
 * KIT picks the voices: FM (clean FM drums), METAL (inharmonic, industrial), BIT (chip-like: fed back, short), GLITCH
 * (zaps and ticks), DEEP (long, subby, dark). The knobs move every lane from its design (64: as designed): TUNE
 * (+-12 semitones), DECAY (a quarter .. four times), PUNCH (the pitch sweep), FM (the index), SNAP (the noise), TONE;
 * VARY gives every hit its own small differences (pitch, decay, brightness: a played kit, or a broken one).
 * A lane per drum, mono (a hit retakes its lane's voice); a closed hat chokes the open one. The hit is the envelope
 * (engine_t.oneshot, as DRUM: the ADSR, glide, pitch routings do nothing; velocity, LEVEL, the sends do).
 * State: the lanes kit_lane[part][] (in RAM: the voice, the envelope, the hit's differences), the voice's s[] and ph[]
 * fmp_run's. */
#define KL_N 8u
typedef struct {
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 none */
    uint8_t choke;
    int8_t st, vp, vd, vi;       /* the GM note's semitones; the hit's pitch (1/16 st), decay, index differences */
    int32_t env;                 /* Q15 */
} kit_lane_t;
static kit_lane_t kit_lane[NPART][KL_N];
static uint8_t kit_vl[NPART][NVOICE];   /* the lane each voice plays */
static uint32_t kit_rnd = 0x1234567u;

static const char *const N_KIT_KIT[] = {"FM", "METAL", "BIT", "GLITCH", "DEEP", 0};
/* each kit's lanes: {note, RATIO, INDEX, FDEC, SWEEP, NTYP, FDBK, NOISE, TONE (FMPERC's), decay (blocks: FMPERC's
 * DEC scale)}; lanes KICK SNARE CLAP HATCL HATOP TOM RIM BELL */
static const int8_t KIT_V[5][KL_N][10] = {
    {{33, 1, 30, 22, 36, 0, 0, 10, -10, 70}, {50, 11, 60, 20, 12, 0, 40, 76, 6, 52}, {60, 13, 26, 10, 0, 1, 20, 124, 8, 62},
     {72, 12, 120, 12, 0, 0, 120, 62, 44, 40}, {72, 12, 120, 30, 0, 2, 120, 58, 46, 76}, {45, 1, 40, 30, 14, 0, 0, 20, -4, 65},
     {72, 14, 70, 8, 8, 0, 0, 40, 20, 26}, {74, 2, 45, 50, 2, 0, 0, 0, 4, 60}},
    {{31, 0, 60, 18, 40, 0, 30, 20, -20, 75}, {52, 13, 90, 24, 8, 0, 70, 60, 10, 55}, {62, 15, 50, 12, 0, 1, 60, 110, 20, 60},
     {80, 15, 127, 10, 0, 0, 127, 50, 50, 35}, {80, 13, 127, 40, 0, 2, 127, 50, 40, 85}, {47, 12, 60, 40, 10, 0, 20, 10, 0, 70},
     {76, 14, 100, 10, 4, 0, 40, 20, 30, 30}, {77, 13, 90, 60, 0, 0, 30, 0, 10, 90}},
    {{36, 1, 10, 12, 48, 0, 60, 0, 0, 55}, {55, 3, 30, 10, 24, 0, 100, 100, 20, 40}, {60, 1, 20, 8, 0, 1, 90, 120, 30, 45},
     {84, 3, 60, 6, 0, 0, 127, 90, 63, 25}, {84, 3, 60, 20, 0, 2, 127, 90, 63, 60}, {48, 1, 20, 20, 24, 0, 50, 0, 0, 50},
     {79, 2, 50, 6, 12, 0, 80, 30, 40, 22}, {79, 3, 40, 40, 0, 0, 60, 0, 30, 55}},
    {{30, 3, 70, 10, 48, 0, 20, 20, 0, 45}, {60, 15, 110, 8, 24, 0, 110, 60, 20, 35}, {64, 14, 80, 6, 0, 1, 80, 120, 30, 40},
     {88, 15, 120, 4, 0, 0, 127, 70, 63, 18}, {88, 15, 120, 16, 0, 2, 127, 70, 55, 50}, {50, 3, 70, 20, 40, 0, 30, 0, 10, 45},
     {84, 10, 90, 4, 30, 0, 60, 20, 40, 18}, {86, 13, 60, 30, 20, 0, 40, 0, 20, 45}},
    {{28, 1, 8, 40, 20, 0, 0, 0, -44, 95}, {48, 11, 40, 30, 8, 0, 30, 70, -10, 70}, {57, 13, 20, 16, 0, 1, 30, 127, -6, 74},
     {70, 12, 100, 16, 0, 0, 100, 50, 30, 50}, {70, 12, 100, 50, 0, 2, 100, 50, 30, 95}, {40, 1, 30, 40, 18, 0, 0, 14, -16, 80},
     {67, 14, 60, 12, 6, 0, 0, 30, 0, 40}, {69, 13, 50, 80, 0, 0, 0, 0, -10, 100}},
};

static uint32_t kit_part(const track_t *t) { return (uint32_t)(t - trk) % NPART; }
static int32_t kit_rand(int32_t span)              /* -span .. span */
{
    kit_rnd ^= kit_rnd << 13;
    kit_rnd ^= kit_rnd >> 17;
    kit_rnd ^= kit_rnd << 5;
    return span ? (int32_t)(kit_rnd % (uint32_t)(2 * span + 1)) - span : 0;
}

/* a hit on a lane reuses its sounding voice (as DRUM: different GM notes on one lane share it) */
static voice_t *kit_reuse(track_t *t, uint32_t note)
{
    kit_lane_t *L = &kit_lane[kit_part(t)][drum_lane(note)];
    voice_t *v;
    if (t < &trk[0] || t >= &trk[NPART] || !L->owner || L->owner > NVOICE)
        return 0;
    v = &t->v[L->owner - 1u];
    return v->active && kit_vl[kit_part(t)][L->owner - 1u] == drum_lane(note) ? v : 0;
}

static void kit_note_on(track_t *t, voice_t *v)
{
    uint32_t i = (uint32_t)(v - t->v), lane = drum_lane(v->note), n, pt = kit_part(t);
    int32_t vary = t->p[P_E7];
    kit_lane_t *L;
    if (t < &trk[0] || t >= &trk[NPART] || i >= NVOICE)
        return;
    L = &kit_lane[pt][kit_vl[pt][i] % KL_N];
    if (L->owner == i + 1u)                           /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &kit_lane[pt][lane];
    n = v->note >= 35u && v->note <= 81u ? v->note : 36u + (v->note + 120u - 36u) % 12u;
    L->st = DRUM_GM[n - 35u][1];
    L->owner = (uint8_t)(i + 1u);
    L->choke = 0;
    L->env = 32767;
    L->vp = (int8_t)kit_rand(vary * 24 / 127);        /* VARY: up to +-1.5 semitones, +-12 decay, +-24 index */
    L->vd = (int8_t)kit_rand(vary * 12 / 127);
    L->vi = (int8_t)kit_rand(vary * 24 / 127);
    kit_vl[pt][i] = (uint8_t)lane;
    fmperc_note_on(t, v);
    v->env_out = v->vel * 258;                        /* (the hit starts at its level, as DRUM) */
    v->env = 1 << 24;
    if (lane == DV_HATC)                              /* a closed hat chokes the open one */
        kit_lane[pt][DV_HATO].choke = 1;
}

static int32_t kit_amp(track_t *t, voice_t *v, int32_t adsr)
{
    uint32_t i = (uint32_t)(v - t->v), pt = kit_part(t);
    const kit_lane_t *L;
    (void)adsr;
    if (!v->active || t < &trk[0] || t >= &trk[NPART] || i >= NVOICE)
        return 0;
    L = &kit_lane[pt][kit_vl[pt][i] % KL_N];
    if (L->owner != i + 1u || L->env < 16) {          /* retaken by another voice, or rung out: the voice ends */
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static void kit_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t i = (uint32_t)(v - t->v), pt = kit_part(t), lane;
    const int8_t *K;
    kit_lane_t *L;
    int16_t e[8];
    int32_t y[CTL], dk, tau, lv, gl, env;
    if (t < &trk[0] || t >= &trk[NPART] || i >= NVOICE || n > CTL)
        return;
    lane = kit_vl[pt][i] % KL_N;
    L = &kit_lane[pt][lane];
    if (L->owner != i + 1u)
        return;
    K = KIT_V[(uint32_t)p[P_E0] % 5u][lane];
    e[0] = K[1];
    e[1] = (int16_t)clamp(K[2] * p[P_E4] / 64 + L->vi, 0, 127);   /* FM */
    e[2] = K[3];
    e[3] = (int16_t)clamp(K[4] * p[P_E3] / 64, 0, 48);           /* PUNCH */
    e[4] = K[5];
    e[5] = K[6];
    e[6] = (int16_t)clamp(K[7] * p[P_E5] / 64, 0, 127);           /* SNAP */
    e[7] = (int16_t)clamp(K[8] + p[P_E6] - 64, -64, 63);          /* TONE */
    fmp_run(e, v, y, n, (K[0] + L->st) * 16 + (p[P_E1] - 64) * 3 + L->vp, 0);   /* TUNE */
    dk = clamp(K[9] + (p[P_E2] - 64) / 2 + L->vd, 0, 127);       /* DECAY */
    tau = L->choke ? 3 : 1 + dk * dk / 6;
    env = L->env;
    L->env = env - env / tau - (env > 0);
    if ((drum_mute[pt] >> lane) & 1u)
        return;                                       /* (a muted lane rings on unheard) */
    lv = clamp(p[P_LN0 + lane], 0, 127);
    gl = lv >= 127 ? 32767 : lv * lv * 2 + (lv * lv >> 6);   /* the lane's LEVEL, square law (as DRUM) */
    for (i = 0; i < n; i++) {
        int32_t g = env + (((L->env - env) * (int32_t)i) >> CTL_LOG2);   /* (the envelope ramped over the block) */
        out[i] += voice_amp((((y[i] * g) >> 15) * gl) >> 15, m, i) << 1;
    }
}

/* {KIT, TUNE, DECAY, PUNCH, FM, SNAP, TONE, VARY}; KIT 0 FM 1 METAL 2 BIT 3 GLITCH 4 DEEP */
static const preset_t KIT_PRESETS[] = {
    {"FM KIT", {0, 64, 64, 64, 64, 64, 64, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 15)},
    {"METAL KIT", {1, 64, 64, 64, 64, 64, 64, 10}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 10, 20)},
    {"BIT KIT", {2, 64, 64, 64, 64, 64, 64, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 10)},
    {"GLITCH KIT", {3, 64, 64, 64, 64, 64, 64, 30}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 20, 15)},
    {"DEEP KIT", {4, 64, 64, 64, 64, 64, 64, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 25)},
    {"TIGHT FM", {0, 66, 36, 80, 70, 70, 70, 0}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 0, 5)},
    {"PLAYED FM", {0, 64, 64, 64, 64, 64, 64, 50}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20)},
    {"BROKEN", {3, 60, 70, 90, 90, 80, 70, 110}, {0, 100, 127, 100}, 0, 0, FX(20, 0, 30, 20)},
    {"HEAVY METAL", {1, 54, 80, 90, 100, 90, 56, 20}, {0, 100, 127, 100}, 0, 0, FX(60, 0, 10, 25)},
    {"LOFI BIT", {2, 58, 50, 70, 50, 80, 40, 20}, {0, 100, 127, 100}, 0, 0, FX(30, 0, 20, 10)},
    /* (Blue Hare, the IDM review) */
    {"BOOM KIT", {4, 60, 100, 90, 50, 50, 40, 0}, {0, 100, 127, 100}, 0, 0, FX(20, 0, 0, 20)},
    {"TINY KIT", {2, 90, 20, 50, 60, 70, 90, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 20, 10)},
    {"INDUSTRIAL", {1, 40, 70, 80, 127, 100, 60, 30}, {0, 100, 127, 100}, 0, 0, FX(80, 0, 20, 30)},
    {"CLICKS", {3, 70, 10, 127, 80, 60, 80, 60}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 30, 15)},
    {"DUSTY FM", {0, 56, 54, 60, 60, 50, 30, 30}, {0, 100, 127, 100}, 0, 0, FX(20, 0, 10, 15)},
    {"DUB KIT", {4, 64, 70, 70, 50, 70, 50, 10}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 60, 40)},
};

/* the GM drum map, the first C the kick (as DRUM) */
static int32_t kit_keymap(const track_t *t, uint32_t k) { return drum_keys(t, k); }

static const engine_t ENG_KIT = {
    .name = "KIT",
    .id = EID_KIT,
    .cc = 0x53,                  /* CC74 FM, CC71 DECAY */
    .cats = "DDDDDDDDDDDDDDDD",
    .page_title = {"KIT", "HIT"},
    .edit = {
        {"KIT", F_ENUM, 0, 4, 0, N_KIT_KIT, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"PUNCH", F_PCT, 0, 127, 64, 0, 0},
        {"FM", F_PCT, 0, 127, 64, 0, 0},
        {"SNAP", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
        {"VARY", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = KIT_PRESETS,
    .npresets = NELEM(KIT_PRESETS),
    .note_on = kit_note_on,
    .render = kit_render,
    .amp = kit_amp,
    .knob = {P_E2, P_E3, P_E4, P_E7},
    .poly = KL_N,
    .oneshot = 1,
    .keys = kit_keymap,
};
