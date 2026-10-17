/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
 * The composition is amb-1's, by Charles Vestal (GPL-3.0): docs/excerpt_amb1.c */
/* GEN (2026-10, Milestone 1): the generative ambient engine, on the ARP button. Its composition is amb1's
 * (docs/excerpt_amb1.c: the pieces, the breathing pad, the bells' loops, the drift, the arc, the euclidean pulse), in
 * fixed point and played on the first four tracks with their own sounds instead of amb1's own voices:
 *   track 1 PAD    four voices that breathe, as the sung tape loops of Music for Airports' "2/1": each swells in, hangs
 *                  and lets go on its own cycle (unequal lengths, 1 : 1.21 : 1.43 : 1.67, set by SWELL), taking its
 *                  note of the chord as it starts, so the chord forms and dissolves rather than sitting there. The
 *                  notes: the chord's tones voice-led from the last ones (the least movement, no two on one note)
 *   track 2 BASS   the chord's root, held; halfway to the next chord, sometimes the fifth (FIFTH); now and then it sits
 *                  a chord out (EVOLVE, and more when the arc is sparse)
 *   track 3 BELLS  four loops of 13, 17, 23 and 29 quarter notes, each in its register, phasing against each other;
 *                  each holds up to DENSITY notes (scale steps over the chord's degree: chord tones mostly, a passing
 *                  2nd, 4th or 6th), grown or pruned to the count at the end of each pass; EVOLVE moves or changes a
 *                  note and rests a pass now and then; sometimes a bell's octave with it
 *   track 4 PULSE  three euclidean patterns, 16, 12 and 7 steps long (a thump on the chord's root, a tick and a longer
 *                  tick on its root and fifth high up), HITS their weight; a bar may turn a pattern or move a hit
 *                  (EVOLVE), or rest 2 to 4 bars
 *        or WAVES  (2026-10-17, the pieces without a pulse: gen_preset_t.waves) the surf: a swell of noise on the chord's
 *                  root (SID's SURF: its filter opens as it rises), 2 .. 5 s long, the next after a gap of 1 .. 6 s;
 *                  fewer as the arc thins
 * DENSITY a role (2026-10-17, G_GDPAD .. G_GDTEX, the GEN MIX page while it plays): 64 the piece as written; down to 0
 * fewer breaths, bass notes, bells, hits, swells, until the role rests; up to 127 about twice as busy (shorter breaths,
 * the bass restruck on the bar, fuller loops, extra ticks, shorter gaps).
 * PLAY ALONG (2026-10-17): a key played while GEN plays is snapped to its scale (the nearest step, the lower on a tie)
 * and sounds on the bells' track (seq.c input_on, gen_play).
 * The harmony: SCALE on ROOT (G_GSCL, G_GROOT); a chord stacks the scale's thirds from a degree (four tones in a scale
 * of six or more, else three). It drifts: after DRIFT's time (x 0.7 .. 1.3) it moves on the next bar, to another degree
 * chosen by weight (in seven-note scales I 4, IV V vi 3, ii 2, iii 1, vii 0.4; else I 3, the others 1.5). The arc: a
 * slow random swell, -1 sparse .. +1 full, a new target every 50 .. 150 s, reached over ~35 s, times MOTION (G_GMOT):
 * the bells' count, the rests, the pulse's strength follow it. Time: 1/16 steps of the song's tempo (beat_samples: the
 * BPM, or the MIDI clock's), whether the transport runs or not. Randomness: one xorshift generator, seeded from the
 * clock at each start (gen.seed: the host tests' fixed one). A piece (GEN_PRESETS) is amb1's: the roles' levels,
 * DENSITY, EVOLVE, DRIFT, HITS, SWELL, MOTION, the tempo, ROOT, the scale, the bells' and the bass's octaves, FIFTH;
 * and here the four tracks' sounds (an engine id and a preset name, ui.c gen_apply) and the FX chain.
 * Included by seq.c: gen_block runs in events_block, every block; its decisions once a 1/16 (out of the ISR's loops). */
#define GEN_ROLES 4u
#define GEN_MAXN 8u                           /* notes a role holds at once */
#define GEN_NPAD 4u
#define GEN_NLOOP 4u
#define GEN_LOOPMAX 29u
#define GEN_NDRUM 3u
enum { GR_PAD, GR_BASS, GR_BELL, GR_PULSE };
typedef struct {
    const char *name;
    uint8_t scale, root, bpm;                 /* SCL's scale (N_SCALE), ROOT 0..11, the tempo */
    uint8_t lvl[GEN_ROLES];                   /* pad, bass, bells, pulse: 0..100 (0: the role rests) */
    uint8_t density, evolve, hits, swell, motion, fifth;   /* 0..100 */
    uint16_t drift;                           /* seconds a chord (amb1's DRIFT: 20 s * 2^(v / 25.6)) */
    int8_t boct, bassoct;                     /* the bells' and the bass's octave */
    uint8_t eng[GEN_ROLES];                   /* each track's engine (its id, EID_*) .. */
    const char *snd[GEN_ROLES];               /* .. and its preset, by name (ui.c gen_apply) */
    uint8_t fx[10];                           /* G_RTYPE G_RSIZE G_RDAMP G_DTYPE G_DTIME G_DFDBK G_DCOLOR G_DMIX G_CRATE G_CDEPTH */
    uint8_t waves;                            /* (2026-10-17) track 4 plays WAVES, not the PULSE */
} gen_preset_t;
/* amb1's eight pieces (docs/excerpt_amb1.c PIECES; its modes as SCL's: Ionian 1 MAJ, Dorian 3 DOR, Phrygian 8, Lydian
 * 9, Mixolydian 4 MIX, Aeolian 2 MIN, pentatonic major 5 PEN, minor 6 MPEN; its echo time as DIV: 1/4. -> 1/4, 1/2 and
 * 1/2. -> 1/2, a bar -> 1/1, 2 and 4 bars; its reverb size as SIZE, over 85 CONCERT, else HALL) */
static const gen_preset_t GEN_PRESETS[] = {
    /*            scl rt bpm  pad bas bel pul  den evo hit swe mot fif drift boct bass  sounds */
    {"GARDEN",     3, 2, 72, {60, 45, 65, 40}, 40, 35, 35, 60, 50, 35, 39, 0, 0,
     {EID_WAVE, EID_WAVE, EID_BLISS, EID_MODAL}, {"WARM PAD", "SUB BASS", "GLASS BELL", "WOOD DRUM"},
     {2, 102, 76, 3, 7, 55, 50, 57, 40, 60}},
    {"DEPARTURES", 1, 1, 60, {35, 0, 75, 40}, 35, 30, 0, 55, 40, 25, 59, -1, 0,
     {EID_WAVE, EID_WAVE, EID_MODAL, EID_SID}, {"STRING PAD", "SUB BASS", "KALIMBA", "SURF"},
     {4, 108, 100, 3, 7, 55, 60, 38, 35, 60}, 1},
    {"VOICES",     2, 5, 56, {85, 25, 0, 30}, 30, 30, 0, 70, 55, 30, 77, 0, -1,
     {EID_WAVE, EID_WAVE, EID_BLISS, EID_SID}, {"CHOIR PAD", "SUB BASS", "CELESTA", "SURF"},
     {4, 114, 42, 1, 9, 50, 40, 25, 30, 70}, 1},
    {"AFTERNOON",  9, 7, 84, {50, 35, 65, 25}, 55, 45, 30, 50, 55, 40, 34, 0, 0,
     {EID_WAVE, EID_WAVE, EID_BLISS, EID_MODAL}, {"GLASS PAD", "SUB BASS", "FM BELLS", "WOOD DRUM"},
     {2, 95, 51, 3, 6, 55, 55, 64, 45, 55}},
    {"NIGHT BUS",  2, 9, 96, {45, 55, 55, 60}, 45, 50, 55, 40, 70, 45, 30, 0, 0,
     {EID_WAVE, EID_SID, EID_BLISS, EID_MODAL}, {"MORPH PAD", "SID BASS", "SOFT EP", "WOOD DRUM"},
     {2, 89, 83, 1, 6, 60, 50, 70, 50, 50}},
    {"SHORELINE",  5, 4, 64, {40, 30, 60, 55}, 30, 40, 0, 80, 85, 20, 59, 0, 0,
     {EID_WAVE, EID_WAVE, EID_MODAL, EID_SID}, {"WARM PAD", "SUB BASS", "GLOCKEN", "SURF"},
     {4, 121, 89, 1, 8, 65, 45, 108, 30, 65}, 1},
    {"GLASS",      6, 3, 76, {35, 30, 80, 20}, 60, 55, 25, 45, 50, 30, 39, 1, 1,
     {EID_WAVE, EID_WAVE, EID_MODAL, EID_MODAL}, {"GLASS PAD", "SUB BASS", "GLASS", "WOOD DRUM"},
     {2, 102, 57, 3, 6, 50, 65, 57, 45, 50}},
    {"TIDES",      4, 10, 70, {55, 40, 60, 35}, 45, 75, 40, 60, 95, 55, 26, 0, 0,
     {EID_WAVE, EID_WAVE, EID_BLISS, EID_MODAL}, {"EVOLVER", "SUB BASS", "TUBULAR", "WOOD DRUM"},
     {4, 108, 51, 4, 7, 55, 50, 64, 40, 60}},
};
#define GEN_NPRESETS (sizeof GEN_PRESETS / sizeof GEN_PRESETS[0])
typedef char gen_names_fit[NELEM(N_GEN_PRESET) == GEN_NPRESETS ? 1 : -1];   /* (params.c: the knob's names) */
static const uint8_t GEN_LOOP_LEN[GEN_NLOOP] = {13, 17, 23, 29};
static const uint8_t GEN_LOOP_BASE[GEN_NLOOP] = {72, 67, 79, 62};   /* each loop's register */
static const uint8_t GEN_DRUM_N[GEN_NDRUM] = {16, 12, 7}, GEN_DRUM_KMAX[GEN_NDRUM] = {5, 7, 3}, GEN_DRUM_ROT[GEN_NDRUM] = {0, 2, 3};
static const uint16_t GEN_BREATH[GEN_NPAD] = {1000, 1210, 1430, 1670};   /* the pad's unequal cycles, /1000 */
static const uint16_t GEN_START[GEN_NPAD] = {0, 900, 820, 950};          /* where each starts its first, /1000 */

typedef struct {
    uint32_t rng;
    uint32_t pos;                             /* samples into the step */
    uint32_t step, bar, nchord;               /* the step of the bar (0..15), bars played, chords played */
    uint32_t six;                             /* 1/16s played */
    /* the harmony */
    uint8_t nsteps, steps[12];                /* the scale, semitones over ROOT */
    uint8_t deg;                              /* the chord's degree */
    uint8_t pcs[4], npc;                      /* its pitch classes (0..11, ROOT included), the root first */
    int32_t drift_left, drift_total;          /* steps to the next chord */
    uint8_t change, fifth_done;
    uint8_t kscl, kroot;                      /* the key the chord was made in (a new one: gen_key) */
    int32_t arc, arc_tgt, arc_left, arcm;     /* the arc, -1000 .. 1000 (arcm: times MOTION) */
    /* the roles' notes: what each holds and for how long (steps; 0: until let go) */
    uint8_t held[GEN_ROLES][GEN_MAXN], nheld[GEN_ROLES];
    uint16_t left[GEN_ROLES][GEN_MAXN];
    uint32_t played[GEN_ROLES];               /* notes each role played since the start (the tests, the studio) */
    /* the pad's four voices: the note sounding (0 none), the next one, where in the breath, its length (steps) */
    uint8_t pnote[GEN_NPAD], ptarget[GEN_NPAD];
    int32_t ppos[GEN_NPAD], plen[GEN_NPAD], pleave[GEN_NPAD];   /* (pleave: steps until a new breath, after a leave) */
    uint8_t bass_rest;
    /* the bells' loops: scale steps over the chord's degree (-1 none), their velocities, where they are */
    int8_t lnote[GEN_NLOOP][GEN_LOOPMAX];
    uint8_t lvel[GEN_NLOOP][GEN_LOOPMAX], lpos[GEN_NLOOP], lrest[GEN_NLOOP];
    /* the pulse */
    uint32_t dpat[GEN_NDRUM];
    uint8_t dpos[GEN_NDRUM], dk[GEN_NDRUM], dkb[GEN_NDRUM], drot[GEN_NDRUM], prest;
    /* WAVES: steps to the next swell */
    int32_t wleft;
    /* PLAY ALONG: the note each key sounds on the bells' track (0 none) */
    uint8_t kn[128];
} gen_state_t;
static struct {
    volatile uint8_t req;                     /* main: 1 start, 2 stop (gen_block takes it) */
    volatile uint8_t on;                      /* ISR: playing */
    uint32_t seed;                            /* 0 = from the clock at the start */
    gen_state_t s;                            /* the piece as it plays (cleared at each start) */
} gen;
#define GS (gen.s)

static uint32_t gen_rand(void)
{
    uint32_t s = GS.rng;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    GS.rng = s;
    return s;
}
static int32_t gen_pick(uint32_t n) { return n ? (int32_t)((gen_rand() >> 8) % n) : 0; }
static int gen_chance(int32_t permille) { return gen_pick(1000u) < permille; }
static const gen_preset_t *gen_piece(void) { return &GEN_PRESETS[(uint32_t)clamp(song.g[G_GPRE], 0, GEN_NPRESETS - 1)]; }
static int32_t gen_secs(int32_t s)            /* seconds as steps at the tempo */
{
    return s * clamp(song.g[G_BPM], 20, 300) / 15;
}
/* a role's DENSITY as a factor, x 1000 (64: 1000, the piece as written; 0: 0; 127: 1984) */
static int32_t gen_dens(uint32_t role) { return clamp(song.g[G_GDPAD + role], 0, 127) * 1000 / 64; }

/* ---------------------------------------------------------------- notes --- */
static void gen_note_off(uint32_t role, uint32_t k)
{
    trk_note_off(&trk[role], GS.held[role][k]);
    GS.nheld[role]--;
    GS.held[role][k] = GS.held[role][GS.nheld[role]];
    GS.left[role][k] = GS.left[role][GS.nheld[role]];
}
static void gen_off_note(uint32_t role, uint32_t note)   /* the role's note let go, if it holds it */
{
    uint32_t k;
    for (k = 0; k < GS.nheld[role]; k++)
        if (GS.held[role][k] == note) {
            gen_note_off(role, k);
            return;
        }
}
static void gen_off(uint32_t role)            /* every note of the role let go */
{
    while (GS.nheld[role])
        gen_note_off(role, GS.nheld[role] - 1u);
}
/* a note of the role, its velocity (0..100, scaled by the role's level), held len steps (0: until let go) */
static void gen_on(uint32_t role, int32_t note, int32_t vel, uint32_t len)
{
    int32_t lvl = gen_piece()->lvl[role];
    note = clamp(note, 0, 127);
    if (!lvl)
        return;
    gen_off_note(role, (uint32_t)note);        /* (the same note again: from its start) */
    if (GS.nheld[role] >= GEN_MAXN)
        gen_note_off(role, 0);
    trk_note_on(&trk[role], (uint32_t)note, (uint32_t)clamp(vel * (60 + 67 * lvl / 100) / 100, 1, 127));
    GS.held[role][GS.nheld[role]] = (uint8_t)note;
    GS.left[role][GS.nheld[role]] = (uint16_t)len;
    GS.nheld[role]++;
    GS.played[role]++;
}

/* --------------------------------------------------------------- harmony --- */
static void gen_scale(void)
{
    uint32_t i, s = (uint32_t)clamp(song.g[G_GSCL], 0, 15);
    uint32_t mask = (s ? SCALE_MASK[s] : SCALE_MASK[1]) | 1u;   /* (CHR: no thirds to stack, the major scale) */
    GS.nsteps = 0;
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u)
            GS.steps[GS.nsteps++] = (uint8_t)i;
    if (GS.deg >= GS.nsteps)
        GS.deg = 0;
}
static void gen_chord_tones(void)            /* amb1 en_chord_tones: stacked thirds, then what is left */
{
    static const uint8_t OFF[7] = {0, 2, 4, 6, 1, 3, 5};
    uint32_t m = GS.nsteps, want = m >= 6u ? 4u : 3u, i, j, n = 0;
    int32_t root = clamp(song.g[G_GROOT], 0, 11);
    for (i = 0; i < 7u && n < want; i++) {
        uint32_t pc = (uint32_t)(root + GS.steps[(GS.deg + OFF[i]) % m]) % 12u, dup = 0;
        for (j = 0; j < n; j++)
            dup |= GS.pcs[j] == pc;
        if (!dup)
            GS.pcs[n++] = (uint8_t)pc;
    }
    GS.npc = (uint8_t)n;
}
static int32_t gen_nearest(int32_t from, int32_t pc, int32_t lo, int32_t hi)   /* class pc nearest from, in lo..hi */
{
    int32_t n = from + ((pc - from) % 12 + 12) % 12;
    if (n - from > 6)
        n -= 12;
    while (n < lo)
        n += 12;
    while (n > hi)
        n -= 12;
    return n;
}
/* amb1 en_voice_lead: the chord's tones to the four voices, the least movement over the 24 orders, no two voices on
 * one note (three tones: the root doubled) */
static void gen_voice_lead(void)
{
    static const uint8_t PERM[24][4] = {
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};
    int32_t t[4], to[4], best = 1 << 30, p, v, a, b;
    for (v = 0; v < 4; v++)
        t[v] = GS.pcs[v < GS.npc ? v : 0];
    for (p = 0; p < 24; p++) {
        int32_t c = 0, n[4];
        for (v = 0; v < 4; v++) {
            n[v] = gen_nearest(GS.ptarget[v], t[PERM[p][v]], 48, 77);
            c += n[v] > GS.ptarget[v] ? n[v] - GS.ptarget[v] : GS.ptarget[v] - n[v];
        }
        for (a = 0; a < 4; a++)
            for (b = a + 1; b < 4; b++)
                if (n[a] == n[b])
                    c += 40;
        if (c < best) {
            best = c;
            for (v = 0; v < 4; v++)
                to[v] = n[v];
        }
    }
    for (v = 0; v < 4; v++)
        GS.ptarget[v] = (uint8_t)to[v];
}
static void gen_drift_reset(void)
{
    GS.drift_total = gen_secs(gen_piece()->drift) * (700 + gen_pick(600u)) / 1000;
    if (GS.drift_total < 16)
        GS.drift_total = 16;
    GS.drift_left = GS.drift_total;
    GS.fifth_done = 0;
}
static void gen_bass_to(uint32_t pc)
{
    int32_t f = gen_dens(GR_BASS);
    gen_off(GR_BASS);
    if (!GS.bass_rest && (f >= 1000 || gen_chance(f)))   /* (DENSITY under 64: some notes rest) */
        gen_on(GR_BASS, 36 + (int32_t)pc % 12 + 12 * gen_piece()->bassoct, 80, 0u);
}
/* a pad voice's new breath: its length (SWELL, x 0.85 .. 1.15), its note (the target), its strength */
static void gen_breathe(uint32_t i)
{
    int32_t secs10 = (40 + 12 * gen_piece()->swell / 10) * GEN_BREATH[i] / 1000;   /* (tenths of a second) */
    int32_t f = gen_dens(GR_PAD);
    GS.plen[i] = gen_secs(secs10) * (850 + gen_pick(300u)) / 10000;
    if (f > 1000)                              /* (DENSITY over 64: shorter breaths, more of them) */
        GS.plen[i] = GS.plen[i] * 1000 / f;
    if (GS.plen[i] < 8)
        GS.plen[i] = 8;
    GS.ppos[i] = 0;
    if (GS.pnote[i])
        gen_off_note(GR_PAD, GS.pnote[i]);
    GS.pnote[i] = 0;
    if (f < 1000 && !gen_chance(f))            /* (under 64: this breath skipped, the voice silent its cycle) */
        return;
    GS.pnote[i] = GS.ptarget[i];
    gen_on(GR_PAD, GS.pnote[i], 50 + 4 * (6 + gen_pick(5u)), 0u);   /* (amb1: 50 + 40 x 0.6 .. 1) */
}
/* to another degree, by weight (amb1 chord_change); the pad's voices on a note the new chord lacks let go and breathe
 * in on their new note a second later; the bass takes the root, or rests the chord out */
static void gen_chord_change(void)
{
    static const uint8_t W7[7] = {40, 20, 10, 30, 30, 30, 4};
    uint32_t w[12], sum = 0, i, k;
    int32_t r;
    for (i = 0; i < GS.nsteps; i++) {
        w[i] = i == GS.deg ? 0u : GS.nsteps == 7u ? W7[i] : i == 0u ? 30u : 15u;
        sum += w[i];
    }
    r = gen_pick(sum);
    for (i = 0; i < GS.nsteps; i++) {
        r -= (int32_t)w[i];
        if (r < 0 && w[i])
            break;
    }
    if (i >= GS.nsteps)
        i = GS.deg == 0u ? (GS.nsteps > 1u ? 1u : 0u) : 0u;
    GS.deg = (uint8_t)i;
    gen_chord_tones();
    gen_voice_lead();
    for (i = 0; i < GEN_NPAD; i++) {           /* the voices on a note the chord lacks: out, a new breath soon */
        uint32_t sh = 0;
        for (k = 0; k < GS.npc; k++)
            sh |= GS.pnote[i] % 12u == GS.pcs[k];
        if (GS.pnote[i] && !sh && !GS.pleave[i]) {
            gen_off_note(GR_PAD, GS.pnote[i]);
            GS.pnote[i] = 0;
            GS.pleave[i] = gen_secs(1) + 1;
        }
    }
    GS.bass_rest = (uint8_t)gen_chance(gen_piece()->evolve * 3 + (GS.arcm < 0 ? -GS.arcm / 4 : 0));
    gen_bass_to(GS.pcs[0]);
    gen_drift_reset();
    GS.nchord++;
}

/* a new key (SCALE, ROOT turned while it plays; amb1 pad_handoff): the chord again on its degree in the new scale, the
 * pad's voices on a note it lacks out and in again a second later on their new notes, the bass on the new root */
static __attribute__((noinline)) void gen_key(void)
{
    uint32_t i, k;
    gen_chord_tones();
    gen_voice_lead();
    for (i = 0; i < GEN_NPAD; i++) {
        uint32_t sh = 0;
        for (k = 0; k < GS.npc; k++)
            sh |= GS.pnote[i] % 12u == GS.pcs[k];
        if (GS.pnote[i] && !sh && !GS.pleave[i]) {
            gen_off_note(GR_PAD, GS.pnote[i]);
            GS.pnote[i] = 0;
            GS.pleave[i] = gen_secs(1) + 1;
        }
    }
    gen_bass_to(GS.pcs[0]);
}

/* -------------------------------------------------------------- the loops --- */
static int32_t gen_offset(void)              /* scale steps above the chord's degree */
{
    if (gen_chance(720))                       /* a chord tone, sometimes an octave up */
        return 2 * gen_pick(GS.npc) + (gen_chance(300) ? GS.nsteps : 0);
    return 1 + 2 * gen_pick(3u);               /* a passing note: the 2nd, 4th or 6th */
}
static int32_t gen_bell_note(uint32_t l, int32_t off)   /* a loop slot's note, in the loop's register */
{
    int32_t d = GS.deg + off, oct = d / GS.nsteps, root = clamp(song.g[G_GROOT], 0, 11), base, n;
    base = GEN_LOOP_BASE[l] - ((GEN_LOOP_BASE[l] - root) % 12 + 12) % 12;
    n = base + GS.steps[d % GS.nsteps] + 12 * oct + 12 * gen_piece()->boct;
    while (n > 103)
        n -= 12;
    while (n < 36)
        n += 12;
    return n;
}
static uint32_t gen_loop_count(uint32_t l)
{
    uint32_t i, c = 0;
    for (i = 0; i < GEN_LOOP_LEN[l]; i++)
        c += GS.lnote[l][i] >= 0;
    return c;
}
static uint32_t gen_loop_target(void)        /* 0..5 notes: DENSITY, swelled and thinned by the arc */
{
    int32_t d = gen_piece()->density * (1000 + 4 * GS.arcm / 5) / 20 * gen_dens(GR_BELL) / 1000;   /* (x 1000) */
    return (uint32_t)((clamp(d, 0, 5000) + gen_pick(1000u)) / 1000);
}
static void gen_loop_add(uint32_t l)
{
    uint32_t i, s;
    for (i = 0; i < 8u; i++) {
        s = (uint32_t)gen_pick(GEN_LOOP_LEN[l]);
        if (GS.lnote[l][s] < 0) {
            GS.lnote[l][s] = (int8_t)gen_offset();
            GS.lvel[l][s] = (uint8_t)(50 + gen_pick(50u));
            return;
        }
    }
}
static int32_t gen_loop_pick(uint32_t l)     /* a sounding slot, -1 none */
{
    uint32_t len = GEN_LOOP_LEN[l], i, s = (uint32_t)gen_pick(len);
    for (i = 0; i < len; i++, s = (s + 1u) % len)
        if (GS.lnote[l][s] >= 0)
            return (int32_t)s;
    return -1;
}
static void gen_loop_regrow(uint32_t l)
{
    uint32_t i, n = gen_loop_target();
    for (i = 0; i < GEN_LOOPMAX; i++)
        GS.lnote[l][i] = -1;
    for (i = 0; i < n; i++)
        gen_loop_add(l);
}
static void gen_loop_pass(uint32_t l)        /* the end of a pass: grow, prune, evolve */
{
    uint32_t c = gen_loop_count(l), t = gen_loop_target();
    int32_t s, ev = gen_piece()->evolve;
    for (; c < t; c++)
        gen_loop_add(l);
    for (; c > t && (s = gen_loop_pick(l)) >= 0; c--)
        GS.lnote[l][s] = -1;
    GS.lrest[l] = (uint8_t)gen_chance(ev * 3 / 2 + (GS.arcm < 0 ? -GS.arcm * 3 / 20 : 0));
    if (gen_chance(ev * 7) && (s = gen_loop_pick(l)) >= 0) {
        if (gen_chance(500)) {
            GS.lnote[l][s] = (int8_t)gen_offset();                 /* another note */
        } else {
            int32_t d = gen_pick(GEN_LOOP_LEN[l]);                /* the same note, elsewhere */
            if (GS.lnote[l][d] < 0) {
                GS.lnote[l][d] = GS.lnote[l][s];
                GS.lvel[l][d] = GS.lvel[l][s];
                GS.lnote[l][s] = -1;
            }
        }
    }
}
static void gen_bell(uint32_t l, uint32_t s)
{
    int32_t n = gen_bell_note(l, GS.lnote[l][s]), v = GS.lvel[l][s];
    gen_on(GR_BELL, n, v, 6u);
    if (n + 12 <= 103 && gen_chance(80 + (GS.arcm > 0 ? GS.arcm * 12 / 100 : 0)))
        gen_on(GR_BELL, n + 12, v * 2 / 5, 4u);                  /* now and then, its octave */
}

/* -------------------------------------------------------------- the pulse --- */
static uint32_t gen_euclid(int32_t k, int32_t n, int32_t rot)   /* amb1 en_euclid: Bjorklund, LSB first */
{
    uint32_t gb[32], m = 0;
    uint8_t gl[32];
    int32_t ng, heads, tails, i, pos = 0;
    if (n <= 0 || n > 32 || k <= 0)
        return 0;
    if (k > n)
        k = n;
    for (i = 0; i < n; i++) {
        gb[i] = i < k ? 1u : 0u;
        gl[i] = 1;
    }
    ng = n;
    heads = k;
    tails = n - k;
    while (tails > 1 && heads > 0) {
        int32_t pairs = heads < tails ? heads : tails;
        for (i = 0; i < pairs; i++) {
            gb[i] |= gb[ng - pairs + i] << gl[i];
            gl[i] = (uint8_t)(gl[i] + gl[ng - pairs + i]);
        }
        if (heads > tails) {
            tails = heads - pairs;
            heads = pairs;
        } else {
            tails -= pairs;
            heads = pairs;
        }
        ng = heads + tails;
    }
    for (i = 0; i < ng; i++) {
        uint32_t j;
        for (j = 0; j < gl[i]; j++)
            if ((gb[i] >> j) & 1u)
                m |= 1u << (((pos + (int32_t)j + rot) % n + n) % n);
        pos += gl[i];
    }
    return m;
}
static void gen_drums_pattern(void)          /* HITS: the patterns' home */
{
    uint32_t d;
    for (d = 0; d < GEN_NDRUM; d++) {
        GS.dkb[d] = GS.dk[d] = (uint8_t)((gen_piece()->hits * GEN_DRUM_KMAX[d] + 50) / 100);
        GS.drot[d] = GEN_DRUM_ROT[d];
        GS.dpat[d] = gen_euclid(GS.dk[d], GEN_DRUM_N[d], GS.drot[d]);
    }
}
static void gen_drums_evolve(void)           /* a bar: a pattern turns, or gains or loses a hit */
{
    uint32_t d = (uint32_t)gen_pick(GEN_NDRUM), n = GEN_DRUM_N[d];
    if (!GS.dkb[d])
        return;
    if (gen_chance(500)) {
        GS.drot[d] = (uint8_t)((GS.drot[d] + (gen_chance(500) ? 1u : n - 1u)) % n);
    } else {
        int32_t k = GS.dk[d] + (gen_chance(500) ? 1 : -1);
        if (k >= GS.dkb[d] - 1 && k <= GS.dkb[d] + 1 && k >= 1 && k <= (int32_t)n)
            GS.dk[d] = (uint8_t)k;
    }
    GS.dpat[d] = gen_euclid(GS.dk[d], (int32_t)n, GS.drot[d]);
}
static void gen_drum(uint32_t d, int32_t vel)   /* the thump on the root; the ticks on the root and fifth, high */
{
    int32_t r = GS.pcs[0], f = GS.npc > 2u ? GS.pcs[2] : GS.pcs[0];
    int32_t n = d == 0u ? 36 + r : d == 1u ? 84 + f : 72 + r;
    gen_on(GR_PULSE, n, vel, d == 2u ? 3u : 1u);
}

/* ---------------------------------------------------------------- a step --- */
static __attribute__((noinline)) void gen_first(void)   /* the first step: the chord, the pad's first breaths */
{
    uint32_t k;
    gen_chord_tones();
    for (k = 0; k < GEN_NPAD; k++)
        GS.ptarget[k] = (uint8_t)(50 + 7 * k);
    gen_voice_lead();
    gen_bass_to(GS.pcs[0]);
    for (k = 0; k < GEN_NPAD; k++) {           /* the first breaths overlap: each from its place in its cycle */
        gen_breathe(k);
        GS.ppos[k] = GS.plen[k] * GEN_START[k] / 1000;
        if (GS.ppos[k] > GS.plen[k] * 55 / 100) {   /* (in its rest: silent until its next breath) */
            gen_off_note(GR_PAD, GS.pnote[k]);
            GS.pnote[k] = 0;
        }
    }
}
static __attribute__((noinline)) void gen_step(void)   /* (once a 1/16: out of the ISR's loops) */
{
    const gen_preset_t *p = gen_piece();
    uint32_t r, k, l, d;
    int32_t sps = gen_secs(1), m = clamp(song.g[G_GMOT], 0, 127);
    gen_scale();
    if (!GS.npc)
        gen_first();
    else if (GS.kscl != (uint8_t)song.g[G_GSCL] || GS.kroot != (uint8_t)song.g[G_GROOT])
        gen_key();
    GS.kscl = (uint8_t)song.g[G_GSCL];
    GS.kroot = (uint8_t)song.g[G_GROOT];
    for (r = 0; r < GEN_ROLES; r++)            /* the notes whose time is over */
        for (k = GS.nheld[r]; k-- > 0;)
            if (GS.left[r][k] && !--GS.left[r][k])
                gen_note_off(r, k);
    /* the arc: a new target every 50 .. 150 s, reached over ~35 s; times MOTION */
    if (--GS.arc_left <= 0) {
        int32_t t = gen_pick(2001u) - 1000;
        GS.arc_tgt = t * (600 + gen_pick(400u)) / 1000 + (t > 0 ? 250 : -250);
        GS.arc_left = (50 + gen_pick(100u)) * sps;
    }
    GS.arc += (clamp(GS.arc_tgt, -1000, 1000) - GS.arc) / (35 * sps + 1);
    GS.arcm = GS.arc * m / 127;
    /* the drift: halfway the bass may lean on the fifth; at its end the chord moves, on the next bar */
    if (!GS.change) {
        GS.drift_left--;
        if (!GS.fifth_done && GS.drift_left < GS.drift_total / 2) {
            GS.fifth_done = 1;
            if (gen_chance(p->fifth * 10) && GS.npc >= 3u && !GS.bass_rest)
                gen_bass_to(GS.pcs[2]);
        }
        if (GS.drift_left <= 0)
            GS.change = 1;
    }
    if (GS.step == 0u && GS.change) {
        GS.change = 0;
        gen_chord_change();
    }
    /* the pad: each voice's breath (rise, hang: held; the fall: its release; a short rest), a new one at its end */
    for (k = 0; k < GEN_NPAD; k++) {
        if (GS.pleave[k]) {                     /* (left a chord: a new breath on its new note a second later) */
            if (!--GS.pleave[k])
                gen_breathe(k);
            continue;
        }
        if (++GS.ppos[k] >= GS.plen[k])
            gen_breathe(k);
        else if (GS.pnote[k] && GS.ppos[k] >= GS.plen[k] * 55 / 100) {
            gen_off_note(GR_PAD, GS.pnote[k]);
            GS.pnote[k] = 0;
        }
    }
    /* the pulse: a bar may rest it (2 .. 4 bars) or turn a pattern (EVOLVE); the hits, the arc's strength */
    if (GS.step == 0u) {
        if (GS.prest)
            GS.prest--;
        else if (gen_chance(p->evolve * 15 / 100))
            GS.prest = (uint8_t)(2 + gen_pick(3u));
        if (gen_chance(p->evolve * 25 / 10))
            gen_drums_evolve();
    }
    if (p->waves) {                            /* (2026-10-17) WAVES: a swell, then a gap */
        int32_t f = gen_dens(GR_PULSE), sw;
        if (--GS.wleft <= 0) {
            sw = gen_secs(2) + gen_pick((uint32_t)gen_secs(3));
            if (f && gen_chance(clamp(700 + 3 * GS.arcm / 10, 250, 1000)))   /* (fewer as the arc thins) */
                gen_on(GR_PULSE, 48 + GS.pcs[0] % 12 + 12 * gen_pick(2u), 55 + gen_pick(45u), (uint32_t)sw);
            GS.wleft = sw + (gen_secs(1) + gen_pick((uint32_t)gen_secs(5))) * 1000 / (f ? f : 1000);
        }
    } else
    for (d = 0; d < GEN_NDRUM; d++) {
        int32_t f = gen_dens(GR_PULSE);
        if (((GS.dpat[d] >> GS.dpos[d]) & 1u) && !GS.prest && (f >= 1000 || gen_chance(f))) {
            int32_t pa = clamp(1000 + 13 * GS.arcm / 10, 300, 1150);
            gen_drum(d, ((GS.dpos[d] == 0u ? 95 : 60) + gen_pick(25u)) * pa / 1000);
        } else if (d == 1u && f > 1000 && !GS.prest && gen_chance((f - 1000) / 6)) {
            gen_drum(d, 40 + gen_pick(20u));     /* (DENSITY over 64: a tick between) */
        }
        if (++GS.dpos[d] >= GEN_DRUM_N[d])
            GS.dpos[d] = 0;
    }
    if (GS.step == 0u && gen_dens(GR_BASS) > 1000 && GS.nheld[GR_BASS] &&
        gen_chance((gen_dens(GR_BASS) - 1000) / 2)) {   /* (DENSITY over 64: the bass restruck on the bar) */
        uint32_t b = GS.held[GR_BASS][0];
        gen_off(GR_BASS);
        gen_on(GR_BASS, (int32_t)b, 70, 0u);
    }
    /* the bells: the loops step in quarter notes */
    if (!(GS.six & 3u))
        for (l = 0; l < GEN_NLOOP; l++) {
            uint32_t s = GS.lpos[l];
            if (GS.lnote[l][s] >= 0 && !GS.lrest[l])
                gen_bell(l, s);
            if (++GS.lpos[l] >= GEN_LOOP_LEN[l]) {
                GS.lpos[l] = 0;
                gen_loop_pass(l);
            }
        }
    GS.six++;
    if (++GS.step >= 16u) {
        GS.step = 0;
        GS.bar++;
    }
}

static int gen_play_off(uint32_t note);         /* (PLAY ALONG, below) */
static __attribute__((noinline)) void gen_stop_now(void)
{
    uint32_t r;
    for (r = 0; r < GEN_ROLES && r < NTRK; r++)
        gen_off(r);
    for (r = 0; r < 128u; r++)                 /* (the keys played along: let go) */
        gen_play_off(r);
    gen.on = 0;
}
static __attribute__((noinline)) void gen_start(void)
{
    uint32_t l;
    memset(&GS, 0, sizeof GS);
    GS.rng = gen.seed ? gen.seed : (fm1_ms * 2654435761u) ^ 0x9E3779B9u;
    if (!GS.rng)
        GS.rng = 1;
    gen_scale();
    gen_chord_tones();                         /* (the loops' notes are degrees over the chord: it first) */
    for (l = 0; l < GEN_NLOOP; l++)
        gen_loop_regrow(l);
    GS.npc = 0;                                 /* (gen_first plays the chord on the first step) */
    gen_drums_pattern();
    gen_drift_reset();
    GS.arc_left = 1;
    GS.pos = 0x7FFFFFFFu;                       /* (the first step at once) */
    gen.on = NTRK >= GEN_ROLES && BLUEHARE_PLUGIN_GEN;   /* (GEN a component, 2026-10-17) */
}
/* every block (events_block): a start or a stop asked for, then the steps due in these n samples */
static __attribute__((noinline)) void gen_block(uint32_t n)
{
    uint32_t len;
    if (gen.req == 1u) {
        gen.req = 0;
        if (gen.on)
            gen_stop_now();
        gen_start();
    } else if (gen.req == 2u) {
        gen.req = 0;
        if (gen.on)
            gen_stop_now();
    }
    if (!gen.on)
        return;
    len = beat_samples() / 4u;
    if (len < 64u)
        len = 64u;
    if (GS.pos == 0x7FFFFFFFu)
        GS.pos = len;
    else
        GS.pos += n;
    while (GS.pos >= len) {
        GS.pos -= len;
        gen_step();
    }
}

/* (2026-10-17) PLAY ALONG: a key while GEN plays (seq.c input_on): snapped to GEN's scale, on the bells' track; 1 taken */
static int gen_play(uint32_t note, uint32_t vel)
{
    int32_t root = clamp(song.g[G_GROOT], 0, 11), pc, best = 0, bd = 99;
    uint32_t i;
    if (!gen.on || note > 127u || !GS.nsteps)
        return 0;
    pc = ((int32_t)note - root + 120) % 12;
    for (i = 0; i < GS.nsteps; i++) {          /* the nearest step, the lower on a tie */
        int32_t d = GS.steps[i] - pc, a;
        if (d > 6)
            d -= 12;
        if (d < -6)
            d += 12;
        a = d < 0 ? -d * 2 + 1 : d * 2;
        if (a < bd) {
            bd = a;
            best = d;
        }
    }
    if (GS.kn[note])
        trk_note_off(&trk[GR_BELL], GS.kn[note]);
    GS.kn[note] = (uint8_t)clamp((int32_t)note + best, 1, 127);
    trk_note_on(&trk[GR_BELL], GS.kn[note], vel);
    return 1;
}
static int gen_play_off(uint32_t note)
{
    if (note > 127u || !GS.kn[note])
        return 0;
    trk_note_off(&trk[GR_BELL], GS.kn[note]);
    GS.kn[note] = 0;
    return 1;
}

/* (2026-10-17, the second review's A: out of os/ui.c) piece k: each of the first four tracks its sound (the preset of
 * that name of its engine; a build without the engine: its substitute, its first preset), loaded the instrument's way
 * (os_load_sound: the OS's undo and audio lock), and the scale, root, tempo, MOTION and the FX chain */
static void os_load_sound(uint32_t track, uint32_t engine, uint32_t preset);
static void gen_load_piece(uint32_t k)
{
    const gen_preset_t *p = &GEN_PRESETS[k % GEN_NPRESETS];
    static const uint8_t FXID[10] = {G_RTYPE, G_RSIZE, G_RDAMP, G_DTYPE, G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX, G_CRATE, G_CDEPTH};
    uint32_t r, i;
    for (r = 0; r < GEN_ROLES && r < NTRK; r++) {
        int sub;
        uint32_t e = eng_load_id(p->eng[r], &sub);
        if (e == ENG_NO)
            continue;
        os_load_sound(r, e, sub ? 0u : patch_find_preset(e, p->snd[r]));
    }
    song.g[G_GSCL] = p->scale;
    song.g[G_GROOT] = p->root;
    song.g[G_BPM] = p->bpm;
    song.g[G_GMOT] = (int16_t)(p->motion * 127 / 100);
    for (i = 0; i < 10u; i++)
        song.g[FXID[i]] = (int16_t)param_fit(&GP[FXID[i]], p->fx[i]);
}

/* the chord now, as a name ("FM7", "Dm7"; amb1 en_chord_name): b holds 8; "" stopped */
static void gen_chord_name(char *b)
{
    static const char *const NN[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    uint32_t iv = 0, i, third, fifth;
    const char *q;
    b[0] = 0;
    if (!gen.on || !GS.npc)
        return;
    for (i = 1; i < GS.npc; i++)
        iv |= 1u << ((GS.pcs[i] - GS.pcs[0] + 12u) % 12u);
    third = (iv >> 4) & 1u ? 4u : (iv >> 3) & 1u ? 3u : 0u;
    fifth = (iv >> 7) & 1u ? 7u : (iv >> 6) & 1u ? 6u : (iv >> 8) & 1u ? 8u : 0u;
    if (third == 4u)
        q = fifth == 8u ? "aug" : (iv >> 11) & 1u ? "M7" : (iv >> 10) & 1u ? "7" : (iv >> 9) & 1u ? "6" : (iv >> 2) & 1u ? "add9" : "";
    else if (third == 3u)
        q = fifth == 6u ? ((iv >> 10) & 1u ? "m7b5" : (iv >> 9) & 1u ? "dim7" : "dim")
            : (iv >> 10) & 1u ? "m7" : (iv >> 11) & 1u ? "mM7" : (iv >> 9) & 1u ? "m6" : (iv >> 2) & 1u ? "madd9" : "m";
    else
        q = (iv >> 5) & 1u ? ((iv >> 10) & 1u ? "7sus4" : "sus4") : (iv >> 2) & 1u ? "sus2" : "5";
    str_cpy(b, NN[GS.pcs[0] % 12u], 8);
    str_cpy(b + str_len(b), q, 8 - str_len(b));
}
