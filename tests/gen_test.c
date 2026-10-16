/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* Host test of GEN, the generative engine (firmware/src/gen.c: amb1's composition), with the firmware's sources
 * (through hostsim.c):
 *   build/host/gen_test [DEMO_DIR]      (run_tests.sh builds and runs it; a demo WAV per piece in DEMO_DIR)
 * Stopped it plays nothing. Started (a fixed seed) every role plays (pad, bass, bells, pulse), each in its range, every
 * note on the scale and ROOT; the pad never holds more than its four voices and breathes (new breaths keep coming); the
 * bells ring on quarter notes only; the chords drift (a few in two minutes, not every bar); the steps follow the
 * tempo; a key turned while it plays is the key of every note held soon after; MOTION 0 holds the arc still, 127 moves
 * it; the same seed plays the same notes at the same steps; a stop lets every note go (silence after the releases).
 * Then every piece plays 40 s: on its scale, in range, not silent, not clipping. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("gen: %-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static uint64_t nhash;
static int32_t peak;
static uint32_t off_scale, off_range, notes_seen, pad_over, bell_off_beat, arc_moved;
static uint32_t last_bells;
static uint16_t scale_now(void)
{
    uint32_t s = (uint32_t)song.g[G_GSCL];
    return (uint16_t)((s ? SCALE_MASK[s] : SCALE_MASK[1]) | 1u);
}
/* n blocks; while it plays, every note a role holds checked against the scale and the role's range */
static void blocks(uint32_t n, FILE *wav)
{
    static const uint8_t LO[4] = {48, 24, 36, 36}, HI[4] = {77, 59, 103, 95};
    uint32_t i, r, k;
    while (n--) {
        uint32_t six = GS.six;
        mix_block(out_buf, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out_buf[i] < 0 ? -out_buf[i] : out_buf[i];
            if (a > peak)
                peak = a;
        }
        if (wav)
            for (i = 0; i < CTL; i++)
                wav_put(wav, out_buf[2 * i], out_buf[2 * i + 1]);
        if (!gen.on)
            continue;
        if (GS.played[2] != last_bells && ((six - 0u) & 3u) != 0u && GS.six != six)   /* (a bell rang this block) */
            bell_off_beat += (((GS.six - 1u) & 3u) != 0u);
        last_bells = GS.played[2];
        arc_moved += GS.arcm != 0;
        pad_over += GS.nheld[0] > 4u;
        for (r = 0; r < GEN_ROLES; r++)
            for (k = 0; k < GS.nheld[r]; k++) {
                uint32_t nt = GS.held[r][k], pc = (nt + 120u - (uint32_t)song.g[G_GROOT]) % 12u;
                nhash = (nhash ^ (nt + 128u * r + 1024u * GS.six)) * 0x100000001B3ull;
                notes_seen++;
                off_scale += !((scale_now() >> pc) & 1u);
                off_range += nt < LO[r] || nt > HI[r];
            }
    }
}
static uint32_t blocks_for(uint32_t ms) { return ms * (FS / 1000u) / CTL; }

static void setup(uint32_t piece)
{
    const gen_preset_t *p = &GEN_PRESETS[piece];
    uint32_t i;
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++)
        for (uint32_t k = 0; k < P_E0; k++)
            trk[i].p[k] = TP[k].def;
    host_preset(&trk[0], BLUEHARE_ENG_WAVE ? ENGI_WAVE : 0u, 8);        /* (GARDEN's sounds, by hand: gen_apply is the */
    host_preset(&trk[1], BLUEHARE_ENG_SH101 ? ENGI_SH101 : 0u, 1);      /* UI's): WARM PAD, SUB BASS, GLASS BELL, WOOD DRUM */
    host_preset(&trk[2], BLUEHARE_ENG_BLISS ? ENGI_BLISS : 0u, 4);
    host_preset(&trk[3], BLUEHARE_ENG_MODAL ? ENGI_MODAL : 0u, 7);
    song.master_q12 = 4096;
    song.g[G_GPRE] = (int16_t)piece;
    song.g[G_GSCL] = p->scale;
    song.g[G_GROOT] = p->root;
    song.g[G_BPM] = p->bpm;
    song.g[G_GMOT] = (int16_t)(p->motion * 127 / 100);
    memset(&gen, 0, sizeof gen);
    gen.seed = 12345u;
    off_scale = off_range = notes_seen = pad_over = bell_off_beat = arc_moved = 0;
    last_bells = 0;
    peak = 0;
}
static void start(void) { gen.req = 1; blocks(1, 0); }
static void stop(void) { gen.req = 2; blocks(1, 0); }

int main(int argc, char **argv)
{
    const char *demo = argc > 1 ? argv[1] : 0;
    uint64_t h1, h2;
    uint32_t i, steps, played[4], chords;
    char path[256];

    setup(0);
    blocks(blocks_for(2000), 0);
    check("stopped: nothing plays", !gen.on && peak == 0 && !GS.played[0]);

    setup(0);                                           /* GARDEN, D dorian, at 120 BPM */
    song.g[G_BPM] = 120;
    start();
    nhash = 0xCBF29CE484222325ull;
    blocks(blocks_for(120000), 0);
    h1 = nhash;
    for (i = 0; i < 4u; i++)
        played[i] = GS.played[i];
    chords = GS.nchord;
    printf("gen: 2 minutes of GARDEN: pad %u, bass %u, bells %u, pulse %u notes; %u chord changes\n", played[0],
           played[1], played[2], played[3], chords);
    check("started: every role plays (pad, bass, bells, pulse)", played[0] && played[1] && played[2] && played[3]);
    check("every note on the scale and ROOT (D dorian), each role in its range", notes_seen > 1000u && !off_scale && !off_range);
    check("the pad: four voices at most, breathing (new breaths keep coming)", !pad_over && played[0] > 12u);
    check("the bells ring on quarter notes only", !bell_off_beat && played[2] > 10u);
    check("the chords drift: a few in two minutes (DRIFT ~39 s), not every bar", chords >= 2u && chords <= 6u);
    steps = GS.bar * 16u + GS.step;
    check("the steps follow the tempo: 1/16s at 120 BPM (8 a second)", steps >= 958u && steps <= 962u);
    check("MOTION (GARDEN's 50 %): the arc moves", arc_moved > 0u);

    setup(0);
    song.g[G_BPM] = 120;
    start();
    nhash = 0xCBF29CE484222325ull;
    blocks(blocks_for(120000), 0);
    h2 = nhash;
    check("the same seed plays the same notes at the same steps", h1 == h2);

    song.g[G_GSCL] = 6;                                 /* MPEN on E while it plays */
    song.g[G_GROOT] = 4;
    blocks(blocks_for(4000), 0);                        /* (the pad's voices out of the key: a second to let go) */
    off_scale = notes_seen = 0;
    blocks(blocks_for(20000), 0);
    check("a key turned while it plays (E minor pentatonic): the notes soon after are in it", notes_seen > 50u && !off_scale);

    stop();
    check("stop: every note let go", !gen.on && !GS.nheld[0] && !GS.nheld[1] && !GS.nheld[2] && !GS.nheld[3]);
    blocks(blocks_for(10000), 0);
    peak = 0;
    blocks(blocks_for(500), 0);
    check("silence after the releases", peak < 64);

    setup(0);
    song.g[G_GMOT] = 0;
    start();
    blocks(blocks_for(60000), 0);
    check("MOTION 0: the arc holds still", arc_moved == 0u);
    stop();

    for (i = 0; i < GEN_NPRESETS; i++) {                /* every piece: its scale, root, tempo, levels (GARDEN's sounds) */
        FILE *w = 0;
        uint32_t frames = blocks_for(40000) * CTL, k, roles = 0;
        setup(i);
        if (demo) {
            snprintf(path, sizeof path, "%s/%s.wav", demo, GEN_PRESETS[i].name);
            for (k = 0; path[k]; k++)
                if (path[k] == ' ')
                    path[k] = '_';
            if ((w = fopen(path, "wb")))
                wav_hdr(w, frames);
        }
        start();
        blocks(frames / CTL, w);
        stop();
        if (w)
            fclose(w);
        for (k = 0; k < 4u; k++)
            roles += (GS.played[k] > 0) == (GEN_PRESETS[i].lvl[k] > 0);
        printf("gen: %-10s pad %3u bass %3u bells %3u pulse %3u, %u chords, peak %.1f dB FS\n", GEN_PRESETS[i].name,
               GS.played[0], GS.played[1], GS.played[2], GS.played[3], GS.nchord, 20.0 * log10((double)peak / 32768.0 + 1e-9));
        check("  its roles (those at level 0 silent), on its scale, in range, not silent, not clipping",
              roles == 4u && notes_seen > 50u && !off_scale && !off_range && peak > 32768 / 100 && peak < 32767);
    }
    printf(bad ? "gen: %d FAILED\n" : "gen: all passed\n", bad);
    return bad ? 1 : 0;
}
