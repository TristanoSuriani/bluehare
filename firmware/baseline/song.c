/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* (Blue Hare, 2026-10-17: the second review's F, out of os/project.c) The song's format: the music as one byte stream,
 * written and read through a sink and a source the caller gives (pw_t, pr_t below): Blue Hare OS cuts it into flash
 * chunks (os/project.c: the store, the A/B copies, the autosave), a host keeps it as one piece (a state, a file). The
 * baseline alone writes and reads it (tests/baseline/baseline_alone.c: through RAM). Included by os/project.c, and by
 * whatever else saves a song.
 *
 * The project: what it holds: every track's sound (engine, preset, parameters), the globals, the selected track, the
 * name, and each track's place in the mix. The sequencer is a placeholder for now (SEQ shows TODO): no song is kept.
 * Projects of earlier versions (Felucca's, and Blue Hare's with the tracker's song, 2 .. 7) are not read.
 *
 * The project is one byte stream (proj_write / proj_read below), at most PROJ_CHUNKS x PROJ_DATA bytes, cut into
 * chunks of PROJ_DATA, each one storage object (OBJ_PROJECT0 + i, storage.c: A/B copies, CRC, the commit record last)
 * behind a 16-byte header: magic, the save's generation, its index, how many the save wrote, its length. A save writes
 * every chunk into the copy that does not hold the last complete save (proj_good), so a save cut short anywhere leaves
 * that one whole: on load the newest generation whose chunks are all there wins (proj_find). The stream keeps only a
 * track's parameters that differ from their defaults.
 *
 * The stream, version 9 (little endian):
 *   u8 version, u8 P_COUNT, u8 G_COUNT; G_COUNT x i16 the globals (SLOT NAME LOAD SAVE as their defaults); u8 the
 *   selected track and 12 bytes the name (neither in the hash below: a rename changes the name in place; the track
 *   selected is no change of the music the autosave has to keep); u8 the track count (4 or 8)
 *   per track: u8 engine id (core.h EID_*), u8 preset, u8 n, n x (u8 id, i16 value), u8 0 (Felucca's FM6 patch flag)
 *   tracks x u8 0 (WIDTH's, the Haas effect, removed 2026-10-16), tracks x u8 LOW CUT, tracks x u8 PAN MODE (core.h
 *   trk_lowcut, trk_pmode: 0 ALT, 1 RND)
 *   (version 10, the layers: core.h lay) u8 the play mode; tracks x u8 each of ON, KEY LO, KEY HI, VEL LO, VEL HI,
 *   TRANS (+ 64), FILTER, CUT, RES
 *   u32 FNV-1a of everything before it but the name
 * A project of more tracks than the build has is refused (SONG HAS 8 TRACKS), one of fewer fills its first tracks (the
 * rest as at power-on). */
#define PROJ_NAME_LEN 12u                      /* the name: upper-case ASCII 32..126, 0-padded; "" = none */
#define PROJ_VER 15u                           /* (Blue Hare) 15 (2026-10-18): the chorus's and EQ's presets (G_COUNT 38); 14 the reverb's MIX, the patches' delays; 13 the FX preset, WET; 12 (2026-10-17): GEN's densities (G_COUNT 35); 11: the EQ, the layers' crossfade; 8: no song (the sequencer is a placeholder); 9: no DUCK;
                                                  * 10: the layers (core.h lay) */

/* the stream's sink and source (2026-10-17): the format's hash and its fields here; where the bytes go, or come from,
 * the caller's (sink, src). A sink of 0 counts the bytes and hashes them only (the size, the autosave's signature) */
typedef struct pw_s {
    uint32_t n, h;                           /* bytes so far, their hash (FNV-1a: start it at 2166136261) */
    int err;
    void (*sink)(struct pw_s *w, const void *p, uint32_t n);
} pw_t;
typedef struct pr_s {
    uint32_t h;
    int err;
    void (*src)(struct pr_s *r, void *p, uint32_t n);   /* n bytes into p, or err set (p then zeros) */
} pr_t;

static uint32_t proj_fnv(uint32_t h, const void *p, uint32_t n)   /* FNV-1a, continued */
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h;
}
static void pw_put(pw_t *w, const void *p, uint32_t n)   /* (not in the stream's hash: the name, rename changes it) */
{
    w->n += n;
    if (w->sink && !w->err)
        w->sink(w, p, n);
}
static void pw_bytes(pw_t *w, const void *p, uint32_t n)
{
    w->h = proj_fnv(w->h, p, n);
    pw_put(w, p, n);
}
static void pw_u8(pw_t *w, uint32_t v) { uint8_t b = (uint8_t)v; pw_bytes(w, &b, 1); }
static void pw_i16(pw_t *w, int32_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)((uint32_t)v >> 8)}; pw_bytes(w, b, 2); }
static void pr_get(pr_t *r, void *p, uint32_t n, int hash)
{
    if (r->err)
        memset(p, 0, n);
    else
        r->src(r, p, n);
    if (hash && !r->err)
        r->h = proj_fnv(r->h, p, n);
}
static void pr_bytes(pr_t *r, void *p, uint32_t n) { pr_get(r, p, n, 1); }
static uint32_t pr_u8(pr_t *r) { uint8_t b; pr_bytes(r, &b, 1); return b; }
static int32_t pr_i16(pr_t *r) { uint8_t b[2]; pr_bytes(r, b, 2); return (int16_t)(b[0] | b[1] << 8); }

/* the music as it is now into the stream (main loop) */
static void song_write(pw_t *w, const char *name)
{
    uint32_t i, k, n;
    uint8_t nm[PROJ_NAME_LEN];
    pw_u8(w, PROJ_VER);
    pw_u8(w, P_COUNT);
    pw_u8(w, G_COUNT);
    for (i = 0; i < G_COUNT; i++)                      /* (the PROJECT page's own: as their defaults, not the music) */
        pw_i16(w, i == G_SLOT || i == G_NAME || i == G_LOAD || i == G_SAVE ? GP[i].def : song.g[i]);
    {
        uint8_t sel = song.sel;                         /* (the selected track: kept, not in the hash) */
        pw_put(w, &sel, 1);
    }
    memset(nm, 0, sizeof nm);
    memcpy(nm, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    pw_put(w, nm, sizeof nm);
    pw_u8(w, NTRK);
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        uint32_t e = eng_idx(t->eng_req);
        int16_t v[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            v[i] = t->p[i];
        pw_u8(w, eng_id(e));                            /* (the engine's id: a build of other engines reads it) */
        pw_u8(w, t->preset);
        for (i = n = 0; i < P_COUNT; i++)
            n += v[i] != param_desc_of(e, i)->def;
        pw_u8(w, n);
        for (i = 0; i < P_COUNT; i++)
            if (v[i] != param_desc_of(e, i)->def) {
                pw_u8(w, i);
                pw_i16(w, v[i]);
            }
        pw_u8(w, 0);                                    /* (Felucca's FM6 patch flag: no patch, FM6 is gone) */
    }
    {
        static const uint8_t zero[NTRK];
        pw_bytes(w, zero, NTRK);                       /* (WIDTH's: removed, 0), LOW CUT, PAN MODE */
    }
    pw_bytes(w, trk_lowcut, NTRK);
    pw_bytes(w, trk_pmode, NTRK);
    {                                                   /* the layers (version 10) */
        uint8_t tr[NTRK];
        uint32_t k;
        for (k = 0; k < NTRK; k++)
            tr[k] = (uint8_t)(lay.tr[k] + 64);
        pw_u8(w, lay.mode);
        pw_bytes(w, lay.on, NTRK);
        pw_bytes(w, lay.klo, NTRK);
        pw_bytes(w, lay.khi, NTRK);
        pw_bytes(w, lay.vlo, NTRK);
        pw_bytes(w, lay.vhi, NTRK);
        pw_bytes(w, tr, NTRK);
        pw_bytes(w, lay.ftype, NTRK);
        pw_bytes(w, lay.fcut, NTRK);
        pw_bytes(w, lay.fres, NTRK);
        pw_u8(w, lay.xsrc);                             /* (version 11: the crossfade; a place + 1, 0 OFF) */
        for (k = 0; k < NTRK; k++)
            tr[k] = (uint8_t)(lay.xpos[k] + 1);
        pw_bytes(w, tr, NTRK);
    }
    {
        uint32_t h = w->h;
        uint8_t b[4] = {(uint8_t)h, (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24)};
        pw_bytes(w, b, 4);
    }
}

/* the stream into the music: `apply` 0 checks it only (every field inside its range, the hash), 1 makes it the music
 * (main loop, the transport stopped and the audio ISR held off by the caller). 0 = good */
/* (an 8-track build) track k, which a project of fewer tracks does not hold: as at power-on, its engine and
 * preset with the engine's values */
static void proj_track_default(uint32_t k)
{
    track_t *t = &trk[k];
    uint32_t e = eng_for_track(t, TRK_DEF[k][0]), i;
    t->eng_req = (uint8_t)e;
    t->user = 0;
    t->preset = (uint8_t)(ENGINES[e]->npresets ? TRK_DEF[k][1] % ENGINES[e]->npresets : 0u);
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = param_desc_of(e, i)->def;
    for (i = 0; ENGINES[e]->npresets && i < 8u; i++)
        t->p[P_E0 + i] = ENGINES[e]->presets[t->preset].e[i];
    t->p[P_PAN] = TRK_PAN[k];                     /* (Blue Hare: as at power-on) */
}

/* 0 = good, 1 = bad, 2 = a song of more tracks than this build has (8 on a 4-track one) */
static int song_read(pr_t *r, int apply, char *name)
{
    uint32_t i, k, n, np, ng;
    uint8_t nm[PROJ_NAME_LEN], b[3], ntr;
    if (pr_u8(r) != PROJ_VER)
        return 1;
    np = pr_u8(r);
    ng = pr_u8(r);
    for (i = 0; i < ng; i++) {
        int32_t v = pr_i16(r);
        if (i == G_RTYPE && v > GP[i].max)
            v = 2;                                  /* (Blue Hare: the native image's reverbs, on the device: HALL) */
        if (apply && i < G_COUNT && i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)clamp(v, GP[i].min, GP[i].max);
    }
    pr_get(r, b, 1, 0);
    if (apply)
        song.sel = (uint8_t)(b[0] < NTRK ? b[0] : 0u);
    pr_get(r, nm, sizeof nm, 0);
    ntr = (uint8_t)pr_u8(r);                        /* the track count */
    if (ntr != 4u && ntr != 8u)
        return 1;
    if (ntr > NTRK)
        return 2;
    for (k = ntr; apply && k < NTRK; k++)           /* (a 4-track song on 8 tracks: 5 .. 8 as at power-on) */
        proj_track_default(k);
    for (k = 0; k < ntr; k++) {
        track_t *t = &trk[k];
        uint32_t e = pr_u8(r), pre = pr_u8(r);
        int sub;
        e = eng_load_id(e, &sub);                   /* the id -> this build's engine (one it lacks: its substitute) */
        if (e == ENG_NO)                            /* (a retired or unknown id: the project is refused) */
            return 1;
        if (apply) {
            e = eng_for_track(t, e);                /* (DUBSIREN off the last track: DUBSIREN8, the values as saved) */
            t->eng_req = (uint8_t)e;
            t->user = 0;
            t->preset = (uint8_t)(ENGINES[e]->npresets && !sub ? pre % ENGINES[e]->npresets : 0u);
            for (i = 0; i < P_COUNT; i++)
                t->p[i] = param_desc_of(e, i)->def;
        }
        n = pr_u8(r);
        for (i = 0; i < n; i++) {
            uint32_t id = pr_u8(r);
            int32_t v = pr_i16(r);
            if (id >= np)
                return 1;
            if (apply && id < P_COUNT)
                t->p[id] = (int16_t)param_fit(param_desc_of(e, id), param_oct(id, v));
        }
        if (apply && sub)                           /* a substitute: its first preset's engine values (the rest of the
                                                     * sound as saved) */
            for (i = 0; i < 8u; i++)
                t->p[P_E0 + i] = ENGINES[e]->npresets ? ENGINES[e]->presets[0].e[i] : ENGINES[e]->edit[i].def;
        if (pr_u8(r))                               /* a Felucca FM6 patch: an engine this firmware has none of */
            return 1;
    }
    {                                               /* (WIDTH's: read, not used), LOW CUT, PAN MODE */
        uint8_t wd[8], lc[8], pm[8];
        pr_bytes(r, wd, ntr);
        pr_bytes(r, lc, ntr);
        pr_bytes(r, pm, ntr);
        for (k = 0; k < ntr; k++)
            if (pm[k] > TP_RND)
                return 1;                           /* (a PAN MODE it does not know: refused) */
        if (apply)
            trk_mix_defaults();                     /* (a track the project does not hold: as at power-on) */
        for (k = 0; apply && k < ntr; k++) {        /* (the others out of range: OFF) */
            trk_lowcut[k] = (uint8_t)(lc[k] < TL_N ? lc[k] : 0u);
            trk_pmode[k] = pm[k];
        }
    }
    {                                               /* the layers (version 10): a value out of range refuses the song */
        uint8_t mode = (uint8_t)pr_u8(r), v[10][8], xsrc;
        uint32_t j;
        for (j = 0; j < 9u; j++)
            pr_bytes(r, v[j], ntr);
        xsrc = (uint8_t)pr_u8(r);
        pr_bytes(r, v[9], ntr);
        if (mode >= LM_N || xsrc >= LX_N)
            return 1;
        for (k = 0; k < ntr; k++)
            if (v[0][k] > 1u || v[1][k] > 127u || v[2][k] > 127u || v[3][k] > 127u || v[4][k] > 127u ||
                v[5][k] < 40u || v[5][k] > 88u || v[6][k] >= LF_N || v[7][k] > 127u || v[8][k] > 127u || v[9][k] > 128u)
                return 1;
        if (apply) {
            lay.mode = mode;
            lay.xsrc = xsrc;
            for (k = 0; k < ntr; k++) {
                lay.on[k] = v[0][k];
                lay.klo[k] = v[1][k];
                lay.khi[k] = v[2][k];
                lay.vlo[k] = v[3][k];
                lay.vhi[k] = v[4][k];
                lay.tr[k] = (int8_t)(v[5][k] - 64);
                lay.ftype[k] = v[6][k];
                lay.fcut[k] = v[7][k];
                lay.fres[k] = v[8][k];
                lay.xpos[k] = (int8_t)(v[9][k] - 1);
            }
        }
    }
    {
        uint32_t h = r->h;
        pr_bytes(r, b, 3);
        n = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16;
        n |= pr_u8(r) << 24;
        if (n != h || r->err)
            return 1;
    }
    if (apply && name) {                            /* (name: PROJ_NAME_LEN + 1 bytes) */
        memset(name, 0, PROJ_NAME_LEN + 1u);
        for (i = 0; i < PROJ_NAME_LEN && nm[i] >= 32u && nm[i] <= 126u; i++)
            name[i] = (char)nm[i];
    }
    return r->err;
}

