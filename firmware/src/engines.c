/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
/* Engine table (this build's engines in display order: the indices; their ids: core.h) and the parts' sounds at
 * power-on. */
#include "dsp.c"
#include "shared/phys_dsp.c"   /* the resonators MODAL and ANVIL play (PHYS's, DaisySP's: MIT) */
#include "shared/drum_map.c"   /* the drum lanes and GM map KIT, WAR and GLITCH play */
/* (Fel-Tracker) the engines' own memory of a track (a cavern, a ring, a voice's grains), one block a track that every
 * such engine borrows: a track plays one engine at a time (voice.c: the old one's voices fade and go before the new one
 * takes the track), so the block is as big as the largest of this build's engines needs, not their sum. An engine
 * asks for it with its key (eng_slab, after the engines): the first ask of a key the block was not lent to clears it
 * (as at power-on), so a sound starts on silence; MODAL and ANVIL share theirs (ANVIL plays MODAL's resonator). 0: the
 * track is no part, or the engine needs more than the block (a build without it): it plays nothing */
enum { SLAB_NONE, SLAB_ABYSS, SLAB_SHRED, SLAB_MODAL, SLAB_ECHOES, SLAB_PARTICLE, SLAB_GLITCH, SLAB_FLUIDS, SLAB_SCREAM,
       SLAB_KARPLUS, SLAB_SIREN8, SLAB_STRINGS, SLAB_FLOOR, SLAB_SANDSTORM };
static void *eng_slab(const track_t *t, uint32_t key, uint32_t bytes);
#include "eng_wave.c"           /* WAVE: wavetables (1.5) */
#include "eng_byte.c"           /* BYTE: bytebeat formulas (1.5) */
#include "eng_west.c"           /* WEST: complex oscillator, folder, low-pass gate (1.5) */
#include "eng_hoover.c"         /* HOOVER: PWM saws, sub, bend: the rave lead (1.5) */
#include "eng_speech.c"         /* SPEECH: LPC words (1.5; tools/gen_speech.py) */
#include "eng_fmperc.c"         /* FMPERC: two-operator FM percussion (1.5) */
#include "eng_karplus.c"        /* KARPLUS: a plucked string, Karplus-Strong (1.5) */
#include "eng_vosim.c"          /* VOSIM: formant pulse trains (1.5) */
#include "eng_particle.c"       /* PARTICLE: a cloud of synthesized grains (1.5) */
#include "eng_chaos.c"          /* CHAOS: chaotic systems as oscillators (1.5) */
#include "eng_sid.c"            /* SID: a voice of the C64's sound chip, in its manner (1.5) */
#include "eng_dubsiren.c"       /* DUBSIREN: the sound-system siren, its own tape echo (1.5) */
#include "eng_dubsiren8.c"      /* DUBSIREN8: its 8-bit cousin, every track (1.5) */
#include "eng_dubchord.c"       /* DUBCHORD: the dub-techno chord, a whole chord in one voice (1.5) */
#include "eng_bliss.c"         /* BLISS: FM electric pianos, bells, mallets (1.5) */
#include "eng_wasp.c"           /* WASP: two oscillators, a gritty multimode filter (1.5) */
#include "eng_modal.c"          /* MODAL: a modal resonator, struck, plucked, bowed (1.5) */
#include "eng_vector.c"         /* VECTOR: four sources, a point moving among them (1.5) */
#include "eng_echoes.c"         /* ECHOES: a note and its repeats, each played anew (1.5) */
#include "eng_glitch.c"         /* GLITCH: a kit of micro-sounds (1.5) */
#include "eng_dark.c"           /* DARK: sub-bass from sines, folded (1.5, FOUNDRY) */
#include "eng_razor.c"          /* RAZOR: a stack of saws, a sharp edge (1.5, FOUNDRY) */
#include "eng_guts.c"           /* GUTS: acid from a lopsided folder (1.5, FOUNDRY) */
#include "eng_slag.c"           /* SLAG: drifting oscillators, molten saturation (1.5, FOUNDRY) */
#include "eng_venom.c"          /* VENOM: two filters biting each other (1.5, FOUNDRY) */
#include "eng_honks.c"          /* HONKS: a reed through two formants (1.5, FOUNDRY) */
#include "eng_war.c"            /* WAR: an industrial kit (1.5, FOUNDRY) */
#include "eng_choke.c"          /* CHOKE: gated noise, hard limited (1.5, FOUNDRY) */
#include "eng_rust.c"           /* RUST: corroded noise, metallic resonances (1.5, FOUNDRY) */
#include "eng_drill.c"          /* DRILL: an FM tone ramped at audio rate (1.5, FOUNDRY) */
#include "eng_anvil.c"          /* ANVIL: heavy metal struck (MODAL's resonator; 1.5, FOUNDRY) */
#include "eng_scream.c"         /* SCREAM: a resonator screaming by itself (1.5, FOUNDRY) */
#include "eng_fluids.c"         /* FLUIDS: dispersed, liquid waves (1.5, FOUNDRY) */
#include "eng_scratch.c"        /* SCRATCH: a turntablist's hands (1.5, FOUNDRY) */
#include "eng_abyss.c"          /* ABYSS: a cavern that rings (1.5, FOUNDRY) */
#include "eng_shred.c"          /* SHRED: a sound torn apart as it plays (1.5, FOUNDRY) */
#include "eng_kit.c"            /* KIT: a drum kit of FMPERC's voices on one part (1.5) */
#include "eng_linnkit.c"        /* LINNKIT: the 80s sampled drum machine, after the LinnDrum (1.5, WAREHOUSE) */
#include "eng_strings.c"        /* STRINGS: a string ensemble, after the Waldorf Streichfett (1.5, WAREHOUSE) */
#include "eng_breaks.c"         /* BREAKS: a synthesized breakbeat player (1.5) */
#include "eng_floor.c"          /* FLOOR: four-on-the-floor beats, as BREAKS the breaks (1.5, WAREHOUSE) */
#include "eng_risers.c"         /* RISERS: build-ups and landings, timed to the song (1.5) */
#include "eng_chipkit.c"        /* CHIPKIT: the drums of the 8-bit consoles (1.5, CHIP) */
#include "eng_sandstorm.c"      /* SANDSTORM: the late-90s trance lead (1.5, WAREHOUSE) */
#ifdef FELUCCA_NATIVE           /* (the native image's alone: engine_ids.h, core.h FELUCCA_NATIVE) */
#include "eng_nebula.c"         /* NEBULA: PARTICLE's cloud whole (native) */
#include "eng_canyon.c"         /* CANYON: ECHOES' repeats, all of them (native) */
#endif

/* the block's size: the largest this build's engines ask for (core.h FELUCCA_ENG_*) */
#define SLAB_MAX(a, b) ((a) > (b) ? (a) : (b))
#define SLAB_IF(on, sz) ((on) ? (uint32_t)(sz) : 0u)
#define ENG_SLAB_BYTES                                                                                                  \
    SLAB_MAX(SLAB_MAX(SLAB_MAX(SLAB_IF(FELUCCA_ENG_ABYSS, sizeof(ab_cav_t)),                                          \
                               SLAB_IF(FELUCCA_ENG_SHRED, sizeof(int16_t) * SH_LEN)),                                 \
                      SLAB_MAX(SLAB_IF(FELUCCA_ENG_MODAL || FELUCCA_ENG_ANVIL, sizeof(md_voice_t) * MD_POLY),         \
                               SLAB_IF(FELUCCA_ENG_ECHOES, sizeof(ec_g_t) * EC_POLY * EC_NG))),                       \
             SLAB_MAX(SLAB_MAX(SLAB_IF(FELUCCA_ENG_PARTICLE, sizeof(pt_grain_t) * PT_POLY * PT_NG),                   \
                               SLAB_IF(FELUCCA_ENG_GLITCH, sizeof(gl_voice_t) * GL_POLY)),                            \
                      SLAB_MAX(SLAB_MAX(SLAB_IF(FELUCCA_ENG_FLUIDS, sizeof(int32_t) * FL_POLY * FL_NAP),              \
                                        SLAB_IF(FELUCCA_ENG_SCREAM, sizeof(sc_t))),                                   \
                               SLAB_MAX(SLAB_MAX(SLAB_IF(FELUCCA_ENG_KARPLUS, sizeof(int16_t) * KP_POLY * KP_LEN),    \
                                                 SLAB_IF(FELUCCA_ENG_DUBSIREN8, sizeof(ds8_echo_t))),                 \
                                        SLAB_MAX(SLAB_MAX(SLAB_IF(FELUCCA_ENG_STRINGS, sizeof(st_ens_t)),             \
                                                          SLAB_IF(FELUCCA_ENG_FLOOR, sizeof(fl_part_t))),             \
                                                 SLAB_IF(FELUCCA_ENG_SANDSTORM, sizeof(ss_voice_t) * SS_POLY))))))
#define ENG_SLAB_WORDS ((ENG_SLAB_BYTES + 3u) / 4u + 1u)
static uint32_t eng_slab_mem[NPART][ENG_SLAB_WORDS] __attribute__((section(".pool")));
static uint8_t eng_slab_key[NPART];
static void *eng_slab(const track_t *t, uint32_t key, uint32_t bytes)
{
    uint32_t k;
    if (t < &trk[0] || t >= &trk[NPART] || bytes > sizeof eng_slab_mem[0])
        return 0;
    k = (uint32_t)(t - trk);
    if (eng_slab_key[k] != key) {
        memset(eng_slab_mem[k], 0, sizeof eng_slab_mem[k]);
        eng_slab_key[k] = (uint8_t)key;
    }
    return eng_slab_mem[k];
}

/* the engine of an id this build has none for (a retired id, or an engine left out): what the editor protocol
 * describes for it (NAMES, DESC: no presets, nothing to edit, named "-"). No track ever plays it */
static void eng_none_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void eng_none_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
#define ED_NONE {"-", F_INT, 0, 0, 0, 0, 0}
static const engine_t ENG_NONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {ED_NONE, ED_NONE, ED_NONE, ED_NONE, ED_NONE, ED_NONE, ED_NONE, ED_NONE},
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_ATK, P_DEC, P_SUS, P_REL},
};

/* this build's engines, in the distribution's order (core.h FELUCCA_DIST_ENGINES: the ENGI_ indices). Only what is
 * listed here is linked */
static const engine_t *const ENGINES[NENGINES] = {
#define X(N) &ENG_##N,
    FELUCCA_DIST_ENGINES(X)
#undef X
};

/* a track's engine as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* ids <-> indices. eng_id(e): engine e's id. eng_of_id(id): the index of the engine with that id, ENG_NO when the build
 * has none (a retired id, an engine left out, an id past the last) */
#define ENG_NO 0xFFu
#define EID_IDX(X) (FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : ENG_NO)
static const uint8_t ENG_OF_ID[EID_COUNT] = {
    [0] = ENG_NO, [1] = ENG_NO, [2] = ENG_NO, [3] = ENG_NO, [4] = ENG_NO, [5] = ENG_NO, [6] = ENG_NO, [7] = ENG_NO,
    [8] = ENG_NO, [9] = ENG_NO, [10] = ENG_NO, [11] = ENG_NO, [12] = ENG_NO, [13] = ENG_NO, [27] = ENG_NO,   /* (retired) */
#define ENGINE(N, id, sub) [EID_##N] = EID_IDX(N),
#include "engine_ids.h"
#undef ENGINE
};
static uint32_t eng_id(uint32_t e) { return ENGINES[eng_idx(e)]->id; }
static uint32_t eng_of_id(uint32_t id) { return id < EID_COUNT ? ENG_OF_ID[id] : ENG_NO; }
/* the engine of id, or ENG_NONE: what the protocol describes for an id (NAMES, DESC) */
static const engine_t *eng_by_id(uint32_t id) { return eng_of_id(id) == ENG_NO ? &ENG_NONE : ENGINES[eng_of_id(id)]; }
/* the engine a stored sound of id loads as: its own when the build has it; an engine left out: its substitute (the
 * one engine_ids.h names, down that chain, else the build's first engine), *sub set; ENG_NO for a retired or unknown id (a clean
 * break: the stores refuse such a sound) */
static const uint8_t EID_SUB[EID_COUNT] = {     /* (engine_ids.h: each engine's substitute) */
#define ENGINE(N, id, sub) [EID_##N] = EID_##sub,
#include "engine_ids.h"
#undef ENGINE
};
/* (Fel-Tracker, 2026-10: 8 engines a distribution) a chain that finds none of the build's: a kit (an engine of drum
 * lanes) loads as the kit track's power-on engine, any other sound as the mid track's (track 3: a pad, a chord), not as
 * the build's first engine (a pad as drums) */
static uint32_t trk_def_engine(uint32_t i);
static int eid_kit(uint32_t id)
{
    return id == EID_KIT || id == EID_BREAKS || id == EID_FMPERC || id == EID_GLITCH || id == EID_WAR || id == EID_CHOKE ||
           id == EID_LINNKIT || id == EID_FLOOR || id == EID_CHIPKIT;
}
static uint32_t eng_load_id(uint32_t id, int *sub)
{
    uint32_t n, id0 = id;
    if (sub)
        *sub = 0;
    if (id >= EID_COUNT || EID_RETIRED(id))
        return ENG_NO;
    for (n = 0; n < EID_COUNT && eng_of_id(id) == ENG_NO; n++)
        id = EID_SUB[id];
    if (sub)
        *sub = n != 0u;
    return n < EID_COUNT ? eng_of_id(id) : trk_def_engine(eid_kit(id0) ? 0u : NPART > 2u ? 2u : 0u);
}

/* (Fel-Tracker) engine e as track t may play it: one on the last track alone (engine_t.last_track: DUBSIREN) is its
 * substitute anywhere else (DUBSIREN8: the same controls and preset order, so the sound carries over), or itself
 * when the build has none (it then plays as it can: DUBSIREN dry). Every load goes through this: a pick, a preset, a
 * project, an instrument, a user preset */
static uint32_t eng_for_track(const track_t *t, uint32_t e)
{
    uint32_t s;
    if (e >= NENGINES || !ENGINES[e]->last_track || t == &trk[NPART - 1])
        return e;
    s = eng_of_id(EID_SUB[ENGINES[e]->id]);
    return s != ENG_NO ? s : e;
}

/* the engines one can pick (all of the build's), in display order: eng_ok(e), the n-th of them eng_vis(n), e's place
 * among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps). (The table is in display order: these are
 * the index itself) */
static int eng_ok(uint32_t e) { return e < NENGINES; }
static uint32_t eng_vis(uint32_t n) { return n % NENGINES; }
static uint32_t eng_rank(uint32_t e) { return e < NENGINES ? e : 0u; }
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return (eng_rank(e) + (dir > 0 ? 1u : NENGINES - 1u)) % NENGINES;
}

/* the parts at power-on (engine index, preset). (Fel-Tracker) The rhythm section first, the wide parts last: with 8
 * tracks 1..4 the drums (KIT's FM KIT, BREAKS) and the basses (SID BASS, WASP's FAT BASS), centred, 5..8 the chords
 * (DUBCHORD), the pad (VOSIM's FORMANT PAD), the lead (SID SYNC LEAD) and the siren (the last track's), spread left and
 * right in turn; with 4, the drums and the bass, then the pad (CHIP: BYTE's arpeggio) and the lead. An engine the build
 * leaves out: the second
 * pick, else its first engine, preset 0 */
#define TRK_PICK(X, p) {FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : 0u, FELUCCA_ENG_##X ? (uint8_t)(p) : 0u}
#define TRK_PICK2(X, p, Y, q)                                                                                          \
    {FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : FELUCCA_ENG_##Y ? (uint8_t)ENGI_##Y : 0u,                                  \
     FELUCCA_ENG_##X ? (uint8_t)(p) : FELUCCA_ENG_##Y ? (uint8_t)(q) : 0u}
#define TRK_PICK3(X, p, Y, q, Z, r)                                                                                    \
    {FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : FELUCCA_ENG_##Y ? (uint8_t)ENGI_##Y : FELUCCA_ENG_##Z ? (uint8_t)ENGI_##Z : 0u,  \
     FELUCCA_ENG_##X ? (uint8_t)(p) : FELUCCA_ENG_##Y ? (uint8_t)(q) : FELUCCA_ENG_##Z ? (uint8_t)(r) : 0u}
#define TRK_PICK5(V, o, X, p, Y, q, Z, r, W, u)                                                                        \
    {FELUCCA_ENG_##V ? (uint8_t)ENGI_##V : FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : FELUCCA_ENG_##Y ? (uint8_t)ENGI_##Y :     \
     FELUCCA_ENG_##Z ? (uint8_t)ENGI_##Z : FELUCCA_ENG_##W ? (uint8_t)ENGI_##W : 0u,                                   \
     FELUCCA_ENG_##V ? (uint8_t)(o) : FELUCCA_ENG_##X ? (uint8_t)(p) : FELUCCA_ENG_##Y ? (uint8_t)(q) :                 \
     FELUCCA_ENG_##Z ? (uint8_t)(r) : FELUCCA_ENG_##W ? (uint8_t)(u) : 0u}
#define TRK_PICK4(X, p, Y, q, Z, r, W, u)                                                                              \
    {FELUCCA_ENG_##X ? (uint8_t)ENGI_##X : FELUCCA_ENG_##Y ? (uint8_t)ENGI_##Y : FELUCCA_ENG_##Z ? (uint8_t)ENGI_##Z :     \
     FELUCCA_ENG_##W ? (uint8_t)ENGI_##W : 0u,                                                                         \
     FELUCCA_ENG_##X ? (uint8_t)(p) : FELUCCA_ENG_##Y ? (uint8_t)(q) : FELUCCA_ENG_##Z ? (uint8_t)(r) :                 \
     FELUCCA_ENG_##W ? (uint8_t)(u) : 0u}
/* (FOUNDRY: WAR's WAR KIT and EBM KIT, DARK's SUB WEIGHT, RAZOR's COLD BASS; DUBCHORD, VOSIM; RAZOR LEAD, SCREAM's
 * SIREN CRY. WAREHOUSE: KIT, FLOOR, GUTS' GUT ACID, HOOVER's REESE BASS; DUBCHORD, VOSIM; SANDSTORM's LEAD, SIREN8's BIT SIREN) */
#if NPART == 8
static const uint8_t TRK_DEF[NPART][2] = {TRK_PICK2(KIT, 0, WAR, 0), TRK_PICK4(FLOOR, 0, BREAKS, 0, KIT, 0, WAR, 10),
                                          TRK_PICK3(SID, 0, DARK, 0, GUTS, 0), TRK_PICK4(WASP, 7, SID, 10, RAZOR, 8, HOOVER, 8),
                                          TRK_PICK2(DUBCHORD, 0, WAVE, 0), TRK_PICK(VOSIM, 13),
                                          TRK_PICK4(SID, 2, RAZOR, 2, SANDSTORM, 0, HOOVER, 0),
                                          TRK_PICK4(DUBSIREN, 0, DUBSIREN8, 0, HOOVER, 5, SCREAM, 7)};   /* (no siren:
                                                                                 * HOOVER's SIREN, SCREAM's SIREN CRY) */
static const int8_t TRK_PAN[NPART] = {0, -8, 0, 0, -24, 24, -16, 16};
#else
/* (Fel-Tracker, 2026-10: 4 tracks everywhere) a role a track: the kit, the bass (both centred), the mid (left), the
 * high (right). CHIP: CHIPKIT's NES KIT, SID BASS, BYTE ARP, SID's SYNC LEAD; IDM: BREAKS, WASP's FAT BASS, WAVE's
 * WARM PAD, MODAL's KALIMBA; FOUNDRY: WAR KIT, DARK's SUB WEIGHT, SLAG's EMBER PAD, RAZOR LEAD; WAREHOUSE: FLOOR,
 * VENOM ACID, DUBCHORD's DUB STAB, SANDSTORM. (KIT and VOSIM first where a build has them: IDM NATIVE, and the host
 * tests' reference build, as before: KIT's FM KIT, SID BASS, VOSIM's FORMANT PAD, SID's SYNC LEAD) */
static const uint8_t TRK_DEF[NPART][2] = {TRK_PICK5(CHIPKIT, 0, KIT, 0, FLOOR, 0, WAR, 0, BREAKS, 0),
                                          TRK_PICK4(SID, 0, WASP, 7, DARK, 0, VENOM, 0),
                                          TRK_PICK5(BYTE, 0, VOSIM, 13, WAVE, 8, SLAG, 7, DUBCHORD, 0),
                                          TRK_PICK4(SID, 2, MODAL, 10, RAZOR, 2, SANDSTORM, 0)};
static const int8_t TRK_PAN[NPART] = {0, 0, -24, 24};
#endif
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
