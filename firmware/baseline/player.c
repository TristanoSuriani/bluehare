/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (Blue Hare, 2026-10-17: the second review's I) A PLAYER: something that plays the layers on its own (a PLUGIN
 * component, firmware/components.txt), TECHNO the first (GEN archived 2026-10-18). What the rest of Blue Hare knows of one is this interface: the
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
    void (*block)(uint32_t n);                /* (the audio ISR) every block: a start or stop asked for, its steps */
    void (*request)(int start);               /* (the main loop) start or stop at the next block */
    int (*playing)(void);
    int (*key_on)(uint32_t note, uint32_t vel);   /* a key while it plays: 1 taken (GEN: PLAY ALONG) */
    int (*key_off)(uint32_t note);
    void (*load)(uint32_t piece);             /* its piece: the sounds (os_load_sound), the key, the tempo, the FX */
    const char *(*piece_name)(uint32_t piece);
    void (*status)(char *a, char *b);         /* two short lines (8 bytes each): GEN the chord, BAR n */
    uint32_t (*notes)(uint32_t role);         /* notes the role played since the start */
} player_t;

/* ---- TECHNO (techno.c) as a player (2026-10-18, in GEN's place) */
static void tc_p_request(int start) { tc.req = start ? 1u : 2u; }
static int tc_p_playing(void) { return tc.on; }
static const char *tc_p_piece_name(uint32_t k) { return TC_PIECES[k % TC_NPIECES].name; }
static uint32_t tc_p_notes(uint32_t role) { return role < TC_ROLES ? tc.s.played[role] : 0u; }
static const player_t TECHNO_PLAYER = {
    "TECHNO", TC_ROLES, TC_ROLE_NAMES, G_GPRE, TC_NPIECES, tc_block, tc_p_request, tc_p_playing, tc_key_on, tc_key_off,
    tc_load_piece, tc_p_piece_name, tc_status, tc_p_notes,
};

/* the players this build has (the distribution's PLUGIN components); the first is ARP's */
static const player_t *const PLAYERS[] = {
#if BLUEHARE_PLUGIN_TECHNO
    &TECHNO_PLAYER,
#endif
    0,
};
static const player_t *player(void) { return PLAYERS[0]; }
static int player_playing(void) { return player() && player()->playing(); }
static int player_key_on(uint32_t note, uint32_t vel) { return player() && player()->key_on(note, vel); }
static int player_key_off(uint32_t note) { return player() && player()->key_off(note); }
/* the role of the active player named name: its index, -1 none */
static int32_t player_role(const char *name)
{
    uint32_t r;
    for (r = 0; player() && r < player()->nroles; r++)
        if (str_eq(player()->roles[r], name))
            return (int32_t)r;
    return -1;
}
