/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin's cores and instances (plugin/core: fel_core.h, fel_loader.c), with no host:
 *   a loaded core plays as the native image's build of its distribution, sample for sample (tests/plugin_core_ref.c);
 *   instances are independent: two playing different things, rendered in turn block by block, each give exactly
 *   what they give alone; two on two threads at once, the same; their flash is their own;
 *   different distributions side by side (the chooser), each its own engines;
 *   the copies an instance opens are kept in the cache directory and reused (the system's check of a new executable,
 *   some 0.2 s, only the first time): loading again and again keeps a copy a core, two at once two; a rebuilt core gets
 *   new ones and the old go; with no cache directory, a temporary copy that leaves nothing behind.
 *   plugin_core_test CORE_DIR REF.f32   (CORE_DIR: build/plugin/cores; REF: plugin_core_ref's, IDM NATIVE) */
#include "../plugin/core/fel_loader.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#define EXT "dylib"
#else
#define EXT "so"
#endif
#define B_PLAY 10u                       /* (panel.c's label order: FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY ..) */
#define REC_FRAMES 88200u                /* 2 s */

static int bad;
static void ck(const char *what, int ok) { printf("plugin core: %-100s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static const char *dir;

static fel_instance_t *load(const char *dist)
{
    char p[1024], err[512];
    fel_instance_t *in;
    snprintf(p, sizeof p, "%s/fel-core-%s." EXT, dir, dist);
    if (!(in = fel_load(p, err, sizeof err)))
        printf("plugin core: %s\n", err);
    return in;
}

/* a scenario: what a host would do to an instance, as steps; run one block (<= 128 frames) at a time */
enum { OP_RENDER, OP_REC, OP_HEAVY, OP_BUTTONS, OP_KEYS, OP_MIDI, OP_END };
typedef struct { uint8_t op; uint32_t a, b, c; } step_t;
static const step_t HEAVY[] = {                  /* web/emu/native_check.c's, as tests/plugin_core_ref.c plays it */
    {OP_RENDER, 22050, 0, 0}, {OP_HEAVY, 0, 0, 0}, {OP_BUTTONS, 1u << B_PLAY, 0, 0}, {OP_RENDER, 2646, 0, 0},
    {OP_BUTTONS, 0, 0, 0}, {OP_RENDER, 2646, 0, 0}, {OP_REC, REC_FRAMES, 0, 0}, {OP_END, 0, 0, 0},
};
static const step_t PLAYED[] = {                 /* something else: keys held, MIDI notes, no song */
    {OP_RENDER, 22050, 0, 0}, {OP_KEYS, (1u << 12) | (1u << 16), 0, 0}, {OP_RENDER, 13230, 0, 0},
    {OP_MIDI, 0x90, 67, 100}, {OP_MIDI, 0x91, 40, 110}, {OP_REC, REC_FRAMES / 2u, 0, 0}, {OP_KEYS, 0, 0, 0},
    {OP_MIDI, 0x80, 67, 0}, {OP_REC, REC_FRAMES / 2u, 0, 0}, {OP_END, 0, 0, 0},
};
typedef struct {
    const fel_core_api_t *api;
    const step_t *s;
    uint32_t left, rec;
    float *out;                          /* REC_FRAMES x L R */
} run_t;

static void run_init(run_t *r, fel_instance_t *in, const step_t *s, float *out)
{
    memset(r, 0, sizeof *r);
    r->api = fel_api(in);
    r->s = s;
    r->out = out;
    r->api->nor_erase();
    r->api->boot();
}
static int run_block(run_t *r)                    /* 0: the scenario is over */
{
    static float l[128], rr[128];
    float *L = l, *R = rr;
    uint32_t m, i;
    while (r->s->op != OP_END && r->s->op != OP_RENDER && r->s->op != OP_REC) {
        switch (r->s->op) {
        case OP_HEAVY: r->api->test_heavy(); break;
        case OP_BUTTONS: r->api->buttons(r->s->a); break;
        case OP_KEYS: r->api->keys(r->s->a); break;
        default: r->api->midi(r->s->a, r->s->b, r->s->c); break;
        }
        r->s++;
    }
    if (r->s->op == OP_END)
        return 0;
    if (!r->left)
        r->left = r->s->a;
    m = r->left < 128u ? r->left : 128u;
    {
        float lb[128], rb[128];                   /* (per call: two threads may be here at once) */
        L = lb, R = rb;
        r->api->render(L, R, m);
        for (i = 0; r->s->op == OP_REC && i < m; i++) {
            r->out[2u * (r->rec + i)] = L[i];
            r->out[2u * (r->rec + i) + 1u] = R[i];
        }
    }
    if (r->s->op == OP_REC)
        r->rec += m;
    if (!(r->left -= m))
        r->s++;
    (void)l, (void)rr;
    return 1;
}
static void run_all(run_t *r)
{
    while (run_block(r))
        ;
}
static void *thread_run(void *p)
{
    run_all(p);
    return 0;
}

static double energy(const float *x)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < 2u * REC_FRAMES; i++)
        e += (double)x[i] * x[i];
    return e;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static uint32_t core_files(const char *tmp)       /* fel-core-*.EXT files in tmp (not their locks) */
{
    DIR *d = opendir(tmp);
    struct dirent *e;
    uint32_t n = 0;
    while (d && (e = readdir(d)))
        n += !strncmp(e->d_name, "fel-core-", 9) && !strstr(e->d_name, ".lock") &&
             !strcmp(e->d_name + strlen(e->d_name) - strlen(EXT), EXT);
    if (d)
        closedir(d);
    return n;
}

int main(int argc, char **argv)
{
    static float ref[2 * REC_FRAMES], a1[2 * REC_FRAMES], b1[2 * REC_FRAMES], a2[2 * REC_FRAMES], b2[2 * REC_FRAMES],
        t1[2 * REC_FRAMES], t2[2 * REC_FRAMES];
    static const char *const DISTS[] = {"idm", "foundry", "warehouse", "idm_native"};
    char tmp[1024], cache[1024], msg[240];
    double t0, first, again;
    fel_instance_t *A, *B, *C[4];
    run_t ra, rb;
    pthread_t th[2];
    FILE *f;
    uint32_t i, k, ok;
    if (argc < 3)
        return 2;
    dir = argv[1];
    snprintf(tmp, sizeof tmp, "%s/plugin_core_tmp", dir);   /* (a temporary directory of its own: count what is left) */
    mkdir(tmp, 0700);
    setenv("TMPDIR", tmp, 1);
    snprintf(cache, sizeof cache, "%s/plugin_core_cache", dir);   /* (a cache of its own: what it holds is counted) */
    {
        DIR *d = opendir(cache);                      /* (empty at the start: the first loads make the copies) */
        struct dirent *e;
        while (d && (e = readdir(d))) {
            char p[2100];
            snprintf(p, sizeof p, "%s/%s", cache, e->d_name);
            if (e->d_name[0] != '.')
                unlink(p);
        }
        if (d)
            closedir(d);
    }
    setenv("FEL_CACHE_DIR", cache, 1);
    if (!(f = fopen(argv[2], "rb")) || fread(ref, sizeof ref, 1, f) != 1)
        return 2;
    fclose(f);

    /* alone: IDM NATIVE's heavy song = the native build's; then something else */
    t0 = now();
    A = load("idm_native");
    first = now() - t0;
    ck("a core loads: fel_core_api, ABI, IDM NATIVE, 44.1 kHz, 8 tracks",
       A && fel_api(A)->abi == FEL_CORE_ABI && !strcmp(fel_api(A)->dist, "IDM NATIVE") && fel_api(A)->sample_rate == 44100u &&
       fel_api(A)->tracks == 8u);
    if (!A)
        return 1;
    run_init(&ra, A, HEAVY, a1);
    run_all(&ra);
    ck("alone, the heavy song (2 s) is the native image's build, sample for sample", !memcmp(a1, ref, sizeof ref) &&
       energy(a1) > 1.0);
    fel_unload(A);
    t0 = now();
    B = load("idm_native");
    again = now() - t0;
    run_init(&rb, B, PLAYED, b1);
    run_all(&rb);
    fel_unload(B);
    ck("alone, keys and MIDI notes sound", energy(b1) > 1.0 && memcmp(a1, b1, sizeof a1));

    /* together: two instances of the same core, in turn, block by block */
    A = load("idm_native");
    B = load("idm_native");
    ck("two instances of one core: two images, their flash apart", A && B && fel_api(A) != fel_api(B) &&
       fel_api(A)->nor() != fel_api(B)->nor());
    run_init(&ra, A, HEAVY, a2);
    run_init(&rb, B, PLAYED, b2);
    for (k = 1; k;)
        k = (uint32_t)run_block(&ra) | (uint32_t)run_block(&rb);
    ck("rendered in turn, block by block: each exactly as alone", !memcmp(a2, a1, sizeof a1) && !memcmp(b2, b1, sizeof b1));
    fel_api(A)->nor()[0] ^= 0x5Au;
    ck("one's flash written: the other's not", fel_api(B)->nor()[0] != fel_api(A)->nor()[0] || fel_api(B)->nor()[0] == 0xFFu);
    fel_unload(A);
    fel_unload(B);

    /* two threads at once */
    A = load("idm_native");
    B = load("idm_native");
    run_init(&ra, A, HEAVY, t1);
    run_init(&rb, B, HEAVY, t2);
    pthread_create(&th[0], 0, thread_run, &ra);
    pthread_create(&th[1], 0, thread_run, &rb);
    pthread_join(th[0], 0);
    pthread_join(th[1], 0);
    ck("two instances on two threads at once: both the native image's, sample for sample",
       !memcmp(t1, ref, sizeof ref) && !memcmp(t2, ref, sizeof ref));
    fel_unload(A);
    fel_unload(B);

    /* the chooser: every distribution side by side */
    ok = 1;
    for (i = 0; i < 4u; i++)
        ok &= (C[i] = load(DISTS[i])) != 0;
    if (ok) {
        static const char *const TAG[] = {"IDM", "FOUNDRY", "WAREHOUSE", "IDM NATIVE"};
        for (i = 0; i < 4u; i++) {
            static float l[4096], r[4096];
            const fel_core_api_t *api = fel_api(C[i]);
            api->nor_erase();
            api->boot();
            api->render(l, r, 4096);                     /* (one call: any length) */
            api->midi(0x90, 60, 100);
            api->render(l, r, 4096);
            ok &= !strcmp(api->dist, TAG[i]) && api->tracks == 8u && api->now_ms() >= 180u && api->screen_draws() > 0;
        }
    }
    for (i = 0; i < 4u; i++)
        fel_unload(C[i]);
    ck("the four distributions loaded side by side, each its own name, clock and screen", ok);

    /* again and again: a kept copy a core, reused */
    ok = 1;
    for (i = 0; i < 24u; i++) {
        static float l[512], r[512];
        fel_instance_t *in = load(DISTS[i % 4u]);
        ok &= in != 0;
        if (in) {
            fel_api(in)->nor_erase();
            fel_api(in)->boot();
            fel_api(in)->render(l, r, 512);
            fel_unload(in);
        }
    }
    snprintf(msg, sizeof msg, "24 loads and unloads: all work; the cache keeps %u copies (one a core: two of IDM NATIVE), "
             "the temporary directory %u", core_files(cache), core_files(tmp));
    ck(msg, ok && core_files(cache) == 5u && core_files(tmp) == 0);
    snprintf(msg, sizeof msg, "a copy known to the system loads at once: the first load %.0f ms, again %.0f ms",
             first * 1e3, again * 1e3);
    ck(msg, again <= first);

    /* a rebuilt core: new copies, the old ones gone */
    {
        char src[1024], cp[1100];
        struct timeval tv[2];
        fel_instance_t *in;
        snprintf(src, sizeof src, "%s/fel-core-idm." EXT, dir);
        snprintf(cp, sizeof cp, "%s/fel-core-rebuilt." EXT, tmp);
        {
            FILE *a = fopen(src, "rb"), *b = fopen(cp, "wb");
            static char buf[1 << 16];
            size_t n;
            while (a && b && (n = fread(buf, 1, sizeof buf, a)) > 0)
                fwrite(buf, 1, n, b);
            if (a)
                fclose(a);
            if (b)
                fclose(b);
        }
        in = fel_load(cp, 0, 0);
        fel_unload(in);
        k = core_files(cache);
        gettimeofday(&tv[0], 0);
        tv[0].tv_sec += 100;
        tv[1] = tv[0];
        utimes(cp, tv);                               /* ("rebuilt": a later time) */
        in = fel_load(cp, 0, 0);
        ok = in != 0 && core_files(cache) == k;
        fel_unload(in);
        unlink(cp);
        ck("a rebuilt core: a new copy, the old one removed", ok);
    }

    /* no cache directory: a temporary copy, nothing left behind */
    setenv("FEL_CACHE_DIR", "/dev/null/no-cache", 1);
    A = load("foundry");
    ok = A != 0 && core_files(tmp) == 0;
    if (A) {
        static float l[512], r[512];
        fel_api(A)->nor_erase();
        fel_api(A)->boot();
        fel_api(A)->render(l, r, 512);
    }
    fel_unload(A);
    ck("no cache directory: a temporary copy, removed once open; it plays", ok);

    {
        char err[512];
        ck("a library that is not a core is refused", !fel_load("/usr/lib/libSystem.B.dylib", err, sizeof err) &&
           strstr(err, "fel_load"));
    }
    rmdir(tmp);
    printf("%s\n", bad ? "PLUGIN CORE TEST FAILED" : "plugin core test passed");
    return bad ? 1 : 0;
}
