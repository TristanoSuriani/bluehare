/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-24) The second core's part (fx.c mix_block, c1_part_job; target.h tgt_c1_*): the busiest part mixed by
 * "core 1" (here inline, as core 1: c1_split_test) into its own buffers and added after must give the samples of one
 * core, bit for bit: every engine of the build on track 1, chords, SPREAD, the modulation matrix, DIST, the patches'
 * delays, the chorus and reverb sends (PLATE), notes coming and going, an engine change mid-song. Then the part picked: the one with the most voices.
 *   dualcore_test */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <sys/wait.h>
#include <unistd.h>

static int fails;
static void check(const char *what, int ok)
{
    printf("dualcore: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
#define NB 1400u                                        /* blocks a run (~1 s) */
static uint64_t run_here(int split, uint32_t e1, uint32_t e3, uint32_t *picked)
{
    int32_t o[2u * CTL];
    uint64_t h = 1469598103934665603ull;
    uint32_t k, i;
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        memset(trk[k].v, 0, sizeof trk[k].v);
    memset(mod_c, 0, sizeof mod_c);
    host_preset(&trk[0], e1, 0);
    host_drums(&trk[1]);
    host_preset(&trk[2], e3, 0);
    trk[0].p[P_VOICE] = trk[2].p[P_VOICE] = V_POLY;
    song.g[G_RTYPE] = 3;                                /* the plate (the host's default is DRY): the sends heard */
    trk[0].p[P_CHOR] = 50;                              /* the chorus send too */
    trk[0].p[P_SPRD] = 90;                              /* SPREAD on the first */
    trk[0].p[P_DIST] = 40;
    trk[2].p[P_DLY] = 60;
    trk[0].p[P_M1SRC] = 1;                              /* a slot of the matrix on each tonal part */
    trk[0].p[P_M1DST] = 2;
    trk[0].p[P_M1AMT] = 40;
    trk[2].p[P_M1SRC] = 1;
    trk[2].p[P_M1DST] = 1;
    trk[2].p[P_M1AMT] = -30;
    c1_split_test = (uint8_t)split;
    for (k = 0; k < NB; k++) {
        if (k % 175u == 0u) {                           /* chords coming and going */
            uint32_t r = k / 175u;
            for (i = 0; i < 4u; i++) {
                trk_note_off(&trk[0], 48 + 3 * i + (r + 1u) % 5u);
                trk_note_on(&trk[0], 48 + 3 * i + r % 5u, 90 + i * 8);
            }
            trk_note_off(&trk[2], 60 + (r + 1u) % 7u);
            trk_note_on(&trk[2], 60 + r % 7u, 100);
        }
        if ((k * CTL) % (FS / 8u) < CTL)
            drum_hit(k * CTL / (FS / 8u));
        if (picked && k == 700u)
            *picked = c1_pick();
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
    }
    c1_split_test = 0;
    return h;
}

/* each render in a fresh process: every state at power-on's (a render leaves the parts' DIST, LOW CUT and delay
 * lines, the master's, ringing into the next) */
static uint64_t run(int split, uint32_t e1, uint32_t e3, uint32_t *picked)
{
    int fd[2];
    uint64_t r[2] = {0, 0};
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    if (!(pid = fork())) {
        uint32_t pk = NPART;
        r[0] = run_here(split, e1, e3, &pk);
        r[1] = pk;
        if (write(fd[1], r, sizeof r) != (ssize_t)sizeof r)
            _exit(1);
        _exit(0);
    }
    if (read(fd[0], r, sizeof r) != (ssize_t)sizeof r)
        r[0] = 0;
    waitpid(pid, 0, 0);
    close(fd[0]);
    close(fd[1]);
    if (picked)
        *picked = (uint32_t)r[1];
    return r[0];
}

int main(void)
{
    uint32_t e, ok = 1, picked = 0, n = 0;
    for (e = 0; e < NENGINES; e++) {
        uint64_t a, b;
        if (ENGINES[e]->oneshot)
            continue;
        a = run(0, e, ENGI_VOSIM, 0);
        b = run(1, e, ENGI_VOSIM, &picked);
        if (a != b) {
            printf("  %s: one core %016llx, two %016llx\n", ENGINES[e]->name, (unsigned long long)a, (unsigned long long)b);
            ok = 0;
        }
        n++;
    }
    check("every tonal engine on track 1 (SPREAD, DIST, the matrix) with the kit and VOSIM: two cores == one, bit for bit",
          ok && n > 5u);
    ok = run(0, ENGI_VA, ENGI_VOCODER, 0) == run(1, ENGI_VA, ENGI_VOCODER, 0) &&
         run(0, ENGI_OBIE, ENGI_SPEECH, 0) == run(1, ENGI_OBIE, ENGI_SPEECH, 0);
    check("..and with the voices' engines on track 3 (VOCODER, SPEECH)", ok);
    run(1, ENGI_VA, ENGI_VOSIM, &picked);
    check("the part handed to core 1: the one with the most voices (track 1's chord)", picked == 0u);
    host_tracks_init();
    for (e = 0; e < NPART; e++)
        memset(trk[e].v, 0, sizeof trk[e].v);
    check("..none when nothing sounds", c1_pick() == NPART);
    printf(fails ? "dualcore: %d FAILED\n" : "dualcore test passed\n", fails);
    return fails ? 1 : 0;
}
