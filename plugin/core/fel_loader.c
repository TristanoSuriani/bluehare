/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* fel_loader.h: an instance is its own copy of the core library. The dynamic loader keeps one image of a file however
 * many times it is opened (the same globals), so each instance opens a copy no other instance has open.
 * The copies are kept, a few numbered slots a core in the cache directory (FEL_CACHE_DIR, else
 * ~/Library/Caches/Fel-Tracker/cores, ~/.cache/fel-tracker/cores on Linux), and reused: the system checks a new
 * executable file the first time it is opened (Gatekeeper, an endpoint scanner: some 0.2 s on a Mac), a known one
 * opens at once, so a session's instances load fast after the first time. A slot is an instance's while it holds the
 * slot's lock (flock: exclusive across loaders and processes, so two plugin formats in one host never share one);
 * its name carries the core's size and time, so a rebuilt core gets new copies and the old ones go. The core's
 * claim() checks the image opened is a fresh one (an image a host kept loaded after closing it would not be): if not,
 * the next slot. No cache directory: a temporary copy each time (removed once open), as slow as the system makes it. */
#include "fel_loader.h"
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#define LIB_EXT ".dylib"
#define CACHE_SUB "Library/Caches/Fel-Tracker/cores"
#else
#define LIB_EXT ".so"
#define CACHE_SUB ".cache/fel-tracker/cores"
#endif
#define SLOTS 64u                        /* instances of one core at once (more: temporary copies) */

struct fel_instance {
    void *lib;
    const fel_core_api_t *api;
    int lock;                            /* the slot's lock (-1: a temporary copy) */
};

static void say(char *err, unsigned n, const char *a, const char *b)
{
    if (err && n)
        snprintf(err, n, "%s%s", a, b ? b : "");
}

static int copy_file(const char *from, const char *to)   /* 0 = done; to appears whole or not at all */
{
    char tmp[1100], buf[65536];
    int in, out, ok = 1;
    ssize_t r = 0;
    snprintf(tmp, sizeof tmp, "%s.%d.part", to, (int)getpid());
    if ((in = open(from, O_RDONLY)) < 0)
        return -1;
    if ((out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755)) < 0) {
        close(in);
        return -1;
    }
    while (ok && (r = read(in, buf, sizeof buf)) > 0)
        ok = write(out, buf, (size_t)r) == r;
    ok &= r == 0;
    close(in);
    ok &= close(out) == 0;
    if (ok && rename(tmp, to) == 0)
        return 0;
    unlink(tmp);
    return -1;
}

static int mkdirs(const char *path)               /* mkdir -p */
{
    char p[1024], *s;
    snprintf(p, sizeof p, "%s", path);
    for (s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0755);
            *s = '/';
        }
    return mkdir(p, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int cache_dir(char *d, unsigned n)
{
    const char *e = getenv("FEL_CACHE_DIR"), *home = getenv("HOME");
    if (e && *e)
        snprintf(d, n, "%s", e);
    else if (home && *home)
        snprintf(d, n, "%s/%s", home, CACHE_SUB);
    else
        return -1;
    return mkdirs(d);
}

/* the core at path opened as an instance: its api checked, claimed; 0 if not (lib closed) */
static const fel_core_api_t *open_core(const char *file, void **lib, char *err, unsigned err_n)
{
    fel_core_api_fn fn;
    const fel_core_api_t *api;
    *lib = dlopen(file, RTLD_NOW | RTLD_LOCAL);
    if (!*lib) {
        say(err, err_n, "fel_load: ", dlerror());
        return 0;
    }
    fn = (fel_core_api_fn)dlsym(*lib, FEL_CORE_SYMBOL);
    api = fn ? fn() : 0;
    if (!api || api->abi != FEL_CORE_ABI) {
        say(err, err_n, "fel_load: not a core of this ABI: ", file);
        dlclose(*lib);
        *lib = 0;
        return 0;
    }
    if (!api->claim()) {                             /* (an image still loaded, another instance's) */
        dlclose(*lib);
        *lib = 0;
        return 0;
    }
    return api;
}

/* the old copies of this core (its stem, another key), none of them in use: removed */
static void sweep(const char *dir, const char *stem, const char *key)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t sl = strlen(stem);
    while (d && (e = readdir(d))) {
        char p[1400], lp[1400];
        int fd;
        if (strncmp(e->d_name, stem, sl) || e->d_name[sl] != '-' || !strncmp(e->d_name + sl + 1, key, strlen(key)))
            continue;
        if (strstr(e->d_name, ".lock"))
            continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        snprintf(lp, sizeof lp, "%s.lock", p);
        if ((fd = open(lp, O_RDWR | O_CREAT, 0644)) >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0) {
            unlink(p);
            unlink(lp);
        }
        if (fd >= 0)
            close(fd);
    }
    if (d)
        closedir(d);
}

fel_instance_t *fel_load(const char *path, char *err, unsigned err_n)
{
    char dir[1024], stem[256], key[64], file[1400], lockp[1400];
    const char *base = strrchr(path, '/');
    const fel_core_api_t *api = 0;
    struct stat st;
    void *lib = 0;
    int lock = -1;
    uint32_t s;
    fel_instance_t *in;
    if (err && err_n)
        *err = 0;
    if (stat(path, &st)) {
        say(err, err_n, "fel_load: no core ", path);
        return 0;
    }
    snprintf(stem, sizeof stem, "%s", base ? base + 1 : path);
    if (strstr(stem, LIB_EXT))
        *strstr(stem, LIB_EXT) = 0;
    snprintf(key, sizeof key, "%llx-%llx", (unsigned long long)st.st_size, (unsigned long long)st.st_mtime);
    if (cache_dir(dir, sizeof dir) == 0) {
        sweep(dir, stem, key);
        for (s = 0; s < SLOTS && !api; s++) {          /* a slot no one holds, its copy made the first time */
            snprintf(file, sizeof file, "%s/%s-%s-%u" LIB_EXT, dir, stem, key, s);
            snprintf(lockp, sizeof lockp, "%s.lock", file);
            if ((lock = open(lockp, O_RDWR | O_CREAT, 0644)) < 0)
                break;
            if (flock(lock, LOCK_EX | LOCK_NB) == 0 && (access(file, R_OK) == 0 || copy_file(path, file) == 0))
                api = open_core(file, &lib, err, err_n);
            if (!api) {
                close(lock);
                lock = -1;
                if (err && *err && strstr(err, "ABI"))
                    return 0;
            }
        }
    }
    if (!api) {                                        /* no cache: a temporary copy */
        const char *t = getenv("TMPDIR");
        int fd;
        snprintf(file, sizeof file, "%s/fel-core-XXXXXX" LIB_EXT, t && *t ? t : "/tmp");
        if ((fd = mkstemps(file, (int)strlen(LIB_EXT))) < 0) {
            say(err, err_n, "fel_load: cannot copy ", path);
            return 0;
        }
        close(fd);
        unlink(file);
        if (copy_file(path, file)) {
            say(err, err_n, "fel_load: cannot copy ", path);
            return 0;
        }
        api = open_core(file, &lib, err, err_n);
        unlink(file);                                  /* (the image stays while it is open) */
        if (!api)
            return 0;
    }
    if (!(in = calloc(1, sizeof *in))) {
        dlclose(lib);
        if (lock >= 0)
            close(lock);
        return 0;
    }
    in->lib = lib;
    in->api = api;
    in->lock = lock;
    return in;
}

const fel_core_api_t *fel_api(const fel_instance_t *in) { return in ? in->api : 0; }

void fel_unload(fel_instance_t *in)
{
    if (!in)
        return;
    dlclose(in->lib);
    if (in->lock >= 0)
        close(in->lock);                               /* (the slot free again: its copy stays for the next) */
    free(in);
}
