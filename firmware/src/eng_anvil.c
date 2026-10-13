/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* ANVIL (1.5, FOUNDRY): heavy metal struck (Fel-Tracker's own voice; MODAL's resonator, eng_modal.c md_render, its
 * pool and its controls). The materials are heavy: MATRL ANVIL (a block: dense, inharmonic), PIPE (a hollow beam),
 * GONG, CHAIN (links, scattered), BEAM (an I-beam), TANK (a big cylinder, its modes crowded low), SLAB (a thick
 * plate), CAST (a cast bell, the hum an octave down); EXCIT HAMMER, SCRAPE (noise), BOW, CLICK. STRUCT, POS, DECAY,
 * DAMP, BRIGHT, BEAT as MODAL's. A track plays one engine at a time: ANVIL and MODAL share md_v.
 * Voice state: MODAL's. */
static const char *const N_AV_MATRL[] = {"ANVIL", "PIPE", "GONG", "CHAIN", "BEAM", "TANK", "SLAB", "CAST", 0};
static const char *const N_AV_EXCIT[] = {"HAMMER", "SCRAPE", "BOW", "CLICK", 0};
static const uint16_t AV_RATIO[8][MD_NM] = {   /* the heavy materials' modes over the fundamental, Q8 */
    {256, 415, 591, 760, 904, 1075, 1190, 1357, 1562, 1766, 1946, 2150},   /* ANVIL */
    {256, 722, 1434, 2355, 3533, 4915, 6554, 8397, 10496, 12800, 15360, 18176},   /* PIPE */
    {256, 389, 499, 612, 712, 799, 909, 1029, 1144, 1262, 1370, 1490},   /* GONG */
    {256, 351, 484, 668, 788, 952, 1155, 1364, 1541, 1830, 2132, 2409},   /* CHAIN */
    {256, 614, 1126, 1766, 2509, 3379, 4378, 5478, 6707, 8064, 9523, 11110},   /* BEAM */
    {256, 287, 330, 387, 453, 520, 604, 696, 796, 909, 1029, 1152},   /* TANK */
    {256, 361, 443, 512, 573, 627, 724, 768, 809, 886, 924, 1024},   /* SLAB */
    {128, 256, 307, 384, 512, 640, 684, 768, 1024, 1357, 1690, 2048},   /* CAST */

};

static void anvil_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    md_render(t, v, out, n, m, AV_RATIO, 27u);       /* (+6 dB over MODAL's: the heavy metals ring lower) */
}

/* {MATRL, EXCIT, STRUCT, POS, DECAY, DAMP, BRIGHT, BEAT}; MATRL 0 ANVIL 1 PIPE 2 GONG 3 CHAIN 4 BEAM 5 TANK 6 SLAB
 * 7 CAST; EXCIT 0 HAMMER 1 SCRAPE 2 BOW 3 CLICK */
static const preset_t ANVIL_PRESETS[] = {
    {"ANVIL", {0, 3, 64, 30, 85, 40, 110, 10}, {0, 0, 127, 80}, 0, 0, FX(10, 0, 20, 35)},
    {"PIPE HIT", {1, 0, 64, 20, 95, 30, 95, 6}, {0, 0, 127, 90}, 0, 0, FX(0, 0, 30, 45)},
    {"GONG", {2, 0, 64, 40, 120, 20, 70, 40}, {0, 0, 127, 110}, 0, 0, FX(0, 20, 30, 70)},
    {"CHAINS", {3, 3, 70, 60, 70, 50, 110, 60}, {0, 0, 127, 60}, 0, 0, FX(10, 0, 30, 40)},
    {"I-BEAM", {4, 0, 64, 25, 100, 35, 100, 8}, {0, 0, 127, 95}, 0, 0, FX(10, 0, 25, 50)},
    {"TANK", {5, 0, 64, 30, 110, 50, 50, 20}, {0, 0, 127, 105}, 0, 0, FX(0, 10, 25, 65)},
    {"SLAB", {6, 0, 60, 45, 80, 45, 90, 30}, {0, 0, 127, 80}, 0, 0, FX(10, 0, 25, 45)},
    {"CAST BELL", {7, 0, 64, 30, 115, 25, 80, 20}, {0, 0, 127, 105}, 0, 0, FX(0, 20, 30, 60)},
    {"SCRAPE", {1, 1, 80, 50, 90, 30, 100, 30}, {0, 0, 127, 80}, 0, 0, FX(10, 10, 30, 50)},
    {"BOWED TANK", {5, 2, 64, 30, 120, 30, 40, 20}, {50, 0, 127, 105}, 0, 0, FX(0, 40, 30, 80)},
    {"FACTORY", {0, 0, 100, 70, 60, 60, 120, 80}, {0, 0, 127, 50}, 0, 0, FX(30, 0, 30, 30)},
    {"SUB GONG", {2, 0, 50, 20, 125, 40, 40, 30}, {0, 0, 127, 115}, 0, 0, FX(0, 20, 20, 70), .oct = -1},
    {"HAMMER TIME", {0, 3, 80, 40, 80, 45, 120, 20}, {0, 0, 127, 70}, 0, 0, FX(20, 0, 20, 30)},
    {"RAIL", {4, 1, 70, 30, 105, 30, 110, 15}, {0, 0, 127, 95}, 0, 0, FX(10, 0, 25, 50)},
    {"BELL TOWER", {7, 0, 64, 40, 125, 20, 90, 30}, {0, 0, 127, 110}, 0, 0, FX(0, 20, 30, 70)},
    {"TANK DRUM", {5, 0, 64, 40, 95, 55, 70, 10}, {0, 0, 127, 90}, 0, 0, FX(0, 0, 25, 45)},
};

static const engine_t ENG_ANVIL = {
    .name = "ANVIL",
    .id = EID_ANVIL,
    .cc = 0x57,                  /* CC74 DECAY, CC71 BRIGHT */
    .cats = "DDFDDFDKFPDFDFKD",
    .page_title = {"METAL", "STRIKE"},
    .edit = {
        {"MATRL", F_ENUM, 0, 7, 0, N_AV_MATRL, 0},
        {"EXCIT", F_ENUM, 0, 3, 0, N_AV_EXCIT, 0},
        {"STRUCT", F_PCT, 0, 127, 64, 0, 0},
        {"POS", F_PCT, 0, 127, 30, 0, 0},
        {"DECAY", F_PCT, 0, 127, 85, 0, 0},
        {"DAMP", F_PCT, 0, 127, 40, 0, 0},
        {"BRIGHT", F_PCT, 0, 127, 100, 0, 0},
        {"BEAT", F_PCT, 0, 127, 10, 0, 0},
    },
    .presets = ANVIL_PRESETS,
    .npresets = NELEM(ANVIL_PRESETS),
    .poly = MD_POLY,
    .note_on = modal_note_on,
    .render = anvil_render,
    .knob = {P_E0, P_E4, P_E6, P_E3},
};
