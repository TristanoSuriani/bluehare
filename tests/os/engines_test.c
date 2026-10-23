/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* A distribution without some engines (distributions/*.txt): run_tests.sh builds this as BLUEHARE_DIST=test_subset
 * (2026-10-23: SID OBIE on track 1, KIT on 2, SPEECH on 3; VOSIM, VA and the rest left out; the archived engines are
 * retired ids in every build). Each track its own list (the TRACK lines): a load of another track's engine finds one
 * of its own (engines.c eng_for_track). The engines
 * left are numbered 0.. in the distribution's order; the ids stay what the stores and the protocol use, and a stored
 * sound of an engine left out loads as its substitute (engines.c eng_load_id):
 *   the table      the indices in order, the ids both ways, the left-out ids map to none, the substitutes
 *   power-on       the parts whose engine is left out: the second pick, else the build's first engine
 *   protocol       INFO names a left-out id "-"; G_ENGSEL / PRESET of one: its substitute, its first preset; NAMES: none
 *   projects       a stored track of a left-out engine loads as its substitute with its first preset's engine values,
 *                  the rest of the sound as saved
 *   user presets   a record of a left-out engine is kept (used), listed nowhere, loads nothing
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
    static const char *const ORDER[] = {"SID", "OBIE", "KIT", "SPEECH"};
    uint8_t a[16] = {0};
    int bad = 0, ok = 1, sub;
    uint32_t i, e, off, k;
    char dbg2[200];

    /* the table */
    ok = NENGINES == NELEM(ORDER) && !BLUEHARE_ENG_VOSIM && !BLUEHARE_ENG_VA && BLUEHARE_ENG_SID && BLUEHARE_ENG_KIT;
    for (i = 0; ok && i < NENGINES; i++)
        ok &= str_eq(ENGINES[i]->name, ORDER[i]) && eng_of_id(eng_id(i)) == i;
    ok &= eng_of_id(EID_VOSIM) == ENG_NO && ENGI_VOSIM == ENG_ABSENT && str_eq(N_ENGNAME[EID_VOSIM], "-") &&
          eng_by_id(EID_VOSIM) == &ENG_NONE && EID_RETIRED(EID_BLISS) && EID_RETIRED(EID_STRINGS) &&
          EID_RETIRED(EID_MODAL) && EID_RETIRED(EID_WAVE) && EID_RETIRED(EID_DUBCHORD);
    for (i = 0; i <= 13u; i++)                          /* (retired: Felucca's own and 1.5's) */
        ok &= EID_RETIRED(i) && eng_of_id(i) == ENG_NO && str_eq(N_ENGNAME[i], "-");
    ok &= EID_RETIRED(EID_KARPLUS) && EID_RETIRED(EID_FMPERC) && eng_of_id(EID_KARPLUS) == ENG_NO;   /* (archived) */
    bad += check("engines: TEST SUBSET, 4 in its order from 0, the ids both ways; VOSIM none; 0 .. 13 and the "
                 "archived (MODAL WAVE DUBCHORD ..) retired", ok);
    ok = eng_load_id(EID_VOSIM, &sub) == ENGI_SPEECH && sub && eng_load_id(EID_VA, &sub) == ENGI_OBIE && sub &&
         eng_load_id(EID_BLISS, &sub) == ENG_NO && eng_load_id(EID_MODAL, &sub) == ENG_NO &&
         eng_load_id(EID_WAVE, &sub) == ENG_NO && eng_load_id(EID_KIT, &sub) == ENGI_KIT && !sub &&
         eng_load_id(EID_KARPLUS, &sub) == ENG_NO && eng_load_id(0, &sub) == ENG_NO && eng_load_id(27, &sub) == ENG_NO;
    bad += check("  substitutes: VOSIM -> SPEECH, VA -> OBIE; retired (0 .. 13, 27) and archived (MODAL WAVE ..): none", ok);

    /* (2026-10-23) each track its own engines */
    ok = eng_on(ENGI_SID, 0) && eng_on(ENGI_OBIE, 0) && !eng_on(ENGI_KIT, 0) && eng_on(ENGI_KIT, 1) && !eng_on(ENGI_SID, 1) &&
         eng_on(ENGI_SPEECH, 2) && !eng_on(ENGI_OBIE, 2) &&
         eng_for_track(&trk[0], ENGI_KIT) == ENGI_SID && eng_for_track(&trk[1], ENGI_SID) == ENGI_KIT &&
         eng_for_track(&trk[2], ENGI_SID) == ENGI_SPEECH && eng_for_track(&trk[0], ENGI_OBIE) == ENGI_OBIE;
    song.sel = 0;
    ok &= eng_shown() == 2u && eng_vis(0) == ENGI_SID && eng_vis(1) == ENGI_OBIE && eng_step(ENGI_OBIE, 1) == ENGI_SID;
    song.sel = 1;
    ok &= eng_shown() == 1u && eng_vis(0) == ENGI_KIT && eng_step(ENGI_KIT, 1) == ENGI_KIT;
    song.sel = 0;
    bad += check("tracks: 1 SID OBIE, 2 KIT, 3 SPEECH; another track's engine loads as one of its own; the picker shows "
                 "the selected track's", ok);

    /* power-on: the engines.c picks, the second where the first is left out */
    bad += check("power-on: track 1 VA's WARM PAD or OBIE's PWM PAD (no VA here: OBIE), 2 KIT, 3 ONYX or SPEECH (SPEECH)",
                 TRK_DEF[0][0] == ENGI_OBIE && str_eq(ENGINES[ENGI_OBIE]->presets[TRK_DEF[0][1]].name, "PWM PAD") &&
                 TRK_DEF[1][0] == ENGI_KIT && TRK_DEF[2][0] == ENGI_SPEECH && !TRK_PAN[0] && !TRK_PAN[1] && !TRK_PAN[2]);

    {   /* (2026-10-17) the components (firmware/components.txt): TEST SUBSET leaves PLATE out */
        static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
        reset();
        song.g[G_RTYPE] = 3;                            /* (a song made where PLATE is: its id, 2026-10-18) */
        memset(c, 0, sizeof c);
        memset(d, 0, sizeof d);
        memset(r, 0, sizeof r);
        fx_buses(c, d, r, w, CTL);
        ok = !BLUEHARE_FX_PLATE && !BLUEHARE_FX_AMB && BLUEHARE_FX_EQ && BLUEHARE_PLUGIN_ARP && BLUEHARE_UI_STROBE &&
             GP[G_RTYPE].max == 2 && fx.rtype == 3u;
        song.g[G_RTYPE] = 2;
        bad += check("components: PLATE left out: REVERB TYPE ends at GATED, a song's PLATE plays GATED; EQ, ARP, STROBE in",
                     ok);
    }

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
    bad += check("INFO: the ids' names, \"-\" for a retired id or an engine left out", ok && !strcmp(N_ENGNAME[EID_VOSIM], "-"));
    song.sel = 2;                                       /* (the voices' track: VOSIM's substitute, SPEECH, plays there) */
    a[0] = 1; a[1] = G_ENGSEL; a[2] = (8192 + EID_VOSIM) & 127; a[3] = (8192 + EID_VOSIM) >> 7;
    request(ED_SET, a, 4);
    ok = TSEL->eng_req == ENGI_SPEECH && TSEL->preset == 0u;
    set_engine_of(TSEL, ENGI_SID);                      /* (not the voices': SPEECH) */
    ok &= TSEL->eng_req == ENGI_SPEECH;
    a[0] = EID_VOSIM; a[1] = 3;
    request(ED_PRESET, a, 2);
    ok &= TSEL->eng_req == ENGI_SPEECH && TSEL->preset == 0u && host_wire[5] == EID_SPEECH;
    a[0] = EID_VOSIM;
    request(ED_NAMES, a, 1);
    ok &= host_wire[5] == EID_VOSIM && host_wire[6] == 0;
    song.sel = 0;
    bad += check("  G_ENGSEL VOSIM on track 3: SPEECH; SID there: SPEECH; PRESET VOSIM 3: SPEECH's first (its id); NAMES "
                 "VOSIM: none", ok);

    /* OCT (Blue Hare): a preset sets it (every factory preset's within -4 .. 4), the next preset puts it back; a song
     * keeps it; a stored value outside -4 .. 4 (ENV DEST FX, which it was) loads as 0 */
    for (e = 0, ok = 1; e < NENGINES; e++)
        for (k = 0; k < ENGINES[e]->npresets; k++)
            ok &= ENGINES[e]->presets[k].oct >= -4 && ENGINES[e]->presets[k].oct <= 4;
    reset();
    set_engine_of(&trk[0], ENGI_SID);
    trk[0].p[P_OCT] = -2;
    apply_preset_to(&trk[0], 6);                        /* a preset with OCT 0 puts it back */
    ok &= trk[0].p[P_OCT] == ENGINES[ENGI_SID]->presets[6].oct;
    bad += check("OCT: every factory preset's within -4 .. 4; a preset load sets it", ok);
    trk[0].p[P_OCT] = -3;
    project_save(0);
    trk[0].p[P_OCT] = 0;
    project_load(0);
    ok = trk[0].p[P_OCT] == -3 && param_oct(P_OCT, 30) == 0 && param_oct(P_OCT, -65) == 0 && param_oct(P_OCT, -2) == -2 &&
         param_oct(P_DIST, 30) == 30 && TP[P_OCT].min == -4 && TP[P_OCT].max == 4 && str_eq(TP[P_OCT].label, "OCT");
    bad += check("  a song keeps OCT -3; an old ENV DEST FX value outside -4 .. 4 loads as 0 (not clamped to 4 octaves)", ok);
    {   /* the sound: 3 octaves down (SID BASS held at C4, its strongest line 100 .. 400 ms, OCT 0 against -3) */
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
            host_preset(&trk[0], ENGI_SID, 0);   /* (SID BASS; TRI FLUTE until the cut, 2026-10-18) */
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
        snprintf(dbg, sizeof dbg, "  SID BASS at C4: %.1f Hz, OCT -3 %.1f Hz (x %.2f: 3 octaves down)", bf[0], bf[1],
                 bf[1] > 0 ? bf[0] / bf[1] : 0);
        bad += check(dbg, bf[1] > 0 && bf[0] / bf[1] > 7.8 && bf[0] / bf[1] < 8.2);
    }

    /* projects: track 1 of a stored song re-stamped VOSIM (as a build with it saves it) */
    reset();
    set_engine_of(&trk[0], ENGI_OBIE);
    apply_preset_to(&trk[0], 2);
    trk[0].p[P_ATK] = 33;
    project_save(0);
    off = PROJ_HDR + 3u + 2u * G_COUNT + 1u + PROJ_NAME_LEN + 1u;   /* track 1's engine byte (after the track count) */
    ok = proj_ram[0][off] == EID_OBIE;
    proj_ram[0][off] = EID_VA;
    stream_rehash();
    project_load(0);
    e = trk[0].eng_req;
    ok &= e == ENGI_OBIE && trk[0].preset == 0u && trk[0].p[P_ATK] == 33;
    for (i = 0; i < 8u; i++)
        ok &= trk[0].p[P_E0 + i] == ENGINES[ENGI_OBIE]->presets[0].e[i];
    bad += check("project: a track of VA (left out) loads as OBIE, its first preset's engine values, the rest as saved",
                 ok);

    /* user presets */
    reset();
    up_store(4, "WAVY");
    up_rec(4)->engine = EID_VOSIM;                        /* (a record a build with VOSIM stored) */
    ok = up_used(4) && up_engine(4) == ENG_NO && !up_avail(4) && up_count() == 0u && up_load(4) == 1;
    bad += check("user preset of VOSIM (left out): kept (used), in no list, loads nothing", ok);

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
        bad += check("every engine left renders every preset (not silent)", loud == n && n > 15u);
    }
    /* the project format: every build writes version 13 (the track count after the name); a project of more tracks than
     * the build is refused, one of fewer fills its first tracks. build/host/song_3trk.bin carries a 3-track project from
     * this test's 3-track run to its 8-track one */
    {
        static const char V4[] = "build/host/song_3trk.bin";
        uint32_t k;
        reset();
        set_engine_of(&trk[1], ENGI_KIT);
        apply_preset_to(&trk[1], 1);
        trk[1].p[P_E2] = 77;
        trk[NTRK - 1u].p[P_ATK] = 21;
        project_save(0);
        ok = proj_ram[0][PROJ_HDR] == PROJ_VER && PROJ_VER == 16u;   /* (2026-10-18: the reverb MIX, the patches' delays) */
        set_engine_of(&trk[1], ENGI_SID);
        trk[NTRK - 1u].p[P_ATK] = 0;
        project_load(0);
        ok &= trk[1].eng_req == ENGI_KIT && trk[1].p[P_E2] == 77 && trk[NTRK - 1u].p[P_ATK] == 21;
        snprintf(dbg2, sizeof dbg2, "project: version %u at %u tracks, every track's sound kept", PROJ_VER, NTRK);
        bad += check(dbg2, ok);
        if (NTRK == 3) {                                /* a project of 8 tracks: refused, the music as it was */
            FILE *f;
            reset();
            set_engine_of(&trk[1], ENGI_KIT);
            trk[2].p[P_ATK] = 64;
            project_save(0);
            if ((f = fopen(V4, "wb"))) {                /* (for the 8-track run: a 3-track project) */
                for (k = 0; k < PROJ_CHUNKS; k++) {
                    uint16_t n = proj_ram_len[k];
                    fwrite(&n, 2, 1, f);
                    fwrite(proj_ram[k], 1, n, f);
                }
                fclose(f);
            }
            off = PROJ_HDR + 3u + 2u * G_COUNT + 1u + PROJ_NAME_LEN;   /* (the track count) */
            proj_ram[0][off] = 8;
            set_engine_of(&trk[2], ENGI_SID);
            project_load(0);
            bad += check("  a project of 8 tracks on a 3-track build: SONG HAS 8 TRACKS, the music as it was",
                         !strcmp(ui.msg, "SONG HAS 8 TRACKS") && trk[2].eng_req == eng_for_track(&trk[2], ENGI_SID));
        } else {                                        /* a 3-track project on 8 tracks */
            FILE *f = fopen(V4, "rb");
            if (!f) {
                printf("editor:   (no %s: run the 3-track engines test first; the 3-track load is not checked)\n", V4);
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
                project_load(0);
                ok = trk[1].eng_req == ENGI_KIT && trk[2].p[P_ATK] == 64 &&
                     trk[5].eng_req == eng_for_track(&trk[5], TRK_DEF[5][0]) && trk[5].p[P_E1] != 99;
                bad += check("  a 3-track project on 8 tracks: tracks 1 .. 3 as saved, 4 .. 8 as at power-on", ok);
            }
        }
    }

    {   /* (Blue Hare) the engines' block of a track (engines.c eng_slab): one a track, as big as this build's
         * largest needs; lent to a key, cleared when another key asks; nothing for a non-part or too big an ask */
        uint8_t *x = eng_slab(&trk[2], SLAB_STRINGS, 2), *y;
        ok = x != 0;
        x[1] = 77;
        ok &= eng_slab(&trk[2], SLAB_STRINGS, 2) == (void *)x && x[1] == 77;   /* (the same key: kept) */
        y = eng_slab(&trk[2], SLAB_MODAL, 2);
        ok &= y == x && x[1] == 0;                                            /* (another: cleared) */
        x[1] = 9;
        ok &= eng_slab(&trk[1], SLAB_MODAL, 2) != (void *)x && x[1] == 9;    /* (another track's: its own) */
        ok &= !eng_slab(&trk[2], SLAB_MODAL, sizeof eng_slab_mem[0] + 1u) && !eng_slab(&trk[NPART], SLAB_MODAL, 4);
        bad += check("engines' block: one a track, the largest this build needs; another engine's ask clears it; too "
                     "big or no part: none", ok);
    }
    printf("%s\n", bad ? "ENGINES TEST FAILED" : "engines test passed");
    return bad != 0;
}
