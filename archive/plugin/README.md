# The plugin (archived)

Archived 2026-10: it is not clear that the plugin works properly. Nothing here is built or tested any more. To bring
it back: `git mv archive/plugin plugin`, the four tests from `archive/plugin/tests/` back to `tests/`, and the plugin
steps, `make plugin` / `make plugin-install` back from git history (`tests/run_tests.sh`, `Makefile`, before this
archive's commit). The build scripts here still name the paths as they were (`plugin/...`).

What it was, from BUILDING.md:


Fel-Tracker as a VST3 / AU / CLAP plugin for Ableton Live and Renoise. Its core:

```
./build.sh
plugin/build_core.sh           # -> build/plugin/cores/fel-core-<dist>.dylib, one a distribution (universal on macOS)
```

A core (`plugin/core/fel_core.c`) is one distribution's native-image build (16 voices a track, the native reverbs,
the native engines in IDM NATIVE) as a shared library exporting a single table, `fel_core_api()`
(`plugin/core/fel_core.h`): the flash, boot, render (44.1 kHz, any number of frames), MIDI, the panel's buttons, keys
and knobs, the screen and the LEDs. The plugin will offer all four (its distribution chooser).

The firmware keeps its state in globals, so an instance is a library of its own (`plugin/core/fel_loader.c`): each
opens a copy no other instance holds, its globals its own. The copies are kept in `~/Library/Caches/Fel-Tracker/cores`
and reused, a lock on each while an instance has it: a Mac checks a new executable file the first time it opens it
(some 0.2 s, more with an endpoint scanner), a known one opens at once. `tests/plugin_core_test.c`: a loaded core
plays the native image's build sample for sample (`tests/plugin_core_ref.c`); two instances rendered in turn, or on
two threads at once, each play exactly what they play alone; their flash is their own; the four distributions load
side by side; the copies are reused and replaced when the core is rebuilt.

The plugin itself (`plugin/src`), built by DPF (`plugin/dpf`, a git submodule: `git submodule update --init --recursive`):

```
plugin/build.sh                # -> build/plugin/Fel-Tracker.vst3, .component (AU), .clap; universal on macOS
plugin/get_pluginval.sh        # (once) pluginval, which tests/run_tests.sh then runs on the VST3
```

An instrument: MIDI in, 18 outputs (the main pair, then a pair a track: nine stereo buses, "Main", "Track 1" ..; the
AU one bus of 18, the main its first pair). Its parameters: MASTER (the device's pot); DISTRIBUTION, the chooser (IDM
NATIVE first, then IDM, FOUNDRY, WAREHOUSE: the cores found in the bundle's `Contents/Resources/cores`; not
automatable); TRACK 1 .. 8 OUTPUT, MAIN or OWN: a track on its own pair leaves the main mix, dry, at the level it had
there (its sends still feed the reverb, delay and chorus, which stay on the main with the bus compressor and the
limiter). A distribution chosen is loaded by a thread of the plugin's own and swapped in between two blocks.

It follows the host's transport, always: its tempo (the delay, the arp, the synced LFOs and the SLICER with it), play
and stop, and its place (seq.c host_block: the sequencer moved to the host's beat every block, sample exact; put
there anew when the host starts or jumps, a row that has just begun played at once). The song starts where PLAY
starts it on the device (LOOP: the pattern from row 0; SONG: the position picked) at the host's beat 0, round and
round; the panel's PLAY shows the transport and does not change it. The state holds the distribution and the song:
the device's flash (the song, the user presets, the settings), written at once when the host saves (the device's
autosave, without waiting for it to be quiet), compressed.

`plugin/src/fel_host.c` is the plugin apart from the format. The core runs at 44.1 kHz; at another host rate
`plugin/src/fel_resample.c` resamples it (a 96-tap Kaiser-windowed sinc, flat to 19 kHz, images and aliases under
-80 dB), the input pulled 48 core frames ahead: the latency reported to the host (52 frames at 48 kHz), the
transport's beat moved by it so the sequencer lands where the host's timeline has it. A row plays in the device's
block of 32 samples it starts in (0 .. 0.7 ms early, as on the FM-1).
`tests/plugin_host_test.c` checks the resampler, the host, the transport (a 1/16 grid at 120 BPM, 60 s at 128 BPM and
48 kHz without drift, a mid-row start, a loop), the track outputs (a track on its pair + the main = the mix) and the
state (saved, loaded into another instance, played); `tests/plugin_clap_test.c` loads the built CLAP as a host does;
pluginval (strictness 8) the VST3.

Its window (`plugin/src/FelTrackerUI.cpp`, DPF's NanoVG): the device's screen at twice its size, the knobs (dragged
up or down, or the wheel over one; MASTER is the parameter), the buttons and the keys (held while the mouse button is
down), lit as the device lights them; above them the distribution (a click: the chooser, then a question; the song goes
with it, its sounds of engines the new one lacks on their substitutes) and the eight tracks' outputs (M main, O own).
It reads a copy of the screen and the LEDs the audio thread keeps (`fel_host_view`) and hands the device the panel
through a queue the audio thread plays (`fel_host_ui_*`, each button or key change held 24 ms of the device's time, as
the native image does). `FEL_UI_SHOT=FILE.ppm` writes its first frame with the screen to a file (a look without a
screen capture: `make -C plugin/src jack` builds it as a standalone app).

Installing it on this Mac (the user's plug-in folders, no administrator needed):

```
plugin/install.sh              # build (universal), sign ad hoc, install VST3, AU and CLAP, run auval
plugin/install.sh --no-build   # install what build/plugin holds
plugin/install.sh --remove     # take them out again
```

Then rescan the plug-ins in the host (Live: Settings > Plug-Ins > Rescan; Renoise: Preferences > Plug/Misc >
Rescan). The bundles are signed ad hoc (no developer certificate): this Mac loads them; another Mac may ask for them to
be allowed (System Settings > Privacy & Security). Checked: auval on the AU, pluginval (strictness 8, its editor
tests too) on the VST3 and the AU, the Intel half under Rosetta (pluginval, strictness 5).

