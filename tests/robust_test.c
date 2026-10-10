/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Robustness against damaged or crafted stored data and editor requests (the 1.0.4 audit's items 5-10): the real
 * C paths of the editor build (editor_test.c is the base: hostsim, stubs, RAM flash).
 *   user sample slots   a SLICE scan bounded by the slot's size; a slot that fails its check leaves no zone behind
 *   projects            (1.5: the song's stream) an engine number past the last refused under a valid hash; no
 *                       position, a pattern of no row, an unknown FX refused; values out of range loaded inside it
 *   user presets        a bank from flash or a backup: each record's pattern inside its fields
 *   editor              a malformed SMP_END / BACKUP_PUT does not stop the transport */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

static void zone_set(smp_zone_t *z, uint32_t off, uint32_t n)
{
    memset(z, 0, sizeof *z);
    z->off = off; z->n = n; z->ls = 0; z->le = n - 1u; z->rate = 1u << 16; z->idx = 0; z->lo = 0; z->hi = 127;
}

static int sample_slots(void)
{
    int bad = 0;
    uint32_t i, dl = SMP_USER_SIZE - SMP_USER_DATA;
    smp_user_hdr_t *h;
    reset(); memset(host_samples, 0, sizeof host_samples);
    /* 16 zones over overlapping data, each almost the whole slot (every zone inside the data) */
    h = (smp_user_hdr_t *)host_samples[0];
    h->magic = SMP_USER_MAGIC; h->version = 1; h->nz = 16; h->data_len = dl;
    for (i = 0; i < 16u; i++) zone_set(&h->zone[i], 32u * i, 2u * (dl - 32u * i));
    smp_user_scan(0);
#if FELUCCA_SLICE
    printf("robust: SLICE material of 16 overlapping zones: %u samples (the slot holds %u)\n", slc_usr[0].len,
           2u * dl);
    bad += check("16 overlapping zones: the SLICE scan is at most the slot's size", usr_nz[0] == 16u &&
                 slc_usr[0].len && slc_usr[0].len <= 2u * dl);
#endif
    /* zone 0 good, zone 1 malformed: nothing of the slot is left (usr_zone[1][0] was copied before the check) */
    h = (smp_user_hdr_t *)host_samples[1];
    h->magic = SMP_USER_MAGIC; h->version = 1; h->nz = 2; h->data_len = 4096;
    zone_set(&h->zone[0], 0, 4096);
    zone_set(&h->zone[1], 0, 4096); h->zone[1].idx = 99;
    smp_user_scan(1);
    bad += check("a slot with a malformed zone: unusable, no zone of it left behind",
                 !usr_nz[1] && !usr_zone[1][0].n && !usr_zone[1][1].n);
    h->zone[1].idx = 0;
    smp_user_scan(1);
    bad += check("the same slot made valid scans again", usr_nz[1] == 2u && usr_zone[1][0].n == 4096u);
    return bad;
}

static int msg_is_ed(const char *m) { return ui.msg_t && str_eq(ui.msg, m); }
/* the song's stream as the store holds it (project.c), its hash made valid again after an edit (the name not in it) */
static void stream_rehash(void)
{
    uint32_t i, j, at = 0, h = 2166136261u, sel = 3u + 2u * G_COUNT, len = 0;   /* (the track, the name: not hashed) */
    for (i = 0; i < PROJ_CHUNKS && proj_ram_len[i]; i++)
        len += proj_ram_len[i] - PROJ_HDR;
    for (i = 0; i < PROJ_CHUNKS && proj_ram_len[i]; i++)
        for (j = PROJ_HDR; j < proj_ram_len[i]; j++, at++) {
            uint8_t *b = &proj_ram[i][j];
            if (at >= len - 4u)                         /* (the hash: the stream's last 4 bytes) */
                *b = (uint8_t)(h >> (8u * (at - (len - 4u))));
            else if (at < sel || at > sel + PROJ_NAME_LEN)
                h = (h ^ *b) * 16777619u;
        }
}
/* a field outside its range is refused even under a valid hash (a crafted or damaged store); a track's value outside
 * its range loads inside it; an instrument naming no engine loads empty */
static int projects(void)
{
    int bad = 0;
    uint32_t e0, off = PROJ_HDR + 3u + 2u * G_COUNT + 1u + PROJ_NAME_LEN, refused = 1;   /* track 1's engine byte */
    uint8_t keep;
    reset();
    *tk_at(0, 0, 0) = tk_pack(60, 0, 0, 0);
    bad += check("a song saves and loads", !project_save(0) && (project_load(0), tk_note(tk_at(0, 0, 0)) == 60u));
    keep = proj_ram[0][off];
    for (e0 = NENGINES; e0 < 256u; e0 += 37u) {
        proj_ram[0][off] = (uint8_t)e0;
        stream_rehash();
        project_new();
        project_load(0);
        refused &= !tk_note(tk_at(0, 0, 0)) && trk[0].eng_req < NENGINES;
    }
    proj_ram[0][off] = keep;
    stream_rehash();
    project_load(0);
    bad += check("a song with an engine past the last (a valid hash) is refused, the music as it was",
                 refused && tk_note(tk_at(0, 0, 0)) == 60u);
    tk.norder = 0;
    project_save(0);
    project_new();
    project_load(0);
    refused = msg_is_ed("LOAD ERROR") && tk.norder == 1u;
    tk.len[5] = 0;
    project_save(0);
    project_load(0);
    refused &= msg_is_ed("LOAD ERROR");
    tk.len[5] = TK_ROWS;
    *tk_at(3, 3, 3) = tk_pack(60, 0, 15, 0);            /* (command 15: none such) */
    project_save(0);
    project_load(0);
    refused &= msg_is_ed("LOAD ERROR");
    bad += check("no position, a pattern of no row, an FX command past the last: refused", refused);
    reset();
    trk[1].p[P_LEVEL] = 999;
    tk.instr[7].src = 50;                               /* (no engine, no user preset) */
    project_save(0);
    project_load(0);
    bad += check("a track's value out of its range loads inside it; an instrument of no engine loads empty",
                 trk[1].p[P_LEVEL] == TP[P_LEVEL].max && tk.instr[7].src == TK_SRC_NONE);
    return bad;
}

static int user_presets(void)
{
    int bad = 0;
    up_bank_t *bk = &up_bank[0];
    up_rec_t *r;
    reset();
    memset(bk, 0, sizeof *bk);
    bk->magic = UP_BANK_MAGIC; bk->rsize = sizeof(up_rec_t); bk->nslot = UP_PER_BANK;
    r = &bk->r[0];                                          /* a note pattern with a note > 127 and stray flags */
    r->used = UP_USED; r->ver = 2; r->engine = 0; r->np = 8; r->name[0] = 'A';
    r->note[3] = 200; r->flags[3] = 0x43;
    r->note[4] = 0; r->flags[4] = 0x03;                     /* a rest with flags */
    r = &bk->r[1];                                          /* a drum grid: accents where there is no hit */
    r->used = UP_USED; r->ver = UP_VER_GRID; r->engine = ENGI_DRUM; r->np = 8; r->name[0] = 'G';
    r->note[0] = 0x05; r->flags[0] = 0xFF;
    up_bank_check(0, sizeof *bk);
    bad += check("a bank from flash: a note pattern's note inside 0..127, its flags inside their fields",
                 up_used(0) && bk->r[0].note[3] == (200u & 127u) && bk->r[0].flags[3] == 3u && !bk->r[0].flags[4]);
    bad += check("a bank from flash: a drum grid's accents only on its hits", up_used(1) && bk->r[1].note[0] == 5u &&
                 bk->r[1].flags[0] == 5u);
    return bad;
}

static int stop_after_checks(void)
{
    int bad = 0;
    uint8_t a[16] = {0};
    reset();
    song.playing = 1; host_progress = 1;                    /* the transport runs and could be stopped */
    a[0] = 0; a[1] = 1; a[2] = 2;
    request(ED_SMP_END, a, 3);                              /* a header far too short */
    bad += check("a malformed SMP_END is refused without stopping the transport", host_wire[6] == 1 && song.playing);
    a[0] = 0; a[1] = 2;
    request(ED_BACKUP_PUT, a, 2);                           /* BEGIN without its arguments */
    bad += check("a malformed BACKUP_PUT is refused without stopping the transport", host_wire[7] == 1 && song.playing);
    a[0] = 1; a[1] = 2;
    request(ED_BACKUP_PUT, a, 9);                           /* data with no session */
    bad += check("BACKUP_PUT data without a session: refused, the transport runs", host_wire[7] == 5 && song.playing);
    song.playing = 0;
    return bad;
}

int main(void)
{
    int bad = sample_slots() + projects() + user_presets() + stop_after_checks();
    printf("%s\n", bad ? "ROBUSTNESS TEST FAILED" : "robustness test passed");
    return bad != 0;
}
