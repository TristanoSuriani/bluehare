/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin core's reference: web/emu/felucca_web.c built as the native image builds it (FELUCCA_NATIVE), linked in,
 * playing the emulator's heavy song with web/emu/native_check.c's gestures and render calls: float32 L R to OUT.
 * tests/plugin_core_test.c holds a loaded core (plugin/core) to it, sample for sample.
 *   cc -O2 -ffp-contract=off -w -DFELUCCA_DIST=<dist> -Ibuild/gen -Ifirmware/src -o REF tests/plugin_core_ref.c -lm */
#include <stdint.h>
#include <time.h>
#define FELUCCA_NATIVE 1
static uint32_t native_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 24000000u + (uint64_t)ts.tv_nsec * 24u / 1000u);
}
#include "../web/emu/native_check.c"
