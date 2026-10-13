/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The demo songs (Fel-Tracker): one per distribution, written into the tracker as a song (its patterns, order,
 * instruments, tempo, name) by the emulator's DEMO button (felucca_web.c web_demo) and by the host renderer
 * (tests/demo_render.c). Every instrument names its engine and preset by name: in a distribution without one the
 * cells of that instrument stay silent (TK_SRC_NONE). Rows are 16ths, a pattern 4 bars (64 rows).
 *   IDM      "IDM DEMO", 124 BPM, D minor: KIT and BREAKS, WASP / SID / CHAOS basses, DUBCHORD stabs, VECTOR pads,
 *            VOSIM and SID leads, MODAL and ECHOES plucks, SPEECH vox, PARTICLE, GLITCH and DUBSIREN on top
 *   FOUNDRY  "FOUNDRY DEMO", 138 BPM, E phrygian: WAR and CHOKE, DARK / RAZOR / GUTS / VENOM basses, SLAG and FLUIDS
 *            beds, HONKS stabs, ANVIL and RUST metal, DRILL, SCRATCH, SHRED, ABYSS and SCREAM */
#define DSG_ROWS 64u

/* engine `eng`'s preset named `pre` into instrument n (1..): none if this build has no such engine or preset */
static void dsg_inst(uint32_t n, const char *eng, const char *pre)
{
    uint32_t e, k;
    tk_inst_t *in = &tk.instr[(n - 1u) % TK_INSTS];
    in->src = TK_SRC_NONE;
    in->preset = 0;
    for (e = 0; e < NENGINES; e++)
        if (str_eq(ENGINES[e]->name, eng))
            for (k = 0; k < ENGINES[e]->npresets; k++)
                if (str_eq(ENGINES[e]->presets[k].name, pre)) {
                    in->src = (uint8_t)eng_id(e);
                    in->preset = (uint8_t)k;
                    return;
                }
}

/* a note (with up to two intervals above it, 0 none) at row r of pattern p, track k; ins 0: the track's sound */
static void dsg_note(uint32_t p, uint32_t r, uint32_t k, uint32_t note, uint32_t ins, uint32_t i1, uint32_t i2)
{
    tk_cell_t *c = tk_at(p, r % DSG_ROWS, k % NTRK);
    uint32_t cmd = tk_cmd(c), val = tk_val(c);
    *c = tk_pack_ch(note, ins, cmd, val, (i1 & 15u) | (i2 & 15u) << 4);
}
static void dsg_off(uint32_t p, uint32_t r, uint32_t k) { dsg_note(p, r, k, TK_OFF, 0, 0, 0); }
/* an FX command on the cell (its note kept) */
static void dsg_fx(uint32_t p, uint32_t r, uint32_t k, uint32_t cmd, uint32_t val)
{
    tk_cell_t *c = tk_at(p, r % DSG_ROWS, k % NTRK);
    *c = tk_pack_ch(tk_note(c), tk_inst(c), cmd, val > 127u ? 127u : val, tk_chord(c));
}
/* an FX ramp: every `step` rows from r0 to r1, the value from v0 to v1 */
static void dsg_ramp(uint32_t p, uint32_t k, uint32_t cmd, uint32_t r0, uint32_t r1, uint32_t step, int32_t v0, int32_t v1)
{
    uint32_t r;
    for (r = r0; r <= r1; r += step)
        dsg_fx(p, r, k, cmd, (uint32_t)(v0 + (v1 - v0) * (int32_t)(r - r0) / (int32_t)(r1 > r0 ? r1 - r0 : 1u)));
}
/* a line from a string, a char a row from row r0: a note of `notes` (cycled) on 'x' (accent 'X': its cell also
 * LEVEL 127), its release on '-', nothing on '.'; ins on the first note */
static void dsg_line(uint32_t p, uint32_t k, uint32_t r0, const char *s, const uint8_t *notes, uint32_t nn, uint32_t ins)
{
    uint32_t r, j = 0, first = 1;
    for (r = 0; s[r] && r0 + r < DSG_ROWS; r++) {
        if (s[r] == 'x' || s[r] == 'X') {
            dsg_note(p, r0 + r, k, notes[j++ % nn], first ? ins : 0u, 0, 0);
            if (s[r] == 'X')
                dsg_fx(p, r0 + r, k, TKC_LEVEL, 127);
            first = 0;
        } else if (s[r] == '-') {
            dsg_off(p, r0 + r, k);
        }
    }
}
/* a chord held from r for len rows (released after) */
static void dsg_chord(uint32_t p, uint32_t k, uint32_t r, uint32_t len, uint32_t root, uint32_t i1, uint32_t i2, uint32_t ins)
{
    dsg_note(p, r, k, root, ins, i1, i2);
    if (r + len < DSG_ROWS)
        dsg_off(p, r + len, k);
}
/* a drum bar (16 rows from r0) on kit track k: kick / snare / hats strings ('x' hit) over a GM kit (36 kick, 38 snare
 * or 39 clap, 42 closed hat, 46 open hat): a kick with a hat is one cell (the hat its interval) */
static void dsg_drums(uint32_t p, uint32_t k, uint32_t r0, const char *kick, const char *snare, const char *hat,
                     uint32_t snote, uint32_t ins)
{
    uint32_t r, first = 1;
    for (r = 0; r < 16u; r++) {
        uint32_t kk = kick[r] == 'x', sn = snare[r] == 'x', h = hat[r] == 'x' ? 42u : hat[r] == 'o' ? 46u : 0u;
        uint32_t i = first ? ins : 0u;
        if (kk)
            dsg_note(p, r0 + r, k, 36, i, h ? h - 36u : 0u, sn ? snote - 36u : 0u);
        else if (sn)
            dsg_note(p, r0 + r, k, snote, i, h ? h - snote : 0u, 0);
        else if (h)
            dsg_note(p, r0 + r, k, h, i, 0, 0);
        else
            continue;
        first = 0;
    }
}
static void dsg_begin(const char *name, uint32_t bpm)
{
#ifdef PROJ_NAME_LEN                              /* (with the song store: a new song, named) */
    uint32_t k;
    project_new();
    for (k = 0; k < PROJ_NAME_LEN && name[k]; k++)
        proj_name[k] = name[k];
    proj_name[k] = 0;
#else                                             /* (the host renderer: the tracker alone) */
    (void)name;
    tk_defaults();
#endif
    song.g[G_BPM] = (int16_t)bpm;
}
static void dsg_order(const uint8_t *o, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && i < TK_ORDER; i++)
        tk.order[i] = o[i];
    tk.norder = (uint8_t)(n < TK_ORDER ? n : TK_ORDER);
}
/* every track's sound: its first instrument (as the song's first rows would load them) */
static void dsg_sounds(const uint8_t *first)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (first[k] && tk.instr[first[k] - 1u].src != TK_SRC_NONE)
            tk_inst_apply(&trk[k], first[k]);
}

/* ------------------------------------------------------------------- IDM --- */
static void demo_idm(void)
{
    /* D minor: D2 38, F2 41, G2 43, A2 45, C3 48; chords Dm (0 3 7), Bb (Bb 46: 0 4 7), C, Am */
    static const uint8_t BASS_A[] = {38, 38, 50, 38, 41, 38, 43, 45}, BASS_B[] = {46, 46, 58, 46, 48, 48, 60, 45};
    static const uint8_t LEAD[] = {74, 72, 69, 67, 69, 72, 74, 77, 76, 74, 72, 69};
    static const uint8_t PLUCK[] = {62, 65, 69, 72, 74, 69, 65, 62};
    static const uint8_t REESE[] = {38, 41, 36, 43};
    static const uint8_t ORDER[] = {0, 1, 2, 3, 2, 4, 5, 5, 6, 7, 3, 4, 8, 8, 2, 9};
    static const uint8_t FIRST[8] = {1, 3, 5, 8, 10, 12, 14, 16};
    uint32_t p, b, r;
    dsg_begin("IDM DEMO", 124);
    dsg_inst(1, "KIT", "DUB KIT");        dsg_inst(2, "KIT", "FM KIT");          dsg_inst(3, "BREAKS", "DUB BREAK");
    dsg_inst(4, "BREAKS", "CUT UP");      dsg_inst(5, "WASP", "RESO BASS");      dsg_inst(6, "SID", "BUZZ BASS");
    dsg_inst(7, "WASP", "REESE");         dsg_inst(8, "DUBCHORD", "DUB STAB");   dsg_inst(9, "DUBCHORD", "DEEP CHORD");
    dsg_inst(10, "VECTOR", "VS PAD");     dsg_inst(11, "VECTOR", "EVOLUTION");   dsg_inst(12, "VOSIM", "TALK LEAD");
    dsg_inst(13, "SID", "SYNC LEAD");     dsg_inst(14, "MODAL", "KALIMBA");      dsg_inst(15, "ECHOES", "DUB BLIP");
    dsg_inst(16, "DUBSIREN", "CLASSIC SIREN"); dsg_inst(17, "GLITCH", "MICRO CUTS"); dsg_inst(18, "PARTICLE", "RAIN");
    dsg_inst(19, "SPEECH", "AA VOX");     dsg_inst(20, "CHAOS", "STRANGE BASS"); dsg_inst(21, "DUBSIREN", "RISER");
    dsg_inst(22, "WEST", "LOG DRUM");     dsg_inst(23, "PARTICLE", "STARDUST");  dsg_inst(24, "VOSIM", "FORMANT PAD");
    /* tracks: 1 drums, 2 breaks / percussion, 3 bass, 4 chords, 5 pad, 6 lead, 7 plucks, 8 effects (DUBSIREN: the last) */
    for (p = 0; p < 10u; p++)
        tk.len[p] = DSG_ROWS;
    /* 0: intro, the pad and rain, a pluck coming in */
    dsg_chord(0, 4, 0, 32, 50, 3, 7, 24);
    dsg_chord(0, 4, 32, 32, 46, 4, 7, 0);
    dsg_ramp(0, 4, TKC_CUT, 0, 60, 4, 30, 90);
    dsg_note(0, 0, 7, 62, 18, 0, 0);
    dsg_line(0, 6, 32, "x..x..x...x..x..x..x..x...x.x...", PLUCK, 8, 14);
    dsg_ramp(0, 6, TKC_DLY, 32, 60, 4, 40, 90);
    /* 1: the kick, the hats, the bass halfway */
    dsg_chord(1, 4, 0, 64, 50, 3, 7, 10);
    for (b = 0; b < 4u; b++)
        dsg_drums(1, 0, b * 16u, "x...x...x...x...", "................", b < 2u ? "..x...x...x...x." : "..x.x.x.x.x.x.x.", 39, b ? 0 : 1);
    dsg_line(1, 2, 32, "x.xx..x.x.x..xx.x.xx..x.x.x.x.x.", BASS_A, 8, 5);
    dsg_ramp(1, 2, TKC_CUT, 32, 63, 2, 20, 70);
    dsg_line(1, 6, 0, "x..x..x...x..x..x..x..x...x.x...x..x..x...x..x..x..x..x...x.x...", PLUCK, 8, 14);
    /* 2, 3: groove A (Dm .. Bb), the chord stabs on the offbeats; 3 adds the lead */
    for (p = 2; p <= 3u; p++) {
        for (b = 0; b < 4u; b++) {
            dsg_drums(p, 0, b * 16u, "x...x...x...x..x", "....x.......x...", "..x.x.x.x.xox.x.", 39, b ? 0 : 1);
            dsg_line(p, 2, b * 16u, "x.xx..x.x.x..xx.", b < 2u ? BASS_A : BASS_B, 8, b ? 0 : 5);
            for (r = 2; r < 16u; r += 4)
                dsg_chord(p, 3, b * 16u + r, 1, b < 2u ? 50 : 46, b < 2u ? 3 : 4, 7, (b == 0 && r == 2) ? 8 : 0);
        }
        dsg_ramp(p, 2, TKC_CUT, 0, 63, 4, 40, 90);
        dsg_ramp(p, 2, TKC_RES, 0, 63, 8, 60, 100);
        dsg_chord(p, 4, 0, 32, 50, 3, 7, 10);
        dsg_chord(p, 4, 32, 32, 46, 4, 7, 0);
        dsg_line(p, 1, 0, "....x.......x.......x.......x.x.....x.......x.......x.......xxxx", LEAD, 1, 22);
        dsg_fx(p, 0, 3, TKC_DLY, 70);
        dsg_fx(p, 0, 7, TKC_REV, 80);
        dsg_note(p, 0, 7, 74, 23, 0, 0);
        dsg_off(p, 48, 7);
    }
    dsg_line(3, 5, 0, "x...x.x.x...x.x-x...x.x.x.x.x...x...x.x.x...x.x-x...x.x.x.x.x...", LEAD, 12, 12);
    dsg_ramp(3, 5, TKC_PAN, 0, 63, 8, 30, 100);
    dsg_fx(3, 0, 5, TKC_DLY, 80);
    /* 4: the break: BREAKS instead of the kit, the reese, glitch cuts, a vox */
    for (b = 0; b < 4u; b++) {
        dsg_note(4, b * 16u, 1, (uint32_t)(36 + (b * 5u) % 12u), b ? 0 : 3, 0, 0);
        dsg_drums(4, 0, b * 16u, "x.......x.......", "................", "................", 39, b ? 0 : 2);
        dsg_note(4, b * 16u, 2, REESE[b], b ? 0 : 7, 0, 0);
        dsg_line(4, 7, b * 16u, "..x...x.x.....xx", (const uint8_t *)"\x24\x27\x2a\x2d\x30", 5, b ? 0 : 17);
        dsg_chord(4, 3, b * 16u + 6u, 2, b & 1u ? 46 : 50, b & 1u ? 4 : 3, 7, b ? 0 : 9);
    }
    dsg_off(4, 63, 1);
    dsg_ramp(4, 2, TKC_CUT, 0, 63, 4, 30, 100);
    dsg_ramp(4, 2, TKC_DIST, 0, 63, 16, 20, 60);
    dsg_line(4, 5, 0, "x.......x...x...x.......x...x...", (const uint8_t *)"\x3e\x41\x45\x43", 4, 19);
    dsg_fx(4, 0, 1, TKC_REV, 40);
    /* 5: breakdown: the evolving pad, echoes, the siren, cutoff falling */
    dsg_chord(5, 4, 0, 64, 46, 4, 7, 11);
    dsg_ramp(5, 4, TKC_CUT, 0, 63, 2, 100, 30);
    dsg_line(5, 6, 0, "x.......x.......x.......x.......x.......x.......x.......x.......", PLUCK, 8, 15);
    dsg_fx(5, 0, 6, TKC_DLY, 100);
    dsg_note(5, 16, 7, 62, 16, 0, 0);
    dsg_off(5, 40, 7);
    dsg_fx(5, 16, 7, TKC_DLY, 110);
    dsg_note(5, 0, 5, 74, 24, 3, 7);
    dsg_off(5, 48, 5);
    /* 6: the build: a snare roll that thickens, the riser, the bass's filter opening */
    for (r = 0; r < 64u; r++) {
        uint32_t every = r < 32u ? 4u : r < 48u ? 2u : 1u;
        if (r % every == 0u)
            dsg_note(6, r, 0, r < 48u ? 38 : 39, r ? 0 : 2, 0, 0);
    }
    dsg_ramp(6, 0, TKC_LEVEL, 0, 63, 4, 60, 127);
    dsg_note(6, 0, 7, 50, 21, 0, 0);
    dsg_line(6, 2, 0, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", (const uint8_t *)"\x26", 1, 6);
    dsg_ramp(6, 2, TKC_CUT, 0, 63, 2, 10, 120);
    dsg_chord(6, 4, 0, 64, 50, 3, 7, 10);
    dsg_ramp(6, 4, TKC_LEVEL, 0, 63, 8, 120, 40);
    /* 7: the drop: everything, the chaos bass, the breaks over the kit */
    for (b = 0; b < 4u; b++) {
        dsg_drums(7, 0, b * 16u, "x..x..x...x..x..", "....x.......x...", "x.x.x.x.x.x.x.xo", 39, b ? 0 : 1);
        dsg_note(7, b * 16u, 1, (uint32_t)(36 + b * 3u), b ? 0 : 4, 0, 0);
        dsg_line(7, 2, b * 16u, "x.x.xx.x.x.x.xx.", b & 1u ? BASS_B : BASS_A, 8, b ? 0 : 20);
        for (r = 2; r < 16u; r += 4)
            dsg_chord(7, 3, b * 16u + r, 1, b & 1u ? 46 : 50, b & 1u ? 4 : 3, 7, (b == 0 && r == 2) ? 8 : 0);
    }
    dsg_line(7, 5, 0, "x.x.x...x.x.x...x.x.x.x.x...x...x.x.x...x.x.x...x.x.x.x.x...x...", LEAD, 12, 13);
    dsg_fx(7, 0, 5, TKC_GLIDE, 30);
    dsg_chord(7, 4, 0, 64, 50, 3, 7, 11);
    dsg_line(7, 7, 0, "..x...x.......x...x...x.......x...x...x.......x.x.x.x.x.x.x.x.x.", (const uint8_t *)"\x24\x2a\x30", 3, 17);
    dsg_ramp(7, 2, TKC_DIST, 0, 63, 16, 30, 70);
    /* 8: groove B: the FM kit, the log drum, the talking lead answered by the vox */
    for (b = 0; b < 4u; b++) {
        dsg_drums(8, 0, b * 16u, "x.....x...x.....", "....x.......x...", "x.xxx.xxx.xxx.xo", 38, b ? 0 : 2);
        dsg_line(8, 1, b * 16u, "..x..x....x..x.x", (const uint8_t *)"\x2b\x2d\x30", 3, b ? 0 : 22);
        dsg_line(8, 2, b * 16u, "x..x..x.x..x..x.", b < 2u ? BASS_A : BASS_B, 8, b ? 0 : 6);
    }
    dsg_line(8, 5, 0, "x.x.x.x-....x.x.x.x-....x.x.x.x-....x.x.x.x-....", LEAD, 12, 12);
    dsg_line(8, 6, 0, "x..x..x...x..x..x..x..x...x.x...x..x..x...x..x..x..x..x...x.x...", PLUCK, 8, 14);
    dsg_chord(8, 4, 0, 64, 41, 4, 7, 24);
    dsg_ramp(8, 6, TKC_PAN, 0, 63, 4, 20, 108);
    /* 9: the outro: the pad alone, rain, the cutoff closing, the siren fading */
    dsg_chord(9, 4, 0, 64, 50, 3, 7, 10);
    dsg_ramp(9, 4, TKC_CUT, 0, 63, 2, 80, 10);
    dsg_ramp(9, 4, TKC_LEVEL, 32, 63, 4, 110, 10);
    dsg_note(9, 0, 7, 62, 18, 0, 0);
    dsg_line(9, 6, 0, "x..x..x...x..x..x..x..x...x.x...", PLUCK, 8, 14);
    dsg_ramp(9, 6, TKC_REV, 0, 31, 4, 40, 120);
    dsg_order(ORDER, sizeof ORDER);
    dsg_sounds(FIRST);
}

/* --------------------------------------------------------------- FOUNDRY --- */
static void demo_foundry(void)
{
    /* E phrygian: E1 28, F1 29, G1 31, A1 33, B1 35, C2 36, D2 38; E2 40 */
    static const uint8_t SUB[] = {28, 28, 40, 28, 29, 28, 31, 28}, ACID[] = {40, 40, 52, 41, 40, 43, 40, 47, 41, 40, 52, 40};
    static const uint8_t RZ[] = {28, 29, 31, 26}, HONK[] = {64, 65, 67, 64, 71, 67};
    static const uint8_t ORDER[] = {0, 1, 2, 3, 2, 4, 5, 6, 7, 7, 3, 8, 8, 2, 9};
    static const uint8_t FIRST[8] = {1, 3, 5, 8, 10, 12, 14, 16};
    uint32_t p, b, r;
    dsg_begin("FOUNDRY DEMO", 138);
    dsg_inst(1, "WAR", "WAR KIT");        dsg_inst(2, "WAR", "DEATH KIT");       dsg_inst(3, "CHOKE", "GATED SNARE");
    dsg_inst(4, "CHOKE", "PISTON");       dsg_inst(5, "DARK", "SUB WEIGHT");     dsg_inst(6, "RAZOR", "NEURO REESE");
    dsg_inst(7, "GUTS", "GUT ACID");      dsg_inst(8, "HONKS", "BRASS BARK");    dsg_inst(9, "HONKS", "SAX STAB");
    dsg_inst(10, "SLAG", "MOLTEN WALL");  dsg_inst(11, "FLUIDS", "OIL PAD");     dsg_inst(12, "VENOM", "VENOM ACID");
    dsg_inst(13, "SCREAM", "PANIC");      dsg_inst(14, "ANVIL", "ANVIL");        dsg_inst(15, "RUST", "FERRIC HAT");
    dsg_inst(16, "ABYSS", "CAVERN");      dsg_inst(17, "DRILL", "POWER DRILL");  dsg_inst(18, "SCRATCH", "BABY SCRATCH");
    dsg_inst(19, "SHRED", "STUTTER");     dsg_inst(20, "SCREAM", "TERROR");      dsg_inst(21, "ANVIL", "GONG");
    dsg_inst(22, "SLAG", "IRON BASS");    dsg_inst(23, "FLUIDS", "LIQUID BASS"); dsg_inst(24, "ABYSS", "SINGING CAVE");
    dsg_inst(25, "DRILL", "MACHINE GUN"); dsg_inst(26, "SCRATCH", "FLARE");     dsg_inst(27, "RAZOR", "SUPERSAW");
    /* tracks: 1 WAR, 2 CHOKE / metal, 3 sub and reese, 4 acid, 5 beds, 6 stabs and leads, 7 metal, 8 effects */
    for (p = 0; p < 10u; p++)
        tk.len[p] = DSG_ROWS;
    /* 0: intro: the molten wall, the cavern, an anvil now and then */
    dsg_chord(0, 4, 0, 64, 40, 7, 12, 10);
    dsg_ramp(0, 4, TKC_CUT, 0, 63, 4, 20, 80);
    dsg_note(0, 0, 7, 40, 24, 0, 0);
    dsg_line(0, 6, 0, "x...............x.......x.......x...............x.......x...x...", (const uint8_t *)"\x28\x29\x2b", 3, 14);
    /* 1: the kick, the hats, the sub */
    dsg_chord(1, 4, 0, 64, 40, 7, 0, 10);
    for (b = 0; b < 4u; b++)
        dsg_drums(1, 0, b * 16u, "x...x...x...x...", "................", b < 2u ? "..x...x...x...x." : "..x.x.x.x.x.x.x.", 38, b ? 0 : 1);
    dsg_line(1, 2, 16, "x...x...x...x...x.x.x...x...x...x...x...x.x.x...", SUB, 8, 5);
    dsg_line(1, 6, 0, "..x...x...x...x...x...x...x...x...x...x...x...x...x...x...x...x.", (const uint8_t *)"\x48", 1, 15);
    /* 2, 3: the grind: the kit, the sub, the acid, the brass barks; 3 adds the scream */
    for (p = 2; p <= 3u; p++) {
        for (b = 0; b < 4u; b++) {
            dsg_drums(p, 0, b * 16u, "x..x..x.x...x...", "....x.......x...", "x.x.x.x.x.x.x.xo", 39, b ? 0 : 1);
            dsg_line(p, 2, b * 16u, "x...x...x...x...", SUB, 8, b ? 0 : 5);
            dsg_line(p, 3, b * 16u, "x.xx.x.xx.x.xx.x", ACID, 12, b ? 0 : 7);
            dsg_note(p, b * 16u + 12u, 1, 40, b ? 0 : 3, 0, 0);
        }
        dsg_ramp(p, 3, TKC_CUT, 0, 63, 2, 20, 110);
        dsg_ramp(p, 3, TKC_RES, 0, 63, 8, 70, 120);
        dsg_chord(p, 4, 0, 64, 40, 7, 0, 10);
        dsg_line(p, 5, 0, "..x.....x.....x...x.....x.....x...x.....x.....x...x.....x.......", HONK, 6, 8);
        dsg_line(p, 6, 0, "..x...x...x...x...x...x...x...x...x...x...x...x...x...x...x...x.", (const uint8_t *)"\x48", 1, 15);
        dsg_fx(p, 0, 5, TKC_DLY, 60);
    }
    dsg_note(3, 0, 7, 76, 13, 0, 0);
    dsg_off(3, 24, 7);
    dsg_note(3, 32, 7, 79, 0, 0, 0);
    dsg_off(3, 56, 7);
    dsg_fx(3, 0, 7, TKC_REV, 70);
    /* 4: the reese: RAZOR instead of the sub, the drill, the shred */
    for (b = 0; b < 4u; b++) {
        dsg_drums(4, 0, b * 16u, "x.......x.x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", 39, b ? 0 : 2);
        dsg_note(4, b * 16u, 2, RZ[b], b ? 0 : 6, 0, 0);
        dsg_note(4, b * 16u + 8u, 7, 52, b ? 0 : 17, 0, 0);
        dsg_off(4, b * 16u + 12u, 7);
    }
    dsg_ramp(4, 2, TKC_CUT, 0, 63, 4, 30, 110);
    dsg_line(4, 5, 0, "x...x...x.x.x...x...x...x.x.x...x...x...x.x.x...x...x...x.x.x...", (const uint8_t *)"\x34\x35\x37", 3, 19);
    dsg_chord(4, 4, 0, 64, 28, 12, 0, 11);
    /* 5: the breakdown: the oil pad, the singing cave, a gong, scratches */
    dsg_chord(5, 4, 0, 64, 40, 3, 7, 11);
    dsg_ramp(5, 4, TKC_CUT, 0, 63, 2, 100, 40);
    dsg_note(5, 0, 7, 40, 24, 0, 0);
    dsg_note(5, 0, 6, 28, 21, 0, 0);
    dsg_note(5, 32, 6, 28, 0, 0, 0);
    dsg_line(5, 5, 16, "x.......x.......x.......x.......", (const uint8_t *)"\x3c\x3e", 2, 18);
    dsg_fx(5, 16, 5, TKC_DLY, 90);
    /* 6: the build: the piston speeding up, the machine gun, the acid opening */
    for (r = 0; r < 64u; r++) {
        uint32_t every = r < 32u ? 4u : r < 48u ? 2u : 1u;
        if (r % every == 0u)
            dsg_note(6, r, 1, 40, r ? 0 : 4, 0, 0);
    }
    dsg_ramp(6, 1, TKC_LEVEL, 0, 63, 4, 50, 127);
    dsg_line(6, 3, 0, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", ACID, 12, 12);
    dsg_ramp(6, 3, TKC_CUT, 0, 63, 2, 10, 127);
    dsg_note(6, 48, 7, 40, 25, 0, 0);
    dsg_off(6, 63, 7);
    dsg_chord(6, 4, 0, 64, 40, 7, 0, 10);
    /* 7: the drop: the death kit, the reese and the acid, the screaming terror, the supersaw */
    for (b = 0; b < 4u; b++) {
        dsg_drums(7, 0, b * 16u, "x.xx..x.x.x..x..", "....x.......x...", "xxxxxxxxxxxxxxxo", 39, b ? 0 : 2);
        dsg_note(7, b * 16u, 2, RZ[b], b ? 0 : 6, 0, 0);
        dsg_line(7, 3, b * 16u, "x.xx.x.xx.x.xx.x", ACID, 12, b ? 0 : 12);
        dsg_note(7, b * 16u + 4u, 1, 40, b ? 0 : 3, 0, 0);
        dsg_note(7, b * 16u + 12u, 1, 40, 0, 0, 0);
    }
    dsg_chord(7, 4, 0, 32, 52, 3, 7, 27);
    dsg_chord(7, 4, 32, 32, 53, 3, 7, 0);
    dsg_note(7, 0, 7, 76, 20, 0, 0);
    dsg_off(7, 16, 7);
    dsg_note(7, 32, 7, 77, 0, 0, 0);
    dsg_off(7, 48, 7);
    dsg_ramp(7, 3, TKC_DIST, 0, 63, 16, 40, 90);
    dsg_line(7, 6, 0, "x...........x...x...........x...x...........x...x.......x.x.x...", (const uint8_t *)"\x28", 1, 14);
    /* 8: the second grind: iron bass, liquid bass answering, the flare */
    for (b = 0; b < 4u; b++) {
        dsg_drums(8, 0, b * 16u, "x...x...x...x.x.", "....x.......x..x", "..x...x...x...x.", 38, b ? 0 : 1);
        dsg_line(8, 2, b * 16u, "x..x..x.x..x..x.", SUB, 8, b ? 0 : 22);
        dsg_line(8, 3, b * 16u + 8u, "x.x.x...", SUB, 8, b ? 0 : 23);
    }
    dsg_line(8, 5, 0, "x...x...x.x.x...x...x...x.x.x...x...x...x.x.x...", HONK, 6, 9);
    dsg_line(8, 7, 0, "..x.......x.......x.......x.......x.......x.......x.......x.....", (const uint8_t *)"\x3c", 1, 26);
    dsg_chord(8, 4, 0, 64, 40, 7, 0, 10);
    /* 9: the end: the cavern, the gong, the wall closing */
    dsg_chord(9, 4, 0, 64, 40, 7, 12, 10);
    dsg_ramp(9, 4, TKC_CUT, 0, 63, 2, 80, 10);
    dsg_ramp(9, 4, TKC_LEVEL, 32, 63, 4, 110, 10);
    dsg_note(9, 0, 7, 40, 16, 0, 0);
    dsg_note(9, 0, 6, 28, 21, 0, 0);
    dsg_order(ORDER, sizeof ORDER);
    dsg_sounds(FIRST);
}

/* the demo of this build's distribution (the one whose engines it has), 0 = none */
static int demo_song(void)
{
    if (FELUCCA_ENG_WAR && FELUCCA_ENG_SCREAM) {
        demo_foundry();
        return 1;
    }
    if (FELUCCA_ENG_KIT && FELUCCA_ENG_WASP) {
        demo_idm();
        return 1;
    }
    return 0;
}
