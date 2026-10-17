# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# One command per target: the scripts underneath, with the environment each needs (the venv's Python with Pillow,
# .local-bin's docker for the firmware, Emscripten for the emulator) set here once.
#   make firmware [DIST=hare_fm1]  the FM-1 image (./build.sh; DIST: distributions/<DIST>.txt, default distributions/BUILD)
#   make firmwares                 every device distribution, the BUILD one last (the target-cost check reads the last build)
#   make emu [DIST=hare_fm1]       the browser emulator (web/emu/build.sh -> build/emu or build/emu-<dist>)
#   make emus                      the emulator of every device distribution
#   make studio [DIST=hare_fm1]    the sound design studio (web/studio/build.sh -> build/studio or build/studio-<dist>)
#   make native [DIST=hare_fm1]    the desktop application (native/build.sh; without DIST every component)
#   make tables                    what is generated from the firmware and the registry: the web editor's mock tables
#                                  (tools/gen_editor_mock.py), AUTHORS.md's components (tools/gen_dist.py --credits)
#   make quick                     the fast tier of the tests (QUICK=1: about a minute)
#   make test [LAYER=..]           the whole suite (NATIVE=0 / SANITIZE=0 leave parts out); LAYER=baseline|os|fm1|native:
#                                  that layer's tests and those of the layers above it (tests/<layer>/steps.sh)
#   make test-changed              the layers the working tree changes (LAYER=auto: the lowest one and above)
#   make test-core                 the suite without the native image (most firmware work)
#   make serve [DIST=hare_fm1]     the emulator on http://localhost:8000
#   make serve-studio [DIST=..]    the studio on http://localhost:8001
SHELL := /bin/bash
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DISTS := hare_fm1
ENV := cd $(ROOT) && { [ ! -f .venv/bin/activate ] || . .venv/bin/activate; } && export PATH="$(ROOT)/.local-bin:$$PATH"
EMSDK ?= $(HOME)/emsdk/emsdk_env.sh
EMENV := { command -v emcc >/dev/null || . $(EMSDK) >/dev/null 2>&1; }
TESTCC := cc -O1 -Wall -Wno-unused-function
EMUDIR = build/emu$(if $(filter-out ,$(DIST)),-$(DIST),)
STUDIODIR = build/studio$(if $(filter-out ,$(DIST)),-$(DIST),)

.PHONY: firmware firmwares emu emus studio native tables quick test test-changed test-core serve serve-studio help
help:
	@sed -n '/^#   make/s/^#   //p' $(MAKEFILE_LIST)

firmware:
	$(ENV) && $(if $(DIST),BLUEHARE_DIST=$(DIST) ,)./build.sh

firmwares:
	$(ENV) && for d in $(DISTS); do BLUEHARE_DIST=$$d ./build.sh || exit 1; done

emu:
	$(ENV) && $(EMENV) && $(if $(DIST),BLUEHARE_DIST=$(DIST) ,)sh web/emu/build.sh

emus:
	$(ENV) && $(EMENV) && for d in $(DISTS); do BLUEHARE_DIST=$$d sh web/emu/build.sh || exit 1; done

studio:
	$(ENV) && $(EMENV) && $(if $(DIST),BLUEHARE_DIST=$(DIST) ,)sh web/studio/build.sh

native:
	$(ENV) && sh native/build.sh $(DIST)

tables:
	$(ENV) && mkdir -p build/host && python3 tools/gen_dist.py . build/gen && \
	  cc -O1 -w -Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1 -o build/host/descdump tests/os/descdump.c -lm && \
	  build/host/descdump > build/host/desc.json && python3 tools/gen_editor_mock.py build/host/desc.json web/editor.html && \
	  python3 tools/gen_dist.py --credits .

quick:
	$(ENV) && QUICK=1 CC="$(TESTCC)" sh tests/run_tests.sh

test:
	$(ENV) && $(EMENV); LAYER="$(or $(LAYER),all)" CC="$(TESTCC)" sh tests/run_tests.sh

test-changed:
	$(ENV) && $(EMENV); LAYER=auto CC="$(TESTCC)" sh tests/run_tests.sh

test-core:
	$(ENV) && $(EMENV); NATIVE=0 CC="$(TESTCC)" sh tests/run_tests.sh

serve:
	cd $(ROOT)/$(EMUDIR) && python3 -m http.server 8000

serve-studio:
	cd $(ROOT)/$(STUDIODIR) && python3 -m http.server 8001
