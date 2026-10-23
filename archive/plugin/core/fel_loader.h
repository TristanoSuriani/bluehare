/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* An instance of a core (fel_core.h): a private copy of its library, loaded on its own, so its globals (the whole
 * device: tracks, song, FX, UI) are this instance's alone. Load and unload on the host's main thread. */
#ifndef FEL_LOADER_H
#define FEL_LOADER_H
#include "fel_core.h"

typedef struct fel_instance fel_instance_t;

/* the core library at path (build/plugin/cores/fel-core-<dist>.dylib, or the plugin bundle's), its own copy; 0 and
 * err (a message, err_n bytes) if it cannot be: no file, not a core, another ABI */
fel_instance_t *fel_load(const char *path, char *err, unsigned err_n);
const fel_core_api_t *fel_api(const fel_instance_t *in);
void fel_unload(fel_instance_t *in);

#endif
