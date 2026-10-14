/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* fel_host: what the plugin does, apart from the plugin format (the DPF wrapper, FelTrackerPlugin.cpp, only passes it
 * on; the tests drive it directly). An instance of a distribution's core (fel_loader.h), the distribution chosen among
 * the cores next to the plugin, the core's 44.1 kHz resampled to the host's rate (fel_resample.h), MIDI events played
 * at their frame. Threads: fel_host_run is the audio thread's; the others the host's main thread (a distribution
 * changed while the audio runs: the new core is made apart, then swapped in between two blocks). */
#ifndef FEL_HOST_H
#define FEL_HOST_H
#include <stdint.h>

#define FEL_DIST_MAX 8u

typedef struct fel_host fel_host_t;

typedef struct {
    uint32_t frame;                      /* in the block */
    uint8_t data[3];                     /* a channel message (others are left out) */
} fel_event_t;

/* cores_dir: where fel-core-<dist>.dylib are; rate: the host's. 0 if it has no core at all */
fel_host_t *fel_host_new(const char *cores_dir, double rate, char *err, unsigned err_n);
void fel_host_free(fel_host_t *h);

/* the distributions found, in the chooser's order (IDM NATIVE first): their stems ("idm_native") and tags */
uint32_t fel_host_dists(const fel_host_t *h);
const char *fel_host_dist_stem(const fel_host_t *h, uint32_t i);
const char *fel_host_dist_tag(const fel_host_t *h, uint32_t i);
uint32_t fel_host_dist(const fel_host_t *h);           /* the one playing */
/* change to distribution i: its core loaded, booted on an erased flash (no song), swapped in. 0 = done */
int fel_host_set_dist(fel_host_t *h, uint32_t i, char *err, unsigned err_n);

int fel_host_set_rate(fel_host_t *h, double rate);    /* (not while running) */
uint32_t fel_host_latency(const fel_host_t *h);        /* host frames */
void fel_host_master(fel_host_t *h, uint32_t v);       /* MASTER 0 .. 1023 */

/* n frames into l, r, the events (frames in order) played at their place */
void fel_host_run(fel_host_t *h, float *l, float *r, uint32_t n, const fel_event_t *ev, uint32_t nev);

#endif
