# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
# Blue Hare OS's tests (2026-10-17, the architecture's step 8): firmware/os (the UI, the keys, storage, presets,
# projects, the editor protocol) and what is tested through it (the sound tests built on ui_test.c or editor_test.c);
# the web editor's pages. Sourced by tests/run_tests.sh; a change here runs FM1's and Native's too.
compile $CC -o "$OUT/storage_test" tests/os/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
compile $CC -Ibuild/gen -o "$OUT/upreset_test" tests/os/upreset_test.c   # (core.h: build/gen/dist_default.h)
run "user presets (UP_PUT parser, bank round trip, versions, grid records, retired engines refused)" "$OUT/upreset_test"
compile $CC -o "$OUT/input_test" tests/os/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders" "$OUT/input_test"
if [ ! -f build/gen/bluehare_tables.h ]; then
    echo "== skip the OS's tests on the firmware's sources (run ./build.sh once)"
    return 0 2>/dev/null || exit 0
fi
I="-Ibuild/gen -Ifirmware/baseline -Ifirmware/os -Ifirmware/fm1"
compile $CC -w $I -o "$OUT/chord_test" tests/os/chord_test.c -lm
run "chord keys: diatonic and fixed chords, voicings, MONO root, releases, MIDI IN" "$OUT/chord_test"
compile $CC -O1 -w $I -o "$OUT/lfo2_test" tests/os/lfo2_test.c -lm
run "1.2 LFO 2: SYNC / TRIG / POL" "$OUT/lfo2_test"
compile $CC -O1 -w $I -o "$OUT/midi_control_test" tests/os/midi_control_test.c -lm
run "USB/TRS clock, bend, sustain, ownership and panic recovery" "$OUT/midi_control_test"
compile $CC -O1 -w $I -o "$OUT/theme_test" tests/os/theme_test.c -lm
run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
compile $CC -O1 -w $I -o "$OUT/text_ref_test" tests/os/text_ref_test.c -lm
run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
if python3 -c "import PIL" 2>/dev/null; then
    run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.25 px, L <= 0.5 px" \
        python3 tests/os/text_spacing_test.py build/gen/ui_fonts.h
fi
compile $CC -O1 -w -Ibuild/gen -o "$OUT/settings_test" tests/os/settings_test.c
run "settings: PER1..PER4 migration, palette ids and preference preservation" "$OUT/settings_test"
compile $CC -w $I -o "$OUT/ui_test" tests/os/ui_test.c -lm
run "UI: sounds, undo, MIDI overflow, pending saves, panel recovery, SEQ's placeholder, GREY gray, MONO neutral, retired engines, GEN's ocean" "$OUT/ui_test"
FV=$(sed -n 's/^#define BLUEHARE_VERSION "\([^"]*\)".*/\1/p' firmware/fm1/bluehare.c)   # (the splash, ABOUT: the real version)
slow compile $CC -O1 -w "-DBLUEHARE_VERSION=\"$FV\"" $I -Itests/os -o "$OUT/ui_render" tests/os/ui_render.c -lm
mkdir -p build/ui_new/ppm build/ui_slot
slow run "UI renders: layout lint (every screen and palette, every page, engine and column value), GREY gray, MONO neutral, alignment by ink (1 px fails), draw cost" \
    sh -c '"$1" build/ui_new build/ui_slot && { ! python3 -c "import PIL" 2>/dev/null || python3 tests/os/ui_render.py build/ui_new build/ui_slot; }' _ "$OUT/ui_render"
compile $CC -w -DBLUEHARE_NTRK=8 $I -o "$OUT/ui_test8" tests/os/ui_test.c -lm
run "UI built with 8 tracks (step 3c): every check above, the view 1..4 / 5..8, GLO / FX on 8 tracks, MIDI IN blocks of 8" "$OUT/ui_test8"
slow compile $CC -O1 -w "-DBLUEHARE_VERSION=\"$FV\"" -DBLUEHARE_NTRK=8 $I -Itests/os -o "$OUT/ui_render8" tests/os/ui_render.c -lm
mkdir -p build/ui_8/ppm build/ui_8slot
slow run "UI renders built with 8 tracks (step 3c): the layout lint on every screen" "$OUT/ui_render8" build/ui_8 build/ui_8slot
compile $CC -w $I -o "$OUT/persistence_test" tests/os/persistence_test.c -lm
run "persistence: deferred settings, retry and failed-save rollback" "$OUT/persistence_test"
compile $CC -w -DBLUEHARE_NTRK=8 $I -o "$OUT/persistence_test8" tests/os/persistence_test.c -lm
run "persistence built with 8 tracks (step 3b): the song store and the autosave, cut anywhere" "$OUT/persistence_test8"
compile $CC -w $I -o "$OUT/backup_test" tests/os/backup_test.c -lm
run "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects, older projects" "$OUT/backup_test"
compile $CC -w $I -o "$OUT/editor_test" tests/os/editor_test.c -lm
run "editor: real C protocol, malformed transfers, queue recovery, MENU settings (writes build/host/menu.json)" \
    env MENU_JSON="$OUT/menu.json" "$OUT/editor_test"
EDITOR_JOB=$nj
compile $CC -w $I -o "$OUT/robust_test" tests/os/robust_test.c -lm
run "robustness: engine ids, retained old projects, preset patterns, malformed requests" "$OUT/robust_test"
for n in 4 8; do
    compile $CC -w -DBLUEHARE_NTRK=$n $I -o "$OUT/width_test$n" tests/os/width_test.c -lm
    run "LOW CUT ($n tracks): the power-on values, the cut, silence after it, the store (WIDTH removed); the layers' store" "$OUT/width_test$n"
done
compile $CC -w $I -o "$OUT/panmode_test" tests/os/panmode_test.c -lm
run "PAN MODE: ALT at power-on (SPREAD in turn), RND a side at random (POLY, and MONO at half width), the store" "$OUT/panmode_test"
compile $CC -w -DBLUEHARE_DIST=test_subset -DBLUEHARE_NTRK=4 $I -o "$OUT/engines_test4" tests/os/engines_test.c -lm
run "a build without some engines and PLATE (TEST SUBSET, 4 tracks): ids, substitutes, components, protocol, projects, presets; a 4-track project for the next" "$OUT/engines_test4"
compile $CC -w -DBLUEHARE_DIST=test_subset -DBLUEHARE_NTRK=8 $I -o "$OUT/engines_test" tests/os/engines_test.c -lm
AFTER=$nj                                           # (it reads the 4-track run's song_4trk.bin)
run "a build without some engines, 8 tracks: the same checks, its power-on, a 4-track project on it" "$OUT/engines_test"
$CC -w $I -o "$OUT/descdump" tests/os/descdump.c -lm
echo "== parameter and engine tables as JSON (for the editor mock test)"
"$OUT/descdump" > "$OUT/desc.json" || fail=1

# ASan + UBSan: the stored-data and protocol paths, and short fuzz runs (fixed seeds: the same inputs every run)
if san_wanted; then
    for t in editor_test backup_test persistence_test robust_test; do
        $SCC -o "$A/$t" tests/os/$t.c -lm
        run "ASan/UBSan: $t" "$A/$t"
    done
    $SCC -o "$A/fuzz_ed" tests/os/fuzz_ed.c -lm
    run "ASan/UBSan fuzz: editor SysEx and raw USB-MIDI packets (20000, seed 7)" "$A/fuzz_ed" 20000 7
    $SCC -o "$A/fuzz_proj" tests/os/fuzz_proj.c -lm
    run "ASan/UBSan fuzz: song stores mutated -> find -> load -> play -> render (2000, seed 13)" "$A/fuzz_proj" 2000 13
fi

if command -v node >/dev/null 2>&1; then
    AFTER=${EDITOR_JOB:-}                               # (the web tests read the editor test's menu.json)
    run "web pages: editor protocol + samples, package builder, update protocol" node web/test_web.mjs
    run "web backup: capture, validation before writes, restore order" node web/test_backup.mjs
else
    echo "== skip web tests (no node)"
fi
