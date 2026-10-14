/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The tracker's PLAY commands (tracker.c TKC_PLAY: the FX column's last command): a row of a 1/16 at 60 BPM (11025
 * samples), one track, the note's starts counted at the voices (a new voice, or one struck again):
 *   T RETRIG 4: four starts in the row, a quarter of it apart; 1: one (none extra)
 *   Y NOTE DELAY 8: the note starts half the row late, its instrument and FX at once
 *   J ARPEGGIO 3 on a MAJ chord: its three notes in turn, a third of the row each; 0 the same (3 steps)
 *   U SLIDE UP 12: the track's pitch an octave up by the row's end, there on the next (empty) row, back at the next note
 *   N SLIDE DOWN 5: five semitones down
 *   % CHANCE: 0 never plays, F always, 7 about half the time
 *   a row without PLAY plays as before; STOP lets go of the slide */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void ck(const char *what, int ok) { printf("tkplay: %-104s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static uint32_t eng_of(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!strcmp(ENGINES[e]->name, n))
            return e;
    return 0;
}

/* note starts: a voice whose age is new (voice.c voice_new: age set at its start) or struck again (age changes) */
static uint32_t ages[NVOICE];
static uint32_t starts(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) {
        if (trk[0].v[i].active && trk[0].v[i].age != ages[i])
            n++;
        ages[i] = trk[0].v[i].active ? trk[0].v[i].age : 0u;
    }
    return n;
}

/* one pattern of `rows` rows (cells[]), LOOP, PLAY; the starts in each block into at[] (their sample), up to max */
static void fresh(void)                              /* the last run stopped, its voices gone */
{
    int32_t o[2 * CTL];
    uint32_t i;
    host_tracks_init();                              /* (first: a tempo, so the clocks have a length) */
    transport_req = 2;
    mix_block(o, CTL);
    for (i = 0; i < NVOICE; i++)
        trk[0].v[i].active = 0;
    host_tracks_init();
}

static uint32_t play(const tk_cell_t *cells, uint32_t rows, uint32_t frames, uint32_t *at, uint32_t max)
{
    uint32_t r, done = 0, n = 0, s;
    fresh();
    song.g[G_BPM] = 60;
    host_preset(&trk[0], eng_of("WAVE"), 0);
    trk[0].p[P_VOICE] = V_POLY;
    tk.len[0] = (uint8_t)rows;
    for (r = 0; r < rows; r++)
        *tk_at(0, r, 0) = cells[r];
    memset(ages, 0, sizeof ages);
    tk_pl.mode = TK_LOOP;
    tk_pl.loop_pat = 0;
    transport_req = 1;
    while (done < frames) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (s = starts(); s && n < max; s--)
            at[n++] = done;
        done += CTL;
    }
    return n;
}

int main(void)
{
    static uint32_t at[256];
    const uint32_t ROW = 11025u;                      /* a 1/16 at 60 BPM */
    tk_cell_t c[4];
    uint32_t n, i;
    char what[200];
    int ok;

    memset(c, 0, sizeof c);
    c[0] = tk_pack(60, 0, TKC_PLAY, TKP_RETRIG << 4 | 4u);
    n = play(c, 4, ROW, at, 64);
    ok = n == 4u;
    for (i = 1; ok && i < 4u; i++)
        ok = (int32_t)(at[i] - i * ROW / 4u) >= -(int32_t)CTL && at[i] - i * ROW / 4u < 2u * CTL;
    snprintf(what, sizeof what, "T RETRIG 4: %u starts in the row, a quarter apart (%u %u %u %u)", n, at[0], at[1], at[2], at[3]);
    ck(what, ok);
    c[0] = tk_pack(60, 0, TKC_PLAY, TKP_RETRIG << 4 | 1u);
    ck("T RETRIG 1: one start", play(c, 4, ROW, at, 64) == 1u);

    c[0] = tk_pack(60, 0, TKC_PLAY, TKP_DELAY << 4 | 8u);
    n = play(c, 4, ROW, at, 64);
    snprintf(what, sizeof what, "Y NOTE DELAY 8: the note half the row late (at %u of %u)", n ? at[0] : 0u, ROW);
    ck(what, n == 1u && at[0] + CTL >= ROW / 2u && at[0] < ROW / 2u + 2u * CTL);

    c[0] = tk_pack_ch(60, 0, TKC_PLAY, TKP_ARP << 4 | 3u, 4u | 7u << 4);   /* C MAJ */
    {
        uint8_t seen[3] = {0, 0, 0};
        uint32_t done = 0, k = 0;
        fresh();
        song.g[G_BPM] = 60;
        host_preset(&trk[0], eng_of("WAVE"), 0);
        trk[0].p[P_VOICE] = V_POLY;
        tk.len[0] = 4;
        memset(tk_row(0, 0), 0, sizeof(tk_cell_t) * NTRK);
        *tk_at(0, 0, 0) = c[0];
        for (i = 1; i < 4u; i++)
            memset(tk_row(0, i), 0, sizeof(tk_cell_t) * NTRK);
        tk_pl.mode = TK_LOOP;
        tk_pl.loop_pat = 0;
        transport_req = 1;
        while (done < ROW) {                          /* the column's note at a sixth, a half and five sixths */
            int32_t o[2 * CTL];
            mix_block(o, CTL);
            done += CTL;
            if (k < 3u && done >= (2u * k + 1u) * ROW / 6u)
                seen[k++] = tk_pl.note[0];
        }
        snprintf(what, sizeof what, "J ARPEGGIO 3 on C MAJ: C, E, G in turn (%u %u %u)", seen[0], seen[1], seen[2]);
        ck(what, seen[0] == 60u && seen[1] == 64u && seen[2] == 67u);
    }
    c[0] = tk_pack_ch(60, 0, TKC_PLAY, TKP_ARP << 4, 4u | 7u << 4);
    n = play(c, 4, ROW, at, 64);
    snprintf(what, sizeof what, "J ARPEGGIO 0: 3 steps too (%u starts)", n);
    ck(what, n == 3u);

    c[0] = tk_pack(60, 0, TKC_PLAY, TKP_SLUP << 4 | 12u);
    c[2] = tk_pack(62, 0, 0, 0);
    play(c, 4, ROW + ROW / 2u, at, 64);
    {
        int32_t at_mid = tk_slide_q8[0];
        uint32_t done = 0;
        while (done < ROW) {                          /* to row 2's start: the next note */
            int32_t o[2 * CTL];
            mix_block(o, CTL);
            done += CTL;
        }
        snprintf(what, sizeof what, "U SLIDE UP 12: an octave by the row's end, there on the empty row (%d / 256), back at the next note (%d)",
                 at_mid, tk_slide_q8[0]);
        ck(what, at_mid == 12 * 256 && tk_slide_q8[0] == 0);
    }
    c[2] = tk_pack(0, 0, 0, 0);
    c[0] = tk_pack(60, 0, TKC_PLAY, TKP_SLDN << 4 | 5u);
    play(c, 4, ROW / 2u, at, 64);
    snprintf(what, sizeof what, "N SLIDE DOWN 5: half the way at the row's middle (%d / 256)", tk_slide_q8[0]);
    ck(what, tk_slide_q8[0] <= -5 * 128 + 64 && tk_slide_q8[0] >= -5 * 128 - 128);
    transport_req = 2;
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
    ck("STOP: the slide let go", tk_slide_q8[0] == 0);

    for (i = 0; i < 4u; i++)
        c[i] = tk_pack(60, 0, TKC_PLAY, TKP_CHANCE << 4 | 0u);
    ck("% CHANCE 0: never", play(c, 4, ROW * 16u, at, 256) == 0u);
    for (i = 0; i < 4u; i++)
        c[i] = tk_pack(60, 0, TKC_PLAY, TKP_CHANCE << 4 | 15u);
    ck("% CHANCE F: always (16 rows, 16 notes)", play(c, 4, ROW * 16u - CTL, at, 256) == 16u);
    for (i = 0; i < 4u; i++)
        c[i] = tk_pack(60, 0, TKC_PLAY, TKP_CHANCE << 4 | 7u);
    n = play(c, 4, ROW * 64u - CTL, at, 256);
    snprintf(what, sizeof what, "%% CHANCE 7: about half (%u of 64)", n);
    ck(what, n >= 20u && n <= 44u);

    for (i = 0; i < 4u; i++)
        c[i] = tk_pack(i ? 0 : 60, 0, 0, 0);
    ck("a row without PLAY: one start, no slide", play(c, 4, ROW, at, 64) == 1u && tk_slide_q8[0] == 0);
    printf("%s\n", bad ? "TKPLAY TEST FAILED" : "tkplay test passed");
    return bad ? 1 : 0;
}
