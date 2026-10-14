#!/usr/bin/make -f
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# The plugin, built by DPF (plugin/dpf): VST3, AU, CLAP. plugin/build.sh runs it (and puts the cores in the bundles).
#   make -C plugin/src DPF_BUILD_DIR=.. DPF_TARGET_DIR=..   (both absolute)

NAME = Fel-Tracker

FILES_DSP = \
	FelTrackerPlugin.cpp \
	fel_host.c \
	fel_resample.c \
	../core/fel_loader.c

include ../dpf/Makefile.plugins.mk

BUILD_C_FLAGS += -std=gnu11

TARGETS += vst3
TARGETS += au
TARGETS += clap

all: $(TARGETS)
