/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* The project: what it holds: every track's sound (engine, preset, parameters), the globals, the selected track, the
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
#define PROJ_NO_SLOT 0xFFu
#if NTRK == 8
#define PROJ_CHUNKS 16u                        /* storage objects OBJ_PROJECT0 .. +3, OBJ_PROJECT4 .. +11 (proj_obj) */
#else
#define PROJ_CHUNKS 4u                         /* storage objects OBJ_PROJECT0 .. +3 */
#endif
#define PROJ_MAGIC 0x314B5254u                 /* "TRK1" */
#define PROJ_VER 12u                           /* (Blue Hare) 12 (2026-10-17): GEN's densities (G_COUNT 35); 11: the EQ, the layers' crossfade; 8: no song (the sequencer is a placeholder); 9: no DUCK;
                                                  * 10: the layers (core.h lay) */
#define PROJ_HDR 16u
#if !BLUEHARE_FLASH                             /* (RAM only: no storage.c; the song kept in RAM, proj_store_*) */
#define ST_PAYLOAD_MAX 3840u
static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));
#endif
#define PROJ_DATA (ST_PAYLOAD_MAX - PROJ_HDR)
typedef struct { uint32_t magic, gen; uint8_t index, count, rsv[2]; uint32_t len; } proj_hdr_t;
_Static_assert(sizeof(proj_hdr_t) == PROJ_HDR, "project chunk header");

static union {                               /* one chunk being written / read; a backup's staging (editor_backup.c) */
    uint32_t align;
    uint8_t raw[ST_PAYLOAD_MAX];
} proj_wire_u;
static uint8_t proj_wire_gen;                /* +1 whenever proj_wire_u is rewritten (a backup's copy lives there) */
static char proj_name[PROJ_NAME_LEN + 1u]    /* the name of the music as it is now (loaded, saved); "" = none. A save */
    __attribute__((section(".pool")));       /* takes it unless one is given */
static uint8_t proj_cur = PROJ_NO_SLOT;      /* 0: the music was loaded from or last saved to the song store */
static struct {
    uint32_t top;                            /* the newest generation in any chunk (a save writes one past it) */
    uint8_t have;                            /* a complete song is stored */
    uint8_t count;                           /* .. in this many chunks */
    uint8_t copy[PROJ_CHUNKS];               /* .. each in this copy (A 0, B 1) */
    uint32_t gen;                            /* .. of this save */
    char name[PROJ_NAME_LEN + 1u];           /* .. named so */
} proj_good;

static uint32_t proj_fnv(uint32_t h, const void *p, uint32_t n)   /* FNV-1a, continued */
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------- writing --- */
/* the stream's sink: `out` the chunk's bytes (proj_wire_u.raw after the header), flushed through `put` as each fills;
 * no `put`: only counted and hashed (the size check, the autosave's signature) */
typedef struct {
    uint32_t n, h, at;                       /* bytes so far, their hash, bytes in the chunk being filled */
    uint32_t chunk, gen, count;              /* the chunk being filled, the save's generation, its chunks */
    int (*put)(uint32_t chunk, uint32_t len);   /* store chunk `chunk` (len bytes after the header): 0 done */
    int err;
} pw_t;

static void pw_flush(pw_t *w)
{
    proj_hdr_t h;
    if (!w->put || w->err)
        return;
    h.magic = PROJ_MAGIC;
    h.gen = w->gen;
    h.index = (uint8_t)w->chunk;
    h.count = (uint8_t)w->count;
    h.rsv[0] = h.rsv[1] = 0;
    h.len = w->at;
    memcpy(proj_wire_u.raw, &h, sizeof h);
    if (w->put(w->chunk, w->at))
        w->err = 1;
    w->chunk++;
    w->at = 0;
}
static void pw_put(pw_t *w, const void *p, uint32_t n)   /* (not in the stream's hash: the name, rename changes it) */
{
    const uint8_t *b = (const uint8_t *)p;
    w->n += n;
    if (!w->put)
        return;
    while (n && !w->err) {
        uint32_t k = PROJ_DATA - w->at < n ? PROJ_DATA - w->at : n;
        memcpy(proj_wire_u.raw + PROJ_HDR + w->at, b, k);
        w->at += k;
        b += k;
        n -= k;
        if (w->at == PROJ_DATA)
            pw_flush(w);
    }
}
static void pw_bytes(pw_t *w, const void *p, uint32_t n)
{
    w->h = proj_fnv(w->h, p, n);
    pw_put(w, p, n);
}
static void pw_u8(pw_t *w, uint32_t v) { uint8_t b = (uint8_t)v; pw_bytes(w, &b, 1); }
static void pw_i16(pw_t *w, int32_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)((uint32_t)v >> 8)}; pw_bytes(w, b, 2); }

/* the music as it is now into the stream (main loop) */
static void proj_write(pw_t *w, const char *name)
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
    if (w->at)
        pw_flush(w);
}

/* ------------------------------------------------------------- reading --- */
/* the stream's source: the chunks of one save, read one at a time into st_buf (storage.c st_body) */
typedef struct {
    uint32_t chunk, count, at, len, h;
    const uint8_t *data;
    int (*get)(uint32_t chunk);              /* chunk `chunk` into st_buf: 0 done */
    int err;
} pr_t;

static int pr_next(pr_t *r)
{
    proj_hdr_t h;
    if (r->err || r->chunk + 1u >= r->count || r->get(r->chunk + 1u))
        return r->err = 1, 1;
    r->chunk++;
    memcpy(&h, st_buf, sizeof h);
    r->data = st_buf + PROJ_HDR;
    r->len = h.len;
    r->at = 0;
    return 0;
}
static void pr_get(pr_t *r, void *p, uint32_t n, int hash)
{
    uint8_t *b = (uint8_t *)p;
    while (n && !r->err) {
        uint32_t k;
        if (r->at == r->len && pr_next(r))
            break;
        k = r->len - r->at < n ? r->len - r->at : n;
        memcpy(b, r->data + r->at, k);
        if (hash)
            r->h = proj_fnv(r->h, b, k);
        r->at += k;
        b += k;
        n -= k;
    }
    if (r->err)
        memset(b, 0, n);
}
static void pr_bytes(pr_t *r, void *p, uint32_t n) { pr_get(r, p, n, 1); }
static uint32_t pr_u8(pr_t *r) { uint8_t b; pr_bytes(r, &b, 1); return b; }
static int32_t pr_i16(pr_t *r) { uint8_t b[2]; pr_bytes(r, b, 2); return (int16_t)(b[0] | b[1] << 8); }

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
static int proj_read(pr_t *r, int apply)
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
    if (apply) {
        memset(proj_name, 0, sizeof proj_name);
        for (i = 0; i < PROJ_NAME_LEN && nm[i] >= 32u && nm[i] <= 126u; i++)
            proj_name[i] = (char)nm[i];
    }
    return r->err;
}

/* ------------------------------------------------------- the song store --- */
/* chunk i's copy c (A 0, B 1) into st_buf: its length, -1 none. Without flash one copy each, in RAM */
#if BLUEHARE_FLASH
/* chunk i's storage object: 0 .. 3 OBJ_PROJECT0 + i, 4 .. 15 (8 tracks) OBJ_PROJECT4 + i - 4 (storage.c) */
static uint32_t proj_obj(uint32_t i) { return i < 4u ? OBJ_PROJECT0 + i : OBJ_PROJECT4 + i - 4u; }
static int proj_store_get(uint32_t i, uint32_t c)
{
    st_hdr_t h;
    if (i >= PROJ_CHUNKS || st_head(proj_obj(i), c, &h) || st_body(proj_obj(i), c, &h))
        return -1;
    return (int)h.len;
}
static int proj_store_put(uint32_t i, uint32_t c, const uint8_t *b, uint32_t len)
{
    return i >= PROJ_CHUNKS || !flash_ok || st_save_to(proj_obj(i), b, len, (int)c) != 0;
}
#else
static uint8_t proj_ram[PROJ_CHUNKS][ST_PAYLOAD_MAX];
static uint16_t proj_ram_len[PROJ_CHUNKS];
static int proj_store_get(uint32_t i, uint32_t c)
{
    if (i >= PROJ_CHUNKS || c || !proj_ram_len[i])
        return -1;
    memcpy(st_buf, proj_ram[i], proj_ram_len[i]);
    return proj_ram_len[i];
}
static int proj_store_put(uint32_t i, uint32_t c, const uint8_t *b, uint32_t len)
{
    (void)c;
    if (i >= PROJ_CHUNKS || len > ST_PAYLOAD_MAX)
        return 1;
    memcpy(proj_ram[i], b, len);
    proj_ram_len[i] = (uint16_t)len;
    return 0;
}
#endif
static uint8_t proj_rd_copy[PROJ_CHUNKS];    /* the copies proj_get reads */
static uint8_t proj_wr_copy[PROJ_CHUNKS];    /* the copies a save writes (not the complete save's) */
static int proj_get(uint32_t chunk)          /* chunk of the save proj_find chose into st_buf */
{
    return chunk >= PROJ_CHUNKS || proj_store_get(chunk, proj_rd_copy[chunk]) < 0;
}
static int proj_put(uint32_t chunk, uint32_t len)
{
    return chunk >= PROJ_CHUNKS || proj_store_put(chunk, proj_wr_copy[chunk], proj_wire_u.raw, PROJ_HDR + len);
}
/* chunk i of a save in copy c: its header (0 = a chunk of ours) */
static int proj_chunk_head(uint32_t i, uint32_t c, proj_hdr_t *ph)
{
    int n = proj_store_get(i, c);
    if (n < (int)PROJ_HDR)
        return 1;
    memcpy(ph, st_buf, sizeof *ph);
    return ph->magic != PROJ_MAGIC || ph->index != i || !ph->count || ph->count > PROJ_CHUNKS ||
           ph->len > (uint32_t)n - PROJ_HDR;
}

/* the newest complete save: its generation, chunks and copies, and its name (proj_good); 0 none */
static int proj_find(void)
{
    proj_hdr_t h[2], x;
    uint32_t c, i, best = 2;
    int ok[2];
    memset(&proj_good, 0, sizeof proj_good);
#if BLUEHARE_FLASH
    if (!flash_ok)
        return 0;
#endif
    for (i = 0; i < PROJ_CHUNKS; i++)                  /* (every save's generation, a cut short one's too: never again) */
        for (c = 0; c < 2u; c++)
            if (!proj_chunk_head(i, c, &x) && x.gen - proj_good.top < 0x80000000u)
                proj_good.top = x.gen;
    for (c = 0; c < 2u; c++)
        ok[c] = !proj_chunk_head(0, c, &h[c]);
    for (c = 0; c < 2u; c++) {                         /* each copy of chunk 0 names a save: is the rest there? */
        uint8_t cp[PROJ_CHUNKS];
        if (!ok[c] || (best < 2u && h[best].gen - h[c].gen < 0x80000000u))
            continue;                                   /* (older than a complete one found) */
        memset(cp, 0, sizeof cp);
        cp[0] = (uint8_t)c;
        for (i = 1; i < h[c].count; i++) {
            uint32_t d;
            for (d = 0; d < 2u; d++)
                if (!proj_chunk_head(i, d, &x) && x.gen == h[c].gen && x.count == h[c].count)
                    break;
            if (d == 2u)
                break;
            cp[i] = (uint8_t)d;
        }
        if (i < h[c].count)
            continue;
        best = c;
        proj_good.count = h[c].count;
        proj_good.gen = h[c].gen;
        memcpy(proj_good.copy, cp, sizeof cp);
    }
    if (best < 2u) {
        pr_t r;
        proj_good.have = 1;
        memcpy(proj_rd_copy, proj_good.copy, sizeof proj_rd_copy);
        memset(&r, 0, sizeof r);
        r.get = proj_get;
        r.count = proj_good.count;
        r.chunk = 0xFFFFFFFFu;
        if (!pr_next(&r)) {                             /* the name: after the version, counts, globals, selection */
            uint8_t nm[PROJ_NAME_LEN];
            uint32_t ng, j;
            (void)pr_u8(&r);
            (void)pr_u8(&r);
            ng = pr_u8(&r);
            for (j = 0; j < ng; j++)
                (void)pr_i16(&r);
            (void)pr_u8(&r);
            pr_bytes(&r, nm, sizeof nm);
            for (j = 0; j < PROJ_NAME_LEN && nm[j] >= 32u && nm[j] <= 126u; j++)
                proj_good.name[j] = (char)nm[j];
        }
    }
    return proj_good.have;
}

/* no song stored any more (a backup restored without one, editor_backup.c): chunk 0 of both copies gone; 0 done */
static int proj_store_clear(void)
{
#if BLUEHARE_FLASH
    uint32_t c;
    if (!flash_ok)
        return 1;
    for (c = 0; c < 2u; c++)
        if (st_erase(st_sector(OBJ_PROJECT0, c)))
            return 1;
#else
    memset(proj_ram_len, 0, sizeof proj_ram_len);
#endif
    proj_find();
    return 0;
}

/* the music as it is now -> the song store, named `name` (0: the current name; "" none); 0 saved, nonzero refused or
 * failed (the stored song as it was). Every slot is the song's: 1.4's four are gone */
static int project_save_as(uint32_t slot, const char *name)
{
    pw_t w;
    uint32_t i, n;
    (void)slot;
    if (transport_busy()) {                            /* a flash erase silences the audio and stalls the */
        ui_message("STOP TO SAVE");                     /* tracker (storage_hw.c): only while stopped */
        return 1;
    }
    if (!name)
        name = proj_name;
#if BLUEHARE_FLASH
    if (!flash_ok) {
        ui_message("SAVE ERROR");
        return 2;
    }
#endif
    memset(&w, 0, sizeof w);
    w.h = 2166136261u;
    proj_write(&w, name);                               /* (counted only: does it fit?) */
    if (w.n > PROJ_CHUNKS * PROJ_DATA) {
        ui_message("SONG TOO BIG");
        return 2;
    }
    n = (w.n + PROJ_DATA - 1u) / PROJ_DATA;             /* (the chunks: every header says how many) */
    proj_find();                                        /* (the complete save's copies are not written over) */
    for (i = 0; i < PROJ_CHUNKS; i++)
        proj_wr_copy[i] = (uint8_t)(proj_good.have && i < proj_good.count ? !proj_good.copy[i] : 0u);
    memset(&w, 0, sizeof w);
    w.h = 2166136261u;
    w.put = proj_put;
    w.gen = proj_good.top + 1u;                         /* (past every generation stored, complete or not) */
    w.count = n;
    proj_wire_gen++;
    proj_write(&w, name);
    if (w.err || !proj_find() || proj_good.gen != w.gen) {
        ui_message("SAVE ERROR");
        return 2;
    }
    str_cpy(proj_name, proj_good.name, sizeof proj_name);
    proj_cur = 0;
    ui_message(BLUEHARE_FLASH ? "SAVED" : "SAVED (RAM)");
    return 0;
}
static int project_save(uint32_t slot) { return project_save_as(slot, 0); }
static void project_cur_name(char *b) { str_cpy(b, proj_name, PROJ_NAME_LEN + 1u); }   /* b: 13 bytes */
static int project_used(uint32_t slot) { (void)slot; return proj_good.have; }   /* (every slot: the song) */

/* the stored song's name -> b (PROJ_NAME_LEN + 1 bytes); 0 = none stored (b "") */
static int project_name(uint32_t slot, char *b)
{
    b[0] = 0;
    if (!project_used(slot))
        return 0;
    str_cpy(b, proj_good.name, PROJ_NAME_LEN + 1u);
    return 1;
}

static void pr_open(pr_t *r, int (*get)(uint32_t), uint32_t count)
{
    memset(r, 0, sizeof *r);
    r->get = get;
    r->count = count;
    r->chunk = 0xFFFFFFFFu;
    r->h = 2166136261u;
}

/* the music becomes the stream of `count` chunks `get` reads (checked first, all of it: a damaged stream changes
 * nothing; then read again from its first chunk, the chunks share st_buf). 0 = loaded */
static int project_restore(int (*get)(uint32_t), uint32_t count)
{
    pr_t r;
    int bad;
    pr_open(&r, get, count);
    if (pr_next(&r))
        return 1;
    if ((bad = proj_read(&r, 0)) != 0)
        return bad;                               /* (2: more tracks than this build's) */
    mom_back(0);                                  /* (ui_input.c MOMENTARY: the values as they were, then the load) */
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                /* the audio ISR must not see half a song */
    seq_stop();
    transport_req = 0;
    pr_open(&r, get, count);
    bad = pr_next(&r) || proj_read(&r, 1);        /* (read and checked a moment ago: a flash that fails now) */
    if (bad)
        trk_mix_defaults();
    fm1_irq_on();
    undo.trk = 0;                                 /* (ui.c) the undo copy belongs to the old music */
    sync_reload = 1;
    ui.force = 1;
    return bad;
}

static void project_load(uint32_t slot)
{
    (void)slot;                                       /* (1.4's slots A..D: the one song) */
    if (!proj_find()) {
        ui_message("EMPTY SLOT");
        return;
    }
    proj_wire_gen++;
    switch (project_restore(proj_get, proj_good.count)) {
    case 0: break;
    case 2: ui_message("SONG HAS 8 TRACKS"); return;   /* (a song of an 8-track build: never cut to 4) */
    default: ui_message("LOAD ERROR"); return;
    }
    proj_cur = 0;
    ui_message("LOADED");
}

/* the stored song renamed (the music playing is not touched, but a song loaded from or saved to the store takes the
 * name too): every chunk of the complete save copied into a new save, its name changed. 0 done, 1 refused, 2 failed */
static int project_rename(uint32_t slot, const char *name)
{
    uint32_t i, ng, off, gen;
    proj_hdr_t h;
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    (void)slot;
    if (!proj_find()) {
        ui_message("EMPTY SLOT");
        return 1;
    }
    gen = proj_good.top + 1u;
    memcpy(proj_rd_copy, proj_good.copy, sizeof proj_rd_copy);
    proj_wire_gen++;
    for (i = 0; i < proj_good.count; i++) {
        if (proj_get(i))
            break;
        memcpy(proj_wire_u.raw, st_buf, ST_PAYLOAD_MAX);
        memcpy(&h, proj_wire_u.raw, sizeof h);
        h.gen = gen;
        memcpy(proj_wire_u.raw, &h, sizeof h);
        if (!i) {                                    /* the name: after the version, the counts, the globals, the track */
            ng = proj_wire_u.raw[PROJ_HDR + 2u];
            off = PROJ_HDR + 3u + 2u * ng + 1u;
            if (off + PROJ_NAME_LEN > PROJ_HDR + h.len)
                break;
            memset(proj_wire_u.raw + off, 0, PROJ_NAME_LEN);
            memcpy(proj_wire_u.raw + off, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
        }
        if (proj_store_put(i, !proj_good.copy[i], proj_wire_u.raw, PROJ_HDR + h.len))
            break;
    }
    if (i < proj_good.count || !proj_find() || proj_good.gen != gen) {
        ui_message("SAVE ERROR");
        return 2;
    }
    if (proj_cur == 0u)
        str_cpy(proj_name, proj_good.name, sizeof proj_name);
    ui_message(BLUEHARE_FLASH ? "RENAMED" : "RENAMED (RAM)");
    return 0;
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#include "settings_persist.c"
#if BLUEHARE_FLASH
static persist_t persist_saved;
static uint8_t persist_pending;                 /* 1 requested, 2 waiting after a flash error */
static uint32_t persist_retry_ms;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if BLUEHARE_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (settings_import(&p, n))
            persist_saved = p;
    }
    proj_find();                                   /* the stored song: its name for the PROJECT page */
    up_boot();                                     /* user presets */
#endif
}

static void settings_poll(void)
{
#if BLUEHARE_FLASH
    persist_t p;
    if (!persist_pending || !flash_ok || transport_busy() ||
        (persist_pending == 2u && (uint32_t)(fm1_ms - persist_retry_ms) < 1000u))
        return;
    p = persist_saved;
    settings_export(&p);
    if (!memcmp(&p, &persist_saved, sizeof p)) {
        persist_pending = 0;
        return;                                    /* unchanged: no erase cycle */
    }
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0) {
        persist_saved = p;
        persist_pending = 0;
    } else {
        persist_pending = 2;
        persist_retry_ms = fm1_ms;
    }
#endif
}

static void settings_save(void)
{
#if BLUEHARE_FLASH
    persist_pending = 1;
#endif
    settings_poll();
}

/* The autosave (1.2, Discussion #130): the music as it is (what the project holds) kept in its own storage object
 * (OBJ_AUTOSAVE, A/B at 0xE5000 / 0xE6000, never the song store), and put back at power-on (autosave_boot) with
 * MENU > SYSTEM > RESTORE LAST ON (the default). It holds the project's stream in as many chunks as the song store
 * (Blue Hare; 1.2 .. 1.5 one chunk): chunk 0 in OBJ_AUTOSAVE, 1 .. 15 in OBJ_AUTO1 .. (storage.c), every chunk of one
 * write carrying its generation. Chunks 1 .. go first, each into the copy that does not hold the complete autosave's;
 * chunk 0 last, through st_save (its copies take turns as before: fm6_vbank.c stages a bank in the one st_save writes
 * next): a write cut short leaves the complete one as it was. A song past the store's size cannot be (the song store
 * refuses it too): then a chunk of no length (count 0), power-on restores nothing and says so (SAVE THE SONG). Old
 * autosaves (one chunk, generation 0) still restore. Flash wear: a 4 KiB erase per write, the two sectors
 * taking turns, so a write only when
 *   - something the project holds changed since the last one (autosave_sig: the stream's hash, no copy; not the
 *     PROJECT page's own controls),
 *   - and it has stayed the same for AS_IDLE_MS (10 s) with the transport stopped, no key or button down, no voice
 *     sounding and no editor transfer (a backup or a sample: autosave_hold); an erase stops the audio for its
 *     ~40 ms, as every save does, so it never happens while anything plays,
 *   - and at least AS_GAP_MS (60 s) after the one before (AS_RETRY_MS after a failed write).
 * RESTORE LAST OFF writes none (and restores none). Never after a crash, a hang or a failed boot (main.c): the music
 * that was playing then could be what crashed. Checked every AS_POLL_MS; the main loop only (never the audio ISR) */
#define AS_POLL_MS 250u
#define AS_IDLE_MS 10000u
#define AS_GAP_MS 60000u
#define AS_RETRY_MS 30000u
static struct {
    uint32_t saved, seen;                        /* the signature written last (or loaded at power-on), seen last */
    uint32_t t, last, poll;                      /* fm1_ms: seen changed / busy, the last write, the last check */
    uint32_t writes;                             /* (written since power-on: the host tests count them) */
    uint8_t wrote, err;                          /* a write since power-on (last is valid); the last one failed */
} as __attribute__((section(".pool")));

/* what an autosave would hold, hashed (the stream counted, no copy) */
static uint32_t autosave_sig(void)
{
    pw_t w;
    uint32_t i, h;
    memset(&w, 0, sizeof w);
    w.h = 2166136261u;
    proj_write(&w, proj_name);                   /* (a row's FX may move a value while playing: no write then) */
    h = proj_fnv(w.h, proj_name, sizeof proj_name);   /* (the name: not in the stream's hash) */
    for (i = 0; i < NTRK; i++)
        h = proj_fnv(h, &trk[i].eng_req, 1);
    return proj_fnv(h, &w.n, sizeof w.n);
}

/* nothing going on that an erase (the audio stopped ~40 ms) would be heard in, or would race */
static int autosave_quiet(void)
{
    uint32_t k, i;
    if (transport_busy() || fm1_in.notes || fm1_in.buttons)
        return 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active)
                return 0;
    return 1;
}

static void autosave_hold(void) { as.t = fm1_ms; }   /* the editor's transfers: their staging RAM is proj_wire_u */

#if BLUEHARE_FLASH
static uint8_t as_copy[PROJ_CHUNKS];             /* chunk i's copy in the complete autosave (as_find) */
static uint8_t as_pass;                          /* as_put: 1 chunks 1 .., 0 chunk 0 (the commit) */
static uint32_t as_obj(uint32_t i) { return i ? OBJ_AUTO1 + i - 1u : OBJ_AUTOSAVE; }
/* chunk i (1 ..) of copy c into st_buf, checked against chunk 0's header h: 0 = it belongs */
static int as_chunk(uint32_t i, uint32_t c, const proj_hdr_t *h)
{
    st_hdr_t sh;
    proj_hdr_t x;
    if (i >= PROJ_CHUNKS || st_head(as_obj(i), c, &sh) || st_body(as_obj(i), c, &sh) || sh.len < PROJ_HDR)
        return 1;
    memcpy(&x, st_buf, sizeof x);
    return x.magic != PROJ_MAGIC || x.index != i || x.gen != h->gen || x.count != h->count || x.len > sh.len - PROJ_HDR;
}
/* the complete autosave: chunk 0's header (the current copy) into *h and every chunk's copy into as_copy; 0 = found */
static int as_find(proj_hdr_t *h)
{
    st_hdr_t sh;
    uint32_t i, c;
    memset(as_copy, 0, sizeof as_copy);
    if (st_current(OBJ_AUTOSAVE, &sh) < 0 || sh.len < PROJ_HDR)
        return 1;
    memcpy(h, st_buf, sizeof *h);
    if (h->magic != PROJ_MAGIC || h->index || h->count > PROJ_CHUNKS)
        return 1;
    for (i = 1; i < h->count; i++) {
        for (c = 0; c < 2u && as_chunk(i, c, h); c++)
            ;
        if (c == 2u)
            return 1;
        as_copy[i] = (uint8_t)c;
    }
    return 0;
}
static int as_put(uint32_t chunk, uint32_t len)  /* pass 1: chunks 1 .. into the other copies; pass 0: chunk 0 */
{
    if (chunk >= PROJ_CHUNKS || (as_pass ? !chunk : chunk != 0u))
        return 0;
    if (!chunk)
        return st_save(OBJ_AUTOSAVE, proj_wire_u.raw, PROJ_HDR + len) != 0;
    return st_save_to(as_obj(chunk), proj_wire_u.raw, PROJ_HDR + len, !as_copy[chunk]) != 0;
}
static int as_get(uint32_t chunk)                /* chunk of the complete autosave (as_find first) into st_buf */
{
    st_hdr_t h;
    if (!chunk)
        return st_current(OBJ_AUTOSAVE, &h) < 0 || h.len < PROJ_HDR;
    return chunk >= PROJ_CHUNKS || st_head(as_obj(chunk), as_copy[chunk], &h) || st_body(as_obj(chunk), as_copy[chunk], &h);
}
#endif

/* main loop, every pass (after settings_poll) */
/* (Blue Hare) the autosave written now, the music's signature sig (autosave_poll when the device is quiet; the plugin's
 * host, plugin/core: a state taken) */
static void autosave_write(uint32_t sig)
{
#if BLUEHARE_FLASH
    pw_t w;
    as.last = fm1_ms;
    as.wrote = 1;
    memset(&w, 0, sizeof w);
    w.h = 2166136261u;
    proj_write(&w, proj_name);                   /* (counted: how many chunks?) */
    proj_wire_gen++;
    if (w.n <= PROJ_CHUNKS * PROJ_DATA) {
        proj_hdr_t h;
        uint32_t n = (w.n + PROJ_DATA - 1u) / PROJ_DATA, gen;
        memset(&h, 0, sizeof h);
        if (as_find(&h) && h.magic != PROJ_MAGIC)
            h.gen = 0xFFFFFFFFu;                 /* (none at all: generation 0) */
        gen = h.gen + 1u;                        /* (past chunk 0's, the commit: chunks of an older write never match) */
        for (as_pass = 1; ; as_pass = 0) {       /* chunks 1 .. first, chunk 0 (the commit) last */
            memset(&w, 0, sizeof w);
            w.h = 2166136261u;
            w.put = as_put;
            w.count = n;
            w.gen = gen;
            proj_write(&w, proj_name);
            as.err = (uint8_t)w.err;
            if (as.err || !as_pass)
                break;
        }
    } else {                                     /* too big: a chunk of no length, nothing to restore */
        proj_hdr_t h = {PROJ_MAGIC, 0, 0, 0, {0, 0}, 0};
        memcpy(proj_wire_u.raw, &h, sizeof h);
        as.err = st_save(OBJ_AUTOSAVE, proj_wire_u.raw, PROJ_HDR) != 0;
    }
    if (!as.err) {
        as.saved = sig;
        as.writes++;
    }
#else
    (void)sig;
#endif
}

static void autosave_poll(void)
{
#if BLUEHARE_FLASH
    uint32_t sig;
    if (!flash_ok || fm1_ms - as.poll < AS_POLL_MS)
        return;
    as.poll = fm1_ms;
    sig = autosave_sig();
    if (sig != as.seen || !autosave_quiet()) {
        as.seen = sig;
        as.t = fm1_ms;
        return;
    }
    if (sig == as.saved || (ui_prefs & PREF_RESTORE_OFF) || fm1_ms - as.t < AS_IDLE_MS ||
        (as.wrote && fm1_ms - as.last < (as.err ? AS_RETRY_MS : AS_GAP_MS)))
        return;
    autosave_write(sig);
#endif
}

/* power-on, after the defaults (main.c): the autosave becomes the music when `allowed` (no crash, hang or failed boot
 * before) and RESTORE LAST is ON; an autosave that does not load (none, torn, damaged, another format) is ignored, one
 * of a song too big to keep says so. The music now (restored or not) is what is saved: nothing is written until it
 * changes. 1 = restored */
static int autosave_boot(int allowed)
{
    int rc = 0;
#if BLUEHARE_FLASH
    if (flash_ok && allowed && !(ui_prefs & PREF_RESTORE_OFF) && !as_get(0)) {
        proj_hdr_t h;
        memcpy(&h, st_buf, sizeof h);
        proj_wire_gen++;
        if (h.magic == PROJ_MAGIC && !h.count) {
            ui_message("SONG NOT RESTORED");
        } else if (h.magic == PROJ_MAGIC && h.count && !h.index && !as_find(&h)) {
            if (!project_restore(as_get, h.count)) {
                proj_cur = PROJ_NO_SLOT;
                ui_message("RESTORED");
                rc = 1;
            }
        }
    }
#else
    (void)allowed;
#endif
    as.saved = as.seen = autosave_sig();
    as.t = fm1_ms;
    return rc;
}

#if BLUEHARE_FLASH
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX, "settings do not fit one flash sector");
#endif
