/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-18) Host test of TECHNO, the player on ARP (firmware/baseline/techno.c), on the baseline alone
 * (tests/lib/hostsim.c, HOST_BASELINE):
 *   build/host/techno_test [DEMO_DIR]   (run_tests.sh builds and runs it; a demo WAV per piece in DEMO_DIR)
 * Stopped it plays nothing. Started (a fixed seed): the arrangement reaches the DROP, every role has played by then;
 * every pitched note on the scale and in its role's range; the kick four on the floor wherever it plays; MOTION 0
 * holds the hats' pattern still, 127 turns it; DENSITY 0 silences a role; a key moves the root; a stop lets every note
 * go; the same seed plays the same notes. Then every piece loads its four sounds by name and plays 40 s: not silent,
 * not clipping. */
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("techno: %-84s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static int32_t peak;
static uint64_t nhash;
static uint32_t off_scale, off_range, kick_off, notes_seen, reached_drop;
static uint32_t blocks_for(uint32_t ms) { return ms * (FS / 1000u) / CTL; }
static void blocks(uint32_t n, FILE *wav)
{
    static const uint8_t LO[4] = {0, 24, 48, 36}, HI[4] = {127, 72, 100, 108};
    uint32_t i, r, k;
    while (n--) {
        uint32_t st = TS.step;
        mix_block(out_buf, CTL);
        for (i = 0; i < 2u * CTL; i++)
            peak = out_buf[i] > peak ? out_buf[i] : -out_buf[i] > peak ? -out_buf[i] : peak;
        if (wav)
            for (i = 0; i < CTL; i++)
                wav_put(wav, out_buf[2 * i], out_buf[2 * i + 1]);
        if (!tc.on)
            continue;
        reached_drop |= TS.sect == TS_DROP;
        for (r = 0; r < TC_ROLES; r++)
            for (k = 0; k < TS.nheld[r]; k++) {
                uint32_t nt = TS.held[r][k], pc = (nt + 120u - (uint32_t)TS.root) % 12u, ok = 0, j;
                notes_seen++;
                nhash = (nhash ^ (nt + 128u * r + 1024u * TS.bar * 16u + st)) * 0x100000001B3ull;
                off_range += nt < LO[r] || nt > HI[r];
                if (r == TR_DRUMS) {
                    kick_off += nt == TK_KICK && st % 4u != 1u && st % 4u != 0u && TS.kghost == 0u;
                    continue;
                }
                for (j = 0; j < TS.nsteps; j++)
                    ok |= TS.steps[j] == pc;
                off_scale += !ok;
            }
    }
}
static void setup(uint32_t piece)
{
    uint32_t i;
    host_tracks_init();
    song.master_q12 = 4096;
    song.g[G_GPRE] = (int16_t)piece;
    for (i = G_GDPAD; i <= G_GDTEX; i++)
        song.g[i] = 64;
    tc_load_piece(piece);
    memset(&tc, 0, sizeof tc);
    tc.seed = 12345u;
    peak = 0;
    nhash = 0;
    off_scale = off_range = kick_off = notes_seen = reached_drop = 0;
}
static void start(void) { tc.req = 1; blocks(1, 0); }
static void stop(void) { tc.req = 2; blocks(1, 0); }

int main(int argc, char **argv)
{
    const char *demo = argc > 1 ? argv[1] : 0;
    char msg[240];
    uint32_t i, played[4];
    uint16_t hat0;
    uint64_t h1;

    setup(0);
    blocks(blocks_for(2000), 0);
    check("stopped: nothing plays", !tc.on && peak == 0 && !TS.played[0]);
    start();
    blocks(blocks_for(60000), 0);                     /* a minute at 128 BPM: 32 bars, through the BUILD into the DROP */
    for (i = 0; i < 4u; i++)
        played[i] = TS.played[i];
    snprintf(msg, sizeof msg, "a minute: the DROP reached (bar %u), every role played (drums %u, bass %u, stab %u, lead %u)",
             TS.bar, played[0], played[1], played[2], played[3]);
    check(msg, reached_drop && played[0] > 100u && played[1] > 20u && played[2] > 10u && played[3] > 0u);
    snprintf(msg, sizeof msg, "  every pitched note on the scale (%u off), in its role's range (%u off), of %u", off_scale, off_range,
             notes_seen);
    check(msg, notes_seen > 100u && !off_scale && !off_range);
    check("  the kick four on the floor (a ghost now and then)", kick_off == 0u);
    stop();
    blocks(blocks_for(3000), 0);
    {
        uint32_t r, k, gated = 0;
        for (r = 0; r < NTRK; r++)
            for (k = 0; k < NVOICE; k++)
                gated += trk[r].v[k].active && trk[r].v[k].gate;
        check("stop: every note let go (no voice held; the reverb's tail rings on)",
              !tc.on && !TS.nheld[0] && !TS.nheld[1] && !TS.nheld[2] && !TS.nheld[3] && !gated);
    }

    setup(0);                                         /* MOTION 0: the hats as they began; 127: they turn */
    song.g[G_GMOT] = 0;
    start();
    hat0 = TS.hat;
    blocks(blocks_for(30000), 0);
    i = TS.hat == hat0;
    setup(0);
    song.g[G_GMOT] = 127;
    start();
    hat0 = TS.hat;
    blocks(blocks_for(30000), 0);
    check("MOTION 0 holds the hats' pattern for 30 s; MOTION 127 turns it", i && TS.hat != hat0);

    setup(0);                                         /* DENSITY 0: the bass silent */
    song.g[G_GDBASS] = 0;
    start();
    blocks(blocks_for(40000), 0);
    check("DENSITY 0 (BASS): the bass silent, the drums on", TS.played[TR_BASS] == 0u && TS.played[TR_DRUMS] > 50u);

    setup(0);                                         /* a key while it plays: the root to its note */
    start();
    blocks(blocks_for(2000), 0);
    check("a key while it plays moves the key (D: the root 2), taken", player_key_on(62, 100) && song.g[G_GROOT] == 2);
    blocks(blocks_for(500), 0);
    check("  the music follows at once", TS.root == 2);

    setup(0);                                         /* the same seed, the same notes */
    start();
    blocks(blocks_for(20000), 0);
    h1 = nhash;
    setup(0);
    start();
    blocks(blocks_for(20000), 0);
    check("the same seed plays the same notes at the same steps", nhash == h1 && h1 != 0);

    for (i = 0; i < TC_NPIECES; i++) {                /* every piece: its sounds by name, 40 s, not silent, not clipping */
        const tc_piece_t *p = &TC_PIECES[i];
        FILE *w = 0;
        uint32_t r, found = 1;
        setup(i);
        for (r = 0; r < TC_ROLES; r++) {
            uint32_t e = eng_of_id(p->eng[r]);
            found &= e != ENG_NO && trk[r].eng_req == e &&
                     (e == ENG_NO || !strcmp(ENGINES[e]->presets[trk[r].preset].name, p->snd[r]));
        }
        if (demo) {
            char path[256];
            snprintf(path, sizeof path, "%s/%s.wav", demo, p->name);
            if ((w = fopen(path, "wb")))
                wav_hdr(w, blocks_for(40000) * CTL);
        }
        start();
        blocks(blocks_for(40000), w);
        if (w)
            fclose(w);
        snprintf(msg, sizeof msg, "%-10s %3u BPM: its four sounds by name, plays (peak %.1f dB FS), not clipping", p->name,
                 p->bpm, 20.0 * log10((peak + 1) / 32768.0));
        check(msg, found && song.g[G_BPM] == p->bpm && peak > 3000 && peak < 32767 * 2);
        stop();
    }
    printf(bad ? "techno: %d FAILED\n" : "techno: all passed\n", bad);
    return bad ? 1 : 0;
}
