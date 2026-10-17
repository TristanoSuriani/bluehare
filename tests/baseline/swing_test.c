/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* Host test of SWING (issue #31), same sources as the firmware (through hostsim.c):
 *   build/host/swing_test
 * 1. the swing: CLOCK's SWG, 0..100 (core.h track_swing; 1.5: a track's own SWG, the step sequencer's, unread).
 * 2. the rows (the tests' song player, hostsim.c host_seq: the sequencer is a placeholder): at 100 they play 1.4 x /
 *    0.6 x the straight 1/16, measured from the transport.
 * 3. the display: SWG (0..100) shows its value, 100 is "100" (not 79); 0..127 percents unchanged. */
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main

static int check(const char *what, int ok)
{
    printf("swing: %-75s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void set_swing(track_t *t, int own, int global)
{
    t->p[P_SSWING] = (int16_t)own;
    song.g[G_SWING] = (int16_t)global;
}

/* ---------------------------------------------------------- 1. swing --- */
static int test_sum(void)
{
    static const int C[][3] = {{0, 0, 0}, {30, 20, 20}, {60, 40, 40}, {100, 0, 0}, {0, 100, 100}, {70, 50, 50},
                               {100, 120, 100}};
    uint32_t k, bad = 0;
    host_tracks_init();
    for (k = 0; k < sizeof C / sizeof C[0]; k++) {
        set_swing(&trk[0], C[k][0], C[k][1]);
        bad += track_swing(&trk[0]) != C[k][2];
    }
    return check("the swing: CLOCK's SWG at most 100, a track's own SWG unread (1.5)", !bad);
}

/* ---------------------------------------------------------- 2. rows --- */
/* the sample where each row starts (block exact: CTL), n rows from the transport start (an empty song of 64 rows) */
static uint32_t seq_starts(uint32_t *at, uint32_t n)
{
    uint32_t f, got = 0, last = 0xFFFFFFFFu;
    host_rows = HS_ROWS;
    transport_req = 1;
    for (f = 0; got < n && f < 20u * FS; f += CTL) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        if (song.playing && hs.row != last) {
            last = hs.row;
            at[got++] = f;
        }
    }
    transport_req = 2;
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
    return got;
}

static int test_seq(void)
{
    static const int C[] = {100, 50, 20, 0};
    uint32_t c, k, at[17];
    int bad = 0;
    char what[160];
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        uint32_t base, longs = 0, shorts = 0, err = 0, got;
        host_tracks_init();
        song.g[G_BPM] = 120;
        set_swing(&trk[0], 60, C[c]);
        base = div_samples(2);
        got = seq_starts(at, 17);
        for (k = 1; k < got; k++) {
            uint32_t len = at[k] - at[k - 1], idx = (k - 1u) & 1u;     /* row 0 is even (long) */
            int32_t want = (int32_t)base + ((idx ? -1 : 1) * C[c] * (int32_t)base / 250);
            err += abs((int32_t)len - want) > CTL;
            if (idx)
                shorts += len;
            else
                longs += len;
        }
        snprintf(what, sizeof what, "rows: SWG %d: rows %.2f x / %.2f x, pairs = 2 rows", C[c],
                 (double)longs / 8.0 / base, (double)shorts / 8.0 / base);
        bad += check(what, got == 17 && !err && abs((int32_t)(longs + shorts) - 16 * (int32_t)base) <= (int32_t)CTL);
    }
    /* the extremes: 1.4 x and 0.6 x, never past */
    host_tracks_init();
    set_swing(&trk[0], 100, 120);
    bad += check("rows at SWG 100 (and past): 1.4 x and 0.6 x of the straight 1/16",
                 swing_step_len(&trk[0], 6000, 0) == 8400 && swing_step_len(&trk[0], 6000, 1) == 3600);
    return bad;
}

/* -------------------------------------------------------- 3. display --- */
static const char *shown(const param_desc_t *d, int32_t v)
{
    static char val[12];
    const char *unit;
    param_format(d, v, val, &unit);
    return val;
}

static int test_display(void)
{
    int ok = 1;
    const param_desc_t *sw[2] = {&TP[P_SSWING], &GP[G_SWING]};
    uint32_t k;
    for (k = 0; k < 2u; k++)
        ok &= sw[k]->fmt == F_PCT && sw[k]->min == 0 && sw[k]->max == 100 && !strcmp(shown(sw[k], 100), "100") &&
              !strcmp(shown(sw[k], 50), "50") && !strcmp(shown(sw[k], 0), "0");
    ok &= !strcmp(shown(&TP[P_SUS], 127), "100") && !strcmp(shown(&TP[P_SUS], 64), "50") &&
          !strcmp(shown(&TP[P_DLY], 0), "0");
    return check("display: SWG / global SWG 100 -> 100 %, 50 -> 50 %; 0..127 percents unchanged", ok);
}

int main(void)
{
    int bad = test_sum() + test_seq() + test_display();
    printf("swing: %s\n", bad ? "FAILED" : "all ok");
    return bad ? 1 : 0;
}
