/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The demo song of a distribution (web/emu/demo_song.c) rendered on the host: the whole order once, into a 16-bit
 * stereo WAV at the device's rate; its peak, how many samples reached full scale, the busiest voice count.
 *   cc -O2 -w -DFELUCCA_DIST=idm -Ibuild/gen -Ifirmware/src -o build/host/demo_render tests/demo_render.c -lm
 *   build/host/demo_render OUT.wav [SECONDS] */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "../web/emu/demo_song.c"

int main(int argc, char **argv)
{
    FILE *w;
    uint32_t f, frames, i, clip = 0, vmax = 0, rows = 0, p;
    int32_t peak = 0;
    double secs;
    if (argc < 2 || !(w = fopen(argv[1], "wb"))) {
        fprintf(stderr, "demo_render OUT.wav [SECONDS]\n");
        return 2;
    }
    host_tracks_init();
    if (!demo_song()) {
        fprintf(stderr, "demo_render: no demo song for this distribution\n");
        return 1;
    }
    for (p = 0; p < tk.norder; p++)
        rows += tk.len[tk.order[p] % TK_PATS];
    secs = argc > 2 ? atof(argv[2]) : rows * 15.0 / song.g[G_BPM] + 4.0;   /* (a row a 16th) + the tails */
    frames = (uint32_t)(secs * FS) / CTL * CTL;
    tk_pl.mode = TK_SONG;
    tk_pl.start_pos = 0;
    transport_req = 1;
    wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        int16_t pcm[2 * CTL];
        if (f >= (uint32_t)((secs - 4.0) * FS) && song.playing)
            transport_req = 2;                    /* (the order played once: stop, the tails ring out) */
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            peak = a > peak ? a : peak;
            clip += a >= 32767;
            pcm[i] = (int16_t)clamp(o[i], -32767, 32767);
        }
        fwrite(pcm, sizeof pcm[0], 2u * CTL, w);
        i = busy_now();
        vmax = i > vmax ? i : vmax;
    }
    fclose(w);
    printf("demo_render: %s %.1f s (%u positions, %u rows at %d BPM), peak %d, %u samples at full scale, at most %u voices\n",
           FELUCCA_DIST_TAG, secs, tk.norder, rows, song.g[G_BPM], peak, clip, vmax);
    return clip || peak < 4000 ? 1 : 0;           /* (clipped, or next to silent: the song is broken) */
}
