/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* (Blue Hare, 2026-10-17: the baseline's MIDI input, out of usb.c) The ring every MIDI IN (USB, TRS, a host's) is put
 * in, 4-byte USB-MIDI event packets with the time and the source; the audio ISR's events_block (seq.c) takes them. A
 * port's code calls midi_enqueue; the baseline needs nothing of the port. Included by core.h, and by usb.c for the
 * parser-only host tests that build it alone */
#ifndef BLUEHARE_MIDI_IN
#define BLUEHARE_MIDI_IN
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
#endif
#define MQ 64u
static uint32_t midi_in_q[MQ];
static uint32_t midi_in_ms[MQ];
static uint8_t midi_in_source[MQ];                    /* 1 USB, 2 TRS */
static volatile uint32_t fm1_ms;                    /* (core.h's too: milliseconds since boot) */
static volatile uint32_t mi_w, mi_r;
static volatile uint8_t midi_in_overflow;               /* audio ISR discards a broken stream and releases notes */
static volatile uint32_t midi_in_lost;                  /* (2026-10-24) overflows handled this session (MENU > INFO MIDI IN) */

static int midi_enqueue(uint32_t pkt, uint32_t source)
{
    uint32_t at = mi_w % MQ;
    if (midi_in_overflow)
        return 0;
    if (mi_w - mi_r >= MQ) {
        midi_in_overflow = 1;
        return 0;
    }
    midi_in_q[at] = pkt;
    midi_in_ms[at] = fm1_ms;
    midi_in_source[at] = (uint8_t)source;
    RING_PUBLISH();
    mi_w++;
    return 1;
}
#endif
