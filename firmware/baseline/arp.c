/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-23) ARP: the arpeggiator back (Felucca's, removed 2026-10-16 in af3a551), the player on the ARP button
 * (player.c) in TECHNO's place: the user's "get rid of the generative thing and restore the arpeggiator, while
 * keeping the visuals". ARP starts and stops it; while it plays, a key on a track whose MODE is not OFF goes into
 * that track's note list and the arp plays the list at RATE, over OCT octaves, each note GATE long; SWG swings
 * the off-beats, PROB thins the steps, HOLD latches the chord (a new chord replaces it), ORD NOTE sorts the list
 * low to high, PLAY keeps the order the keys were pressed in. Each track its own arp (its own settings, P_AMODE
 * .. P_AORDER: the ids of the kits' old lane levels, 25 .. 32): a latched bass on track 1 runs on while track 3 is
 * played. Its clock: the tempo, the MIDI clock's when it runs (stopped with the transport: its notes end in real
 * time). The modes (Felucca 1.2's): UP DN UPDN RND ORD REPEAT DNUP UP+8 CONV DIVG PINKY THUMB WALK CHORD.
 * For the visuals (ui_strobe.c) its roles are the tracks: 1 BASS, 2 DRUMS, 3 VOICE (their names steer the
 * scenes); the part changes with each new chord and every 16 bars; the step is the bar's 1/16 */
enum { AM_OFF, AM_UP, AM_DN, AM_UPDN, AM_RND, AM_ORD, AM_REPEAT, AM_DNUP, AM_UP8, AM_CONV, AM_DIVG, AM_PINKY,
       AM_THUMB, AM_WALK, AM_CHORD };
#define AR_MAXH 16u
typedef struct {
    uint8_t held[AR_MAXH];       /* the note list, in press order */
    uint8_t nheld;
    uint8_t phys;                /* keys physically held (HOLD: a new chord when it is 0) */
    uint8_t note;                /* the sounding arp note, 0 none */
    uint8_t ch[3];               /* CHORD: the other notes sounding with it */
    uint8_t walk;                /* WALK: its place in the list */
    uint32_t pos;                /* samples into the step */
    uint32_t idx;                /* the step */
    uint32_t off;                /* samples until the note's end */
    uint32_t played;             /* notes played since the start (the visuals) */
    int16_t holdp;               /* HOLD as last seen */
} arp_t;
static struct {
    arp_t t[NPART];
    uint8_t on, req;             /* playing; asked: 1 start, 2 stop */
    uint32_t chords;             /* new chords since the start (the visuals' part) */
    uint32_t spos, step, bar;    /* the bar's 1/16s: samples into one, which, bars since the start */
} ar;
static const char *const AR_ROLE_NAMES[8] = {"BASS", "DRUMS", "VOICE", "T4", "T5", "T6", "T7", "T8"};

static void ar_off(track_t *t, arp_t *a)               /* the sounding arp note(s) end */
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        if (a->ch[i]) {
            trk_note_off(t, a->ch[i]);
            a->ch[i] = 0;
        }
    if (a->note)
        trk_note_off(t, a->note);
    a->note = 0;
}

static void ar_add(track_t *t, arp_t *a, uint32_t note)
{
    uint32_t i;
    if (t->p[P_AHOLD] && !a->phys)
        a->nheld = 0;                                   /* HOLD: a new chord replaces the latched one */
    a->phys++;
    for (i = 0; i < a->nheld; i++)
        if (a->held[i] == note)
            return;
    if (a->nheld < AR_MAXH)
        a->held[a->nheld++] = (uint8_t)note;
    if (a->nheld == 1u) {                               /* the first note: a step at once */
        a->pos = 0xFFFFFFFu;
        a->idx = 0xFFFFFFFFu;
        ar.chords++;
    }
}

static int ar_remove(track_t *t, arp_t *a, uint32_t note)   /* -> 1 the note was the arp's */
{
    uint32_t i, k = 0, had = 0;
    for (i = 0; i < a->nheld; i++)
        had |= a->held[i] == note;
    if (!had)
        return 0;
    if (a->phys)
        a->phys--;
    if (t->p[P_AHOLD])
        return 1;
    for (i = 0; i < a->nheld; i++)
        if (a->held[i] != note)
            a->held[k++] = a->held[i];
    a->nheld = (uint8_t)k;
    return 1;
}

/* the place in the list (len notes) step a->idx plays; UP+8 adds its note */
static __attribute__((noinline)) uint32_t ar_pick(const track_t *t, arp_t *a, uint32_t *list, uint32_t len)
{
    uint32_t i = a->idx, m = (uint32_t)t->p[P_AMODE], cyc, k, w;
    if (m == AM_UP8 && len < 64u)
        list[len++] = (uint32_t)clamp((int32_t)list[0] + 12 * t->p[P_AOCT], 0, 127);
    cyc = len > 1u ? 2u * len - 2u : 1u;
    k = i % cyc;
    switch (m) {
    case AM_DN:
        return len - 1u - i % len;
    case AM_UPDN:
        return k < len ? k : cyc - k;
    case AM_DNUP:
        return k < len ? len - 1u - k : k - (len - 1u);
    case AM_RND:
        return rng() % len;
    case AM_CONV:
    case AM_DIVG:
        k = i % len;
        if (m == AM_DIVG)
            k = len - 1u - k;
        return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY:
        return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB:
        return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    case AM_WALK:
        w = a->walk;
        if (!i)
            w = 0;
        else if (w >= len)
            w = len - 1u;
        else if (len > 1u)
            w = !w ? 1u : w == len - 1u ? w - 1u : (rng() >> 7) & 1u ? w + 1u : w - 1u;
        a->walk = (uint8_t)w;
        return w;
    default:
        return i % len;
    }
}

/* CHORD: the first 4 notes together, an octave up each step through OCT */
static __attribute__((noinline)) void ar_chord(track_t *t, arp_t *a, const uint32_t *list, uint32_t n)
{
    uint32_t i, nt, top = 0, o = a->idx % (uint32_t)(t->p[P_AOCT] > 0 ? t->p[P_AOCT] : 1);
    for (i = 0; i < n && i < 4u; i++) {
        nt = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
        if (nt == 127u && top++)
            continue;
        if (!a->note)
            a->note = (uint8_t)nt;
        else
            a->ch[i - 1u] = (uint8_t)nt;
        trk_note_on(t, nt, 100);
    }
}

/* n: samples the arp's clock advances by; real: samples its gate counts down by */
static void ar_step(track_t *t, arp_t *a, uint32_t n, uint32_t real)
{
    uint32_t period, cnt, list[64], len = 0, i, j, o;
    int32_t sw;
    if (a->holdp && !t->p[P_AHOLD] && !a->phys)
        a->nheld = 0;                                   /* HOLD let go with no key down: the latched chord goes */
    a->holdp = t->p[P_AHOLD];
    if (a->note) {
        if (a->off <= real)
            ar_off(t, a);
        else
            a->off -= real;
    }
    if (!t->p[P_AMODE] || !a->nheld) {
        if (a->note)
            ar_off(t, a);
        return;
    }
    period = div_samples((uint32_t)t->p[P_ARATE]);
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    a->pos += n;
    if (a->pos < period + (uint32_t)((a->idx & 1u) ? sw : -sw) && a->pos != 0xFFFFFFFu + n)
        return;
    a->pos = 0;
    for (i = 0; i < a->nheld; i++)                      /* the list: the held notes (sorted, or as played), OCT octaves */
        list[i] = a->held[i];
    cnt = a->nheld;
    if (!t->p[P_AORDER] && t->p[P_AMODE] != AM_ORD)
        for (i = 1; i < cnt; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                uint32_t x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 1; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < cnt && len + cnt < 64u; i++)
            list[cnt + len++] = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    len += cnt;
    if (t->p[P_AMODE] == AM_REPEAT) {                   /* REPEAT: the last note pressed, no octaves */
        list[0] = a->held[a->nheld - 1u];
        len = 1;
    }
    a->idx++;
    j = t->p[P_AMODE] == AM_CHORD ? 0u : ar_pick(t, a, list, len);
    if (a->note)
        ar_off(t, a);
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        a->off = period * (uint32_t)t->p[P_AGATE] / 128u;
        a->played++;
        if (t->p[P_AMODE] == AM_CHORD) {
            ar_chord(t, a, list, cnt);
            return;
        }
        a->note = (uint8_t)list[j];
        trk_note_on(t, a->note, 100);
    }
}

static void ar_stop_all(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++) {
        ar_off(&trk[k], &ar.t[k]);
        ar.t[k].nheld = ar.t[k].phys = 0;
    }
}

/* ---- the player (player.c) */
static void ar_block(uint32_t n, uint32_t real)
{
    uint32_t k, q;
    if (ar.req) {
        if (ar.req == 1u && !ar.on) {
            memset(&ar.t, 0, sizeof ar.t);
            ar.chords = ar.step = ar.bar = 0;
            ar.spos = 0;
            ar.on = 1;
        } else if (ar.req == 2u && ar.on) {
            ar_stop_all();
            ar.on = 0;
        }
        ar.req = 0;
    }
    if (!ar.on)
        return;
    for (k = 0; k < NPART; k++)
        ar_step(&trk[k], &ar.t[k], n, real);
    q = div_samples(2);                                 /* (the bar's 1/16s: the visuals' STEPS) */
    ar.spos += n;
    while (q && ar.spos >= q) {
        ar.spos -= q;
        if (++ar.step >= 16u) {
            ar.step = 0;
            ar.bar++;
        }
    }
}
static void ar_request(int start) { ar.req = start ? 1u : 2u; }
static int ar_playing(void) { return ar.on; }
static int ar_key_on(track_t *t, uint32_t note, uint32_t vel)
{
    (void)vel;
    if (!ar.on || t < &trk[0] || t >= &trk[NPART] || !t->p[P_AMODE] || note > 127u)
        return 0;
    ar_add(t, &ar.t[t - trk], note);
    return 1;
}
static int ar_key_off(track_t *t, uint32_t note)
{
    if (!ar.on || t < &trk[0] || t >= &trk[NPART])
        return 0;
    return ar_remove(t, &ar.t[t - trk], note);
}
static void ar_load(uint32_t k) { (void)k; }
static const char *ar_piece_name(uint32_t k) { (void)k; return "ARP"; }
static void ar_status(char *a, char *b)                /* the selected track's MODE, the bar */
{
    str_cpy(a, N_AMODE[clamp(TSEL->p[P_AMODE], 0, AM_CHORD)], 8);
    str_cpy(b, "BAR ", 8);
    fmt_int(b + 4, (int32_t)(ar.bar % 1000u) + 1);
}
static uint32_t ar_notes(uint32_t role) { return role < NPART ? ar.t[role].played : 0u; }
static uint32_t ar_part(void) { return ar.chords + ar.bar / 16u; }
static uint32_t ar_step16(void) { return ar.step; }
