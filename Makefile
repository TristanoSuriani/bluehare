# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# One command per target: the scripts underneath, with the environment each needs (the venv's Python with Pillow,
# .local-bin's docker for the firmware, Emscripten for the emulator) set here once.
#   make firmware [DIST=foundry]   the FM-1 image (./build.sh; DIST: distributions/<DIST>.txt, default distributions/BUILD)
#   make firmwares                 every device distribution, IDM last (the target-cost check reads the last build)
#   make emu [DIST=chip]           the browser emulator (web/emu/build.sh -> build/emu or build/emu-<dist>)
#   make emus                      the emulator of every device distribution
#   make native [DIST=idm_native]  the desktop application (native/build.sh)
#   make quick                     the fast tier of the tests (QUICK=1: about a minute)
#   make test                      the whole suite (NATIVE=0 / SANITIZE=0 leave parts out)
#   make test-core                 the suite without the native image (most firmware work)
#   make serve [DIST=foundry]      the emulator on http://localhost:8000
SHELL := /bin/bash
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DISTS := idm foundry warehouse chip
ENV := cd $(ROOT) && { [ ! -f .venv/bin/activate ] || . .venv/bin/activate; } && export PATH="$(ROOT)/.local-bin:$$PATH"
EMSDK ?= $(HOME)/emsdk/emsdk_env.sh
EMENV := { command -v emcc >/dev/null || . $(EMSDK) >/dev/null 2>&1; }
TESTCC := cc -O1 -Wall -Wno-unused-function
EMUDIR = build/emu$(if $(filter-out ,$(DIST)),-$(DIST),)

.PHONY: firmware firmwares emu emus native quick test test-core serve help
help:
	@sed -n '/^#   make/s/^#   //p' $(MAKEFILE_LIST)

firmware:
	$(ENV) && $(if $(DIST),FELUCCA_DIST=$(DIST) ,)./build.sh

firmwares:
	$(ENV) && for d in $(filter-out idm,$(DISTS)) idm; do FELUCCA_DIST=$$d ./build.sh || exit 1; done

emu:
	$(ENV) && $(EMENV) && $(if $(DIST),FELUCCA_DIST=$(DIST) ,)sh web/emu/build.sh

emus:
	$(ENV) && $(EMENV) && for d in $(DISTS); do FELUCCA_DIST=$$d sh web/emu/build.sh || exit 1; done

native:
	$(ENV) && sh native/build.sh $(DIST)

quick:
	$(ENV) && QUICK=1 CC="$(TESTCC)" sh tests/run_tests.sh

test:
	$(ENV) && $(EMENV); CC="$(TESTCC)" sh tests/run_tests.sh

test-core:
	$(ENV) && $(EMENV); NATIVE=0 CC="$(TESTCC)" sh tests/run_tests.sh

serve:
	cd $(ROOT)/$(EMUDIR) && python3 -m http.server 8000
