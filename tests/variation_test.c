/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The tracker's variations (tracker.c tk_var_*): an order entry's variation playing its tempo, mutes, chance, the
 * tracks' sends and SLICER over their own values (which stay as they are), all of it gone at STOP; the song store
 * keeping them (version 4) and bounding what it reads; the editor's ORDER, VARIATION and VARCOUNT. */
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
    /* pattern 0: a note every row on tracks 1 and 2; the order: 0A, 0B */
    for (r = 0; r < TK_ROWS; r++) {
        *tk_at(0, r, 0) = tk_pack(48u + r % 12u, 0, 0, 0);
        *tk_at(0, r, 1) = tk_pack(60u + r % 7u, 0, 0, 0);
    }
    tk.len[0] = 16;
    tk.order[0] = TK_OENT(0, 0);
    tk.order[1] = TK_OENT(0, 1);
    tk.norder = 2;
    tk_nvar[0] = 2;
    bad += check("a new variation is everything as the tracks have it", tk_var_is_default(&tk_var[0][1]) &&
                 tk_var[0][1].chance[0] == 100u && !tk_var[0][1].bpm && !tk_var[0][1].mute);

    /* tempo */
    song.g[G_BPM] = 120;
    tk_var[0][1].bpm = 90;
    play_song(0);
    ok = song_bpm() == 120u && tk_pl.var == 0u;
    rows(16);                                           /* into 0B */
    ok &= tk_pl.pos == 1u && tk_pl.var == 1u && song_bpm() == 90u && song.g[G_BPM] == 120;
    rows(16);                                           /* round to 0A */
    ok &= tk_pl.pos == 0u && song_bpm() == 120u;
    stop();
    ok &= song_bpm() == 120u && !tempo_ovr;
    bad += check("tempo: 0B plays at its 90 BPM, 0A at the song's 120; the song's own BPM untouched; STOP: the song's", ok);

    /* the sends and the SLICER over the track's own */
    trk[0].p[P_REV] = 20;
    trk[0].p[P_SLCR] = SL_OFF;
    tk_var[0][1].vo[0][VO_REV] = 100 + 1;
    tk_var[0][1].vo[0][VO_SLCR] = SL_GATE + 1;
    tk_var[0][1].vo[0][VO_SLPAT] = 5 + 1;
    play_song(1);
    ok = tp_vo(&trk[0], VO_REV) == 100 && tp_vo(&trk[0], VO_SLCR) == SL_GATE && tp_vo(&trk[0], VO_SLPAT) == 5 &&
         tp_vo(&trk[0], VO_DLY) == trk[0].p[P_DLY] && tp_vo(&trk[1], VO_REV) == trk[1].p[P_REV] &&
         trk[0].p[P_REV] == 20 && trk[0].p[P_SLCR] == SL_OFF;
    trk[0].p[P_REV] = 33;                               /* (an edit while it plays: the track's own value) */
    ok &= tp_vo(&trk[0], VO_REV) == 100;
    stop();
    ok &= tp_vo(&trk[0], VO_REV) == 33 && tp_vo(&trk[0], VO_SLCR) == SL_OFF;
    bad += check("sends and SLICER: 0B's REV 100, GATE pattern 5 on track 1 over its own (REV 20, an edit to 33 kept); "
                 "KEEP elsewhere; STOP: the track's own", ok);
    memset(tk_var[0][1].vo, 0, sizeof tk_var[0][1].vo);

    /* mutes */
    tk_var[0][1].mute = 2u;                             /* track 2 */
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
    bad += check("mutes: 0B's mute of track 2: none of its notes; on 0A they play", n0 == 0u && n1 >= 10u);
    tk_var[0][1].mute = 0;

    /* chance */
    tk_var[0][1].chance[0] = 0;
    tk_var[0][1].chance[1] = 50;
    play_song(1);
    {
        uint32_t a = 0, b = 0, j;
        uint8_t last0 = 0, last1 = 0;
        for (i = 0; i < 15u; i++) {                     /* (stay in 0B: 16 rows, the first played at PLAY) */
            rows(1);
            a += tk_pl.note[0] != 0u && tk_pl.note[0] != last0;
            b += tk_pl.note[1] != last1;
            last0 = tk_pl.note[0];
            last1 = tk_pl.note[1];
        }
        (void)j;
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
    tk_var[0][1].chance[0] = tk_var[0][1].chance[1] = 100;

    /* the store keeps them (version 4) */
    tk_var[0][1].bpm = 77;
    tk_var[0][1].mute = 1;
    tk_var[0][1].chance[1] = 42;
    tk_var[0][1].vo[1][VO_DLY] = 64 + 1;
    tk_var[3][2].bpm = 140;                             /* a variation C of pattern 3, no order entry */
    tk_nvar[3] = 3;
    tk.order[2] = TK_OENT(3, 2);
    tk.norder = 3;
    {
        tk_var_t keep01 = tk_var[0][1], keep32 = tk_var[3][2];
        ok = !project_save(0);
        project_new();
        ok &= tk_var_is_default(&tk_var[0][1]) && tk_nvar[0] == 1u;
        project_load(0);
        ok &= !memcmp(&tk_var[0][1], &keep01, sizeof keep01) && !memcmp(&tk_var[3][2], &keep32, sizeof keep32) &&
              tk_nvar[0] == 2u && tk_nvar[3] == 3u && tk.order[1] == TK_OENT(0, 1) && tk.order[2] == TK_OENT(3, 2) &&
              proj_ram[0][PROJ_HDR] == 4u;
        bad += check("the store: version 4 keeps the variations (0B, 3C) and the order's letters", ok);
    }

    /* bounds: a stored variation's fields inside their ranges */
    tk_var[1][1].bpm = 250;
    tk_var[1][1].chance[0] = 180;
    tk_var[1][1].vo[0][VO_SLCR] = 9;
    tk_var[1][1].vo[0][VO_SLPAT] = 1;                   /* (pattern 0: none such) */
    tk.order[3] = TK_OENT(1, 3);                        /* (an entry of D, which 1 lacks) */
    tk.norder = 4;
    tk_bound();
    bad += check("bounds: BPM 250 -> SONG, chance 180 -> 100, SLICER 9 and pattern 0 -> KEEP; an entry's D: 1 has D now",
                 !tk_var[1][1].bpm && tk_var[1][1].chance[0] == 100u && !tk_var[1][1].vo[0][VO_SLCR] &&
                 !tk_var[1][1].vo[0][VO_SLPAT] && tk_nvar[1] == 4u);

    /* the editor's ORDER takes the letters */
    {
        uint8_t a[8] = {4, 2, 0, TK_OENT(2, 1), TK_OENT(5, 0)};
        uint32_t m;
        a[0] = 4; a[1] = 2; a[2] = 0;
        m = request(ED_TRACKER, a, 5);
        bad += check("the editor's ORDER: 2B 5A taken, 2 has B now", m > 0u && tk.norder == 2u &&
                     tk.order[0] == TK_OENT(2, 1) && tk_nvar[2] >= 2u);
    }
    /* the editor's VARIATION and VARCOUNT (ops 9, 10) */
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
        bad += check("VARIATION read: 2A, the pattern's 1, SONG, no mute, 100 %, every value KEEP", ok && !ed_rv(&host_wire[w + 3]));
        a[1] = 2; a[2] = 1;                             /* 2B: a new one */
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
        ok = m == 11u + EDT_VAR_N && !host_wire[6] && tk_nvar[2] == 2u && tk_var[2][1].bpm == 96u &&
             tk_var[2][1].mute == 2u && tk_var[2][1].chance[0] == 60u && tk_var[2][1].vo[0][VO_DLY] == 51u &&
             tk_var[2][1].vo[0][VO_SLCR] == SL_GATE + 1u && tk_var[2][1].vo[0][VO_SLPAT] == 4u && !tk_var[2][1].vo[1][VO_DLY];
        bad += check("VARIATION write at the count: 2B added with its tempo, mute, chance, DELAY and SLICER", ok);
        a[2] = 3;                                       /* 2D: past the count */
        bad += check("  past the count: rc 1, nothing added", request(ED_TRACKER, a, 3 + EDT_VAR_N) && host_wire[6] == 1 &&
                     tk_nvar[2] == 2u);
        a[2] = 1; a[3] = (250 + 8192) & 127; a[4] = (250 + 8192) >> 7;
        bad += check("  a BPM of 250: rc 1, 2B as it was", request(ED_TRACKER, a, 3 + EDT_VAR_N) && host_wire[6] == 1 &&
                     tk_var[2][1].bpm == 96u);
        bad += check("  a pattern past the last, a variation past D: no reply", (a[1] = TK_PATS, !request(ED_TRACKER, a, 3)) &&
                     (a[1] = 2, a[2] = TK_VARS, !request(ED_TRACKER, a, 3)));
        tk.order[0] = TK_OENT(2, 1);
        tk_pl.loop_pat = 2; tk_pl.loop_var = 1;
        a[0] = EDT_VARCOUNT; a[1] = 2;
        m = request(ED_TRACKER, a, 2);
        ok = m == 10u && host_wire[7] == 2 && host_wire[8] == 2;
        a[2] = 1;
        m = request(ED_TRACKER, a, 3);
        ok &= m == 10u && !host_wire[6] && host_wire[8] == 1 && tk_nvar[2] == 1u && tk_var_is_default(&tk_var[2][1]) &&
              tk.order[0] == TK_OENT(2, 0) && !tk_pl.loop_var;
        a[2] = 3;
        request(ED_TRACKER, a, 3);
        ok &= tk_nvar[2] == 3u && !memcmp(&tk_var[2][2], &tk_var[2][0], sizeof tk_var[2][0]);
        a[2] = 5;
        ok &= request(ED_TRACKER, a, 3) == 10u && host_wire[6] == 1 && tk_nvar[2] == 3u;
        bad += check("VARCOUNT: 2 has 2; to 1: B back to the default, the order's 2B and LOOP's on A; to 3: copies of A; 5: rc 1", ok);
        {   /* a device edit of a variation changes the song's signature (TRACKER_CHANGED kind 1) */
            uint32_t s0 = ed_tk_sig(TK_PATS);
            tk_var[2][1].chance[0] = 7;
            ok = ed_tk_sig(TK_PATS) != s0;
            s0 = ed_tk_sig(TK_PATS);
            tk_var[5][3].bpm = 99;                      /* (an unused one: not the song's) */
            bad += check("the song's signature: a variation's edit changes it, an unused variation's does not",
                         ok && ed_tk_sig(TK_PATS) == s0);
        }
    }
    printf("%s\n", bad ? "VARIATION TEST FAILED" : "variation test passed");
    return bad ? 1 : 0;
}
