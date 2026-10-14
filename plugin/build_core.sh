#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
# Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# The plugin's cores (plugin/core/fel_core.c): one shared library a distribution, the native image's build of it,
# exporting only fel_core_api (plugin/core/fel_core.h). The plugin offers them all (its distribution chooser).
#   plugin/build_core.sh [DIST ..]   (none: idm foundry warehouse idm_native)
#     -> build/plugin/cores/fel-core-<dist>.dylib (.so on Linux; macOS: universal, arm64 and x86_64)
#   FEL_CORE_OUT=DIR: there instead; FEL_ARCH="-arch arm64": one architecture (the tests: quicker)
# Needs one ./build.sh before (build/gen: the tables, the fonts, the distributions' headers).
set -e
cd "$(dirname "$0")/.."
[ -f build/gen/felucca_tables.h ] || { echo "build_core: run ./build.sh once first (build/gen)"; exit 1; }
VERSION=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)
OUT="${FEL_CORE_OUT:-build/plugin/cores}"
mkdir -p "$OUT"
case "$(uname -s)" in
Darwin)
    EXT=dylib
    LINK="-dynamiclib -Wl,-exported_symbol,_fel_core_api -install_name @rpath/fel-core.dylib"
    ARCH="${FEL_ARCH:--arch arm64 -arch x86_64}" ;;   # (universal: Apple silicon and Intel hosts)
*)
    EXT=so
    LINK="-shared -fPIC -Wl,--version-script=plugin/core/fel_core.map"
    ARCH="" ;;
esac
for d in ${*:-idm foundry warehouse idm_native}; do
    [ -f "build/gen/dist_$d.h" ] || { echo "build_core: no distribution $d"; exit 1; }
    # shellcheck disable=SC2086
    ${CC:-cc} -O2 -ffp-contract=off -std=gnu99 -w -fvisibility=hidden $ARCH "-DFELUCCA_VERSION=\"$VERSION\"" \
        "-DFELUCCA_DIST=$d" -Ibuild/gen -Ifirmware/src $LINK -o "$OUT/fel-core-$d.$EXT" plugin/core/fel_core.c -lm
    echo "core: $OUT/fel-core-$d.$EXT"
done
