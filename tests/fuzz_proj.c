/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md */
/* Fuzz harness (1.5: the song store, project.c): random songs saved, their chunks mutated (headers and data, lengths,
 * with and without the stream's hash made valid again: attacker-controlled flash), then found, loaded, played and
 * rendered. run_tests.sh runs it briefly under ASan / UBSan with a fixed seed.   fuzz_proj [ITERATIONS [SEED]] */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"                                /* hostsim, the stubs, RAM flash, reset() */
static uint32_t rs = 0x2545F491u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
/* the stream's hash made valid again over the chunks as they are (the name and the selected track not in it) */
static void rehash(void)
{
    uint32_t i, j, at = 0, h = 2166136261u, sel = 3u + 2u * G_COUNT, len = 0;
    for (i = 0; i < PROJ_CHUNKS && proj_ram_len[i] >= PROJ_HDR; i++)
        len += proj_ram_len[i] - PROJ_HDR;
    if (len < 4u)
        return;
    for (i = 0; i < PROJ_CHUNKS && proj_ram_len[i] >= PROJ_HDR; i++)
        for (j = PROJ_HDR; j < proj_ram_len[i]; j++, at++) {
            uint8_t *b = &proj_ram[i][j];
            if (at >= len - 4u)
                *b = (uint8_t)(h >> (8u * (at - (len - 4u))));
            else if (at < sel || at > sel + PROJ_NAME_LEN)
                h = (h ^ *b) * 16777619u;
        }
}
int main(int argc, char **argv)
{
    static int32_t o[CTL * 2 + 16];
    uint32_t iters = argc > 1 ? (uint32_t)atoi(argv[1]) : 20000u, i, j, k, loaded = 0;
    if (argc > 2) rs = (uint32_t)atoi(argv[2]) | 1u;
    reset();
    for (i = 0; i < iters; i++) {
        uint32_t pats = 1u + rnd() % 8u, n = rnd() % 3000u;
        tk_defaults();
        for (j = 0; j < n; j++)                         /* a random song (any field, raw) */
            *tk_at(rnd() % pats, rnd() % TK_ROWS, rnd() % NTRK) = tk_pack(rnd(), rnd(), rnd() % TKC_N, rnd());
        tk.norder = (uint8_t)(1u + rnd() % TK_ORDER);
        for (j = 0; j < tk.norder; j++) tk.order[j] = (uint8_t)(rnd() % TK_PATS);
        for (j = 0; j < TK_INSTS; j++) { tk.instr[j].src = (uint8_t)rnd(); tk.instr[j].preset = (uint8_t)rnd(); }
        tk_bound();
        if (project_save(0))
            continue;
        k = rnd() % 64u;
        for (j = 0; j < k; j++) {                       /* mutations: anywhere, often a header */
            uint32_t c = rnd() % PROJ_CHUNKS, off = rnd() % 4u == 0 ? rnd() % PROJ_HDR : rnd() % ST_PAYLOAD_MAX;
            proj_ram[c][off] = (uint8_t)(rnd() % 4u == 0 ? 0xFF : rnd() % 4u == 0 ? 0 : rnd());
            if (rnd() % 16u == 0) proj_ram_len[c] = (uint16_t)(rnd() % (ST_PAYLOAD_MAX + 1u));
        }
        if (rnd() % 2u)
            rehash();
        tk_defaults();
        project_load(0);
        loaded += str_eq(ui.msg, "LOADED");
        tk_pl.mode = rnd() % 2u ? TK_SONG : TK_LOOP;
        tk_pl.loop_pat = (uint8_t)(rnd() % TK_PATS);
        transport_req = 1;
        for (j = 0; j < 24u; j++) {                     /* played: rows, instruments, FX, notes */
            events_block(div_samples(2) / 3u + 1u);
            mix_block(o, CTL);
        }
        transport_req = 2;
        events_block(CTL);
        if (rnd() % 64u == 0) reset();
    }
    printf("fuzz_proj ok: %u iterations, %u loaded\n", iters, loaded);
    return 0;
}
