/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 32
 * slots in two storage.c objects (OBJ_UPRESET0/1, A/B sectors at
 * 0xDC000..0xDFFFF), 16 records each, mirrored in RAM so browsing never
 * reads flash. A record: engine, name, the instrument parameters, a 16-step
 * pattern (the old step sequencer's; 1.5 stores an empty one and never plays one, but keeps a stored one and
 * UP_GET / UP_PUT still carry it). An FM6 sound's patch is kept beside the record (up_fm6.c, since 1.0.3:
 * the record itself is unchanged). Loading one loads the sound only. The format is unchanged.
 *
 * Versions: a bank whose magic, record size or slot count differ reads as
 * empty; so does a record with another layout version. A record keeps np =
 * the P_COUNT it was stored with; when that differs from today's it is
 * mapped by count: its last 8 values are P_E0..P_E7, the first np - 8 are
 * P_LEVEL.. in order, and parameters it does not have take their defaults.
 * That holds as long as common parameters are only ever added just before
 * P_E0 (else bump UP_VER and translate). Version 1 (before 1.0) has the
 * same layout. (Felucca's PHYS and DRUM records are of retired engines now:
 * Fel-Tracker removed those, and such a record is refused like any retired id.)
 * Version 3 (UP_VER_GRID, the DRUM grid of 1.0.5 .. 1.4) is the same record with a drum grid as its pattern: note[i] is
 * step i's lane hits (bit l = lane l), flags[i] their accents. Older firmware shows a version 3 record as empty and
 * keeps its bytes. A note pattern's flags: 1 accent, 2 slide, 4 tie, 8 | 16 the step's RATCH (UP_PF_KEEP).
 * 1.2 (P_COUNT 103: LFO 2's SYNC TRIG POL, QUANTIZE): no version bump, records map by count as above (one of 99
 * gets the four at their defaults, which play as before); firmware before reads a record of 103 by count too (its
 * 91 parameters before P_E0 and the engine's 8: the four new ones are left out).
 * SPREAD (P_SPRD, #148, before 1.2's release) made it P_COUNT 104 the same way: a record of 103 gets SPREAD 0.
 * A record of a retired engine (DIGITAL, SAMPLE, GRAIN, SLICE: removed in 1.5, core.h EID_RETIRED) is not valid: the
 * slot shows as empty (UP_PUT refuses one) and its bytes stay in flash until the slot is written.
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
#define UP_PER_BANK 16u
#define UP_PMAX 72u                              /* room for P_COUNT to grow */
#define UP_USED 0xA5u
#define UP_VER 4u                                /* 2 since 1.0; 1 is read too (PHYS MODEL 2 was DUST) */
#define UP_VER_GRID 5u                           /* 2 with a drum grid as the pattern (see the top) */
#define UP_PF_KEEP (1u | 2u | 0x18u)             /* a note pattern's flags kept: accent, slide, RATCH (see the top) */
#define UP_BANK_MAGIC 0x31425055u                /* "UPB1" */
typedef struct {
    uint8_t used, ver, engine, np;               /* UP_USED, UP_VER, engine, P_COUNT when stored */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    union { int16_t p[UP_PMAX]; uint8_t packed[UP_PMAX * 2u]; };
    uint8_t note[16], flags[16];                 /* note 0 = rest; flags 1 accent, 2 slide, 4 tie, RATCH
                                                  * (UP_VER_GRID: lane hits, their accents) */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 192, "user preset record layout");
_Static_assert(P_COUNT <= UP_PMAX * 2u && P_COUNT < 128, "user preset record: P_COUNT");
static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK];

static up_rec_t *up_rec(uint32_t k) { return &up_bank[k / UP_PER_BANK].r[k % UP_PER_BANK]; }

/* 1.2 (Discussion #90): the sound's category (category.c CAT_*) in the record's last byte, packed[UP_CAT_AT]: a
 * version 4 / 5 record's values take np = P_COUNT (< 128) of its 144 bytes and nothing reads the others (up_valid
 * neither), so every record of before holds 0 there (CAT_NONE: listed by its engine, cat_of_engine) and firmware
 * before keeps the byte (a bank is written whole) without reading it: no format change, no version bump. A record of
 * versions 1..3 (a 16-bit value in every pair) has none; giving it one (a rename) rewrites it as version 4 / 5 when its
 * values fit a byte (up_to_v4; not a version 1 PHYS sound, whose MODEL 2 means DUST) */
#define UP_CAT_AT (UP_PMAX * 2u - 1u)
#include "category.c"
static uint32_t up_cat_of(const up_rec_t *r)
{
    uint32_t c = r->ver >= 4u && r->np <= UP_CAT_AT ? r->packed[UP_CAT_AT] : 0u;
    return c && c < CAT_N ? c : cat_of_engine(r->engine);
}
static int up_to_v4(up_rec_t *r)                 /* 1 = r is version 4 / 5 (now) */
{
    int16_t v[UP_PMAX];
    uint32_t i;
    if (r->ver >= 4u)
        return 1;
    for (i = 0; i < r->np; i++)
        if ((v[i] = r->p[i]) < -64 || v[i] > 127)
            return 0;
    memset(r->packed, 0, sizeof r->packed);
    r->ver = r->ver == 3u ? UP_VER_GRID : UP_VER;
    for (i = 0; i < r->np; i++)
        r->packed[i] = (uint8_t)(v[i] + 64);
    return 1;
}
static void up_set_cat(up_rec_t *r, uint32_t c)  /* c CAT_BASS..CAT_OTHER; a record that cannot hold one keeps none */
{
    if (c && c < CAT_N && up_to_v4(r))
        r->packed[UP_CAT_AT] = (uint8_t)c;
}

static int up_valid(const up_rec_t *r)
{
    if (!(r->used == UP_USED && r->ver >= 1u && r->ver <= UP_VER_GRID && r->engine < EID_COUNT && !EID_RETIRED(r->engine) &&
          r->np >= 8u && r->np <= (r->ver >= 4u ? UP_PMAX * 2u : UP_PMAX) && r->name[0])) return 0;
    if (r->ver >= 4u) for (uint32_t i = 0; i < r->np; i++) if (r->packed[i] > 191u) return 0;
    return 1;
}

static int up_used(uint32_t k) { return k < UP_SLOTS && up_valid(up_rec(k)); }

static int up_grid(const up_rec_t *r) { return r->ver == 3u || r->ver == UP_VER_GRID; }
static int16_t up_value(const up_rec_t *r, uint32_t k) { return r->ver >= 4u ? (int16_t)r->packed[k] - 64 : r->p[k]; }
static void up_set_value(up_rec_t *r, uint32_t k, int16_t v)
{
    if (r->ver >= 4u) r->packed[k] = (uint8_t)(v + 64); else r->p[k] = v;
}

static void up_pat_norm(uint8_t *note, uint8_t *flags);
/* after loading bank b (len bytes, -1 = none): wrong shape -> empty; each record's pattern inside its fields, as
 * UP_PUT and SAVE store it (flash or a restored backup may hold anything there) */
static void up_bank_check(uint32_t b, int len)
{
    up_bank_t *bk = &up_bank[b];
    uint32_t i, k;
    if (len != (int)sizeof *bk || bk->magic != UP_BANK_MAGIC || bk->rsize != sizeof(up_rec_t) ||
        bk->nslot != UP_PER_BANK)
        memset(bk, 0, sizeof *bk);
    for (i = 0; i < UP_PER_BANK; i++) {
        up_rec_t *r = &bk->r[i];
        if (!up_valid(r))
            continue;
        for (k = 0; k < 16u; k++)
            if (up_grid(r))
                r->flags[k] &= r->note[k];          /* a drum grid: accents only on its hits */
            else
                up_pat_norm(&r->note[k], &r->flags[k]);
    }
}

/* the record's values in today's P_* order (mapped by count, see above); def = the defaults */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    int16_t values[UP_PMAX * 2u];
    for (uint32_t i = 0; i < r->np && i < NELEM(values); i++) values[i] = up_value(r, i);
    params_by_count(out, values, r->np, def);
}

static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

static void up_name(uint32_t k, char *b)       /* upper case, 0-terminated: b holds 13 */
{
    const up_rec_t *r = up_rec(k);
    uint32_t i;
    for (i = 0; i < 12u && r->name[i]; i++)
        b[i] = r->name[i] >= 'a' && r->name[i] <= 'z' ? (char)(r->name[i] - 32) : r->name[i];
    b[i] = 0;
}

static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & UP_PF_KEEP) : 0u;
    }
}

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) [, kind, 16 x hi [, category]] -> *r
 * (values not yet clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when there is one. kind 1 (and its 16
 * bytes): a drum grid, the pairs the low 7 bits of each step's hits and accents, hi bit 0 / 1 their bit 7 (lane 8).
 * category (1.2, category.c CAT_*, 0 none): after the kind's 16 bytes (kind 0 sends them too, ignored) */
static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k, end;
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    end = k + 2u * P_COUNT + 32u;
    if (a[0] >= UP_SLOTS || a[1] >= EID_COUNT || EID_RETIRED(a[1]) || 2u + n >= na || !up_name_ok(a + 2, n) ||
        (na != end && ((na != end + 17u && na != end + 18u) || a[end] > 1u)) || (na == end + 18u && a[end + 17u] >= CAT_N))
        return 1;
    up_rec_t staged, *target = r;
    r = &staged;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->ver = UP_VER;
    r->engine = a[1];
    r->np = P_COUNT;
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    for (i = 0; i < P_COUNT; i++, k += 2u)
        {
            int32_t value = (int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192;
            if (value < -64 || value > 127) return 1;
            up_set_value(r, i, (int16_t)value);
        }
    if (na >= k + 33u + 16u && a[k + 32u] == 1u) {    /* a drum grid */
        r->ver = UP_VER_GRID;
        for (i = 0; i < 16u; i++) {
            uint32_t hi = a[k + 33u + i];
            r->note[i] = (uint8_t)((a[k + 2u * i] & 127u) | (hi & 1u) << 7);
            r->flags[i] = (uint8_t)(((a[k + 2u * i + 1u] & 127u) | (hi & 2u) << 6) & r->note[i]);
        }
        if (na == end + 18u)
            up_set_cat(r, a[end + 17u]);
        *target = *r;
        return 0;
    }
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
    if (na == end + 18u)
        up_set_cat(r, a[end + 17u]);
    *target = *r;
    return 0;
}

#ifndef UP_HOST
/* parameter i's descriptor for a record of engine id e (an engine the build lacks: ENG_NONE's, its values 0) */
static const param_desc_t *up_desc(uint32_t e, uint32_t i) { return i >= P_E0 ? &eng_by_id(e)->edit[i - P_E0] : &TP[i]; }
static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = up_desc(r->engine, i)->def;
    up_params(r, v, def);
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)param_fit(up_desc(r->engine, i), param_oct(i, v[i]));
}


static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if FELUCCA_FLASH
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        up_bank_check(b, flash_ok ? st_load(OBJ_UPRESET0 + b, &up_bank[b], sizeof up_bank[b]) : -1);
#endif
#ifdef FELUCCA_FAVORITES
    for (uint32_t k = 0; k < UP_SLOTS; k++)
        if (!up_used(k)) favorite_set(EID_COUNT, k, 0);
#endif
}

/* record k = *r (0: erase), then the bank to flash: 0 ok, 1 bad slot, 2 flash error or the transport runs (nothing
 * written), 3 no flash (kept in RAM) */
static int up_put(uint32_t k, const up_rec_t *r)
{
    up_bank_t *bk;
#if FELUCCA_FLASH
    up_rec_t old;
    uint32_t magic;
    uint16_t rsize, nslot;
#endif
    if (k >= UP_SLOTS)
        return 1;
    if (transport_busy()) {                            /* no flash erase while playing (project_save) */
        ui_message("STOP TO SAVE");
        return 2;
    }
    bk = &up_bank[k / UP_PER_BANK];
#if FELUCCA_FLASH
    old = *up_rec(k);
    magic = bk->magic;
    rsize = bk->rsize;
    nslot = bk->nslot;
#endif
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    if (r)
        *up_rec(k) = *r;
    else
        memset(up_rec(k), 0, sizeof(up_rec_t));
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_UPRESET0 + k / UP_PER_BANK, bk, sizeof *bk)) {
        *up_rec(k) = old;
        bk->magic = magic;
        bk->rsize = rsize;
        bk->nslot = nslot;
        return 2;
    }
#endif
    if (!r) {
#ifdef FELUCCA_FAVORITES
        if (favorite_set(EID_COUNT, k, 0)) settings_save();
#endif
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            if (trk[i].user == k + 1u)
                trk[i].user = 0;
    }
    up_gen++;
#if FELUCCA_FLASH
    if (flash_ok)
        return 0;
#endif
    return 3;
}

static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the automatic name of engine e's (an index; ENG_NO: one the build lacks) sound in slot k: engine name + slot number
 * ("ANALOG 07"); b holds 13 */
static void up_auto_name(char *b, uint32_t e, uint32_t k)
{
    char l[4];
    str_cpy(b, e < NENGINES ? ENGINES[e]->name : "USER", 9);
    up_slot_label(l, k);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), l + 1, 3);
}

/* the record's name = name (at most 12), 0 or "": the automatic one */
static void up_set_name(up_rec_t *r, uint32_t k, const char *name)
{
    char b[16];
    uint32_t i;
    if (!name || !name[0]) {
        up_auto_name(b, eng_of_id(r->engine), k);
        name = b;
    }
    memset(r->name, 0, sizeof r->name);
    for (i = 0; i < 12u && name[i]; i++)
        r->name[i] = name[i];
}

static uint32_t up_cat(uint32_t k) { return up_cat_of(up_rec(k % UP_SLOTS)); }   /* a used slot's category */
static uint32_t track_cat(const track_t *t);     /* (ui.c: the category of the sound a track plays) */

/* the selected part's sound -> slot k; name 0 or "": the automatic name (up_auto_name); cat its category (CAT_*), 0:
 * the one of the sound it came from (track_cat); up_put's result */
static int up_store_cat(uint32_t k, const char *name, uint32_t cat)
{
    up_rec_t r;
    uint32_t i;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.engine = (uint8_t)eng_id(TSEL->eng_req);     /* (a record names its engine by id) */
    r.np = P_COUNT;
    up_set_name(&r, k, name);
    for (i = 0; i < P_COUNT; i++)
        up_set_value(&r, i, TSEL->p[i]);
    up_set_cat(&r, cat ? cat : track_cat(TSEL));      /* (the pattern: empty) */
    return up_put(k, &r);
}

static int up_store(uint32_t k, const char *name) { return up_store_cat(k, name, 0); }

/* slot k renamed (name 0 or "": the automatic one) and given category cat (0: kept), the sound and its pattern as
 * they are; up_put's result, 1 for an empty slot */
static int up_rename_cat(uint32_t k, const char *name, uint32_t cat)
{
    up_rec_t r;
    if (!up_used(k))
        return 1;
    r = *up_rec(k);
    up_set_name(&r, k, name);
    up_set_cat(&r, cat);
    return up_put(k, &r);
}
static int up_rename(uint32_t k, const char *name) { return up_rename_cat(k, name, 0); }

/* the engine (index) a used slot's sound plays on; ENG_NO: an engine the build lacks (the slot is kept, listed with
 * no engine's sounds, loads nothing) */
static uint32_t up_engine(uint32_t k) { return eng_of_id(up_rec(k % UP_SLOTS)->engine); }

/* slot k -> the selected part's sound: engine and every parameter except the track's own (param_kept:
 * the mix, ARP, SCL, the old pattern parameters, the SLICER); the record's pattern is never loaded. 0 ok, 1 empty (or
 * a sound of an engine the build lacks: up_engine; the record is kept as it is) */
static int up_load(uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_used(k) || up_engine(k) == ENG_NO)
        return 1;
    r = up_rec(k);
    up_values(r, v);
    load_begin(t);                         /* (ui.c: the copy for SAVE held = undo) */
    panic_req |= (uint8_t)(1u << song.sel);
    {
        fm1_irq_off();                                  /* the audio ISR must not see half a sound */
        t->eng_req = (uint8_t)eng_for_track(t, up_engine(k));   /* (DUBSIREN off the last track: DUBSIREN8) */
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i))
                t->p[i] = v[i];
        t->preset = 0;
        fm1_irq_on();
    }
    t->user = (uint8_t)(k + 1u);
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}

/* a user preset as the tracker's instrument (tracker.c tk_user_sound, the audio ISR or the main loop with it held off):
 * its engine, its EDIT values, its voice mode, its FM6 patch into track t; the track's settings stay (tracker.c). A
 * slot emptied since changes nothing */
static void up_tk_sound(track_t *t, uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    if (!up_used(k) || up_engine(k) == ENG_NO)
        return;
    r = up_rec(k);
    up_values(r, v);
    t->eng_req = (uint8_t)eng_for_track(t, up_engine(k));   /* (DUBSIREN off the last track: DUBSIREN8) */
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = v[P_E0 + i];
    t->p[P_VOICE] = v[P_VOICE];
    t->p[P_OCT] = v[P_OCT];                             /* (the sound's octave goes with it) */
    t->preset = 0;
    t->user = (uint8_t)(k + 1u);
}



/* the PRESETS list's user presets: the used slots of an engine the build has */
static int up_avail(uint32_t k) { return up_used(k) && up_engine(k) != ENG_NO; }
static uint32_t up_count(void)                 /* those slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_avail(k);
    return n;
}

static uint32_t up_nth(uint32_t n)             /* slot of the n-th of them (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_avail(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_rank(uint32_t slot)         /* used slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar. name: the save's or the rename's (0 or "": automatic);
 * cat: its category (0: the sound's, a rename keeps it) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name, uint32_t cat)   /* 0 load, 1 erase, 2 save, 3 rename */
{
    char l[4];
    int rc;
    up_slot_label(l, k);
    if ((op < 2u || op == 3u) && !up_used(k)) {
        ui_message("EMPTY SLOT");
        return;
    }
    if (op && (song.playing || transport_req == 1u)) {    /* (up_put refuses too) */
        ui_message("STOP TO SAVE");
        return;
    }
    if (op == 0u) {
        up_load(k);
        ui_say("LOADED ", l);
        return;
    }
    rc = op == 1u ? up_put(k, 0) : op == 3u ? up_rename_cat(k, name, cat) : up_store_cat(k, name, cat);
    if (rc == 3)
        ui_message(op == 1u ? "ERASED (RAM)" : op == 3u ? "RENAMED (RAM)" : "SAVED (RAM)");
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : op == 3u ? "RENAMED " : "SAVED ", l);
    ui.force = 1;
}
static void up_ui(uint32_t op, uint32_t k) { up_ui_named(op, k, 0, 0); }
#endif
