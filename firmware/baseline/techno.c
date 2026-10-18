/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-18) TECHNO: the player on ARP (player.c), in GEN's place: evolving techno on the first four
 * patches, the user's "evolving techno patterns instead of the ambient ones". Four roles: DRUMS (KIT's General MIDI
 * notes), BASS, STAB, LEAD. Its own clock, 1/16s at the tempo (the MIDI clock's when there is one: fx.c beat_samples).
 * An arrangement in phrases of 8 bars: INTRO (kick, hats), GROOVE (+ percussion, the bass), BUILD (+ the stabs,
 * brighter bar by bar, the hats thicker, a snare roll into the next), DROP (everything, the open hats, the lead),
 * BREAK (no kick, no bass: the stabs soft, the lead, a roll back in), round again; MOTION shortens the sections and
 * mutates the patterns more (0: the same groove for ever; 127: something moves every bar). The kick four on the
 * floor (a ghost now and then); the hats and the percussion euclidean, turning and thinning or filling; the bass an
 * offbeat or rolling line on the root, its fifth and octave, accented (velocity opens OBIE's and SID's filters: the line
 * moves); the stabs a chord of the scale on a turning euclid; the lead a motif of the scale's pentatonic, re-written a
 * note at a time each phrase. DENSITY (the GEN MIX page's four) scales each role (0: silent). A key while it plays
 * moves the key (the root to the key's note: a live transposition); its release nothing. Pieces: the sounds, the
 * tempo, the key and scale, MOTION and the reverb. The audio side only (block); the OS starts and stops it, loads the
 * pieces (os_load_sound) */
#define TC_ROLES 4u
#define TC_MAXN 6u
enum { TR_DRUMS, TR_BASS, TR_STAB, TR_LEAD };
enum { TS_INTRO, TS_GROOVE, TS_BUILD, TS_DROP, TS_BREAK, TS_N };
enum { TK_KICK = 36, TK_RIM = 37, TK_SNARE = 38, TK_CLAP = 39, TK_CH = 42, TK_TOM = 45, TK_OH = 46, TK_BELL = 56 };
static const char *const TC_ROLE_NAMES[TC_ROLES] = {"DRUMS", "BASS", "STAB", "LEAD"};
static const char *const N_TC_SECTION[TS_N] = {"INTRO", "GROOVE", "BUILD", "DROP", "BREAK"};
typedef struct {
    const char *name;
    uint8_t bpm, root, scale, motion;          /* ROOT 0 C .. 11 B; SCALE N_SCALE's (2 MIN, 3 DOR, 7 HARM, 8 PHRY) */
    uint8_t eng[TC_ROLES];                     /* the engines by id (EID_*) */
    const char *snd[TC_ROLES];                 /* their presets by name */
    uint8_t rtype, rmix;                       /* the reverb (G_RTYPE 0 DRY 1 SPRING 2 GATED 3 PLATE), its MIX */
} tc_piece_t;
static const tc_piece_t TC_PIECES[] = {
    {"WAREHOUSE", 128, 9, 2, 60, {EID_KIT, EID_OBIE, EID_OBIE, EID_VOSIM}, {"FM KIT", "POLY BASS", "FIFTHS STAB", "TALK LEAD"}, 3, 22},
    {"ACID", 132, 9, 8, 80, {EID_KIT, EID_SID, EID_OBIE, EID_WAVE}, {"TIGHT FM", "FILTER ACID", "BIG STAB", "RESO LEAD"}, 2, 18},
    {"RAVE", 140, 5, 2, 90, {EID_KIT, EID_HOOVER, EID_HOOVER, EID_VOSIM}, {"BOOM KIT", "REESE BASS", "RAVE STAB", "VOX LEAD"}, 3, 26},
    {"DUB", 122, 2, 2, 50, {EID_KIT, EID_WAVE, EID_OBIE, EID_DUBSIREN}, {"DUB KIT", "SUB BASS", "OB KEYS", "CLASSIC SIREN"}, 1, 40},
    {"INDUSTRIAL", 135, 4, 8, 75, {EID_KIT, EID_OBIE, EID_VOSIM, EID_HOOVER}, {"INDUSTRIAL", "RESO SWEEP", "GROWL", "MENTAL"}, 2, 30},
    {"MINIMAL", 126, 0, 3, 40, {EID_KIT, EID_SID, EID_MODAL, EID_OBIE}, {"CLICKS", "SID BASS", "STEEL BAR", "SQUARE LEAD"}, 3, 20},
    {"PEAK TIME", 134, 7, 7, 85, {EID_KIT, EID_OBIE, EID_OBIE, EID_OBIE}, {"HEAVY METAL", "POLY BASS", "OB BRASS", "SYNC LEAD"}, 3, 24},
    {"DEEP", 124, 10, 3, 55, {EID_KIT, EID_WAVE, EID_WAVE, EID_MODAL}, {"DEEP KIT", "SUB BASS", "PPG KEYS", "GLASS"}, 3, 34},
};
#define TC_NPIECES (sizeof TC_PIECES / sizeof TC_PIECES[0])
typedef char tc_names_fit[NELEM(N_TC_PIECE) == TC_NPIECES ? 1 : -1];   /* (params.c: the knob's names) */
static const uint8_t TC_SECT_BARS[TS_N] = {8, 8, 8, 16, 8};
static const uint8_t TC_NEXT[TS_N] = {TS_GROOVE, TS_BUILD, TS_DROP, TS_BREAK, TS_BUILD};

typedef struct {
    uint32_t rng, pos;                         /* the random generator; samples into the step */
    uint32_t step, bar, sbar;                  /* the step of the bar (0..15), bars played, bars into the section */
    uint8_t sect;                              /* the section */
    uint8_t nsteps, steps[12];                 /* the scale, semitones over ROOT */
    int32_t root;                              /* the key's root (a key played moves it) */
    uint16_t hat, perc, bass, stab;            /* the patterns, a bit a step */
    uint8_t hk, pk, prot, sk, srot, kghost;    /* their hits and turns */
    int8_t bnote[16];                          /* the bass line: semitones over the root (-1 rest) */
    int8_t lead[32];                           /* the lead's two bars: scale steps (-1 rest) */
    uint8_t held[TC_ROLES][TC_MAXN], nheld[TC_ROLES], left[TC_ROLES][TC_MAXN];
    uint32_t played[TC_ROLES];                 /* notes each role played since the start (STROBE, the tests) */
} tc_state_t;
static struct {
    volatile uint8_t req;                      /* main: 1 start, 2 stop (tc_block takes it) */
    volatile uint8_t on;
    uint32_t seed;                             /* 0 = from the clock at the start */
    tc_state_t s;
} tc;
#define TS (tc.s)

static uint32_t tc_rand(void)
{
    uint32_t s = TS.rng;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    TS.rng = s;
    return s;
}
static int32_t tc_pick(uint32_t n) { return n ? (int32_t)((tc_rand() >> 8) % n) : 0; }
static int tc_chance(int32_t permille) { return tc_pick(1000u) < permille; }
static const tc_piece_t *tc_piece(void) { return &TC_PIECES[(uint32_t)clamp(song.g[G_GPRE], 0, TC_NPIECES - 1)]; }
static int32_t tc_dens(uint32_t role) { return clamp(song.g[G_GDPAD + role], 0, 127) * 1000 / 64; }   /* x 1000 */
static int32_t tc_motion(void) { return clamp(song.g[G_GMOT], 0, 127); }

/* ---------------------------------------------------------------- notes --- */
static void tc_note_off(uint32_t r, uint32_t k)
{
    trk_note_off(&trk[r], TS.held[r][k]);
    TS.nheld[r]--;
    TS.held[r][k] = TS.held[r][TS.nheld[r]];
    TS.left[r][k] = TS.left[r][TS.nheld[r]];
}
static void tc_off(uint32_t r)
{
    while (TS.nheld[r])
        tc_note_off(r, TS.nheld[r] - 1u);
}
/* a note of role r, held len 1/16s; DENSITY thins it (a hit at 50 % density: half the time) */
static void tc_on(uint32_t r, int32_t note, int32_t vel, uint32_t len)
{
    uint32_t k;
    int32_t d = tc_dens(r);
    if (!d || (d < 1000 && !tc_chance(d)))
        return;
    note = clamp(note, 0, 127);
    for (k = 0; k < TS.nheld[r]; k++)
        if (TS.held[r][k] == (uint32_t)note) {
            tc_note_off(r, k);
            break;
        }
    if (TS.nheld[r] >= TC_MAXN)
        tc_note_off(r, 0);
    trk_note_on(&trk[r], (uint32_t)note, (uint32_t)clamp(vel, 1, 127));
    TS.held[r][TS.nheld[r]] = (uint8_t)note;
    TS.left[r][TS.nheld[r]] = (uint8_t)(len ? len : 1u);
    TS.nheld[r]++;
    TS.played[r]++;
}
static void tc_tick_notes(void)              /* a 1/16 gone: the notes whose length is up let go */
{
    uint32_t r, k;
    for (r = 0; r < TC_ROLES; r++)
        for (k = 0; k < TS.nheld[r];)
            if (--TS.left[r][k] == 0u)
                tc_note_off(r, k);
            else
                k++;
}

/* --------------------------------------------------------------- patterns --- */
static uint32_t tc_euclid(int32_t k, int32_t n, int32_t rot)   /* k hits spread over n steps, turned by rot */
{
    uint32_t m = 0;
    int32_t i;
    if (k <= 0 || n <= 0)
        return 0;
    for (i = 0; i < n; i++)
        if (((i * k) % n) < k)
            m |= 1u << (((i + rot) % n + n) % n);
    return m;
}
static void tc_scale(void)
{
    uint32_t i, s = (uint32_t)clamp(song.g[G_GSCL], 0, 15), mask = (s ? SCALE_MASK[s] : SCALE_MASK[2]) | 1u;
    TS.nsteps = 0;
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u)
            TS.steps[TS.nsteps++] = (uint8_t)i;
}
static int32_t tc_degree(int32_t d)           /* scale step d (any integer) as semitones over the root */
{
    int32_t n = TS.nsteps ? TS.nsteps : 7, o = d >= 0 ? d / n : -((-d + n - 1) / n);
    return o * 12 + TS.steps[(uint32_t)(d - o * n) % (uint32_t)n];
}
static void tc_bass_line(void)                /* a new line: offbeat or rolling, the root, its fifth, the octave */
{
    static const uint16_t SHAPES[4] = {0x4444u, 0xEEEEu, 0x6DB6u, 0x5555u};   /* offbeat 8ths, rolling, 3+3+2, 8ths */
    uint32_t i;
    TS.bass = SHAPES[tc_pick(4u)];
    for (i = 0; i < 16u; i++) {
        int32_t c = tc_pick(10u);
        TS.bnote[i] = (int8_t)((TS.bass >> i) & 1u ? (c < 6 ? 0 : c < 8 ? 7 : c < 9 ? 12 : tc_degree(2)) : -1);
    }
}
static void tc_lead_motif(void)               /* two bars of the pentatonic (scale steps 0 2 4 7 9 of the scale's) */
{
    static const int8_t PENT[5] = {0, 2, 4, 7, 9};
    uint32_t i;
    for (i = 0; i < 32u; i++)
        TS.lead[i] = (int8_t)(i % 2u == 0u && tc_chance(i % 8u == 0u ? 800 : 350) ? PENT[tc_pick(5u)] - 7 * tc_pick(2u) : -1);
}
static void tc_mutate(void)                   /* a bar: patterns turn, gain or lose a hit, a note changes (MOTION) */
{
    int32_t m = tc_motion();
    if (tc_chance(m * 4)) {
        TS.hk = (uint8_t)clamp(TS.hk + (tc_chance(500) ? 1 : -1), 3, 12);
        TS.hat = (uint16_t)tc_euclid(TS.hk, 16, 2);
    }
    if (tc_chance(m * 3)) {
        TS.prot = (uint8_t)((TS.prot + 1u) % 16u);
        TS.perc = (uint16_t)tc_euclid(TS.pk, 16, TS.prot);
    }
    if (tc_chance(m * 3)) {
        TS.srot = (uint8_t)((TS.srot + 3u) % 16u);
        TS.stab = (uint16_t)tc_euclid(TS.sk, 16, TS.srot);
    }
    if (tc_chance(m * 3)) {
        uint32_t i = (uint32_t)tc_pick(16u);
        if ((TS.bass >> i) & 1u)
            TS.bnote[i] = (int8_t)(tc_chance(500) ? 0 : tc_chance(500) ? 7 : 12);
    }
    if (tc_chance(m * 2)) {
        uint32_t i = (uint32_t)tc_pick(32u) & ~1u;
        TS.lead[i] = TS.lead[i] < 0 ? (int8_t)tc_degree(tc_pick(5u)) : -1;
    }
    TS.kghost = (uint8_t)(tc_chance(m * 2) ? 10u + (uint32_t)tc_pick(2u) * 4u : 0u);
}
static void tc_section_next(void)
{
    TS.sect = TC_NEXT[TS.sect];
    TS.sbar = 0;
    if (TS.sect == TS_BUILD)
        TS.sk = (uint8_t)(3u + (uint32_t)tc_pick(3u)), TS.stab = (uint16_t)tc_euclid(TS.sk, 16, TS.srot);
    if (TS.sect == TS_DROP && tc_chance(400 + tc_motion() * 4))
        tc_bass_line();
    if (TS.sect == TS_BREAK)
        tc_lead_motif();
}
static uint32_t tc_sect_len(void)             /* bars: MOTION 127 halves them */
{
    uint32_t b = TC_SECT_BARS[TS.sect];
    return b - b * (uint32_t)tc_motion() / 254u;
}

/* ------------------------------------------------------------------ steps --- */
static __attribute__((noinline)) void tc_step(void)   /* once a 1/16: out of the ISR's loops */
{
    uint32_t s = TS.step, sect = TS.sect, last = TS.sbar + 1u == tc_sect_len();
    int32_t root = TS.root, bright = (int32_t)(TS.sbar * 127u / (tc_sect_len() ? tc_sect_len() : 1u));
    tc_tick_notes();
    /* DRUMS */
    if (sect != TS_BREAK && (s % 4u == 0u || s == TS.kghost))
        tc_on(TR_DRUMS, TK_KICK, s % 4u == 0u ? 120 : 70, 1);
    if (sect >= TS_GROOVE && sect != TS_BREAK && (s == 4u || s == 12u))
        tc_on(TR_DRUMS, TK_CLAP, 105, 1);
    if ((TS.hat >> s) & 1u)
        tc_on(TR_DRUMS, TK_CH, s % 4u == 2u ? 90 : 55 + tc_pick(25u), 1);
    if (sect == TS_DROP && s % 4u == 2u)
        tc_on(TR_DRUMS, TK_OH, 85, 2);
    if (sect >= TS_GROOVE && sect != TS_INTRO && ((TS.perc >> s) & 1u))
        tc_on(TR_DRUMS, s % 3u ? TK_RIM : TK_BELL, 70 + tc_pick(30u), 1);
    if (last && (sect == TS_BUILD || sect == TS_BREAK) && s >= 8u)
        tc_on(TR_DRUMS, TK_SNARE, 60 + (int32_t)(s - 8u) * 8, 1);   /* the roll into the next */
    /* BASS: GROOVE, BUILD, DROP; accented */
    if ((sect == TS_GROOVE || sect == TS_BUILD || sect == TS_DROP) && TS.bnote[s] >= 0)
        tc_on(TR_BASS, 36 + root + TS.bnote[s], s % 4u == 2u ? 115 : 70 + tc_pick(40u), 1);
    /* STAB: BUILD (brighter bar by bar), DROP, BREAK (soft); a chord of the scale */
    if ((sect == TS_BUILD || sect == TS_DROP || sect == TS_BREAK) && ((TS.stab >> s) & 1u)) {
        int32_t v = sect == TS_BUILD ? 50 + bright * 70 / 127 : sect == TS_BREAK ? 60 : 105;
        tc_on(TR_STAB, 60 + root + tc_degree(0), v, 2);
        tc_on(TR_STAB, 60 + root + tc_degree(2), v, 2);
        tc_on(TR_STAB, 60 + root + tc_degree(4), v, 2);
    }
    /* LEAD: DROP and BREAK, the motif over two bars */
    if ((sect == TS_DROP || sect == TS_BREAK) && TS.lead[(TS.bar % 2u) * 16u + s] >= 0)
        tc_on(TR_LEAD, 72 + root + tc_degree(TS.lead[(TS.bar % 2u) * 16u + s]), 90 + tc_pick(30u), 2);
    if (++TS.step >= 16u) {                     /* the bar's end */
        TS.step = 0;
        TS.bar++;
        tc_mutate();
        if (++TS.sbar >= tc_sect_len())
            tc_section_next();
    }
}
static __attribute__((noinline)) void tc_stop_now(void)
{
    uint32_t r;
    for (r = 0; r < TC_ROLES && r < NTRK; r++)
        tc_off(r);
    tc.on = 0;
}
static __attribute__((noinline)) void tc_start(void)
{
    memset(&TS, 0, sizeof TS);
    TS.rng = tc.seed ? tc.seed : (fm1_ms * 2654435761u) ^ 0x9E3779B9u;
    if (!TS.rng)
        TS.rng = 1;
    tc_scale();
    TS.root = clamp(song.g[G_GROOT], 0, 11);
    TS.hk = 4;
    TS.hat = (uint16_t)tc_euclid(TS.hk, 16, 2);    /* (the offbeats first) */
    TS.pk = (uint8_t)(3u + (uint32_t)tc_pick(3u));
    TS.perc = (uint16_t)tc_euclid(TS.pk, 16, 1);
    TS.sk = 3;
    TS.stab = (uint16_t)tc_euclid(TS.sk, 16, 3);
    tc_bass_line();
    tc_lead_motif();
    TS.pos = 0x7FFFFFFFu;                       /* (the first step at once) */
    tc.on = NTRK >= TC_ROLES;
}
/* every block: a start or a stop asked for, then the steps due in these n samples */
static __attribute__((noinline)) void tc_block(uint32_t n)
{
    uint32_t len;
    if (tc.req == 1u) {
        tc.req = 0;
        if (tc.on)
            tc_stop_now();
        tc_start();
    } else if (tc.req == 2u) {
        tc.req = 0;
        if (tc.on)
            tc_stop_now();
    }
    if (!tc.on)
        return;
    if (TS.root != clamp(song.g[G_GROOT], 0, 11) || TS.nsteps == 0)   /* (ROOT or SCALE turned: at once) */
        TS.root = clamp(song.g[G_GROOT], 0, 11);
    len = beat_samples() / 4u;
    if (len < 64u)
        len = 64u;
    if (TS.pos == 0x7FFFFFFFu)
        TS.pos = len;
    else
        TS.pos += n;
    while (TS.pos >= len) {
        TS.pos -= len;
        tc_step();
    }
}
/* a key while it plays: the key's note the root (a live transposition); taken. Its release: nothing */
static int tc_key_on(uint32_t note, uint32_t vel)
{
    (void)vel;
    if (!tc.on || note > 127u)
        return 0;
    song.g[G_GROOT] = (int16_t)(note % 12u);
    return 1;
}
static int tc_key_off(uint32_t note) { return tc.on && note <= 127u; }

/* piece k: the four sounds (by name; a build without the engine: its substitute's first preset, os_load_sound: the
 * OS's undo and audio lock), the tempo, the key and scale, MOTION, the reverb */
static void os_load_sound(uint32_t track, uint32_t engine, uint32_t preset);
static void tc_load_piece(uint32_t k)
{
    const tc_piece_t *p = &TC_PIECES[k % TC_NPIECES];
    uint32_t r;
    for (r = 0; r < TC_ROLES && r < NTRK; r++) {
        int sub;
        uint32_t e = eng_load_id(p->eng[r], &sub);
        if (e == ENG_NO)
            continue;
        os_load_sound(r, e, sub ? 0u : patch_find_preset(e, p->snd[r]));
    }
    song.g[G_BPM] = p->bpm;
    song.g[G_GROOT] = p->root;
    song.g[G_GSCL] = p->scale;
    song.g[G_GMOT] = p->motion;
    song.g[G_RTYPE] = (int16_t)param_fit(&GP[G_RTYPE], p->rtype);
    song.g[G_RMIX] = p->rmix;
}
static void tc_status(char *a, char *b)       /* the section, the bar */
{
    str_cpy(a, N_TC_SECTION[TS.sect % TS_N], 8);
    str_cpy(b, "BAR ", 8);
    fmt_int(b + 4, (int32_t)(TS.bar % 1000u) + 1);
}
