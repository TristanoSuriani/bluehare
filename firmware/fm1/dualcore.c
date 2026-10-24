/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* (2026-10-24) The second core on the device: target.h's tgt_c1_run / tgt_c1_wait over hal/fm1_cpu1.h (ChoralRoot
 * FM-1's, after fm1-x0x and Melodee PR 21, all GPL-3.0), what fx.c mix_block hands it (one part's mix_part a block),
 * and when it may start. After ChoralRoot's cr_cpu1.h, the essentials:
 *   power-on (main.c fm1_main, before audio_init and the timers): core 1 starts unless MENU > SYSTEM > CORES is 1, the
 *   boot before this one failed (the boot guard: a crash or a hang in its first 30 s), or the bar says no (a crash
 *   beside core 1 at the last boot, a hang of it: the watchdog's reset after it ran, or a give-up). Before it starts
 *   the core rails go up to JieLi's levels for this clock (hal/fm1_power.h: SYSVDD 1.35 V, VDC14 1.50 V, never more,
 *   a step every 100 us); the PC limits open while it boots through the ROM. Not answering in 20 ms: one core.
 *   Each block: a new misread (fm1_c1_rejected) is counted, a second one gives core 1 up; a stall (8 ms) or a fault
 *   gives it up at once. Given up: held for the rest of the session, the bar set (the next warm boot keeps it off),
 *   one core, the same sound (fx.c: the samples one core's either way).
 * MENU > INFO's CORES row: c1_state. */
enum { C1_OFF, C1_ON, C1_FAILED, C1_GAVE_UP, C1_BARRED };
static uint8_t c1_state = C1_OFF;
static uint32_t c1_seen, c1_misreads;

static void c1_give_up(void)
{
    fm1_cpu1_hold();
    c1_state = C1_GAVE_UP;
    fm1_cpu1_bar(3u);                                   /* (ChoralRoot's C1_BAR_GAVE_UP) */
}

static int tgt_c1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (c1_state != C1_ON)
        return 0;
    if (fm1_c1_fault.count) {
        c1_give_up();
        return 0;
    }
    if (fm1_c1_rejected != c1_seen) {                   /* misreads: the first counted, a second gives up */
        c1_seen = fm1_c1_rejected;
        if (++c1_misreads > 1u) {
            c1_give_up();
            return 0;
        }
    }
    return fm1_cpu1_run(fn, arg);
}

static int tgt_c1_wait(void)
{
    int rc = fm1_cpu1_wait();
    if (rc)
        c1_give_up();
    return rc;
}

/* main.c fm1_main: core 1 at power-on (interrupts off, before audio_init and the timers). wdt_reset: this boot came
 * from the watchdog */
static void c1_boot(int failed_before, int wdt_reset)
{
    uint32_t barred = 0;
    if (fm1_c1_bar.magic == FM1_C1_BAR_MAGIC)           /* (a power-on's RAM is not the record) */
        barred = fm1_c1_bar.barred || (fm1_c1_bar.ran && wdt_reset);
    fm1_c1_bar.magic = FM1_C1_BAR_MAGIC;
    fm1_c1_bar.ran = fm1_c1_bar.barred = 0;
    menu_cores_boot = (uint8_t)(1u + !!(ui_prefs2 & (PREF_ONE_CORE >> 16)));   /* (the CORES row: AT RESTART) */
    if (ui_prefs2 & (PREF_ONE_CORE >> 16))
        return;                                         /* CORES 1 */
    if (failed_before || barred) {
        c1_state = C1_BARRED;
        return;
    }
    fm1_power_init();                                   /* the rails up before core 1 starts */
    fm1_guard_unlock_top();                             /* (fm1_main locks it after the IRQs are attached) */
    fm1_guard_pc_open();
    c1_state = fm1_cpu1_start() ? C1_FAILED : C1_ON;
    fm1_guard_enable(FM1_GUARD_PC);
}

/* MENU > INFO: what the second core is doing (2026-10-24: running, with the misread it survived, if any) */
static const char *c1_status(void)
{
    static const char *const N[5] = {"1 (CORES 1)", "2", "1 (NO ANSWER)", "1 (GAVE UP)", "1 (LAST BOOT FAILED)"};
    if (c1_state == C1_ON && c1_misreads)
        return "2 (1 MISREAD)";
    return N[c1_state % 5u];
}

/* (2026-10-24) the serial console's status (console.c con_status): the second core's state and what it survived */
static void c1_con(void)
{
    con_kv("c1_state", c1_state);                       /* 0 CORES 1, 1 running, 2 no answer, 3 gave up, 4 barred */
    con_kv("c1_misreads", (int32_t)fm1_c1_rejected);
    con_kv("c1_stalls", (int32_t)fm1_c1_stalls);
    con_kv("c1_faults", (int32_t)fm1_c1_fault.count);
}
