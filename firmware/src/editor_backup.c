/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
/* Complete musical archive: no raw addresses are accepted. Small objects are staged
 * in main-loop RAM and fully validated before the existing A/B commit path writes.
 * Ids 32..34 (the user sample slots until 1.5) are listed empty; an archive's sample objects are not restored.
 * Id 8 (the FM6 patch bank of 1.0..1.0.2) is listed empty since 1.0.3; a PUT of it from an older archive moves its
 * patches into the user presets restored before it (ids 6, 7), as the first boot after the update does (up_fm6.c).
 * Id 9 is the user presets' FM6 patches (up_fm6.c), appended in 1.0.3.
 * 1.5 (the tracker): id 0, the music in RAM, is listed empty (a song is up to four storage objects: project.c); ids
 * 2..5 (and with 8 tracks 10..21: the song's chunks 4..15, Fel-Tracker) are the stored song's chunks as they are on the flash (project.c proj_good: the complete save's copies; a chunk
 * past its count, none stored: empty). A PUT of them writes each with a new generation, the same for the chunks of one
 * restore (ed_bk_rgen), so the restored song is the newest complete one; a project of 1.4 is refused.
 */
#if NTRK == 8                                   /* (Fel-Tracker, TRACKS 8) ids 10 .. 21: the song's chunks 4 .. 15 */
static const uint8_t ED_BK_IDS[25] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 32, 33, 34};
#else
static const uint8_t ED_BK_IDS[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 32, 33, 34};
#endif
/* the song's chunk an id is (2 .. 5: 0 .. 3; with 8 tracks 10 .. 21: 4 .. 15), -1 none */
static int ed_bk_song(uint32_t id)
{
    if (id >= 2u && id <= 5u)
        return (int)id - 2;
    return NTRK == 8 && id >= 10u && id <= 21u ? (int)id - 6 : -1;
}
#define ED_BK_N ((uint32_t)sizeof ED_BK_IDS)
#define ED_BK_MAX ((uint32_t)sizeof proj_wire_u.raw)
#define ED_BK_RAW (proj_wire_u.raw)  /* reuse the existing serialized main-loop scratch */
static persist_t ed_bk_settings;
static uint32_t ed_bk_rgen;                     /* a restore's generation for the song's chunks (ids 2..5) */
static uint8_t ed_bk_chunk = 0xFFu;             /* the stored chunk ED_BK_RAW holds (ed_bk_object), 0xFF none */
static uint32_t ed_bk_clen;                     /* .. its length */
static uint8_t ed_bk_valid, ed_bk_put, ed_bk_id, ed_bk_gen;
static uint32_t ed_bk_len, ed_bk_crc, ed_bk_pos, ed_bk_ms, ed_bk_usb;
static void ed_bk_u32(uint32_t n) { for (uint32_t i = 0; i < 5u; i++) ed_b((n >> (i * 7u)) & 127u); }
static uint32_t ed_bk_r32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static void ed_bk_pack(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, mask = 0;
        for (uint32_t i = 0; i < k; i++) mask |= (uint32_t)(p[i] >> 7) << i;
        ed_b(mask);
        for (uint32_t i = 0; i < k; i++) ed_b(p[i]);
        p += k; n -= k;
    }
}
static const uint8_t *ed_bk_object(uint32_t id, uint32_t *len)
{
    *len = 0;
    if (id == 0u) return ED_BK_RAW;                 /* (the music in RAM: not one object any more) */
    if (id == 1u) { *len = sizeof ed_bk_settings; return (const uint8_t *)&ed_bk_settings; }
    if (ed_bk_song(id) >= 0) {
#if FELUCCA_FLASH
        uint32_t i = (uint32_t)ed_bk_song(id);
        st_hdr_t h;
        if (ed_bk_chunk != i || ed_bk_gen != proj_wire_gen) {   /* the chunk into the staging RAM */
            ed_bk_chunk = 0xFFu;
            if (!proj_find() || i >= proj_good.count ||
                st_head(proj_obj(i), proj_good.copy[i], &h) || st_body(proj_obj(i), proj_good.copy[i], &h))
                return ED_BK_RAW;
            memcpy(ED_BK_RAW, st_buf, h.len);
            ed_bk_gen = ++proj_wire_gen;
            ed_bk_chunk = (uint8_t)i;
            ed_bk_clen = h.len;
        }
        *len = ed_bk_clen;
#endif
        return ED_BK_RAW;
    }
    if (id == 6u || id == 7u) {
        if (up_bank[id - 6u].magic == UP_BANK_MAGIC) *len = sizeof up_bank[0];
        return (const uint8_t *)&up_bank[id - 6u];
    }
    if (id == 8u || id == 9u)                       /* Felucca's FM6 patch banks (FM6 removed): always empty */
        return ED_BK_RAW;
    if (id >= 32u && id < 35u)                      /* the user sample slots (removed in 1.5): always empty */
        return ED_BK_RAW;
    return 0;
}
static uint32_t ed_bk_capture(void)
{
    if (ed_flash_stop()) return 3;
    ed_bk_gen = ++proj_wire_gen;
    ed_bk_chunk = 0xFFu;
#if FELUCCA_FLASH
    ed_bk_settings = persist_saved;                 /* fields absent from this build survive */
#else
    memset(&ed_bk_settings, 0, sizeof ed_bk_settings);
#endif
    settings_export(&ed_bk_settings);
    ed_bk_put = 0; ed_bk_valid = 1; ed_bk_usb = usb.resets;
    return 0;
}
static int ed_bk_panel_valid(const panel_t *p)
{
    uint32_t b = 0, e = 0;
    if (p->magic != PANEL_MAGIC) return 0;
    for (uint32_t i = 0; i < NB; i++) {
        if (p->btn[i] >= NB || (b & (1u << p->btn[i]))) return 0;
        b |= 1u << p->btn[i];
    }
    for (uint32_t i = 0; i < NE; i++) {
        if (p->enc[i] >= NE || (e & (1u << p->enc[i])) || (p->dir[i] != 1 && p->dir[i] != -1)) return 0;
        e |= 1u << p->enc[i];
    }
    return 1;
}
static uint32_t ed_bk_commit(void)
{
    uint8_t *raw = ED_BK_RAW;
    uint32_t obj;
    if (ed_bk_pos != ed_bk_len || st_crc32(raw, ed_bk_len) != ed_bk_crc) return 2;
    if (ed_bk_id == 0u) {
        return ed_bk_len ? 2u : 0u;                 /* (an archive's music in RAM: a project of 1.4) */
    } else if (ed_bk_song(ed_bk_id) >= 0) {         /* a chunk of the song (project.c); none: nothing to write (an */
        proj_hdr_t h;                                /* archive of no song at all, id 2 empty: none stored after it) */
        uint32_t ci = (uint32_t)ed_bk_song(ed_bk_id);
        if (!ed_bk_len) {
            if (ed_bk_id == 2u && proj_store_clear()) return 4;
            proj_cur = ed_bk_id == 2u ? PROJ_NO_SLOT : proj_cur;
            return 0;
        }
        memcpy(&h, raw, sizeof h);
        if (ed_bk_len < PROJ_HDR || h.magic != PROJ_MAGIC || h.index != ci || !h.count ||
            h.count > PROJ_CHUNKS || h.len != ed_bk_len - PROJ_HDR) return 2;
#if FELUCCA_FLASH
        if (!flash_ok) return 4;
        if (!ci || !ed_bk_rgen) {                   /* the first chunk of a restore: past every stored save */
            proj_find();
            ed_bk_rgen = proj_good.gen + 1u;
            memset(proj_wr_copy, 0, sizeof proj_wr_copy);
            for (obj = 0; obj < PROJ_CHUNKS; obj++)
                proj_wr_copy[obj] = (uint8_t)(proj_good.have && obj < proj_good.count ? !proj_good.copy[obj] : 0u);
        }
        h.gen = ed_bk_rgen;
        memcpy(raw, &h, sizeof h);
        if (st_save_to(proj_obj(ci), raw, ed_bk_len, proj_wr_copy[ci])) return 4;
        if (h.index + 1u == h.count) {              /* the last chunk: the restored song is the stored one */
            ed_bk_rgen = 0;
            proj_find();
            proj_cur = PROJ_NO_SLOT;
        }
#endif
        ed_bk_chunk = 0xFFu;
        sync_reload = 1; ui.force = 1;
        return 0;
    } else if (ed_bk_id == 1u) {
        const persist_t *p = (const persist_t *)raw;
        if (ed_bk_len != sizeof *p || p->magic != PERSIST_MAGIC || !palette_stored_ok(p->palette) ||
            p->lowcut > 2u || !leds_stored_ok(p->zoom) || !hold_stored_ok(p->bold) || p->favorites.filter > 1u || !ed_bk_panel_valid(&p->panel)) return 2;
        obj = OBJ_SETTINGS;
    } else if (ed_bk_id == 6u || ed_bk_id == 7u) {
        const up_bank_t *p = (const up_bank_t *)raw;
        if (ed_bk_len && (ed_bk_len != sizeof *p || p->magic != UP_BANK_MAGIC ||
            p->rsize != sizeof(up_rec_t) || p->nslot != UP_PER_BANK)) return 2;
        /* Unknown record versions remain inert bytes, preserving future/older bank data. */
        obj = OBJ_UPRESET0 + ed_bk_id - 6u;
    } else if (ed_bk_id == 8u || ed_bk_id == 9u) {  /* Felucca's FM6 patch banks: FM6 is gone, nothing to restore */
        return 0;
    } else return 1;
#if FELUCCA_FLASH
    if (!flash_ok || st_save(obj, raw, ed_bk_len)) return 4;
#else
    (void)obj;
#endif
    if (ed_bk_id == 1u) {
        memcpy(&ed_bk_settings, raw, sizeof ed_bk_settings);
        settings_import(&ed_bk_settings, sizeof ed_bk_settings);
        panel_init(); settings_init(); palette_set(settings.palette);
#if FELUCCA_FLASH
        persist_saved = ed_bk_settings; persist_pending = 0;
#endif
    } else {
        uint32_t b = ed_bk_id - 6u;
        memset(&up_bank[b], 0, sizeof up_bank[b]);
        if (ed_bk_len) memcpy(&up_bank[b], raw, ed_bk_len);
        up_bank_check(b, (int)ed_bk_len); up_gen++;
    }
    sync_reload = 1; ui.force = 1;
    return 0;
}
/* a stored song chunk's length (project.c: its header and up to PROJ_DATA bytes); 0: none */
static int proj_store_len(uint32_t len) { return !len || (len >= PROJ_HDR && len <= ST_PAYLOAD_MAX); }
static uint32_t ed_bk_write(const uint8_t *a, uint32_t n)
{
    /* the arguments first: a malformed request never stops the transport */
    if (n < 2u || a[0] > 3u || a[1] > 9u) return 1;
    if (a[0] == 0u) {
        if (n != 12u || a[6] > 15u || a[11] > 15u) return 1;
        uint32_t len = ed_bk_r32(a + 2);
        if (len > ED_BK_MAX || (a[1] == 0u && len) ||
            (a[1] == 1u && len != sizeof(persist_t)) ||
            (ed_bk_song(a[1]) >= 0 && len && !proj_store_len(len)) ||
            ((a[1] == 6u || a[1] == 7u) && len && len != sizeof(up_bank_t))) return 1;
        /* (8, 9: Felucca's FM6 objects, gone: taken and ignored, so an older archive restores) */
        if (ed_flash_stop()) return 3;
        ed_bk_valid = 0; ed_bk_put = 1; ed_bk_id = a[1]; ed_bk_len = len; ed_bk_gen = ++proj_wire_gen;
        ed_bk_crc = ed_bk_r32(a + 7); ed_bk_pos = 0;
        ed_bk_usb = usb.resets; ed_bk_ms = fm1_ms;
        return 0;
    }
    if (!ed_bk_put || ed_bk_id != a[1] || ed_bk_usb != usb.resets || fm1_ms - ed_bk_ms > 15000u ||
        ed_bk_gen != proj_wire_gen) {                 /* (a project save / load reused the staging RAM) */
        ed_bk_put = 0; return 5;
    }
    ed_bk_ms = fm1_ms;
    if (a[0] == 3u) { ed_bk_put = 0; return n == 2u ? 0u : 1u; }
    if (a[0] == 2u) {
        if (n != 2u) return 1;
        if (ed_flash_stop()) return 3;
        uint32_t rc = ed_bk_commit(); ed_bk_put = 0; return rc;
    }
    if (n < 9u || a[6] > 15u || ed_bk_r32(a + 2) != ed_bk_pos) return 1;
    uint32_t count = ed_unpack7(a + 7, n - 7u, ed_buf, 256u);
    if (!count || count > ed_bk_len - ed_bk_pos) return 1;
    if (ed_flash_stop()) return 3;
    memcpy(ED_BK_RAW + ed_bk_pos, ed_buf, count); ed_bk_pos += count;
    return 0;
}
static int ed_backup_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == ED_BACKUP_LIST) {
        uint32_t rc = n ? 1u : ed_bk_capture();
        ed_b(1); ed_b(rc); ed_b(rc ? 0u : ED_BK_N);
        if (!rc) for (uint32_t i = 0; i < ED_BK_N; i++) {
            uint32_t len;
            const uint8_t *p = ed_bk_object(ED_BK_IDS[i], &len);
            ed_b(ED_BK_IDS[i]); ed_bk_u32(len); ed_bk_u32(st_crc32(p, len));
            fm1_wdt_feed();
        }
        return 1;
    }
    if (cmd == ED_BACKUP_GET) {
        uint32_t len = 0, off = n >= 6u ? ed_bk_r32(a + 1) : 0u;
        uint32_t count = n == 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u;
        const uint8_t *p = n ? ed_bk_object(a[0], &len) : 0;
        uint32_t rc = (!ed_bk_valid || ed_bk_usb != usb.resets || (n && !a[0] && ed_bk_gen != proj_wire_gen)) ? 5u : transport_busy() ? 3u :
            n != 8u || a[5] > 15u || !p || !count || count > 256u || off > len || count > len - off ? 1u : 0u;
        ed_b(n ? a[0] : 127u); ed_b(rc); ed_bk_u32(off); ed_b(rc ? 0u : count & 127u); ed_b(rc ? 0u : count >> 7);
        if (!rc) ed_bk_pack(p + off, count);
        return 1;
    }
    if (cmd == ED_BACKUP_PUT) {
        uint32_t rc = ed_bk_write(a, n);
        ed_b(n ? a[0] : 127u); ed_b(n >= 2u ? a[1] : 127u); ed_b(rc);
        return 1;
    }
    return 0;
}
