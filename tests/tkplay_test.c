/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The tracker's RETRIG (tracker.c TKC_RETRIG: since the 2026-10 plan the only FX command; NOTE DELAY, ARPEGGIO, SLIDE
 * and CHANCE went with the parameter commands): a row of a 1/16 at 60 BPM (11025 samples), one track, the note's starts
 * counted at the voices (a new voice, or one struck again):
 *   T RETRIG 4: four starts in the row, a quarter of it apart; 1: one (none extra); 15: fifteen
 *   a row without it plays as before */
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
    c[0] = tk_pack(60, 0, TKC_RETRIG, 4u);
    n = play(c, 4, ROW, at, 64);
    ok = n == 4u;
    for (i = 1; ok && i < 4u; i++)
        ok = (int32_t)(at[i] - i * ROW / 4u) >= -(int32_t)CTL && at[i] - i * ROW / 4u < 2u * CTL;
    snprintf(what, sizeof what, "T RETRIG 4: %u starts in the row, a quarter apart (%u %u %u %u)", n, at[0], at[1], at[2], at[3]);
    ck(what, ok);
    c[0] = tk_pack(60, 0, TKC_RETRIG, 1u);
    ck("T RETRIG 1: one start", play(c, 4, ROW, at, 64) == 1u);
    c[0] = tk_pack(60, 0, TKC_RETRIG, 15u);
    n = play(c, 4, ROW, at, 64);
    snprintf(what, sizeof what, "T RETRIG 15: %u starts in the row", n);
    ck(what, n == 15u);
    for (i = 0; i < 4u; i++)
        c[i] = tk_pack(i ? 0 : 60, 0, 0, 0);
    ck("a row without RETRIG: one start", play(c, 4, ROW, at, 64) == 1u);
    printf("%s\n", bad ? "TKPLAY TEST FAILED" : "tkplay test passed");
    return bad ? 1 : 0;
}
