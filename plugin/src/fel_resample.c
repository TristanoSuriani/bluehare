/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* fel_resample.h. An output frame at pos (input frames, 32.32) is the taps around it, buf[i - taps/2 + 1 ..
 * i + taps/2] (i = floor(pos)), weighted by the kernel at pos's fraction. The input is pulled only as far as the
 * outputs asked for need it (no further: an event the host sends between two calls lands as late as any other). */
#include "fel_resample.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define KAISER_BETA 8.0                  /* ~-80 dB */
#define PASS 0.474                       /* the cut, of the lower rate: half the transition under its Nyquist */

static double bessel_i0(double x)
{
    double s = 1.0, t = 1.0;
    int k;
    for (k = 1; k < 40; k++) {
        t *= (x / (2.0 * k)) * (x / (2.0 * k));
        s += t;
    }
    return s;
}

int fel_rs_init(fel_rs_t *rs, double in_rate, double out_rate)
{
    double scale = out_rate < in_rate ? out_rate / in_rate : 1.0, fc = PASS * scale, half;
    uint32_t taps, p, j;
    memset(rs, 0, sizeof *rs);
    if (in_rate <= 0.0 || out_rate < in_rate / 4.0)
        return -1;
    taps = (uint32_t)ceil(FEL_RS_TAPS / scale);
    taps += taps & 1u;
    if (taps > FEL_RS_MAXTAPS)
        taps = FEL_RS_MAXTAPS;
    rs->kernel = malloc(sizeof(float) * (FEL_RS_PHASES + 1u) * taps);
    if (!rs->kernel)
        return -1;
    rs->in_rate = in_rate;
    rs->out_rate = out_rate;
    rs->taps = taps;
    half = taps / 2.0;
    for (p = 0; p <= FEL_RS_PHASES; p++) {           /* phase p: the output p / PHASES past input i */
        float *k = &rs->kernel[p * taps];
        double sum = 0.0;
        for (j = 0; j < taps; j++) {
            double t = (double)j - half + 1.0 - (double)p / FEL_RS_PHASES, w = t / half, x = 2.0 * fc * t, v;
            v = fabs(x) < 1e-12 ? 2.0 * fc : 2.0 * fc * sin(M_PI * x) / (M_PI * x);
            v *= w * w < 1.0 ? bessel_i0(KAISER_BETA * sqrt(1.0 - w * w)) / bessel_i0(KAISER_BETA) : 0.0;
            k[j] = (float)v;
            sum += v;
        }
        for (j = 0; j < taps; j++)                   /* each phase passes DC whole */
            k[j] = (float)(k[j] / sum);
    }
    rs->step = (uint64_t)llround(in_rate / out_rate * 4294967296.0);
    fel_rs_reset(rs);
    return 0;
}

void fel_rs_free(fel_rs_t *rs)
{
    free(rs->kernel);
    rs->kernel = 0;
}

void fel_rs_reset(fel_rs_t *rs)
{
    memset(rs->buf, 0, sizeof rs->buf);
    rs->fill = rs->taps / 2u - 1u;                   /* the taps before the first input: silence */
    rs->pos = (uint64_t)(rs->taps / 2u - 1u) << 32;
    rs->pulled = 0;
}

double fel_rs_where(const fel_rs_t *rs)
{
    return (double)rs->pulled - (double)rs->fill + (double)rs->pos / 4294967296.0;   /* (buf[0]: input pulled - fill) */
}

uint32_t fel_rs_latency(const fel_rs_t *rs)
{
    return (uint32_t)lround(rs->taps / 2.0 * rs->out_rate / rs->in_rate);
}

static void compact(fel_rs_t *rs)                    /* the frames no output needs again: gone */
{
    uint32_t first = (uint32_t)(rs->pos >> 32) + 1u - rs->taps / 2u, c;
    if (!first)
        return;
    for (c = 0; c < FEL_RS_MAXCH; c++)
        memmove(rs->buf[c], rs->buf[c] + first, sizeof(float) * (rs->fill - first));
    rs->fill -= first;
    rs->pos -= (uint64_t)first << 32;
}

void fel_rs_process(fel_rs_t *rs, float *const *out, uint32_t nch, uint32_t n, fel_rs_pull pull, void *ctx)
{
    const uint32_t taps = rs->taps, h = taps / 2u;
    uint32_t o = 0, c;
    float *in[FEL_RS_MAXCH];
    nch = nch > FEL_RS_MAXCH ? FEL_RS_MAXCH : nch;
    while (o < n) {
        /* the input the rest of the outputs need, as much as the buffer holds */
        uint32_t need = (uint32_t)((rs->pos + rs->step * (uint64_t)(n - o - 1u)) >> 32) + h + 1u;
        if (need > rs->fill) {
            if (need > FEL_RS_BUF)
                compact(rs);
            need = (uint32_t)((rs->pos + rs->step * (uint64_t)(n - o - 1u)) >> 32) + h + 1u;
            if (need > FEL_RS_BUF)
                need = FEL_RS_BUF;
            if (need > rs->fill) {
                for (c = 0; c < FEL_RS_MAXCH; c++)
                    in[c] = rs->buf[c] + rs->fill;
                for (c = nch; c < FEL_RS_MAXCH; c++)    /* (the channels not asked for: silence, for later) */
                    memset(in[c], 0, sizeof(float) * (need - rs->fill));
                pull(ctx, in, nch, need - rs->fill);
                rs->pulled += need - rs->fill;
                rs->fill = need;
            }
        }
        /* the outputs that input makes */
        for (; o < n; o++) {
            uint32_t i = (uint32_t)(rs->pos >> 32), j;
            uint32_t fr = (uint32_t)(rs->pos & 0xFFFFFFFFu), ph = (uint32_t)(((uint64_t)fr * FEL_RS_PHASES) >> 32);
            float a = (float)(((uint64_t)fr * FEL_RS_PHASES) & 0xFFFFFFFFu) * (1.0f / 4294967296.0f);
            const float *k0 = &rs->kernel[ph * taps], *k1 = k0 + taps;
            float k[FEL_RS_MAXTAPS];
            if (i + h >= rs->fill)
                break;
            for (j = 0; j < taps; j++)
                k[j] = k0[j] + a * (k1[j] - k0[j]);
            for (c = 0; c < nch; c++) {
                const float *x = rs->buf[c] + i + 1u - h;
                float s = 0.0f;
                for (j = 0; j < taps; j++)
                    s += k[j] * x[j];
                out[c][o] = s;
            }
            rs->pos += rs->step;
        }
    }
}
