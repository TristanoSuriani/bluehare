/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md */
/* The firmware editor handler, USB framing and UART recovery against RAM flash.
 * Build with the generated tables and the same flags as hostsim.c. */
static unsigned char host_flash[3][0x14000];   /* (flash 0xA0000..: what the editor's writes reach) */
#define BLUEHARE_OTA 1
#define BLUEHARE_FLASH 0
#define BLUEHARE_VERSION "TEST"
#define BLUEHARE_CDC 1                                    /* (as the firmware: MENU > USB SERIAL switches the console) */
#define main hostsim_main
#include "../lib/hostsim.c"
#undef main

static uint32_t host_progress = 1, host_erases, host_writes;
static uint8_t host_wire[4096];
static uint32_t host_wire_n;
static void host_drain(void)
{
    while (so_r != so_w) {
        uint32_t p = sx_out_q[so_r++ % SXQ], cin = p & 15u, i;
        uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n && host_wire_n < sizeof host_wire; i++)
            host_wire[host_wire_n++] = (uint8_t)(p >> (8u * (i + 1u)));
    }
}
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { host_drain(); fm1_ms++; }
static void fm1_wdt_feed(void)
{
    fm1_ms++;
    if (host_progress && transport_req == 2u) { seq_stop(); transport_req = 0; }
}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../../firmware/os/gfx.c"
#include "../../firmware/os/panel.c"
#include "../../firmware/os/ui.c"
#include "../../firmware/os/menu_items.c"
static void panel_setup(void) {}
#include "../../firmware/os/upreset.c"
#include "../../firmware/os/project.c"

static uint32_t flash_ok = 1;
static void audio_silence(void) {}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static uint8_t *host_flash_ptr(uint32_t off) { return &host_flash[0][0] + off - 0xA0000u; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    memset(host_flash_ptr(off), 0xFF, 4096); *took = 0; host_erases++; return 0;
}
static int fl_write(uint32_t off, const void *p, uint32_t n)
{
    memcpy(host_flash_ptr(off), p, n); host_writes++; return 0;
}
static int st_read(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
#include "../../firmware/os/storage.c"
#include "../../firmware/os/editor.c"

static int check(const char *what, int ok)
{
    printf("editor: %-70s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}
static void reset(void)
{
    uint32_t t, i;
    memset(&song, 0, sizeof song); memset(trk, 0, sizeof trk);
    trk_mix_defaults();
    memset(&ed_w, 0, sizeof ed_w); memset(&ui, 0, sizeof ui);
    memset(&favorites, 0, sizeof favorites); memset(&settings, 0, sizeof settings); settings_init();
    memset(&proj_good, 0, sizeof proj_good); memset(up_bank, 0, sizeof up_bank);
    memset(&um, 0, sizeof um);
    host_progress = 1; host_erases = host_writes = host_wire_n = 0;
    transport_req = panic_req = 0; sx_ready = sx_collect = sx_busy = 0;
    so_r = so_w = mi_r = mi_w = 0; midi_in_overflow = 0; usb.config = 1;
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (t = 0; t < NTRK; t++) {
        track_defaults(&trk[t]); set_engine_of(&trk[t], 0); apply_preset_to(&trk[t], 0);
        trk[t].engine = trk[t].eng_req;
    }
}
static uint32_t request(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i;
    host_wire_n = 0; ota_frame_done();
    sysex_byte(0xF0); sysex_byte(ED_HDR0); sysex_byte(ED_HDR1); sysex_byte(ED_HDR2); sysex_byte((uint8_t)cmd);
    for (i = 0; i < n; i++) sysex_byte(a[i]);
    sysex_byte(0xF7); ed_service(); host_drain();
    return host_wire_n;
}
static uint32_t pack7(const uint8_t *p, uint32_t n, uint8_t *a)
{
    uint32_t o = 0, i;
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = o++;
        a[m] = 0;
        for (i = 0; i < k; i++, n--) { a[m] |= (*p >> 7) << i; a[o++] = *p++ & 127u; }
    }
    return o;
}

static int preferences(void)
{
    int bad = 0;
    uint8_t a[4] = {0, 7, 0, 0};
    reset();
    uint32_t m = request(ED_INFO, a, 0);               /* (the tags end the reply: no 58, the tracker's, any more) */
    bad += check("INFO explicitly tags display capabilities after SONG without changing command 33 (1.5: no step tags but "
                 "4D 01, kept where the editors look for the tags after it)",
        ED_SONG == 33 && ED_UI_STATE == 34 && ED_FAV_SET == 38 &&
        host_wire[m - 18] == 16 && host_wire[m - 17] == 0x55 &&   /* (CHAIN_ROWS: 16 as before, no A..D song) */
        host_wire[m - 16] == 1 && host_wire[m - 15] == 9 &&
        host_wire[m - 14] == 0x4d && host_wire[m - 13] == 1 && host_wire[m - 12] == 64u && host_wire[m - 11] == 1 &&
        host_wire[m - 10] == 0x42 && host_wire[m - 9] == 1 && host_wire[m - 8] == 3 &&
        host_wire[m - 7] == 0x53 && host_wire[m - 6] == 1 && host_wire[m - 5] == 3 &&   /* (no FM6 tags 46 50 56: FM6 gone) */
        host_wire[m - 4] == 0x4E && host_wire[m - 3] == 1 && host_wire[m - 2] == 20);   /* MENU settings: 20 items (MIDI IN
                                                                                    * gone, 2026-10-23) */
    request(ED_UI_SET, a, 2);
    bad += check("UI_SET updates the actual palette and reports RAM-only saving",
        host_wire[5] == 3 && settings.palette == 7 && T_BG == UI_PALETTES[7].bg);
    a[1] = NPALETTES; request(ED_UI_SET, a, 2);
    bad += check("out-of-range palette leaves the display unchanged", host_wire[5] == 1 && settings.palette == 7);
    a[0] = 1; a[1] = 1; request(ED_UI_SET, a, 2);
    bad += check("the retired font weight is not supported (rc 2), UI_STATE says 127",
        host_wire[5] == 2 && host_wire[8] == 9 && host_wire[10] == 127);
    a[0] = 2; request(ED_UI_SET, a, 2);
    bad += check("unsupported preference is reported without applying it", host_wire[5] == 2);
    a[0] = EID_VA; a[1] = 0; a[2] = 64; a[3] = 1;
    request(ED_FAV_SET, a, 4);
    bad += check("FAV_SET marks a factory sound (VA's first)", host_wire[5] == 3 && favorite_has(EID_VA, 0));
    request(ED_FAV_GET, a, 4);
    bad += check("FAV_GET reads the bounded favorite range", host_wire[5] == 0 && host_wire[10] == 1);
    a[0] = EID_COUNT; a[1] = 31; request(ED_FAV_SET, a, 4);
    bad += check("empty user slot cannot be starred", host_wire[5] == 1 && !favorite_has(EID_COUNT, 31));
    up_store(31, "Saved"); request(ED_FAV_SET, a, 4);
    bad += check("saved user slot can be starred without changing its sound", host_wire[5] == 3 && favorite_has(EID_COUNT, 31));
    a[1] = (uint8_t)(UP_SLOTS - 1u); a[3] = 2; request(ED_FAV_GET, a, 4);   /* (U80 and one past it) */
    bad += check("favorite range cannot cross the end of user slots", host_wire[5] == 1);
    request(ED_FAV_SET, a, 3);
    bad += check("short favorite writes return an error without reading absent bytes", host_wire[5] == 1);
    bad += check("UI_STATE rejects unexpected request bytes", request(ED_UI_STATE, a, 1) == 0);
    a[0] = 0; request(ED_SONG, a, 1);
    bad += check("SONG still responds through its original command", host_wire_n > 6 && host_wire[4] == 33 && host_wire[5] == 0);
    return bad;
}

static int framing(void)
{
    uint32_t i, bad = 0;
    static const uint8_t rt[] = {0xF0, 0x7D, 0xF8, 0x46, 0xFE, 0x4C, ED_PING, 0xFF, 0xF7};
    static const uint8_t aborted[] = {0xF0, 0x7D, 0x46, 0x4C, ED_SET, 0, P_LEVEL, 0x90, 0, 64, 0xF7};
    reset();
    for (i = 0; i < sizeof rt; i++) sysex_byte(rt[i]);
    ed_service(); host_drain();
    bad += check("realtime bytes interleaved in SysEx do not enter the editor frame",
                 host_wire_n == 7u && host_wire[4] == ED_PING && host_wire[5] == 0);
    ota_frame_done(); host_wire_n = 0;
    for (i = 0; i < sizeof aborted; i++) sysex_byte(aborted[i]);
    ed_service(); host_drain();
    bad += check("a non-realtime status aborts SysEx without changing parameters",
                 !sx_ready && !host_wire_n && TSEL->p[P_LEVEL] == TP[P_LEVEL].def);
    bad += check("a valid request works after an aborted frame", request(ED_PING, 0, 0) == 7u);
    ota_frame_done();
    midi_in_event(0x467DF004u);                          /* F0 7D 46 */
    midi_in_event(0x00004C04u);                          /* unfinished SysEx */
    midi_in_event(0x643C9009u);
    sysex_byte(0xF7);
    bad += check("a channel event in another USB packet aborts an unfinished SysEx",
                 !sx_ready && mi_w == 1u && midi_in_q[0] == 0x643C9009u);
    midi_in_event(0x643C8009u);                          /* wrong CIN */
    midi_in_event(0x643CFF09u);                          /* wrong status */
    midi_in_event(0xFF3C9009u);                          /* not a seven-bit velocity */
    midi_in_event(0x64FF9009u);                          /* not a seven-bit note */
    bad += check("USB channel packets reject mismatched CIN and high data bits", mi_w == 1u);
    midi_in_event(0xFF05C00Cu);                          /* one-byte status: padding is ignored */
    midi_in_event(0xFF07D00Du);
    bad += check("USB program and channel pressure keep their one-byte payload", mi_w == 3u);
    mi_w = UINT32_MAX - 5u; mi_r = mi_w;
    for (i = 0; i < MQ + 8u; i++) midi_in_event(0x643C9009u);
    bad += check("USB MIDI ring stops at capacity across counter wrap", mi_w - mi_r == MQ && midi_in_overflow);
    mi_r = mi_w; midi_in_event(0x643C9009u);
    bad += check("USB rejects new messages until the audio consumer clears overflow", mi_w == mi_r);
    midi_in_overflow = 0; midi_in_event(0x643C9009u);
    bad += check("USB resumes after the audio consumer clears overflow", mi_w - mi_r == 1u);
    return bad;
}

static int uart_recovery(void)
{
    uint32_t i;
    int bad = 0;
    reset();
    um_byte(0x90); um_byte(50);
    for (i = 0; i < UM_RING; i++) um_ring[i] = 0;
    um_ring[126] = 0x90; um_ring[127] = 72; um_ring[0] = 99;
    uart_midi_take(UM_RING + 1u);
    bad += check("UART DMA overrun never combines a stale partial note with new data",
                 !mi_w && midi_in_overflow && um.drops == 2u && um.bytes == UM_RING && !um.pend);
    midi_in_overflow = 0; um_byte(0x90); um_byte(72); um_byte(99);
    bad += check("UART receives a complete fresh note after DMA recovery",
                 mi_w == 1u && midi_in_q[0] == 0x63489009u);
    reset();
    um_byte(0x90); um_byte(50);
    for (i = 0; i < UM_RING; i++) um_ring[i] = 60;
    uart_midi_take(UM_RING + 2u);
    bad += check("UART discards running-status data after lost bytes until a new status",
                 !mi_w && midi_in_overflow && um.drops == 2u && !um.st && !um.got);
    midi_in_overflow = 0;
    um_byte(0x91); um_byte(70); um_byte(100);
    bad += check("UART recovers immediately on a complete channel message", mi_w == 1u && midi_in_q[0] == 0x64469109u);
    reset(); mi_w = MQ;
    um_byte(0x90); um_byte(60); um_byte(100);
    bad += check("UART queue overflow latches the same recovery flag as USB", midi_in_overflow && um.drops == 1u && mi_w == MQ);
    mi_r = mi_w; um_byte(61); um_byte(101);
    bad += check("UART rejects a new stream until audio clears overflow", mi_w == mi_r && um.drops == 2u);
    midi_in_overflow = 0; um_byte(62); um_byte(102);
    bad += check("UART resumes valid running status after audio clears overflow",
                 mi_w - mi_r == 1u && midi_in_q[mi_r % MQ] == 0x663E9009u);
    return bad;
}

/* 1.5: the step sequencer's commands are gone: STEP_GET (6), STEP_SET (7), TRACK_STEP (30), MOTION (64) get no reply,
 * whatever they carry */
static int steps(void)
{
    static const uint8_t CMD[4] = {6, 7, 30, 64};
    uint8_t a[16] = {0, 1, 60, 0, 0, 0, 0, 1, 100, 0, 0, 0, 80, 1, 3, 0};
    uint32_t c, n, ok = 1;
    reset();
    for (c = 0; c < 4u; c++)
        for (n = 0; n <= sizeof a; n++)
            ok &= !request(CMD[c], a, n);
    return check("1.5: STEP_GET, STEP_SET, TRACK_STEP and MOTION get no reply (the steps and their motion are gone)", ok);
}

static int samples(void)
{
    uint8_t a[640] = {0};
    int bad = 0;
    reset();
    bad += check("no sample slots (removed in 1.5): SMP_INFO says 0 slots of 0 KiB",
                 request(ED_SMP_INFO, a, 0) == 8u && host_wire[5] == 0 && host_wire[6] == 0);
    bad += check("  SMP_BEGIN / WRITE / END: no reply; ERASE of USR1..3: done (rc 0, they are empty); no flash touched",
                 !request(ED_SMP_BEGIN, a, 1) && !request(ED_SMP_WRITE, a, 8) && !request(ED_SMP_END, a, 8) &&
                 request(ED_SMP_ERASE, a, 1) == 8u && host_wire[5] == 0 && host_wire[6] == 0 && (a[0] = 3, 1) &&
                 !request(ED_SMP_ERASE, a, 1) && !host_erases && !host_writes);
    song.playing = 1; host_progress = 0; a[0] = 0; a[1] = 0;
    bad += check("user-preset STORE does not mutate RAM when audio cannot stop",
                 request(ED_UP_STORE, a, 2) == 8u && host_wire[6] == 2 && !up_used(0));
    bad += check("user-preset ERASE reports failure when audio cannot stop",
                 request(ED_UP_ERASE, a, 1) == 8u && host_wire[6] == 2);
    return bad;
}

static int song_protocol(void)                    /* 1.5: the A..D song is gone (the tracker's): SONG answers empty */
{
    static const uint8_t set[] = {1, 1, 0, 2}, play[] = {2}, query[] = {0}, stop[] = {3}, lanes[] = {4};
    static const uint8_t info[] = {0};
    uint32_t n, i, tag = 0;
    int bad = 0;
    reset();
    n = request(ED_SONG, query, 1);
    bad += check("SONG query: an empty song (no section), nothing playing", n == 12u && !host_wire[6] && !host_wire[7] &&
                 !host_wire[8]);
    bad += check("SONG set: refused (rc 1), nothing changed", request(ED_SONG, set, sizeof set) == 12u && host_wire[6] == 1 &&
                 song.sel == 0);
    bad += check("SONG play: refused (rc 1), no transport", request(ED_SONG, play, 1) == 12u && host_wire[6] == 1 &&
                 !transport_req);
    bad += check("SONG op 4 (lanes): an empty song", request(ED_SONG, lanes, 1) == 12u && !host_wire[6] && !host_wire[7]);
    bad += check("SONG stop still stops the transport", request(ED_SONG, stop, 1) == 12u && transport_req == 2u);
    transport_req = 0;
    n = request(ED_INFO, info, 0);
    for (i = 5; i + 2u < n; i++)
        if (host_wire[i] == 0x57 && host_wire[i + 1u] == 1 && host_wire[i + 2u] == NTRK) tag = 1;
    bad += check("INFO says no song lanes (57 01 04) any more", !tag);
    return bad;
}

static uint32_t wire_frames(uint32_t cmd, const uint8_t **args);
static uint32_t sync_pass(void);
/* TRACKER (78) and its TRACKER_CHANGED push (79), the tracker's, are gone with it: no reply; WATCH bit 2 not taken */
static int tracker_protocol(void)
{
    uint8_t a[4] = {0, 0, 0, 0};
    uint32_t i, ok;
    int bad = 0;
    reset();
    bad += check("TRACKER (78): no reply (the tracker is gone)", !request(78, a, 1) && !request(78, a, 4));
    a[0] = 7;
    request(ED_WATCH, a, 1);
    ok = host_wire[5] == 3u;
    for (i = 0; i < 20u; i++) { sync_pass(); ok &= !wire_frames(79, 0); }
    bad += check("  WATCH 7: answered 3 (bit 2 not taken), no TRACKER_CHANGED", ok);
    return bad;
}

static int malformed_saves(void)
{
    static const uint8_t slot[] = {1, 4}, name[] = {0, 'X', 0, 'Y'};
    const uint8_t save[] = {1, 0};
    uint32_t gen;
    int bad = 0;
    reset();
    bad += check("PROJECT refuses an invalid slot instead of wrapping to slot 0",
                 !request(ED_PROJECT, slot, sizeof slot) && !project_used(0));
    bad += check("UP_STORE rejects bytes after its name terminator without saving",
                 request(ED_UP_STORE, name, sizeof name) == 8u && host_wire[6] == 1 && !up_used(0));
    bad += check("a successful PROJECT save confirms its slot", request(ED_PROJECT, save, sizeof save) == 9u && project_used(0));
    gen = proj_good.gen;
    TSEL->p[P_LEVEL] = 21; song.playing = 1; host_progress = 0;
    bad += check("PROJECT never confirms an old used slot when STOP timed out",
                 !request(ED_PROJECT, save, sizeof save) && proj_find() && proj_good.gen == gen);
    return bad;
}


/* UP_PUT -> UP_GET: every value round-trips through the v4 record (a byte each), negative ones too */
static int user_preset_roundtrip(void)
{
    static uint8_t a[16 + 2u * P_COUNT + 32u + 18u];
    int16_t want[P_COUNT], got[P_COUNT];
    uint32_t k = 0, i, p, ok, n;
    int bad = 0;
    reset();
    a[k++] = 3; a[k++] = EID_SID;                          /* slot U04, SID (ANALOG's id 0 retired) */
    memcpy(a + k, "ROUND", 5); k += 5; a[k++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = param_desc_of(ENGI_SID, i);
        int16_t v = d->def;
        uint32_t u;
        if (i == P_LEVEL) v = 90;
        if (i == P_PAN) v = -20;
        if (i == P_ED_FLT) v = -40;
        if (i == P_LD_FLT) v = d->min;
        if (i == P_E1) v = d->max;
        want[i] = v; u = (uint32_t)(v + 8192);
        a[k++] = u & 127u; a[k++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 16u; i++) { a[k++] = i & 1u ? 0u : (uint8_t)(48u + i); a[k++] = 0; }
    request(ED_UP_PUT, a, k);
    bad += check("UP_PUT stores a v4 record (no flash here: kept in RAM)",
                 host_wire[6] != 1u && up_used(3) && up_rec(3)->ver == UP_VER);
    up_values(up_rec(3), got);
    for (i = 0, ok = 1; i < P_COUNT; i++) ok &= got[i] == want[i];
    bad += check("UP_PUT keeps every value (PAN -20, FLT -40, min and max)", ok);
    a[0] = 3;
    request(ED_UP_GET, a, 1);
    ok = host_wire[5] == 3 && host_wire[6] == 1 && host_wire[7] == EID_SID && !memcmp(host_wire + 8, "ROUND", 6);
    for (i = 0, p = 14; i < P_COUNT; i++, p += 2u)
        ok &= (int32_t)(host_wire[p] | host_wire[p + 1] << 7) - 8192 == want[i];
    for (i = 0; i < 16u; i++, p += 2u)
        ok &= host_wire[p] == (i & 1u ? 0u : 48u + i);
    bad += check("UP_GET returns the values and the pattern UP_PUT sent", ok);
    bad += check("  .. then kind 0 and (1.2) the category: none sent, SID's (OTHER)",
                 host_wire[p] == 0u && host_wire[p + 1u] == CAT_OTHER && host_wire[p + 2u] == 0xF7);
    {   /* 1.2 (Discussion #90): UP_PUT's category after the kind's 16 bytes; UP_LIST lists the categories after the slots */
        uint32_t k0 = k;
        a[k++] = 0;
        for (i = 0; i < 16u; i++) a[k++] = 0;
        a[k++] = CAT_PAD;
        a[0] = 3;
        request(ED_UP_PUT, a, k);
        ok = host_wire[6] != 1u && up_cat(3) == CAT_PAD;
        a[k - 1u] = CAT_N;
        request(ED_UP_PUT, a, k);
        ok &= host_wire[6] == 1u && up_cat(3) == CAT_PAD;
        a[0] = 3;
        request(ED_UP_GET, a, 1);
        ok &= host_wire[p] == 0u && host_wire[p + 1u] == CAT_PAD;
        a[0] = 2; a[1] = 2;
        n = request(ED_UP_LIST, a, 2);
        ok &= host_wire[5] == 2u && host_wire[6] == 2u && host_wire[n - 3u] == 0u && host_wire[n - 2u] == CAT_PAD;
        bad += check("UP_PUT with a category (after the kind's 16 bytes; past the last: rc 1), UP_GET and UP_LIST give it", ok);
        k = k0;
    }
    return bad;
}

/* the frames of cmd in host_wire: how many, and the args of the last (into *args) */
static uint32_t wire_frames(uint32_t cmd, const uint8_t **args)
{
    uint32_t i, k = 0;
    for (i = 0; i + 5u < host_wire_n; i++)
        if (host_wire[i] == 0xF0 && host_wire[i + 1] == ED_HDR0 && host_wire[i + 4] == cmd) {
            k++;
            if (args) *args = host_wire + i + 5;
        }
    return k;
}
static uint32_t sync_pass(void)                          /* one main-loop pass of the pushes */
{
    host_wire_n = 0;
    fm1_ms += 30u;
    ed_sync();
    host_drain();
    return host_wire_n;
}

/* #65: WATCH while watching keeps what is not pushed yet; the editor's own sound load is not echoed as RELOAD */
static int live_sync(void)
{
    int bad = 0;
    uint8_t a[4] = {1, 0, 0, 0};
    uint32_t i, other = 0;
    const uint8_t *x = 0;
    reset();
    usb.resets = 0;
    request(ED_WATCH, a, 1);
    bad += check("WATCH 1 starts watching", host_wire[5] == 1 && ed_w.on && !sync_pass());
    TSEL->p[P_LEVEL] = 77;                               /* a device change, not pushed yet */
    request(ED_WATCH, a, 1);
    bad += check("WATCH 1 while watching keeps a pending change: CHANGED still goes out",
                 sync_pass() && wire_frames(ED_CHANGED, &x) == 1u && x[1] == P_LEVEL && ed_rv(x + 2) == 77);
    TSEL->p[P_LEVEL] = 66;
    a[0] = 0; request(ED_WATCH, a, 1);
    a[0] = 1; request(ED_WATCH, a, 1);
    bad += check("WATCH 0 then WATCH 1 starts from the values as they are (no push)", !sync_pass());
    TSEL->p[P_LEVEL] = 55; trk[1].p[P_PAN] = 3;
    a[0] = 3; request(ED_WATCH, a, 1);
    sync_pass();
    bad += check("WATCH 3 while watching: CHANGED kept, TRACK_CHANGED from the mix as it is",
                 host_wire_n && wire_frames(ED_CHANGED, 0) == 1u && !wire_frames(ED_TRACK_CHANGED, 0) && ed_w.v4);
    usb.resets++;
    TSEL->p[P_LEVEL] = 44;
    request(ED_WATCH, a, 1);
    bad += check("WATCH after a USB reset starts over", !sync_pass());

    /* PRESET */
    TSEL->p[P_LEVEL] = 33;                               /* pending; a sound load keeps P_LEVEL */
    a[0] = EID_SID; a[1] = 2;
    request(ED_PRESET, a, 2);
    sync_pass();
    bad += check("the editor's own PRESET: no RELOAD, no CHANGED for what it loaded, a pending CHANGED stays",
                 TSEL->preset == 2 && !wire_frames(ED_RELOAD, 0) && wire_frames(ED_CHANGED, &x) == 1u &&
                 x[1] == P_LEVEL && !sync_pass());
    for (i = 0; i < 3u; i++) {                           /* any preset of any engine */
        a[0] = (uint8_t)(i ? EID_VOSIM : EID_VA); a[1] = (uint8_t)i;
        request(ED_PRESET, a, 2);
        other += sync_pass() != 0;
    }
    bad += check("PRESET of other engines: no push at all", !other && ed_w.eng == ed_eng(TSEL));
    a[0] = 1; a[1] = G_ENGSEL; a[2] = (8192 + EID_VA) & 127; a[3] = (8192 + EID_VA) >> 7;
    request(ED_SET, a, 4);
    bad += check("SET of G_ENGSEL: no RELOAD", ed_eng(TSEL) == EID_VA && !sync_pass());
    sync_reload = 1;                                     /* a load on the device, not pushed yet */
    a[0] = EID_SID; a[1] = 1;
    request(ED_PRESET, a, 2);
    sync_pass();
    bad += check("a RELOAD due before the editor's PRESET still goes out", wire_frames(ED_RELOAD, 0) == 1u);
    a[0] = 0; request(ED_WATCH, a, 1);
    sync_reload = 0;
    a[0] = EID_SID; a[1] = 3;
    request(ED_PRESET, a, 2);
    a[0] = 1; request(ED_WATCH, a, 1);
    bad += check("not watching: PRESET as before, WATCH then starts from it", !sync_pass() && TSEL->preset == 3);
    return bad;
}

/* #64: SysEx through the USB packet path (ep1_take) at full speed: a 256-byte BACKUP_PUT piece (293 pack7 bytes, 101
 * event packets, 7 USB packets) arrives whole; one request at a time always works, a frame sent before the reply
 * to the one before is dropped whole (no reply; never a partial frame) */
static uint32_t usb_frame(const uint8_t *f, uint32_t n, uint8_t *usbp)   /* F0..F7 -> USB-MIDI event packets */
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t k = n - i >= 3u ? 3u : n - i, cin = k == 3u && i + 3u < n ? 4u : k == 3u ? 7u : 4u + k;
        usbp[o++] = (uint8_t)cin;
        usbp[o++] = f[i];
        usbp[o++] = k > 1u ? f[i + 1] : 0;
        usbp[o++] = k > 2u ? f[i + 2] : 0;
        i += k;
    }
    return o;
}
static uint32_t usb_feed(const uint8_t *p, uint32_t n)   /* whole 64-byte USB packets, as the host sends them */
{
    uint32_t o, refused = 0;
    for (o = 0; o < n; o += 64u)
        while (!ep1_take(p + o, n - o > 64u ? 64u : n - o))
            refused++;                                  /* NAK: the host sends it again */
    return refused;
}
static uint32_t put_frame(uint8_t *f, uint32_t op, uint32_t off, uint32_t count)
{
    static const uint8_t zero[256];
    uint32_t n = 0, i;
    f[n++] = 0xF0; f[n++] = ED_HDR0; f[n++] = ED_HDR1; f[n++] = ED_HDR2; f[n++] = ED_BACKUP_PUT;
    f[n++] = (uint8_t)op; f[n++] = 2;
    if (op == 0u) {
        for (i = 0; i < 5u; i++) f[n++] = (uint8_t)((ST_PAYLOAD_MAX >> (7u * i)) & (i == 4u ? 15u : 127u));
        for (i = 0; i < 5u; i++) f[n++] = 0;
    } else if (op == 1u) {
        for (i = 0; i < 5u; i++) f[n++] = (uint8_t)((off >> (7u * i)) & (i == 4u ? 15u : 127u));
        n += pack7(zero, count, f + n);
    }
    f[n++] = 0xF7;
    return n;
}
static int usb_burst(void)
{
    static uint8_t f[700], u[1000], u2[2000];
    uint32_t off, n, k, ok = 1, max = 0;
    const uint8_t *x = 0;
    int bad = 0;
    reset();
    usb.rx_pend = 0;
    n = put_frame(f, 0, 0, 0);
    usb_feed(u, usb_frame(f, n, u)); host_wire_n = 0; ed_service(); host_drain();
    bad += check("BACKUP_PUT begin through ep1_take", wire_frames(ED_BACKUP_PUT, &x) == 1u && !x[2]);
    for (off = 0; off < ST_PAYLOAD_MAX; off += 256u) {
        uint32_t c = ST_PAYLOAD_MAX - off > 256u ? 256u : ST_PAYLOAD_MAX - off;
        n = put_frame(f, 1, off, c);
        if (n > max) max = n;
        k = usb_frame(f, n, u);
        usb_feed(u, k);
        host_wire_n = 0; ed_service(); host_drain();
        ok &= wire_frames(ED_BACKUP_PUT, &x) == 1u && !x[2] && sx_ready == 0;
    }
    bad += check("256-byte pieces in 64-byte USB packets at full speed: every piece taken (rc 0)",
                 ok && ed_bk_pos == ST_PAYLOAD_MAX && max == 4u + 1u + 7u + 293u + 1u && max <= sizeof sx_frame);
    n = put_frame(f, 3, 0, 0);
    usb_feed(u, usb_frame(f, n, u)); host_wire_n = 0; ed_service(); host_drain();
    bad += check("abort through ep1_take", wire_frames(ED_BACKUP_PUT, &x) == 1u && !x[2] && !ed_bk_put);

    /* two frames back to back, before the reply: the second is dropped whole, the first answered intact */
    n = put_frame(f, 0, 0, 0);
    usb_feed(u, usb_frame(f, n, u)); host_wire_n = 0; ed_service(); host_drain();
    k = usb_frame(f, put_frame(f, 1, 0, 256), u2);
    k += usb_frame(f, put_frame(f, 1, 256, 256), u2 + k);
    usb_feed(u2, k);
    host_wire_n = 0; ed_service(); ed_service(); host_drain();
    bad += check("pipelined: the first piece is taken, the second (sent before its reply) dropped whole",
                 wire_frames(ED_BACKUP_PUT, &x) == 1u && !x[2] && ed_bk_pos == 256u && !sx_ready);
    n = put_frame(f, 1, 512, 256);
    usb_feed(u, usb_frame(f, n, u)); host_wire_n = 0; ed_service(); host_drain();
    bad += check("... so the next piece is refused (rc 1, offset), as a host that waits never sees",
                 wire_frames(ED_BACKUP_PUT, &x) == 1u && x[2] == 1u && ed_bk_pos == 256u);
    request(ED_BACKUP_PUT, (const uint8_t[]){3, 2}, 2);
    return bad;
}

/* ---- MENU_DESC / MENU_SET (72, 73): the menu's settings over the editor ---- */
typedef struct { uint32_t index, id, kind, nnames, tab, rest; int32_t value, min, max; char name[16], names[16][12], tabname[16], unit[16]; } menu_item_t;
static uint32_t menu_desc(uint32_t index, menu_item_t *it)   /* the reply's payload length; it parsed */
{
    uint8_t a[1] = {(uint8_t)index};
    uint32_t n = request(ED_MENU_DESC, a, 1), p = 14, k;
    memset(it, 0, sizeof *it);
    if (n < 8u || host_wire[4] != ED_MENU_DESC || host_wire[n - 1] != 0xF7) return 0;
    it->index = host_wire[5]; it->id = host_wire[6];
    if (n < 15u) return n - 6u;
    it->kind = host_wire[7];
    it->value = ed_rv(host_wire + 8); it->min = ed_rv(host_wire + 10); it->max = ed_rv(host_wire + 12);
    for (k = 0; p < n - 1u && host_wire[p] && k < 15u; ) it->name[k++] = (char)host_wire[p++];
    p++;
    if (it->kind == 1u) {                                   /* (a number: its unit, then the tab) */
        for (k = 0; p < n - 1u && host_wire[p] && k < 15u; ) it->unit[k++] = (char)host_wire[p++];
        p++;
    }
    while (it->kind == 0u && p < n - 1u && it->nnames < 16u && (int32_t)it->nnames < it->max - it->min + 1) {   /* (kind 0: max - min + 1) */
        for (k = 0; p < n - 1u && host_wire[p] && k < 11u; ) it->names[it->nnames][k++] = (char)host_wire[p++];
        p++; it->nnames++;
    }
    it->tab = 127;                                          /* 1.0.5: then the row's tab, index and name */
    if (p < n - 1u) {
        it->tab = host_wire[p++];
        for (k = 0; p < n - 1u && host_wire[p] && k < 15u; ) it->tabname[k++] = (char)host_wire[p++];
        p++;
    }
    it->rest = p < n - 1u ? n - 1u - p : 0u;                /* (bytes left over: none) */
    return n - 6u;
}
static uint32_t menu_set(uint32_t id, int32_t v)            /* -> rc; host_wire[6], [7..8]: id, value */
{
    uint8_t a[3] = {(uint8_t)id, (uint8_t)((v + 8192) & 127), (uint8_t)(((v + 8192) >> 7) & 127)};
    uint32_t n = request(ED_MENU_SET, a, 3);
    return n == 10u && host_wire[4] == ED_MENU_SET ? host_wire[5] : 99u;
}
static int menu_protocol(void)
{
    static const char *const WANT[20][2] = {
        {"COLOR", 0}, {"STYLE", "FLAT,LINE,LED"}, {"LARGE", "OFF,ON"}, {"ANIM", "ON,OFF,IDLE"}, {"LEDS", "OFF,DIM LO,DIM HI,INV"},
        {"HOLD", "0.3 s,0.4 s,0.5 s,0.6 s"}, {"KNOB ACCEL", "OFF,ON"}, {"FX LATCH", "OFF,ON"}, {"BPM LOCK", "OFF,ON"},
        {"SPEAKER EQ", "FLAT,LOWCUT,BASS+"}, {"USB LEVEL", "MASTER,FIXED"}, {"USB SERIAL", "ON,OFF"},
        {"CLICK", "OFF,ON"}, {"CLICK LEVEL", "LOW,MID,HIGH"},   /* (1.1: appended; COUNT-IN, id 14, gone in 1.5) */
        {"RESTORE LAST", "ON,OFF"}, {"SCALE LEDS", "OFF,ON"},                                        /* (1.2) */
        {"SCREEN OFF", "NEVER,5 MIN,15 MIN,30 MIN,60 MIN"},
        {"SCOPE", "OUT,MIX"},             /* (1.2; STEP PREVIEW, id 19, gone with the tracker; CHORD ENTRY, id 20, in 1.5) */
        {"TUNE", 0},                      /* (1.2: the project's TUNE, a number; MIDI IN, id 21, gone 2026-10-23) */
        {"HOME", "SCOPE,TRACKS"}};                                                                    /* (1.3) */
    static const int32_t DEF[20] = {-1, 0, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};   /* (COLOR: the default palette) */
    static const uint8_t TAB[20] = {0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 2, 2, 3, 1, 0, 0, 2, 0};   /* DISPLAY CONTROL AUDIO
                                                                                                     * SYSTEM (MIDI's gone) */
    static const char *const TABN[4] = {"DISPLAY", "CONTROL", "AUDIO", "SYSTEM"};
    int bad = 0, ok = 1;
    uint32_t i, k, n;
    menu_item_t it;
    char joined[96];
    const char *json = getenv("MENU_JSON");
    FILE *jf = json ? fopen(json, "w") : 0;
    reset();
    if (jf) fprintf(jf, "[");
    for (i = 0; i < 20u; i++) {
        uint32_t id = i < 14u ? i : i < 18u ? i + 1u : i + 4u;   /* (ids 14 COUNT-IN, 19 STEP PREVIEW, 20 CHORD ENTRY, 21
                                                                   * MIDI IN gone) */
        n = menu_desc(i, &it);
        joined[0] = 0;
        for (k = 0; k < it.nnames; k++) { if (k) strcat(joined, ","); strcat(joined, it.names[k]); }
        ok &= id == 22u ? n > 10u && it.index == i && it.id == id && it.kind == 1 && it.min == -50 && it.max == 50 &&
                         !it.nnames && !strcmp(it.unit, "ct") && !strcmp(it.name, "TUNE")     /* (a number: cents) */
                       : n > 10u && it.index == i && it.id == id && it.kind == 0 && it.min == 0 && !it.unit[0] &&
                         it.max + 1 == (int32_t)it.nnames && !strcmp(it.name, WANT[i][0]) &&
                         (WANT[i][1] ? !strcmp(joined, WANT[i][1]) : it.nnames == NPALETTES);
        ok &= it.value == (DEF[i] < 0 ? (int32_t)settings.palette : DEF[i]) &&
              it.tab == TAB[i] && !strcmp(it.tabname, TABN[TAB[i]]) && !it.rest;
        if (i == 0)
            for (k = 0; k < NPALETTES; k++) ok &= !strcmp(it.names[k], UI_PALETTES[k].name);
        if (!ok) { printf("  MENU_DESC %u: %s [%s] value %d max %d tab %u %s\n", i, it.name, joined, it.value, it.max, it.tab, it.tabname); break; }
        if (jf) {
            fprintf(jf, "%s\n {\"id\":%u,\"kind\":%u,\"min\":%d,\"max\":%d,\"value\":%d,\"name\":\"%s\",\"names\":[", i ? "," : "",
                    it.id, it.kind, it.min, it.max, it.value, it.name);
            for (k = 0; k < it.nnames; k++) fprintf(jf, "%s\"%s\"", k ? "," : "", it.names[k]);
            fprintf(jf, "],\"unit\":\"%s\",\"tab\":%u,\"tabName\":\"%s\"}", it.unit, it.tab, it.tabname);
        }
    }
    if (jf) { fprintf(jf, "]\n"); fclose(jf); }
    bad += check("MENU_DESC: 20 items (1.0.4's 12 in the menu's order, then 1.1's CLICK, CLICK LEVEL, 1.2's RESTORE LAST, SCALE LEDS, SCREEN OFF, 1.2's SCOPE, TUNE, 1.3's HOME), ids 0..23 without 14, 19, 20 and 21 (COUNT-IN, STEP PREVIEW, CHORD ENTRY, MIDI IN gone), names, defaults; TUNE a number -50..50 ct", ok);
    bad += check("MENU_DESC (1.0.5): after the names (TUNE: the unit) each item's tab, index and name (DISPLAY CONTROL AUDIO SYSTEM)", ok);
    {   /* an older editor reads the names and stops: the tab is past them, nothing it reads moved */
        uint32_t m = menu_desc(4, &it), p = 14, q;
        for (q = 0; q < 1u + it.nnames; q++) { while (host_wire[p]) p++; p++; }   /* name, the names */
        ok = m > 10u && host_wire[p] == 0 && !strcmp((const char *)host_wire + p + 1, "DISPLAY") &&
             p + 1u + 8u == m + 6u - 1u;
        bad += check("MENU_DESC: the tab comes after every byte of the 1.0.4 reply (older editors ignore it)", ok);
    }
    ok = 1;
    for (i = 0; i < 20u; i++) {
        menu_desc(i, &it);
        ok &= strcmp(it.name, "CALIBRATION") && strcmp(it.name, "ABOUT") && strcmp(it.name, "INFO");
    }
    n = menu_desc(20, &it);
    ok &= n == 2u && it.index == 20 && it.id == 127;
    n = menu_desc(127, &it);
    bad += check("MENU_DESC: no CALIBRATION / INFO / ABOUT; an index past the list answers index, 127 (no item)",
                 ok && n == 2u && it.index == 127 && it.id == 127);
    {
        uint8_t a[3] = {0, 0, 0};
        ok = request(ED_MENU_DESC, a, 0) == 0 && request(ED_MENU_DESC, a, 2) == 0 && request(ED_MENU_SET, a, 2) == 0 &&
             request(ED_MENU_SET, a, 1) == 0;
        bad += check("MENU_DESC / MENU_SET with the wrong length: no reply", ok);
    }

    /* MENU_SET: each setting, applied as the menu applies it; RAM only here (no flash: rc 3) */
    reset();
    ui.force = 0;
    ok = menu_set(0, 2) == 3 && host_wire[6] == 0 && ed_rv(host_wire + 7) == 2 && settings.palette == 2u &&
         T_BG == UI_PALETTES[2].bg && ui.force;
    ok &= menu_set(1, 1) == 3 && ui_style == ST_LINE;
    ok &= menu_set(2, 1) == 3 && (ui_prefs & PREF_LARGE);
    ok &= menu_set(3, 1) == 3 && (ui_prefs & PREF_ANIM_OFF);
    ok &= menu_set(4, 0) == 3 && settings_leds == LEDS_OFF && menu_set(4, 1) == 3 && settings_leds == LEDS_DIM_LO;
    ok &= menu_set(5, 3) == 3 && settings_hold == 3u;
    ok &= menu_set(6, 1) == 3 && (ui_prefs & PREF_ACCEL);
    ok &= menu_set(7, 1) == 3 && (ui_prefs & PREF_LATCH);
    ok &= menu_set(8, 1) == 3 && (ui_prefs & PREF_BPM_LOCK);
    ok &= menu_set(9, 2) == 3 && settings.lowcut == 2u && fx_lowcut == 2u;
    ok &= menu_set(10, 1) == 3 && (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;
    ok &= menu_set(12, 0) == 3 && click_mode == CLICK_OFF && menu_set(12, 1) == 3 && click_mode == CLICK_ON;
    ok &= menu_set(13, 0) == 3 && click_lvl == 0u;
    ok &= menu_set(14, 2) == 1 && click_mode == CLICK_ON && click_lvl == 0u;   /* (COUNT-IN's id: gone, unknown) */
    ok &= menu_set(15, 1) == 3 && (ui_prefs & PREF_RESTORE_OFF);
    ok &= menu_set(16, 1) == 3 && (ui_rec_prefs & 0x40u) && click_mode == CLICK_ON && click_lvl == 0u;   /* (its own bit) */
    ok &= menu_set(17, 0) == 3 && scr_get() == 0u && menu_set(17, 9) == 3 && scr_get() == 4u &&   /* (clamped) */
          menu_set(17, 2) == 3 && scr_get() == 2u && ui_scr == 1u;
    ok &= menu_set(18, 1) == 3 && ui_prefs2 == 1u && scope_mix;                                    /* (1.2: their own byte) */
    ok &= menu_set(19, 1) == 1 && ui_prefs2 == 1u && menu_set(20, 1) == 1 && ui_prefs2 == 1u;   /* (19: STEP PREVIEW's
                                                         * id, 20: CHORD ENTRY's, gone, unknown) */
    ok &= menu_set(21, 2) == 1 && menu_set(22, -13) == 0 && song.g[G_TUNE] == -13;   /* (21: MIDI IN's id, gone, unknown;
                                                         * TUNE the project's, rc 0: in the music, no settings record) */
    ok &= menu_set(23, 1) == 3 && ui_home_view == HV_TRACKS && ui_prefs2 == 1u && ui_scr == 1u;   /* (1.3: its own byte) */
    for (i = 0; i < 20u; i++) {                         /* MENU_DESC reads them back */
        static const int32_t SET[20] = {2, 1, 1, 1, 1, 3, 1, 1, 1, 2, 1, 0, 1, 0, 1, 1, 2, 1, -13, 1};
        menu_desc(i, &it);
        ok &= it.value == SET[i];
    }
    bad += check("MENU_SET: every setting applied as the menu does (palette, EQ, USB LEVEL, CLICK at once, COUNT-IN's, STEP PREVIEW's and CHORD ENTRY's ids unknown, RESTORE LAST, SCALE LEDS, SCREEN OFF, SCOPE, HOME), read back", ok);
    ok = menu_set(3, 0) == 3 && !(ui_prefs & PREF_ANIM_OFF) && (ui_prefs & PREF_LARGE) && menu_set(10, 0) == 3 &&
         !fx_usb_fixed && menu_set(1, 0) == 3 && ui_style == ST_FLAT;
    bad += check("MENU_SET: a flag back to its default leaves the other flags", ok);

    /* clamping: the reply carries the device's value */
    ok = menu_set(0, 50) == 3 && ed_rv(host_wire + 7) == (int32_t)NPALETTES - 1 && settings.palette == NPALETTES - 1u;
    ok &= menu_set(0, -5) == 3 && ed_rv(host_wire + 7) == 0 && settings.palette == 0u;
    ok &= menu_set(4, 99) == 3 && ed_rv(host_wire + 7) == 3 && settings_leds == LEDS_INV;
    ok &= menu_set(5, 8191) == 3 && ed_rv(host_wire + 7) == 3 && settings_hold == 3u;
    ok &= menu_set(2, 7) == 3 && ed_rv(host_wire + 7) == 1 && (ui_prefs & PREF_LARGE);
    ok &= menu_set(12, -1) == 3 && ed_rv(host_wire + 7) == 0 && click_mode == CLICK_OFF;
    ok &= menu_set(22, -99) == 0 && ed_rv(host_wire + 7) == -50 && song.g[G_TUNE] == -50;
    ok &= menu_set(22, 51) == 0 && ed_rv(host_wire + 7) == 50 && song.g[G_TUNE] == 50;
    bad += check("MENU_SET: out-of-range values clamped, the reply says the value the device took (TUNE -50..50)", ok);

    /* an id nobody has */
    {
        static uint8_t fav0[sizeof favorites], set0[sizeof settings];
        uint8_t hold0 = settings_hold, leds0 = settings_leds;
        memcpy(fav0, &favorites, sizeof favorites); memcpy(set0, &settings, sizeof settings);
        ok = menu_set(24, 1) == 1 && host_wire[6] == 24 && ed_rv(host_wire + 7) == 1;
        ok &= menu_set(126, -3) == 1 && host_wire[6] == 126 && ed_rv(host_wire + 7) == -3;
        ok &= menu_set(127, 0) == 1 && host_wire[6] == 127;
        ok &= !memcmp(fav0, &favorites, sizeof favorites) && !memcmp(set0, &settings, sizeof settings) &&
              hold0 == settings_hold && leds0 == settings_leds;
        bad += check("MENU_SET: an unknown id answers rc 1, id and value echoed, nothing changed", ok);
    }

    /* the MENU page shown: the editor's change redraws it */
    ui.menu = 1; ui.force = 0;
    menu_set(7, 0);
    bad += check("MENU_SET while the MENU page is shown: the page redraws (ui.force)", ui.force && ui.menu == 1);
    ui.menu = 0;

    /* USB SERIAL: the reply first, the re-enumeration 200 ms later (usb_serial_apply, called every frame). (usb.up
     * stays 0 here: the host has no USB registers; usb.config 0 = the device off the bus, enumerated afresh) */
    {
        uint32_t t0;
        reset();
        usb_cdc_on = 1;
        usb_serial_apply();
        ok = usb_cdc_on && usb.config;
        ok &= menu_set(11, 1) == 3 && host_wire[6] == 11 && ed_rv(host_wire + 7) == 1 && (ui_prefs & PREF_SERIAL_OFF);
        t0 = fm1_ms;
        usb_serial_apply();
        ok &= usb_cdc_on && usb.config;                 /* the reply has left; USB as it was */
        fm1_ms = t0 + 150u; usb_serial_apply();
        ok &= usb_cdc_on && usb.config;
        fm1_ms = t0 + 201u; usb_serial_apply();
        ok &= !usb_cdc_on && !usb.config;               /* off the bus: usb_retry attaches again without the console */
        bad += check("MENU_SET USB SERIAL OFF: reply first, applied ~200 ms later (the device re-enumerates)", ok);
        usb.config = 1;
        ok = menu_set(11, 0) == 3 && !(ui_prefs & PREF_SERIAL_OFF);
        t0 = fm1_ms;
        ui.menu = 1;                                    /* the MENU shown: applied when it closes, as the menu's own */
        fm1_ms = t0 + 500u;                             /* (ui_input calls usb_serial_apply only with the menu closed) */
        ok &= !usb_cdc_on && usb.config;
        ui.menu = 0; usb_serial_apply();
        ok &= usb_cdc_on && !usb.config;
        usb.config = 1;
        ok &= menu_set(11, 0) == 3;                     /* unchanged: no re-enumeration */
        fm1_ms += 300u; usb_serial_apply();
        ok &= usb_cdc_on && usb.config;
        bad += check("MENU_SET USB SERIAL ON: applied later (with the MENU open: when it closes); unchanged: no drop", ok);
    }
    reset();
    return bad;
}


#ifndef EDITOR_TEST_NO_MAIN                              /* (robust_test.c, fuzz_*.c: this file is their base) */
int main(void)
{
    int bad = preferences() + framing() + uart_recovery() + steps() + samples() + song_protocol() + malformed_saves() +
              tracker_protocol() +
              user_preset_roundtrip() + live_sync() + usb_burst() + menu_protocol();
    printf("%s\n", bad ? "EDITOR TEST FAILED" : "editor test passed");
    return bad != 0;
}
#endif
