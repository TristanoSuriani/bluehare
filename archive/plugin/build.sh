#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# The plugin: the cores (plugin/build_core.sh), then the plugin by DPF (plugin/src/Makefile), the cores copied into
# each bundle's Contents/Resources/cores.
#   plugin/build.sh  -> build/plugin/Fel-Tracker.vst3, Fel-Tracker.component, Fel-Tracker.clap
#   FEL_ARCH="-arch arm64": one architecture (quicker; default: universal, arm64 and x86_64)
# Needs one ./build.sh before, and plugin/dpf (git submodule update --init --recursive).
set -e
cd "$(dirname "$0")/.."
ROOT=$PWD
OUT=build/plugin
[ -f plugin/dpf/Makefile.plugins.mk ] || { echo "plugin: run git submodule update --init --recursive"; exit 1; }
FEL_CORE_OUT=$OUT/cores sh plugin/build_core.sh
ARCH="${FEL_ARCH:--arch arm64 -arch x86_64}"
MIN="-mmacosx-version-min=10.15"
mkdir -p "$OUT/obj"
if [ "$(cat "$OUT/obj/arch" 2>/dev/null)" != "$ARCH" ]; then   # (other architectures: make would keep the old objects)
    rm -rf "$OUT/obj" "$OUT/core" plugin/dpf/build       # (and ../core's objects, and DPF's own library)
    mkdir -p "$OUT/obj"
    echo "$ARCH" >"$OUT/obj/arch"
fi
# shellcheck disable=SC2086
make -C plugin/src -j8 DPF_BUILD_DIR="$ROOT/$OUT/obj" DPF_TARGET_DIR="$ROOT/$OUT" \
    CFLAGS="$ARCH $MIN" CXXFLAGS="$ARCH $MIN" LDFLAGS="$ARCH $MIN" PKG_CONFIG=/usr/bin/false >"$OUT/make.log" 2>&1 ||
    { tail -30 "$OUT/make.log"; exit 1; }
for b in "$OUT/Fel-Tracker.vst3" "$OUT/Fel-Tracker.component" "$OUT/Fel-Tracker.clap"; do
    plutil -replace CFBundleIdentifier -string "org.fel-tracker.fel-tracker.${b##*.}" "$b/Contents/Info.plist"   # (DPF's
                                                                                 # template's: studio.kx.distrho.*)
    mkdir -p "$b/Contents/Resources/cores"
    cp "$OUT"/cores/fel-core-*.dylib "$b/Contents/Resources/cores/"
    echo "plugin: $b"
done
