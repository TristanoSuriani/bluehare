/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* (Blue Hare, 2026-10-17: the second review's A, out of os/ui.c) A sound loaded into a layer: what a preset or an
 * engine writes into a track's parameters, the sound model's own rule. The OS wraps these with what is its (the undo,
 * the editor's sync, the audio interrupt held off while an engine changes: os/ui.c apply_preset_to, set_engine_of);
 * GEN asks the OS to load a piece's sounds (os_load_sound) and the tests and hosts call these directly. Included by
 * seq.c, after the note input (panic_req) */

/* the track's settings, not the sound's: what a sound load (factory or user preset, an engine jump, TOOLS INIT)
 * leaves alone. The mix (LEVEL, PAN, MUTE: the TRACKS faders), the scale and key map (SCL), the inert ids (17 .. 24,
 * the arpeggiator's until 2026-10; the old step sequencer's LEN DIV SWING GATE QUANTIZE, kept in the stored formats;
 * 33 .. 36, the SLICER's until 2026-10-16) */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_RSV17 && i <= P_SGATE) ||
           (i >= P_RSV33 && i <= P_RSV36) || i == P_CHRD || i == P_VOIC || i == P_SQNT ||
           i == P_DLY ||                              /* (2026-10-18: the patch's delay MIX, TIME, FEED: the patch's) */
           (i >= P_AMODE && i <= P_AORDER);           /* (2026-10-23: the track's arp, as its MIDI IN, 19 and 20) */
}

/* preset pi of the engine the track asked for (eng_req): the sound only (not param_kept); its notes released (MONO /
 * POLY may change) */
static void patch_preset(track_t *t, uint32_t pi)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    const preset_t *pr;
    uint32_t i;
    panic_req |= (uint8_t)(1u << trk_index(t));
    t->user = 0;
    if (!e->npresets)
        return;
    pi %= e->npresets;
    pr = &e->presets[pi];
    t->preset = (uint8_t)pi;
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))                           /* sounds the same after any edit */
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = pr->vmode ? (int16_t)(pr->vmode - 1u) : pr->mono ? V_LEGATO : V_POLY;   /* (mono: legato) */
    t->p[P_DETUNE] = pr->det ? (int16_t)(pr->det - 1u) : TP[P_DETUNE].def;
    t->p[P_GLIDE] = pr->glide ? (int16_t)(pr->glide - 1u) : TP[P_GLIDE].def;
    t->p[P_OCT] = pr->oct;                            /* the sound's octave */
    t->p[P_ED_SHP] = pr->eshp;                        /* (a moving shape: ENV and LFO -> SHAPE, the LFO's rate, wave) */
    t->p[P_LD_SHP] = pr->lshp;
    if (pr->lrate)
        t->p[P_LRATE] = (int16_t)(pr->lrate - 1u);
    if (pr->lwave)
        t->p[P_LWAVE] = (int16_t)(pr->lwave - 1u);
    for (i = 0; i < 4u; i++)                          /* the sends (2026-10-18: not the delay's: the patch's, out by */
        if (P_DIST + i != P_DLY)                      /* default) */
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
}

/* engine index ei (this build's) with its defaults and its first preset. The caller holds the audio side off (the ISR
 * must see the old engine with its values or the new one with its own: voice.c engine_block) */
static void patch_engine(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    patch_preset(t, 0);
}

/* engine index e's preset named name (0: none of that name) */
static uint32_t patch_find_preset(uint32_t e, const char *name)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e % NENGINES]->npresets; i++)
        if (str_eq(ENGINES[e % NENGINES]->presets[i].name, name))
            return i;
    return 0;
}
