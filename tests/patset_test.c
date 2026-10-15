/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* A pattern's own settings (tracker.c tk_var_*: the variations' A, since the 2026-10 plan the only one): a pattern
 * playing its tempo, mutes, chance, the tracks' sends and SLICER over their own values (which stay as they are), all
 * of it gone at STOP; the song store keeping them and bounding what it reads; the editor's ORDER (plain patterns),
 * VARIATION (A alone) and VARCOUNT (1 alone). */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

/* n rows of the song playing (the transport started by the caller): blocks until tk_pl.rows grew by n */
static void rows(uint32_t n)
{
    int32_t o[2 * CTL];
    uint32_t want = tk_pl.rows + n, guard = 0;
    while (tk_pl.rows < want && guard++ < 200000u)
        mix_block(o, CTL);
}
static void play_song(uint32_t pos)
{
    int32_t o[2 * CTL];
    tk_pl.mode = TK_SONG;
    tk_pl.start_pos = (uint8_t)pos;
    transport_req = 1;
    mix_block(o, CTL);
    mix_block(o, CTL);
}
static void stop(void)
{
    int32_t o[2 * CTL];
    transport_req = 2;
    mix_block(o, CTL);
    mix_block(o, CTL);
}

int main(void)
{
    int bad = 0, ok;
    uint32_t r, n0, n1, i;
    reset();
    /* patterns 0 and 1: a note every row on tracks 1 and 2; the order: 0, 1 */
    for (r = 0; r < TK_ROWS; r++) {
        *tk_at(0, r, 0) = *tk_at(1, r, 0) = tk_pack(48u + r % 12u, 0, 0, 0);
        *tk_at(0, r, 1) = *tk_at(1, r, 1) = tk_pack(60u + r % 7u, 0, 0, 0);
    }
    tk.len[0] = tk.len[1] = 16;
    tk.order[0] = TK_OENT(0, 0);
    tk.order[1] = TK_OENT(1, 0);
    tk.norder = 2;
    bad += check("one set of settings a pattern (TK_VARS 1); a pattern's are everything as the tracks have it",
                 TK_VARS == 1u && tk_var_is_default(&tk_var[1][0]) && tk_var[1][0].chance[0] == 100u && !tk_var[1][0].bpm &&
                 !tk_var[1][0].mute && tk_nvar[1] == 1u);

    /* tempo */
    song.g[G_BPM] = 120;
    tk_var[1][0].bpm = 90;
    play_song(0);
    ok = song_bpm() == 120u && tk_pl.pat == 0u;
    rows(16);                                           /* into 1 */
    ok &= tk_pl.pos == 1u && tk_pl.pat == 1u && song_bpm() == 90u && song.g[G_BPM] == 120;
    rows(16);                                           /* round to 0 */
    ok &= tk_pl.pos == 0u && song_bpm() == 120u;
    stop();
    ok &= song_bpm() == 120u && !tempo_ovr;
    bad += check("tempo: pattern 1 plays at its 90 BPM, 0 at the song's 120; the song's own BPM untouched; STOP: the song's", ok);

    /* the sends and the SLICER over the track's own */
    trk[0].p[P_REV] = 20;
    trk[0].p[P_SLCR] = SL_OFF;
    tk_var[1][0].vo[0][VO_REV] = 100 + 1;
    tk_var[1][0].vo[0][VO_SLCR] = SL_GATE + 1;
    tk_var[1][0].vo[0][VO_SLPAT] = 5 + 1;
    play_song(1);
    ok = tp_vo(&trk[0], VO_REV) == 100 && tp_vo(&trk[0], VO_SLCR) == SL_GATE && tp_vo(&trk[0], VO_SLPAT) == 5 &&
         tp_vo(&trk[0], VO_DLY) == trk[0].p[P_DLY] && tp_vo(&trk[1], VO_REV) == trk[1].p[P_REV] &&
         trk[0].p[P_REV] == 20 && trk[0].p[P_SLCR] == SL_OFF;
    trk[0].p[P_REV] = 33;                               /* (an edit while it plays: the track's own value) */
    ok &= tp_vo(&trk[0], VO_REV) == 100;
    stop();
    ok &= tp_vo(&trk[0], VO_REV) == 33 && tp_vo(&trk[0], VO_SLCR) == SL_OFF;
    bad += check("sends and SLICER: pattern 1's REV 100, GATE pattern 5 on track 1 over its own (REV 20, an edit to 33 "
                 "kept); KEEP elsewhere; STOP: the track's own", ok);
    memset(tk_var[1][0].vo, 0, sizeof tk_var[1][0].vo);
    tk_var[1][0].bpm = 0;

    /* mutes */
    tk_var[1][0].mute = 2u;                             /* track 2 */
    play_song(1);
    n0 = 0;
    for (i = 0; i < 12u; i++) {                         /* (track 2's voices: none started while muted) */
        uint32_t act = 0, j;
        rows(1);
        for (j = 0; j < NVOICE; j++)
            act |= trk[1].v[j].active && trk[1].v[j].gate;
        n0 += act;
    }
    stop();
    play_song(0);
    for (i = 0, n1 = 0; i < 12u; i++) {
        uint32_t act = 0, j;
        rows(1);
        for (j = 0; j < NVOICE; j++)
            act |= trk[1].v[j].active && trk[1].v[j].gate;
        n1 += act;
    }
    stop();
    bad += check("mutes: pattern 1's mute of track 2: none of its notes; in pattern 0 they play", n0 == 0u && n1 >= 10u);
    tk_var[1][0].mute = 0;

    /* chance */
    tk_var[1][0].chance[0] = 0;
    tk_var[1][0].chance[1] = 50;
    play_song(1);
    {
        uint32_t a = 0, b = 0;
        uint8_t last0 = 0, last1 = 0;
        for (i = 0; i < 15u; i++) {                     /* (stay in 1: 16 rows, the first played at PLAY) */
            rows(1);
            a += tk_pl.note[0] != 0u && tk_pl.note[0] != last0;
            b += tk_pl.note[1] != last1;
            last0 = tk_pl.note[0];
            last1 = tk_pl.note[1];
        }
        ok = a == 0u && b > 0u && b < 15u;
        stop();
        play_song(1);                                   /* again: the same rows play (the same seed at every PLAY) */
        for (i = 0, last1 = 0, n0 = 0; i < 15u; i++) {
            rows(1);
            n0 += tk_pl.note[1] != last1;
            last1 = tk_pl.note[1];
        }
        stop();
        ok &= n0 == b;
        bad += check("chance: 0 % plays nothing, 50 % some of the notes, the same ones at every PLAY", ok);
    }
    tk_var[1][0].chance[0] = tk_var[1][0].chance[1] = 100;

    /* the store keeps them */
    tk_var[1][0].bpm = 77;
    tk_var[1][0].mute = 1;
    tk_var[1][0].chance[1] = 42;
    tk_var[1][0].vo[1][VO_DLY] = 64 + 1;
    tk_var[3][0].bpm = 140;                             /* pattern 3's, no order entry */
    {
        tk_var_t keep1 = tk_var[1][0], keep3 = tk_var[3][0];
        ok = !project_save(0);
        project_new();
        ok &= tk_var_is_default(&tk_var[1][0]);
        project_load(0);
        ok &= !memcmp(&tk_var[1][0], &keep1, sizeof keep1) && !memcmp(&tk_var[3][0], &keep3, sizeof keep3) &&
              tk.order[1] == TK_OENT(1, 0) && proj_ram[0][PROJ_HDR] == PROJ_VER;
        bad += check("the store keeps a pattern's settings (1, and 3 in no order entry)", ok);
    }

    /* bounds: stored settings inside their ranges; an order entry's variation bits gone */
    tk_var[2][0].bpm = 250;
    tk_var[2][0].chance[0] = 180;
    tk_var[2][0].vo[0][VO_SLCR] = 9;
    tk_var[2][0].vo[0][VO_SLPAT] = 1;                   /* (pattern 0: none such) */
    tk.order[3] = (uint8_t)(1u | 3u << 5);              /* (an old entry of 1D) */
    tk.norder = 4;
    tk_bound();
    bad += check("bounds: BPM 250 -> SONG, chance 180 -> 100, SLICER 9 and pattern 0 -> KEEP; an old entry 1D plays 1",
                 !tk_var[2][0].bpm && tk_var[2][0].chance[0] == 100u && !tk_var[2][0].vo[0][VO_SLCR] &&
                 !tk_var[2][0].vo[0][VO_SLPAT] && tk_nvar[1] == 1u && TK_OPAT(tk.order[3]) == 1u && !TK_OVAR(tk.order[3]));

    /* the editor's ORDER: plain patterns */
    {
        uint8_t a[8] = {4, 2, 0, TK_OENT(2, 0), TK_OENT(5, 0)};
        uint32_t m = request(ED_TRACKER, a, 5);
        bad += check("the editor's ORDER: 2 5 taken", m > 0u && tk.norder == 2u && tk.order[0] == TK_OENT(2, 0) &&
                     tk.order[1] == TK_OENT(5, 0) && tk_nvar[2] == 1u);
    }
    /* the editor's VARIATION (a pattern's settings, A alone) and VARCOUNT (1 alone) */
    {
        static uint8_t a[4 + EDT_VAR_N];
        uint32_t m, k, j, w = 7u;                       /* (w: the reply's values after op, rc) */
        reset();
        tk.order[0] = TK_OENT(2, 0);
        tk.norder = 1;
        a[0] = EDT_VAR; a[1] = 2; a[2] = 0;
        m = request(ED_TRACKER, a, 3);
        ok = m == 11u + EDT_VAR_N && !host_wire[6] && host_wire[w] == 2 && host_wire[w + 1] == 0 &&
             host_wire[w + 2] == 1 && host_wire[w + 3 + 4] == 100;
        for (k = 0; k < NTRK * VO_N; k++)               /* (KEEP: -1) */
            ok &= ed_rv(&host_wire[w + 7 + NTRK + 2 * k]) == -1;
        bad += check("VARIATION read: pattern 2's settings: SONG, no mute, 100 %, every value KEEP", ok && !ed_rv(&host_wire[w + 3]));
        {   /* bpm 96, track 2 muted, chance 60 on track 1, track 1's DELAY 50 and SLICER GATE pattern 3 */
            uint8_t *x = a + 3;
            memset(x, 0, EDT_VAR_N);
            x[0] = (96 + 8192) & 127; x[1] = (96 + 8192) >> 7;
            x[2] = (2 + 8192) & 127; x[3] = (2 + 8192) >> 7;
            for (k = 0; k < NTRK; k++)
                x[4 + k] = k ? 100 : 60;
            for (k = 0; k < NTRK; k++)
                for (j = 0; j < VO_N; j++) {
                    int32_t v = k == 0 && j == VO_DLY ? 50 : k == 0 && j == VO_SLCR ? SL_GATE : k == 0 && j == VO_SLPAT ? 3 : -1;
                    x[4 + NTRK + 2 * (k * VO_N + j)] = (uint8_t)((v + 8192) & 127);
                    x[5 + NTRK + 2 * (k * VO_N + j)] = (uint8_t)((v + 8192) >> 7);
                }
        }
        m = request(ED_TRACKER, a, 3 + EDT_VAR_N);
        ok = m == 11u + EDT_VAR_N && !host_wire[6] && tk_var[2][0].bpm == 96u && tk_var[2][0].mute == 2u &&
             tk_var[2][0].chance[0] == 60u && tk_var[2][0].vo[0][VO_DLY] == 51u &&
             tk_var[2][0].vo[0][VO_SLCR] == SL_GATE + 1u && tk_var[2][0].vo[0][VO_SLPAT] == 4u && !tk_var[2][0].vo[1][VO_DLY];
        bad += check("VARIATION write: pattern 2's tempo, mute, chance, DELAY and SLICER", ok);
        a[3] = (250 + 8192) & 127; a[4] = (250 + 8192) >> 7;
        bad += check("  a BPM of 250: rc 1, as it was", request(ED_TRACKER, a, 3 + EDT_VAR_N) && host_wire[6] == 1 &&
                     tk_var[2][0].bpm == 96u);
        bad += check("  a pattern past the last, a variation B: no reply", (a[1] = TK_PATS, !request(ED_TRACKER, a, 3)) &&
                     (a[1] = 2, a[2] = 1, !request(ED_TRACKER, a, 3)));
        a[0] = EDT_VARCOUNT; a[1] = 2;
        m = request(ED_TRACKER, a, 2);
        ok = m == 10u && host_wire[7] == 2 && host_wire[8] == 1;
        a[2] = 1;
        ok &= request(ED_TRACKER, a, 3) == 10u && !host_wire[6] && tk_nvar[2] == 1u;
        a[2] = 2;
        ok &= request(ED_TRACKER, a, 3) == 10u && host_wire[6] == 1 && tk_nvar[2] == 1u;
        bad += check("VARCOUNT: 1 a pattern; 1 taken, 2 refused (rc 1)", ok);
        {   /* a device edit of a pattern's settings changes the song's signature (TRACKER_CHANGED kind 1) */
            uint32_t s0 = ed_tk_sig(TK_PATS);
            tk_var[2][0].chance[0] = 7;
            bad += check("the song's signature: a pattern's settings edited change it", ed_tk_sig(TK_PATS) != s0);
        }
    }
    printf("%s\n", bad ? "PATTERN SETTINGS TEST FAILED" : "pattern settings test passed");
    return bad ? 1 : 0;
}
