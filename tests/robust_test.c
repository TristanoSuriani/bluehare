/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Robustness against damaged or crafted stored data and editor requests (the 1.0.4 audit's items 5-10): the real
 * C paths of the editor build (editor_test.c is the base: hostsim, stubs, RAM flash).
 *   projects            (1.5: the song's stream) an engine number past the last or retired refused under a valid hash; no
 *                       position, a pattern of no row, an unknown FX refused; values out of range loaded inside it
 *   user presets        a bank from flash or a backup: each record's pattern inside its fields; a retired engine's
 *                       record not valid
 *   editor              SMP_END (no sample slots) / a malformed BACKUP_PUT does not stop the transport */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

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
    for (e0 = 1; e0 < 14u; e0 += e0 == 1u ? 3u : e0 == 4u ? 4u : 5u) {   /* 1 4 8 13: DIGITAL SAMPLE GRAIN SLICE */
        proj_ram[0][off] = (uint8_t)e0;
        stream_rehash();
        project_new();
        project_load(0);
        refused &= ENG_RETIRED(e0) && !tk_note(tk_at(0, 0, 0)) && eng_ok(trk[0].eng_req);
    }
    proj_ram[0][off] = keep;
    stream_rehash();
    project_load(0);
    bad += check("a song with an engine past the last or retired (a valid hash) is refused, the music as it was",
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
    bk->r[1].engine = 13;                                   /* (SLICE, retired) */
    bad += check("a record of a retired engine: not valid (the slot shows empty)", !up_used(1));
    return bad;
}

static int stop_after_checks(void)
{
    int bad = 0;
    uint8_t a[16] = {0};
    reset();
    song.playing = 1; host_progress = 1;                    /* the transport runs and could be stopped */
    a[0] = 0; a[1] = 1; a[2] = 2;
    bad += check("SMP_END (no sample slots since 1.5): no reply, the transport runs", !request(ED_SMP_END, a, 3) &&
                 song.playing);
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
    int bad = projects() + user_presets() + stop_after_checks();
    printf("%s\n", bad ? "ROBUSTNESS TEST FAILED" : "robustness test passed");
    return bad != 0;
}
