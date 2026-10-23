/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-17: the second review's I) A PLAYER: something that plays the layers on its own (a PLUGIN
 * component, firmware/components.txt): since 2026-10-23 the arpeggiator (arp.c; GEN archived 2026-10-18, TECHNO
 * 2026-10-23). What the rest of Blue Hare knows of one is this interface: the
 * audio side calls block every block and gives it the keys while it plays; the OS starts and stops it (ARP), loads its
 * pieces (the knob whose global id is piece_id), shows its two status lines and follows its roles' notes (GEN's ocean:
 * by the roles' names). A new generator is a file that fills a player_t, a PLUGIN line in the registry and its entry
 * in PLAYERS below, beside GEN or in its place. Included by seq.c after the players' own files */
typedef struct {
    const char *name;                         /* "GEN" */
    uint8_t nroles;                           /* the layers it plays, from track 1 */
    const char *const *roles;                 /* their names ("PAD", "BASS", "BELL", ...: the ocean follows them) */
    uint8_t piece_id;                         /* the global that picks its piece (G_GPRE), 0xFF none */
    uint32_t npieces;
    void (*block)(uint32_t n, uint32_t real); /* (the audio ISR) every block: a start or stop asked for, its steps (n:
                                               * the clock's samples, 0 with an external clock stopped; real: time's) */
    void (*request)(int start);               /* (the main loop) start or stop at the next block */
    int (*playing)(void);
    int (*key_on)(track_t *t, uint32_t note, uint32_t vel);   /* a key on t while it plays: 1 taken */
    int (*key_off)(track_t *t, uint32_t note);
    void (*load)(uint32_t piece);             /* its piece: the sounds (os_load_sound), the key, the tempo, the FX */
    const char *(*piece_name)(uint32_t piece);
    void (*status)(char *a, char *b);         /* two short lines (8 bytes each): GEN the chord, BAR n */
    uint32_t (*notes)(uint32_t role);         /* notes the role played since the start */
    uint32_t (*part)(void);                   /* (2026-10-18) which part of the music: changes with its sections (the
                                               * visuals' scene and colours follow it); 0 = none */
    uint32_t (*step)(void);                   /* the 1/16 of the bar, 0 .. 15 (the visuals' STEPS scene) */
} player_t;

/* ---- the arpeggiator (arp.c) as a player (2026-10-23, in TECHNO's place) */
static const player_t ARP_PLAYER = {
    "ARP", NPART, AR_ROLE_NAMES, 0xFFu, 0, ar_block, ar_request, ar_playing, ar_key_on, ar_key_off,
    ar_load, ar_piece_name, ar_status, ar_notes, ar_part, ar_step16,
};

/* the players this build has (the distribution's PLUGIN components); the first is ARP's */
static const player_t *const PLAYERS[] = {
#if BLUEHARE_PLUGIN_ARP
    &ARP_PLAYER,
#endif
    0,
};
static const player_t *player(void) { return PLAYERS[0]; }
static int player_playing(void) { return player() && player()->playing(); }
static int player_key_on(track_t *t, uint32_t note, uint32_t vel) { return player() && player()->key_on(t, note, vel); }
static int player_key_off(track_t *t, uint32_t note) { return player() && player()->key_off(t, note); }
/* the role of the active player named name: its index, -1 none */
static int32_t player_role(const char *name)
{
    uint32_t r;
    for (r = 0; player() && r < player()->nroles; r++)
        if (str_eq(player()->roles[r], name))
            return (int32_t)r;
    return -1;
}
