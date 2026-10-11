/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A distribution without some engines (1.5, distributions/*.txt): run_tests.sh builds this as FELUCCA_DIST=idm, which has
 * no VOICE, TRIO, WHEEL, FM6. The engines left are numbered 0.. in the distribution's order; the ids stay what the stores and the protocol use, and a
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
    static const char *const ORDER[] = {"ANALOG", "PHASE", "WAVE", "BYTE", "WEST", "HOOVER", "PHYS", "KARPLUS", "VOSIM",
                                        "NOISE", "DRUM", "FMPERC", "SPEECH", "LOFI"};
    uint8_t a[16] = {0};
    int bad = 0, ok = 1, sub;
    uint32_t i, e, off;

    /* the table */
    ok = NENGINES == NELEM(ORDER) && !FELUCCA_ENG_VOICE && !FELUCCA_ENG_TRIO && !FELUCCA_ENG_WHEEL && !FELUCCA_ENG_FM6 && FELUCCA_ENG_HOOVER;
    for (i = 0; ok && i < NENGINES; i++)
        ok &= str_eq(ENGINES[i]->name, ORDER[i]) && eng_of_id(eng_id(i)) == i;
    ok &= eng_of_id(EID_TRIO) == ENG_NO && eng_of_id(EID_WHEEL) == ENG_NO && eng_of_id(EID_VOICE) == ENG_NO &&
          ENGI_TRIO == ENG_ABSENT && str_eq(N_ENGNAME[EID_TRIO], "-") && eng_by_id(EID_WHEEL) == &ENG_NONE;
    bad += check("engines: IDM, 14 in its order from 0, the ids both ways; VOICE TRIO WHEEL FM6 none", ok);
    ok = eng_load_id(EID_HOOVER, &sub) == ENGI_HOOVER && !sub && eng_load_id(EID_WHEEL, &sub) == ENGI_WAVE && sub &&
         eng_load_id(EID_FM6, &sub) == ENGI_WAVE && sub &&
         eng_load_id(EID_VOICE, &sub) == ENGI_SPEECH && sub && eng_load_id(EID_TRIO, &sub) == ENGI_ANALOG && sub &&
         eng_load_id(EID_WAVE, &sub) == ENGI_WAVE && !sub && eng_load_id(13, &sub) == ENG_NO;
    bad += check("  substitutes: HOOVER itself, WHEEL -> FM6 -> WAVE, FM6 -> WAVE, VOICE -> SPEECH, TRIO -> ANALOG; a retired id: none", ok);

    /* power-on */
    reset();                                            /* (tk_defaults: the instruments of the power-on parts) */
    bad += check("power-on: ANALOG ACID, VOSIM FORMANT PAD (no FM6), LOFI PULSE LD, DRUM KIT here; the song's instruments by id",
                 TRK_DEF[0][0] == ENGI_ANALOG && TRK_DEF[0][1] == 4u && TRK_DEF[1][0] == ENGI_VOSIM &&
                 str_eq(ENGINES[ENGI_VOSIM]->presets[TRK_DEF[1][1]].name, "FORMANT PAD") &&
                 TRK_DEF[2][0] == ENGI_LOFI && TRK_DEF[3][0] == ENGI_DRUM && tk.instr[1].src == EID_VOSIM &&
                 tk.instr[3].src == EID_DRUM);

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
    ok &= TSEL->eng_req == ENGI_ANALOG && TSEL->preset == 0u && host_wire[5] == EID_ANALOG;
    a[0] = EID_TRIO;
    request(ED_NAMES, a, 1);
    ok &= host_wire[5] == EID_TRIO && host_wire[6] == 0;
    bad += check("  G_ENGSEL WHEEL: WAVE; PRESET TRIO 3: ANALOG's first preset (replies its id); NAMES TRIO: none", ok);

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
    ok &= e == ENGI_ANALOG && trk[0].preset == 0u && trk[0].p[P_ATK] == 33 && tk_note(tk_at(0, 0, 0)) == 60u;
    for (i = 0; i < 8u; i++)
        ok &= trk[0].p[P_E0 + i] == ENGINES[ENGI_ANALOG]->presets[0].e[i];
    bad += check("project: a track of TRIO (left out) loads as ANALOG, its first preset's engine values, the rest as saved",
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
