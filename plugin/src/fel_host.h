/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* fel_host: what the plugin does, apart from the plugin format (the DPF wrapper, FelTrackerPlugin.cpp, only passes it
 * on; the tests drive it directly). An instance of a distribution's core (fel_loader.h), the distribution chosen among
 * the cores next to the plugin, the core's 44.1 kHz resampled to the host's rate (fel_resample.h), MIDI events played
 * at their frame, the host's transport followed (tempo, play, the beat), the tracks' own outputs, the song saved and
 * loaded (the device's flash). Threads: fel_host_run is the audio thread's; the others the host's main thread (a
 * distribution changed while the audio runs: the new core is made apart, then swapped in between two blocks). */
#ifndef FEL_HOST_H
#define FEL_HOST_H
#include <stddef.h>
#include <stdint.h>

#define FEL_DIST_MAX 8u
#define FEL_HOST_OUTS 18u                /* the main pair, then a pair a track (8) */

typedef struct fel_host fel_host_t;

typedef struct {
    uint32_t frame;                      /* in the block */
    uint8_t data[3];                     /* a channel message (others are left out) */
} fel_event_t;

typedef struct {
    uint32_t valid;                      /* 0: the host says nothing (the device keeps its own tempo and PLAY) */
    uint32_t playing;
    double bpm;
    double beats;                        /* the quarter notes since the host's start, at the block's first frame */
} fel_transport_t;

/* cores_dir: where fel-core-<dist>.dylib are; rate: the host's. 0 if it has no core at all */
fel_host_t *fel_host_new(const char *cores_dir, double rate, char *err, unsigned err_n);
void fel_host_free(fel_host_t *h);

/* the distributions found, in the chooser's order (IDM NATIVE first): their stems ("idm_native") and tags */
uint32_t fel_host_dists(const fel_host_t *h);
const char *fel_host_dist_stem(const fel_host_t *h, uint32_t i);
const char *fel_host_dist_tag(const fel_host_t *h, uint32_t i);
uint32_t fel_host_dist(const fel_host_t *h);           /* the one playing */
uint32_t fel_host_tracks(const fel_host_t *h);         /* its tracks (8 or 4) */
/* change to distribution i: its core loaded, booted on an erased flash (no song), swapped in. 0 = done */
int fel_host_set_dist(fel_host_t *h, uint32_t i, char *err, unsigned err_n);

/* the song: the device's flash (the song, the presets, the settings) as it stood after the last block that wrote it,
 * compressed: *out malloc'd (free it), its size returned; 0 none */
size_t fel_host_save(fel_host_t *h, uint8_t **out);
/* distribution i with a flash fel_host_save gave (any distribution's: the device loads what it can): a new core booted
 * on it, swapped in. 0 = done */
int fel_host_load(fel_host_t *h, uint32_t i, const uint8_t *blob, size_t n, char *err, unsigned err_n);

int fel_host_set_rate(fel_host_t *h, double rate);    /* (not while running) */
uint32_t fel_host_latency(const fel_host_t *h);        /* host frames */
void fel_host_master(fel_host_t *h, uint32_t v);       /* MASTER 0 .. 1023 */
void fel_host_outputs(fel_host_t *h, uint32_t mask);   /* bit k: track k to its own pair (any thread; the next block) */
const void *fel_host_core(const fel_host_t *h);         /* (tests: the core playing, a fel_core_api_t) */

/* n frames into out[0 .. nout) (the main pair, then the tracks' pairs; nout 2 or FEL_HOST_OUTS), the events (frames in
 * order) played at their place, the host's transport at the block's start (0: none) */
void fel_host_run(fel_host_t *h, float *const *out, uint32_t nout, uint32_t n, const fel_event_t *ev, uint32_t nev,
                  const fel_transport_t *tr);

#endif
