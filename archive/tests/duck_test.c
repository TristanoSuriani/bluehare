/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* DUCK (core.h trk_duck; fx.c duck_key, mix_ducked): none at power-on; a key (the kick on track 1) ducks a pad held on
 * track 2 at once and lets it back over ~a quarter second, as deep as the pad's DUCK; a track at OFF, and every track
 * without a key, plays as it did; the song store keeps it, the power-on values have none. */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"
#include <math.h>

static uint32_t eng_by(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!strcmp(ENGINES[e]->name, n))
            return e;
    return 0;
}

/* track 1 a kick at 0 s (KIT), track 2 a held WAVE pad, track 3 another pad: 1 s; the main's energy over 20..120 ms
 * and 600..800 ms after the kick (the pad alone: the kick's own is gone by then?), dk_g[1] at its lowest and at the end */
static void run(uint8_t d0, uint8_t d1, double *e_early, double *e_late, int32_t *gmin, int32_t *gend)
{
    uint32_t b, i;
    int32_t o[2 * CTL];
    host_tracks_init();
    trk_duck[0] = d0;                                /* (after the defaults: they put none) */
    trk_duck[1] = d1;
    host_drums(&trk[0]);
    trk[0].p[P_E2] = 0;                              /* (KIT's DECAY a quarter: a short kick, its tail out of the way) */
    host_preset(&trk[1], eng_by("WAVE"), 0);
    host_preset(&trk[2], eng_by("WAVE"), 0);
    for (i = 0; i < 3u; i++)
        trk[i].p[P_CHOR] = trk[i].p[P_DLY] = trk[i].p[P_REV] = 0;
    trk[1].p[P_SUS] = trk[2].p[P_SUS] = 127;
    trk_note_on(&trk[1], 60, 100);
    trk_note_on(&trk[2], 67, 100);
    for (b = 0; b < 300u; b++)                       /* (the pads at their sustain) */
        mix_block(o, CTL);
    dk_env = 0;
    for (i = 0; i < NTRK; i++)
        dk_g[i] = 4096;
    trk_note_on(&trk[0], 36, 120);
    *e_early = *e_late = 0;
    *gmin = 4096;
    for (b = 0; b < FS / CTL; b++) {
        uint32_t t = b * CTL;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            double x = o[2 * i] + (double)o[2 * i + 1];
            if (t >= FS / 50u && t < FS * 12u / 100u)
                *e_early += x * x;
        }
        *gmin = dk_g[1] < *gmin ? dk_g[1] : *gmin;
    }
    *gend = dk_g[1];
    (void)e_late;
}

int main(void)
{
    int bad = 0, ok;
    uint32_t k;
    double e0, e1, ex;
    int32_t gmin, gend;
    char what[200];
    reset();
    for (ok = 1, k = 0; k < NTRK; k++)
        ok &= !trk_duck[k];
    bad += check("power-on: no DUCK on any track", ok);

    run(0, 0, &e0, &ex, &gmin, &gend);                 /* no key: as before */
    run(TD_KEY, TD_MAX, &e1, &ex, &gmin, &gend);
    snprintf(what, sizeof what, "the kick on track 1 KEY, the pad on 2 DUCK 15: down to %.0f %% at once (%.1f dB less in its first 100 ms), "
             "back to %.0f %% a second on", 100.0 * gmin / 4096, 10 * log10(e1 / e0), 100.0 * gend / 4096);
    bad += check(what, gmin < 4096 / 8 && e1 < e0 && gend > 4096 * 9 / 10);
    run(TD_KEY, 5, &e1, &ex, &gmin, &gend);
    snprintf(what, sizeof what, "DUCK 5: shallower (down to %.0f %%)", 100.0 * gmin / 4096);
    bad += check(what, gmin > 4096 / 2 && gmin < 4096 * 85 / 100);
    run(TD_KEY, 0, &e1, &ex, &gmin, &gend);
    bad += check("a KEY with no track to duck: nothing done (no envelope, no gain)", dk_env == 0 && gmin == 4096 && gend == 4096);

    /* the store */
    trk_duck[0] = TD_KEY;
    trk_duck[NTRK - 1u] = 9;
    ok = !project_save(0);
    trk_mix_defaults();
    ok &= !trk_duck[0] && !trk_duck[NTRK - 1u];
    project_load(0);
    ok &= proj_ram[0][PROJ_HDR] == PROJ_VER && trk_duck[0] == TD_KEY && trk_duck[NTRK - 1u] == 9u;
    bad += check("the store keeps DUCK; the power-on values have none", ok);
    printf("%s\n", bad ? "DUCK TEST FAILED" : "duck test passed");
    return bad != 0;
}
