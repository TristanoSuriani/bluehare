/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* fel_resample: the core's 44.1 kHz (up to FEL_RS_MAXCH channels: the main and the track outputs) to the host's rate, a windowed sinc (Kaiser) of FEL_RS_TAPS taps, read at
 * FEL_RS_PHASES phases between two input samples (linearly interpolated). Pulled: the output asks for input as it goes
 * (pull(ctx, in, nch, n) fills n frames of nch channels), so the core renders only what the host's block needs. Its pass band is flat
 * (0.1 dB) to 0.43 of the lower of the two rates (19 kHz at 44.1), its stop band (-80 dB) starts at that rate's
 * Nyquist: no image, no alias. The signal is not delayed, but the input is pulled FEL_RS_TAPS / 2 frames ahead, so an
 * event sent to the core between two calls is heard that much later (fel_rs_latency: in output frames). */
#ifndef FEL_RESAMPLE_H
#define FEL_RESAMPLE_H
#include <stdint.h>

#define FEL_RS_TAPS 96u                  /* at the input rate, when the output's is higher (more, scaled, when lower) */
#define FEL_RS_PHASES 512u
#define FEL_RS_MAXTAPS 384u              /* (down to a quarter of the input rate: 11 kHz) */
#define FEL_RS_BUF 1024u                 /* the input kept: the taps and a pull's chunk */
#define FEL_RS_MAXCH 18u

typedef void (*fel_rs_pull)(void *ctx, float *const *in, uint32_t nch, uint32_t n);

typedef struct {
    double in_rate, out_rate;
    uint32_t taps;                       /* even */
    float *kernel;                       /* (FEL_RS_PHASES + 1) * taps */
    float buf[FEL_RS_MAXCH][FEL_RS_BUF]; /* input frames buf[.][0 .. fill): the oldest first */
    uint32_t fill;
    uint64_t pos;                        /* the next output's place in buf, 32.32 */
    uint64_t step;                       /* input frames an output frame, 32.32 */
    uint64_t pulled;                     /* input frames pulled since the reset */
} fel_rs_t;

/* 0 = done; a rate below a quarter of in_rate, or no memory: -1 */
int fel_rs_init(fel_rs_t *rs, double in_rate, double out_rate);
void fel_rs_free(fel_rs_t *rs);
void fel_rs_reset(fel_rs_t *rs);         /* silence in the taps, as at init */
/* n frames of nch channels (<= FEL_RS_MAXCH; a call may use fewer than the last: the others keep their past) */
void fel_rs_process(fel_rs_t *rs, float *const *out, uint32_t nch, uint32_t n, fel_rs_pull pull, void *ctx);
uint32_t fel_rs_latency(const fel_rs_t *rs);   /* output frames, rounded */
double fel_rs_where(const fel_rs_t *rs);       /* the input frame (since the reset) the next output is at */

#endif
