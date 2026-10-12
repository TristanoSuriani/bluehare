/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* A distribution without some engines (1.5, distributions/*.txt): run_tests.sh builds this as FELUCCA_DIST=idm, which has
 * no VOICE, TRIO, WHEEL, FM6, ANALOG, PHYS, PHASE, NOISE. The engines left are numbered 0.. in the distribution's order; the ids stay what the stores and the protocol use, and a
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
    static const char *const ORDER[] = {"SID", "ACID", "WAVE", "BREAKS", "WEST", "HOOVER", "DUBSIREN", "SIREN8",
                                        "KARPLUS", "VOSIM", "PARTICLE", "CHAOS", "FMKEYS", "KIT", "SPEECH", "RISERS"};
    uint8_t a[16] = {0};
    int bad = 0, ok = 1, sub;
    uint32_t i, e, off;
    char dbg2[200];

    /* the table */
    ok = NENGINES == NELEM(ORDER) && !FELUCCA_ENG_VOICE && !FELUCCA_ENG_TRIO && !FELUCCA_ENG_WHEEL && !FELUCCA_ENG_FM6 && !FELUCCA_ENG_ANALOG &&
         !FELUCCA_ENG_PHYS && !FELUCCA_ENG_PHASE && !FELUCCA_ENG_NOISE && FELUCCA_ENG_HOOVER;
    for (i = 0; ok && i < NENGINES; i++)
        ok &= str_eq(ENGINES[i]->name, ORDER[i]) && eng_of_id(eng_id(i)) == i;
    ok &= eng_of_id(EID_TRIO) == ENG_NO && eng_of_id(EID_WHEEL) == ENG_NO && eng_of_id(EID_VOICE) == ENG_NO &&
          ENGI_TRIO == ENG_ABSENT && str_eq(N_ENGNAME[EID_TRIO], "-") && eng_by_id(EID_WHEEL) == &ENG_NONE;
    bad += check("engines: IDM, 16 in its order from 0, the ids both ways; VOICE TRIO WHEEL FM6 ANALOG PHYS PHASE NOISE none", ok);
    ok = eng_load_id(EID_HOOVER, &sub) == ENGI_HOOVER && !sub && eng_load_id(EID_WHEEL, &sub) == ENGI_WAVE && sub &&
         eng_load_id(EID_FM6, &sub) == ENGI_WAVE && sub &&
         eng_load_id(EID_VOICE, &sub) == ENGI_SPEECH && sub && eng_load_id(EID_TRIO, &sub) == ENGI_SID && sub &&
         eng_load_id(EID_ANALOG, &sub) == ENGI_SID && sub && eng_load_id(EID_PHYS, &sub) == ENGI_KARPLUS && sub &&
         eng_load_id(EID_FMPERC, &sub) == ENGI_KIT && sub && ENGI_FMPERC == ENG_ABSENT &&
         eng_load_id(EID_WAVE, &sub) == ENGI_WAVE && !sub && eng_load_id(13, &sub) == ENG_NO;
    bad += check("  substitutes: WHEEL, FM6 -> WAVE; VOICE -> SPEECH; TRIO, ANALOG -> SID; PHYS -> KARPLUS; FMPERC (gone) -> KIT; retired: none", ok);

    /* power-on */
    reset();                                            /* (tk_defaults: the instruments of the power-on parts) */
    bad += check("power-on: SID BASS (no ANALOG), VOSIM FORMANT PAD (no FM6), SID SYNC LEAD, FM KIT; the instruments by id",
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
    bad += check("INFO: the ids' names, \"-\" for a retired id or an engine left out", ok && !strcmp(N_ENGNAME[EID_TRIO], "-"));
    a[0] = 1; a[1] = G_ENGSEL; a[2] = (8192 + EID_WHEEL) & 127; a[3] = (8192 + EID_WHEEL) >> 7;
    request(ED_SET, a, 4);
    ok = TSEL->eng_req == ENGI_WAVE && TSEL->preset == 0u;
    a[0] = EID_TRIO; a[1] = 3;
    request(ED_PRESET, a, 2);
    ok &= TSEL->eng_req == ENGI_SID && TSEL->preset == 0u && host_wire[5] == EID_SID;
    a[0] = EID_TRIO;
    request(ED_NAMES, a, 1);
    ok &= host_wire[5] == EID_TRIO && host_wire[6] == 0;
    bad += check("  G_ENGSEL WHEEL: WAVE; PRESET TRIO 3: SID's first preset (replies its id); NAMES TRIO: none", ok);

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

    /* DUBSIREN on the last track alone (its 16-bit echo is that track's); on another a DUBSIREN sound is DUBSIREN8 with
     * the same preset and values: a pick, an instrument, a song */
    reset();
    set_engine_of(&trk[0], ENGI_DUBSIREN);
    ok = trk[0].eng_req == ENGI_DUBSIREN8;
    set_engine_of(&trk[NPART - 1], ENGI_DUBSIREN);
    ok &= trk[NPART - 1].eng_req == ENGI_DUBSIREN && ENGINES[ENGI_DUBSIREN]->last_track && !ENGINES[ENGI_DUBSIREN8]->last_track;
    tk.instr[0].src = EID_DUBSIREN;
    tk.instr[0].preset = 4;                             /* LASER */
    tk_inst_apply(&trk[1], 1);
    ok &= trk[1].eng_req == ENGI_DUBSIREN8 && trk[1].preset == 4u && trk[1].p[P_E4] == DUBSIREN_PRESETS[4].e[4];
    tk_inst_apply(&trk[NPART - 1], 1);
    ok &= trk[NPART - 1].eng_req == ENGI_DUBSIREN && trk[NPART - 1].preset == 4u;
    bad += check("DUBSIREN: the last track's alone; a pick or an instrument on another track: DUBSIREN8, the same preset", ok);
    for (i = 0, ok = NELEM(DUBSIREN8_PRESETS) >= NELEM(DUBSIREN_PRESETS); i < NELEM(DUBSIREN_PRESETS); i++)
        ok &= !memcmp(DUBSIREN8_PRESETS[i].e, DUBSIREN_PRESETS[i].e, 8) && !memcmp(DUBSIREN8_PRESETS[i].env, DUBSIREN_PRESETS[i].env, 4) &&
              !memcmp(DUBSIREN8_PRESETS[i].fx, DUBSIREN_PRESETS[i].fx, 4) && DUBSIREN8_PRESETS[i].mono == DUBSIREN_PRESETS[i].mono;
    for (i = 0; i < 8u; i++)
        ok &= ENG_DUBSIREN8.edit[i].min == ENG_DUBSIREN.edit[i].min && ENG_DUBSIREN8.edit[i].max == ENG_DUBSIREN.edit[i].max;
    ok &= sizeof ds_echo == sizeof(ds_echo_t) && sizeof ds8_echo == NPART * sizeof(ds8_echo_t) && sizeof(ds8_echo_t) < 5200u;
    bad += check("  DUBSIREN8's first 12 presets are DUBSIREN's, its controls the same; one 16-bit line, an 8-bit one a track", ok);
    reset();
    set_engine_of(&trk[0], ENGI_DUBSIREN8);
    apply_preset_to(&trk[0], 4);
    trk[0].p[P_E1] = 99;
    project_save(0);
    off = PROJ_HDR + 3u + 2u * G_COUNT + 1u + PROJ_NAME_LEN;    /* track 1's engine byte */
    proj_ram[0][off] = EID_DUBSIREN;                    /* (a song with DUBSIREN on track 1, as before this rule) */
    stream_rehash();
    project_new();
    project_load(0);
    bad += check("  a song with DUBSIREN on track 1 loads it as DUBSIREN8, its preset and values as saved",
                 trk[0].eng_req == ENGI_DUBSIREN8 && trk[0].preset == 4u && trk[0].p[P_E1] == 99);
    {   /* the voices end: DUBSIREN8's 8-bit echo dies to silence; DUBSIREN forced onto track 1 plays dry, and ends */
        int32_t o[2 * CTL];
        uint32_t f, m, end[2], pk[2];
        for (m = 0; m < 2u; m++) {
            reset();                                    /* (no song, no instruments of the tests above) */
            host_tracks_init();
            host_preset(&trk[0], m ? ENGI_DUBSIREN : ENGI_DUBSIREN8, 8);   /* RUNAWAY ECHO / GRIT RUNAWAY */
            panic_req = 0;                              /* (reset's preset loads: their release is not this note's) */
            trk_note_on(&trk[0], 60, 110);
            for (f = 0, pk[m] = 0, end[m] = 0; f < 30u * FS; f += CTL) {
                uint32_t k, on = 0;
                mix_block(o, CTL);
                if (f / CTL == FS / CTL)                    /* let go after 1 s */
                    trk_note_off(&trk[0], 60);
                for (k = 0; k < CTL; k++)
                    pk[m] = (uint32_t)abs(o[2 * k]) > pk[m] ? (uint32_t)abs(o[2 * k]) : pk[m];
                for (k = 0; k < NVOICE; k++)
                    on |= trk[0].v[k].active;
                if (f > FS && !on) {
                    end[m] = f;
                    break;
                }
            }
        }
        snprintf(dbg2, sizeof dbg2, "  DUBSIREN8 FDBK 118 let go: its voice ends after %.1f s (peak %u); DUBSIREN on track 1, dry: %.1f s (peak %u)",
                 (end[0] - FS) / (double)FS, pk[0], (end[1] - FS) / (double)FS, pk[1]);
        bad += check(dbg2, end[0] && end[1] && pk[0] > 2000u && pk[1] > 2000u);
    }

    /* projects: track 1 of a stored song re-stamped TRIO (as a full build saves it) */
    reset();
    set_engine_of(&trk[0], ENGI_WAVE);
    apply_preset_to(&trk[0], 2);
    trk[0].p[P_ATK] = 33;
    *tk_at(0, 0, 0) = tk_pack(60, 0, 0, 0);
    project_save(0);
    off = PROJ_HDR + 3u + 2u * G_COUNT + 1u + PROJ_NAME_LEN;    /* track 1's engine byte */
    ok = proj_ram[0][off] == EID_WAVE;
    proj_ram[0][off] = EID_TRIO;
    stream_rehash();
    project_new();
    project_load(0);
    e = trk[0].eng_req;
    ok &= e == ENGI_SID && trk[0].preset == 0u && trk[0].p[P_ATK] == 33 && tk_note(tk_at(0, 0, 0)) == 60u;
    for (i = 0; i < 8u; i++)
        ok &= trk[0].p[P_E0 + i] == ENGINES[ENGI_SID]->presets[0].e[i];
    bad += check("project: a track of TRIO (left out) loads as SID, its first preset's engine values, the rest as saved",
                 ok);

    /* user presets */
    reset();
    up_store(4, "WAVY");
    up_rec(4)->engine = EID_WHEEL;                     /* (a record the default distribution stored) */
    ok = up_used(4) && up_engine(4) == ENG_NO && !up_avail(4) && up_count() == 0u && up_load(4) == 1;
    bad += check("user preset of WHEEL (left out): kept (used), in no list, loads nothing", ok);

    /* the song */
    reset();
    tk.instr[9].src = EID_WHEEL; tk.instr[9].preset = 3;
    fm1_irq_off();
    tk_inst_apply(&trk[2], 10);
    fm1_irq_on();
    bad += check("instrument of WHEEL (left out): plays WAVE's first preset (FM6 left out too)", trk[2].eng_req == ENGI_WAVE && trk[2].preset == 0u);

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
    printf("%s\n", bad ? "ENGINES TEST FAILED" : "engines test passed");
    return bad != 0;
}
