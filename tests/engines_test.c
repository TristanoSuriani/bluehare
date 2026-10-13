/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* A distribution without some engines (1.5, distributions/*.txt): run_tests.sh builds this as FELUCCA_DIST=idm, which has
 * no HOOVER, BYTE, KARPLUS, FMPERC, DUBSIREN8, FMKEYS, RISERS nor FOUNDRY's engines (Felucca's own ANALOG .. FM6 are
 * retired ids since Fel-Tracker removed them). The engines left are numbered 0.. in the distribution's order; the ids stay what the stores and the protocol use, and a
 * stored sound of an engine left out loads as its substitute (engines.c eng_load_id):
 *   the table      the indices in order, the ids both ways, the left-out ids map to none, the substitutes
 *   power-on       the parts whose engine is left out: the build's first engine
 *   protocol       INFO names a left-out id "-"; G_ENGSEL / PRESET of one: its substitute, its first preset; NAMES: none
 *   projects       a stored track of a left-out engine loads as its substitute with its first preset's engine values,
 *                  the rest of the sound as saved
 *   user presets   a record of a left-out engine is kept (used), listed nowhere, loads nothing
 *   the song       an instrument of a left-out engine plays its substitute
 *   sound          every engine left renders its presets (nothing of the others linked in) */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

/* the song's stream as the store holds it, its hash made valid again after an edit (as robust_test.c) */
static void stream_rehash(void)
{
    uint32_t i, j, at = 0, h = 2166136261u, sel = 3u + 2u * G_COUNT, len = 0;
    for (i = 0; i < PROJ_CHUNKS && proj_ram_len[i]; i++)
        len += proj_ram_len[i] - PROJ_HDR;
    for (i = 0; i < PROJ_CHUNKS && proj_ram_len[i]; i++)
        for (j = PROJ_HDR; j < proj_ram_len[i]; j++, at++) {
            uint8_t *b = &proj_ram[i][j];
            if (at >= len - 4u)
                *b = (uint8_t)(h >> (8u * (at - (len - 4u))));
            else if (at < sel || at > sel + PROJ_NAME_LEN)
                h = (h ^ *b) * 16777619u;
        }
}

int main(void)
{
    static const char *const ORDER[] = {"SID", "WASP", "WAVE", "BREAKS", "WEST", "VECTOR", "DUBSIREN", "MODAL",
                                        "DUBCHORD", "VOSIM", "PARTICLE", "CHAOS", "ECHOES", "KIT", "SPEECH", "GLITCH"};
    uint8_t a[16] = {0};
    int bad = 0, ok = 1, sub;
    uint32_t i, e, off;
    char dbg2[200];

    /* the table */
    ok = NENGINES == NELEM(ORDER) && !FELUCCA_ENG_HOOVER && !FELUCCA_ENG_BYTE && !FELUCCA_ENG_KARPLUS && !FELUCCA_ENG_FMPERC &&
         !FELUCCA_ENG_DUBSIREN8 && !FELUCCA_ENG_FMKEYS && !FELUCCA_ENG_RISERS && !FELUCCA_ENG_DARK && FELUCCA_ENG_MODAL &&
         FELUCCA_ENG_VECTOR && FELUCCA_ENG_ECHOES && FELUCCA_ENG_GLITCH;
    for (i = 0; ok && i < NENGINES; i++)
        ok &= str_eq(ENGINES[i]->name, ORDER[i]) && eng_of_id(eng_id(i)) == i;
    ok &= eng_of_id(EID_HOOVER) == ENG_NO && eng_of_id(EID_BYTE) == ENG_NO && eng_of_id(EID_DARK) == ENG_NO &&
          ENGI_HOOVER == ENG_ABSENT && str_eq(N_ENGNAME[EID_HOOVER], "-") && eng_by_id(EID_BYTE) == &ENG_NONE;
    for (i = 0; i <= 13u; i++)                          /* (retired: Felucca's own and 1.5's) */
        ok &= EID_RETIRED(i) && eng_of_id(i) == ENG_NO && str_eq(N_ENGNAME[i], "-");
    bad += check("engines: IDM, 16 in its order from 0, the ids both ways; HOOVER BYTE DARK .. none; 0 .. 13 retired", ok);
    ok = eng_load_id(EID_HOOVER, &sub) == ENGI_SID && sub && eng_load_id(EID_DUBSIREN8, &sub) == ENGI_SID && sub &&
         eng_load_id(EID_FMKEYS, &sub) == ENGI_WAVE && sub && eng_load_id(EID_RISERS, &sub) == ENGI_CHAOS && sub &&
         eng_load_id(EID_KARPLUS, &sub) == ENGI_WEST && sub && eng_load_id(EID_FMPERC, &sub) == ENGI_KIT && sub &&
         eng_load_id(EID_BYTE, &sub) == ENGI_SID && sub && eng_load_id(EID_DARK, &sub) == ENGI_SID && sub &&
         eng_load_id(EID_GUTS, &sub) == ENGI_WASP && sub && eng_load_id(EID_ANVIL, &sub) == ENGI_MODAL && sub &&
         eng_load_id(EID_MODAL, &sub) == ENGI_MODAL && !sub && eng_load_id(EID_VECTOR, &sub) == ENGI_VECTOR && !sub &&
         eng_load_id(EID_ECHOES, &sub) == ENGI_ECHOES && !sub && eng_load_id(EID_GLITCH, &sub) == ENGI_GLITCH && !sub &&
         eng_load_id(EID_WASP, &sub) == ENGI_WASP && !sub && eng_load_id(EID_WAVE, &sub) == ENGI_WAVE && !sub &&
         eng_load_id(0, &sub) == ENG_NO && eng_load_id(12, &sub) == ENG_NO && eng_load_id(13, &sub) == ENG_NO &&
         eng_load_id(27, &sub) == ENG_NO;
    bad += check("  substitutes: HOOVER, DUBSIREN8, BYTE, DARK -> SID; FMKEYS -> WAVE; RISERS -> CHAOS; KARPLUS -> WEST; FMPERC "
                 "-> KIT; GUTS -> WASP; ANVIL -> MODAL; retired (0 .. 13, 27): none", ok);

    /* power-on */
    reset();                                            /* (tk_defaults: the instruments of the power-on parts) */
    bad += check("power-on: SID BASS, VOSIM FORMANT PAD, SID SYNC LEAD, FM KIT; the instruments by id",
                 TRK_DEF[0][0] == ENGI_SID && TRK_DEF[0][1] == 0u && TRK_DEF[1][0] == ENGI_VOSIM &&
                 str_eq(ENGINES[ENGI_VOSIM]->presets[TRK_DEF[1][1]].name, "FORMANT PAD") &&
                 TRK_DEF[2][0] == ENGI_SID && TRK_DEF[2][1] == 2u && TRK_DEF[3][0] == ENGI_KIT && tk.instr[1].src == EID_VOSIM &&
                 tk.instr[3].src == EID_KIT);

    /* protocol */
    reset();
    request(ED_INFO, a, 0);
    {
        const char *n = (const char *)host_wire + 5;
        n += strlen(n) + 1;                             /* (the version) */
        ok = (uint8_t)n[0] == EID_COUNT;
        n += 5;                                         /* (NENGINES P_COUNT G_COUNT NSTEP P_E0: then the names) */
        for (i = 0; i < EID_COUNT; i++, n += strlen(n) + 1)
            ok &= !strcmp(n, eng_by_id(i)->name);
    }
    bad += check("INFO: the ids' names, \"-\" for a retired id or an engine left out", ok && !strcmp(N_ENGNAME[EID_HOOVER], "-"));
    a[0] = 1; a[1] = G_ENGSEL; a[2] = (8192 + EID_FMKEYS) & 127; a[3] = (8192 + EID_FMKEYS) >> 7;
    request(ED_SET, a, 4);
    ok = TSEL->eng_req == ENGI_WAVE && TSEL->preset == 0u;
    a[0] = EID_HOOVER; a[1] = 3;
    request(ED_PRESET, a, 2);
    ok &= TSEL->eng_req == ENGI_SID && TSEL->preset == 0u && host_wire[5] == EID_SID;
    a[0] = EID_HOOVER;
    request(ED_NAMES, a, 1);
    ok &= host_wire[5] == EID_HOOVER && host_wire[6] == 0;
    bad += check("  G_ENGSEL FMKEYS: WAVE; PRESET HOOVER 3: SID's first preset (replies its id); NAMES HOOVER: none", ok);

    /* OCT (Fel-Tracker): a preset sets it, the next preset puts it back (FMPERC's 6 kicks carry -3: FMPERC is not in this
     * distribution, its table is read as it stands); a tracker instrument sets it too; a song keeps it; a stored value
     * outside -4 .. 4 (ENV DEST FX, which it was) loads as 0 */
    for (i = 0, ok = 1, off = 0; i < NELEM(FMPERC_PRESETS); i++) {
        int kick = strstr(FMPERC_PRESETS[i].name, "KICK") != 0;
        off += (uint32_t)kick;
        ok &= FMPERC_PRESETS[i].oct == (kick ? -3 : 0);
    }
    reset();
    set_engine_of(&trk[0], ENGI_SID);
    trk[0].p[P_OCT] = -2;
    apply_preset_to(&trk[0], 6);                        /* a preset with OCT 0 puts it back */
    ok &= trk[0].p[P_OCT] == 0 && off == 6u;
    trk[0].p[P_OCT] = 3;
    tk.instr[0].src = EID_SID;
    tk.instr[0].preset = 1;
    tk_inst_apply(&trk[0], 1);                          /* .. so does a tracker instrument */
    ok &= trk[0].p[P_OCT] == 0;
    bad += check("OCT: FMPERC's 6 KICK presets carry -3, the others 0; a preset load and an instrument set it", ok);
    trk[0].p[P_OCT] = -3;
    project_save(0);
    project_new();
    project_load(0);
    ok = trk[0].p[P_OCT] == -3 && param_oct(P_OCT, 30) == 0 && param_oct(P_OCT, -65) == 0 && param_oct(P_OCT, -2) == -2 &&
         param_oct(P_DIST, 30) == 30 && TP[P_OCT].min == -4 && TP[P_OCT].max == 4 && str_eq(TP[P_OCT].label, "OCT");
    bad += check("  a song keeps OCT -3; an old ENV DEST FX value outside -4 .. 4 loads as 0 (not clamped to 4 octaves)", ok);
    {   /* the sound: 3 octaves down (SID TRI FLUTE held at C4, its strongest line 100 .. 400 ms, OCT 0 against -3) */
        static int16_t rec[FS / 2u];
        char dbg[200];
        double bf[2];
        int32_t o[2 * CTL];
        uint32_t f, k, m;
        for (m = 0; m < 2u; m++) {
            double best = 0, fr;
            host_tracks_init();
            for (f = 0; f < FS / 10u; f += CTL)
                mix_block(o, CTL);
            host_preset(&trk[0], ENGI_SID, 6);
            trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_DIST] = 0;
            trk[0].p[P_OCT] = m ? -3 : 0;
            trk_note_on(&trk[0], 60, 110);
            for (f = 0, k = 0; f < FS / 2u; f += CTL) {
                mix_block(o, CTL);
                for (i = 0; i < CTL && k < FS / 2u; i++)
                    rec[k++] = (int16_t)clamp(o[2 * i], -32768, 32767);
            }
            for (bf[m] = 0, fr = 20; fr < 600; fr += 0.5) {
                double re = 0, im = 0;
                for (k = 4410; k < 17640; k++) {
                    double w = 0.5 - 0.5 * cos(2 * M_PI * (k - 4410) / 13230.0), x = rec[k] * w;
                    re += x * cos(2 * M_PI * fr * k / FS);
                    im += x * sin(2 * M_PI * fr * k / FS);
                }
                if (re * re + im * im > best) {
                    best = re * re + im * im;
                    bf[m] = fr;
                }
            }
        }
        snprintf(dbg, sizeof dbg, "  SID TRI FLUTE at C4: %.1f Hz, OCT -3 %.1f Hz (x %.2f: 3 octaves down)", bf[0], bf[1],
                 bf[1] > 0 ? bf[0] / bf[1] : 0);
        bad += check(dbg, bf[1] > 0 && bf[0] / bf[1] > 7.8 && bf[0] / bf[1] < 8.2);
    }

    /* DUBSIREN's echo is the last track's alone; IDM has no DUBSIREN8 since MODAL took its place (engines.c
     * eng_for_track): on another track DUBSIREN plays itself, dry. A pick, an instrument, a song */
    reset();
    set_engine_of(&trk[0], ENGI_DUBSIREN);
    ok = trk[0].eng_req == ENGI_DUBSIREN && ENGI_DUBSIREN8 == ENG_ABSENT && ENGINES[ENGI_DUBSIREN]->last_track;
    set_engine_of(&trk[NPART - 1], ENGI_DUBSIREN);
    ok &= trk[NPART - 1].eng_req == ENGI_DUBSIREN;
    tk.instr[0].src = EID_DUBSIREN;
    tk.instr[0].preset = 4;                             /* LASER */
    tk_inst_apply(&trk[1], 1);
    ok &= trk[1].eng_req == ENGI_DUBSIREN && trk[1].preset == 4u && trk[1].p[P_E4] == DUBSIREN_PRESETS[4].e[4];
    bad += check("DUBSIREN: no DUBSIREN8 in IDM: on any track itself (its echo the last track's), a pick, an instrument", ok);
    reset();
    set_engine_of(&trk[0], ENGI_DUBSIREN);
    apply_preset_to(&trk[0], 4);
    trk[0].p[P_E1] = 99;
    project_save(0);
    project_new();
    project_load(0);
    bad += check("  a song with DUBSIREN on track 1 loads it so, its preset and values as saved",
                 trk[0].eng_req == ENGI_DUBSIREN && trk[0].preset == 4u && trk[0].p[P_E1] == 99);
    {   /* DUBSIREN on track 1 plays dry, and its voice ends once let go */
        int32_t o[2 * CTL];
        uint32_t f, end = 0, pk = 0;
        reset();                                        /* (no song, no instruments of the tests above) */
        host_tracks_init();
        host_preset(&trk[0], ENGI_DUBSIREN, 8);         /* RUNAWAY ECHO */
        panic_req = 0;                                  /* (reset's preset loads: their release is not this note's) */
        trk_note_on(&trk[0], 60, 110);
        for (f = 0; f < 30u * FS; f += CTL) {
            uint32_t k, on = 0;
            mix_block(o, CTL);
            if (f / CTL == FS / CTL)                    /* let go after 1 s */
                trk_note_off(&trk[0], 60);
            for (k = 0; k < CTL; k++)
                pk = (uint32_t)abs(o[2 * k]) > pk ? (uint32_t)abs(o[2 * k]) : pk;
            for (k = 0; k < NVOICE; k++)
                on |= trk[0].v[k].active;
            if (f > FS && !on) {
                end = f;
                break;
            }
        }
        snprintf(dbg2, sizeof dbg2, "  DUBSIREN on track 1, dry, let go: its voice ends after %.1f s (peak %u)",
                 (end - FS) / (double)FS, pk);
        bad += check(dbg2, end && pk > 2000u);
    }

    /* projects: track 1 of a stored song re-stamped HOOVER (as a build with it saves it) */
    reset();
    set_engine_of(&trk[0], ENGI_WAVE);
    apply_preset_to(&trk[0], 2);
    trk[0].p[P_ATK] = 33;
    *tk_at(0, 0, 0) = tk_pack(60, 0, 0, 0);
    project_save(0);
    off = PROJ_HDR + 3u + 2u * G_COUNT + 1u + PROJ_NAME_LEN + (PROJ_VER >= 3u);   /* (v3: the track count first) */    /* track 1's engine byte */
    ok = proj_ram[0][off] == EID_WAVE;
    proj_ram[0][off] = EID_HOOVER;
    stream_rehash();
    project_new();
    project_load(0);
    e = trk[0].eng_req;
    ok &= e == ENGI_SID && trk[0].preset == 0u && trk[0].p[P_ATK] == 33 && tk_note(tk_at(0, 0, 0)) == 60u;
    for (i = 0; i < 8u; i++)
        ok &= trk[0].p[P_E0 + i] == ENGINES[ENGI_SID]->presets[0].e[i];
    bad += check("project: a track of HOOVER (left out) loads as SID, its first preset's engine values, the rest as saved",
                 ok);

    /* user presets */
    reset();
    up_store(4, "WAVY");
    up_rec(4)->engine = EID_BYTE;                         /* (a record a build with BYTE stored) */
    ok = up_used(4) && up_engine(4) == ENG_NO && !up_avail(4) && up_count() == 0u && up_load(4) == 1;
    bad += check("user preset of BYTE (left out): kept (used), in no list, loads nothing", ok);

    /* the song */
    reset();
    tk.instr[9].src = EID_FMKEYS; tk.instr[9].preset = 3;
    fm1_irq_off();
    tk_inst_apply(&trk[2], 10);
    fm1_irq_on();
    bad += check("instrument of FMKEYS (left out): plays WAVE's first preset", trk[2].eng_req == ENGI_WAVE && trk[2].preset == 0u);

    /* sound */
    {
        uint32_t pi, f, loud = 0, n = 0;
        int32_t w[2u * CTL];
        host_tracks_init();
        for (f = 0; f < FS / 10u; f += CTL)             /* (past the output's fade-in after a start) */
            mix_block(w, CTL);
        for (e = 0; e < NENGINES; e++)
            for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
                int32_t o[2u * CTL], pk = 0;
                host_tracks_init();
                host_preset(&trk[0], e, pi);
                trk_note_on(&trk[0], 60, 100);
                for (f = 0; f < FS / 4u; f += CTL) {
                    mix_block(o, CTL);
                    for (i = 0; i < 2u * CTL; i++)
                        pk = o[i] > pk ? o[i] : -o[i] > pk ? -o[i] : pk;
                }
                trk_note_off(&trk[0], 60);
                loud += pk > 100;
                n++;
            }
        bad += check("every engine left renders every preset (not silent)", loud == n && n > 40u);
    }
    /* the song format (Fel-Tracker step 3b): an 8-track build writes version 3 (a cell's place 2 bytes, the track count
     * after the name; every build version 4 now, the variations); a song of more tracks than the build is refused, one of fewer fills its
     * first tracks. build/host/song_v2.bin carries a 4-track song from this test's 4-track run to its 8-track one */
    {
        static const char V2[] = "build/host/song_v2.bin";
        uint32_t c, k;
        reset();
        set_engine_of(&trk[1], ENGI_WEST);
        apply_preset_to(&trk[1], 3);
        trk[1].p[P_E1] = 77;
        *tk_at(0, 63, NTRK - 1u) = tk_pack(60, 1, 0, 0);       /* the last place: past 255 at 8 tracks */
        *tk_at(TK_PATS - 3u, 40, 0) = tk_pack_ch(48, 2, 0, 0, 0x37);   /* pattern 22 (the pool's at 8), a chord */
        project_save(0);
        ok = proj_ram[0][PROJ_HDR] == PROJ_VER && PROJ_VER == 4u;   /* (every build: the variations' version) */
        project_new();
        project_load(0);
        ok &= tk_note(tk_at(0, 63, NTRK - 1u)) == 60u && tk_chord(tk_at(TK_PATS - 3u, 40, 0)) == 0x37u &&
              trk[1].eng_req == ENGI_WEST && trk[1].p[P_E1] == 77;
        snprintf(dbg2, sizeof dbg2, "song: version %u at %u tracks, the last place (row 64, track %u) and a chord in pattern 22 kept",
                 PROJ_VER, NTRK, NTRK);
        bad += check(dbg2, ok);
        reset();                                        /* a big song: past 4 chunks at 8 tracks, refused at 4 */
        for (c = 0, k = 0; c < TK_PATS * TK_ROWS * NTRK && k < 4200u; c++, k++)
            *tk_at(c / (TK_ROWS * NTRK), (c / NTRK) % TK_ROWS, c % NTRK) = tk_pack(36 + c % 48u, 1 + c % 9u, 0, 0);
        snprintf(dbg2, sizeof dbg2, "  4,200 cells: %s", NTRK == 8 ? "saved in more than 4 chunks, loaded the same" : "SONG TOO BIG (4 chunks)");
        if (NTRK == 8) {
            ok = !project_save(0) && proj_good.count > 4u && proj_good.count <= PROJ_CHUNKS;
            project_new();
            project_load(0);
            ok &= tk_note(tk_at(0, 0, 0)) == 36u && tk_note(tk_at(4199u / (TK_ROWS * NTRK), (4199u / NTRK) % TK_ROWS, 4199u % NTRK)) == 36u + 4199u % 48u;
        } else {
            ok = project_save(0) != 0;
        }
        bad += check(dbg2, ok);
        if (NTRK == 4) {                                /* a version-3 song of 8 tracks: refused, the music as it was */
            FILE *f;
            reset();
            set_engine_of(&trk[1], ENGI_WEST);
            *tk_at(5, 10, 2) = tk_pack(64, 1, 0, 0);
            project_save(0);
            if ((f = fopen(V2, "wb"))) {                /* (for the 8-track run: a 4-track song) */
                for (k = 0; k < PROJ_CHUNKS; k++) {
                    uint16_t n = proj_ram_len[k];
                    fwrite(&n, 2, 1, f);
                    fwrite(proj_ram[k], 1, n, f);
                }
                fclose(f);
            }
            off = PROJ_HDR + 3u + 2u * G_COUNT + 1u + PROJ_NAME_LEN;   /* (where version 3 has the track count) */
            proj_ram[0][PROJ_HDR] = 3;
            proj_ram[0][off] = 8;
            set_engine_of(&trk[2], ENGI_SID);
            project_load(0);
            bad += check("  a version-3 song of 8 tracks on a 4-track build: SONG HAS 8 TRACKS, the music as it was",
                         !strcmp(ui.msg, "SONG HAS 8 TRACKS") && trk[2].eng_req == ENGI_SID);
        } else {                                        /* a 4-track song (version 2) on 8 tracks */
            FILE *f = fopen(V2, "rb");
            if (!f) {
                printf("editor:   (no %s: run the 4-track engines test first; the version-2 load is not checked)\n", V2);
            } else {
                reset();
                memset(proj_ram_len, 0, sizeof proj_ram_len);
                for (k = 0; k < PROJ_CHUNKS; k++) {
                    uint16_t n = 0;
                    if (fread(&n, 2, 1, f) != 1)
                        break;
                    proj_ram_len[k] = n;
                    if (fread(proj_ram[k], 1, n, f) != n)
                        break;
                }
                fclose(f);
                set_engine_of(&trk[5], ENGI_SID);       /* (track 6 before: back to its power-on sound) */
                trk[5].p[P_E1] = 99;
                project_new();
                project_load(0);
                ok = trk[1].eng_req == ENGI_WEST && tk_note(tk_at(5, 10, 2)) == 64u && !tk_note(tk_at(5, 10, 6)) &&
                     trk[5].eng_req == eng_for_track(&trk[5], TRK_DEF[5][0]) && trk[5].p[P_E1] != 99;
                bad += check("  a 4-track song (version 2) on 8 tracks: tracks 1 .. 4 as saved, 5 .. 8 as at power-on", ok);
            }
        }
    }

    printf("%s\n", bad ? "ENGINES TEST FAILED" : "engines test passed");
    return bad != 0;
}
