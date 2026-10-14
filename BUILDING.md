# Building Fel-Tracker (and Felucca)

Fel-Tracker (Tristano Suriani's fork of Felucca by Leo Kuroshita) builds the same way Felucca does, and still under
Felucca's file names (`felucca.bin`, `felucca.fwsc`). The distributions (below) are Fel-Tracker's. Whose work is
whose: [AUTHORS.md](AUTHORS.md).

**One command per target:** `make help` lists them. `make firmware DIST=foundry`, `make emu DIST=chip`,
`make native`, `make plugin`, `make quick` (the fast tests, about a minute), `make test` (the whole suite, its
steps in parallel). The Makefile sets the environment each needs (the venv, `.local-bin`, Emscripten); the scripts
below still work on their own.

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).
- Emscripten (optional, `emcc`: the browser emulator, `web/emu/`).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 1.0` makes a release build: the package identity becomes `FM-1_910`
and the version string `v1.0`; the package is `build/felucca-1.0.fwsc`, and
`build/release-1.0/` holds what a release ships: the package, the app
(`felucca-1.0-app.bin`), `SHA256SUMS`, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/src/usb.c`) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |

`FELUCCA_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/src/usb.c`.

## Distributions

The tree holds every engine (`firmware/src/engine_ids.h`); a **distribution** is the set one build holds, at most 16,
in the order the device lists them: `distributions/<name>.txt`, one engine a line, and optionally `TRACKS 4` or
`TRACKS 8`, the build's track count (4 without the line), and `PALETTE <name>`, the palette a device starts with
(tools/gen_ui_palettes.py: IDM's IDM, FOUNDRY's FOUNDRY; GREY without the line).

```
./build.sh                         # the distribution distributions/BUILD names: IDM (build/felucca.fwsc, "v0.10.0 IDM")
FELUCCA_DIST=foundry ./build.sh    # another one (build/felucca-foundry.fwsc)
```

| Distribution | Tracks | Engines |
| --- | --- | --- |
| `idm` | 4 | BREAKS GLITCH WASP WEST WAVE VECTOR MODAL PARTICLE |
| `foundry` | 4 | WAR ANVIL DARK RAZOR SLAG RUST SCREAM DRILL |
| `warehouse` | 4 | FLOOR BREAKS VENOM DUBCHORD STRINGS HOOVER SANDSTORM RISERS |
| `chip` | 4 | CHIPKIT SID WAVE BYTE ECHOES SPEECH DUBSIREN8 |
| `idm_native` | 4 | the IDM of before 2026-10, PARTICLE and ECHOES as NEBULA and CANYON: SID WASP WAVE BREAKS WEST VECTOR DUBSIREN MODAL DUBCHORD VOSIM NEBULA CHAOS CANYON KIT SPEECH GLITCH (`NATIVE`: the native image's alone, "Native image" below) |
| `ref` | 4 | the host tests' build, never an image: the IDM of before 2026-10 (SID WASP WAVE BREAKS WEST VECTOR DUBSIREN MODAL DUBCHORD VOSIM PARTICLE CHAOS ECHOES KIT SPEECH GLITCH), the engines the shared tests were written for |

Since the 2026-10 plan every build has 4 tracks and the shipped ones at most 8 engines, two for each track's role (kit,
bass, mid, high). The code still builds with 8 tracks (`TRACKS 8`, or the tests' `-DFELUCCA_NTRK=8`): with 8 tracks the places four wide (the tracker's columns, the MIXER, HOME's rows, the GLO and FX layers' cards) show the
selected track's four, 1 .. 4 or 5 .. 8; songs are stream version 3 (a 4-track build writes version 2 and refuses an
8-track song; an 8-track build loads both); the song store and the autosave hold 16 chunks; the SLICER's STUT records
on tracks 1 .. 4 only; DUBSIREN's echo is the last track's, 8 (elsewhere it plays dry); MIDI IN routes in blocks of 8 (CH1-8, SEL, CH9-16).

Felucca's own engines (ANALOG FM6 PHASE LOFI VOICE TRIO WHEEL PHYS NOISE DRUM) and its default distribution are gone
from Fel-Tracker: their ids are retired (`core.h` `EID_RETIRED`), what other engines used of them lives in
`firmware/src/shared/` (PHYS's resonators for MODAL and ANVIL, DRUM's lanes and GM map for KIT, WAR and GLITCH). The
host tests build the primary distribution (`distributions/BUILD`: IDM) with 4 tracks unless they name one.

The engines a distribution leaves out are not linked. A build numbers its engines 0, 1, .. in the distribution's
order (`core.h` `ENGI_*`, `engines.c` `ENGINES[]`), but everything stored and the editor protocol name an engine by its
fixed id (`EID_*`), so a project, a user preset, a song or a backup moves between distributions:

- a sound of an engine the distribution lacks loads as its substitute (`engine_ids.h`), with the substitute's first
  preset (a project's track keeps the rest of its sound); if that one is missing too, down the same chain, else the
  distribution's first engine;
- a user preset of such an engine is kept as it is but listed nowhere and loads nothing (the editor still reads and
  writes it);
- the power-on parts (SID BASS, VOSIM FORMANT PAD, SID SYNC LEAD, KIT FM KIT) whose engine is missing start as the first engine;
- the editor sees the missing engines as "-" (`INFO`), as it does the retired ids.

A new engine: its `eng_<name>.c` (an `ENG_<NAME>` with `.id`), a line in `engine_ids.h` (the next free id, its
substitute; raise `core.h` `EID_COUNT`), an include in `engines.c`, and its name in the distributions that hold it.
The favourites have rows for ids up to 21 (`favorites.c`). The EDIT layer has 16 white keys for engines; INIT is the
white key after them, or with 16 engines the last black key. Then `tools/gen_editor_mock.py build/host/desc.json
web/editor.html` (after the host tests) brings the editor's mock device up to date.

## Browser emulator

```
./build.sh
web/emu/build.sh
python3 -m http.server -d build/emu 8790     # open http://localhost:8790/
```

(A desktop application of the same: "Native image" below.)

`web/emu/build.sh` builds the distribution `distributions/BUILD` names (IDM) into `build/emu/`;
`FELUCCA_DIST=foundry web/emu/build.sh` another one into `build/emu-foundry/` (serve that folder instead). The page
shows the distribution after the version.

The computer's keyboard: the bottom rows play the keys, the number row the buttons, [ ] OCT, ↑ ↓ KNOB 1 (with Shift
SELECT; up a step back, down a step on), ← → ALGORITHM; \ (or the QE AD ZC button) turns Q E, A D, Z C from notes
into KNOB 2, 3, 4 (each pair: down, up) and back. ` (or the Full screen button) shows the device alone, as large
as the screen allows (Escape leaves it); Shift ` (or Big screen) shows the screen large above a smaller panel, on the
page and in full screen. The two choices are remembered in the browser.

MIDI in: a keyboard plugged into the computer plays the emulator (Web MIDI: Chrome, Edge or Firefox, not Safari; the
page on localhost or https). Once switched on, the line under the key hints names the inputs found and blinks on what
they send. MIDI channel 1 plays track 1, 2 track 2 .. (MENU > MIDI > MIDI IN: CH1-8); SEL plays the selected track
from any channel, CH9-16 takes channels 9 .. 16 (a controller's pads, often on 10, then play track 2).

`web/emu/felucca_web.c` builds the firmware's sources as the host tests do, against a simulated FM-1, and
Emscripten compiles it to WebAssembly (`build/emu/felucca.wasm`, with `index.html`, `worklet.js` and the font in `fonts/`). The page
keeps the flash's storage sectors (projects, user presets, settings) in the browser. Not emulated: USB (audio,
MIDI out, the editor, updates) and TRS MIDI. A build of it is on the site:
<https://hugelton.github.io/Felucca/webapp/try/>.

## Native image

```
./build.sh
native/build.sh                # or native/build.sh foundry, warehouse, chip
open "build/native/Fel-Tracker IDM.app"      # or run build/native/fel-tracker-idm
```

Fel-Tracker as a desktop application (`native/felucca_native.c`): the same simulated FM-1 the browser emulator runs
(`web/emu/felucca_web.c`), compiled natively, in a window of its own (SDL2; on macOS linked into the program, so the
`.app` needs nothing installed), the computer's sound out at 44.1 kHz, MIDI in from every source (CoreMIDI on macOS),
its saves in a file (`~/Library/Application Support/Fel-Tracker/<distribution>/flash.bin` on macOS). The window: the
screen at three times its size, the knobs (MASTER the level), the buttons and the
keys (click and hold), lit as the device lights them; a knob turns when dragged up or down, or with the wheel or the
trackpad over it; the computer keyboard as on the emulator page (Esc quits). Needs
SDL2: `brew install sdl2` (Linux: libsdl2-dev). It is the base for what the device cannot hold: heavier engines and
effects run on the computer's CPU and memory, not the FM-1's; a VST built on it is planned.

It is built with `FELUCCA_NATIVE` (`firmware/src/core.h`), which lifts the limits that were the device's. The
device's engines stay exactly as they are (a sound plays as on the FM-1: 0 of their golden renders differ); what the
native image adds is its own:

| | FM-1 (and the browser emulator) | native image |
|---|---|---|
| voices | 12 shared by all the tracks (8 when the CPU is busy); a new note takes one from another track | 16 on every track, its own: nothing taken from another track |
| reverb (FX > REVERB TYPE) | ROOM, SPRING, HALL (8 lines at half the rate), GATED | and CONCERT, CAVERN, SHIMMER (`firmware/src/fx_native.c`, below) |
| delay line | 1.49 s (1/1, 2BAR, 4BAR cut short at most tempos) | 23.8 s: 4BAR down to 41 BPM; PING's right side 11.9 s |
| engines | the distribution's | and the native engines, in a `NATIVE` distribution (`idm_native`): NEBULA, CANYON |
| MENU > INFO CPU | the device's audio interrupt | the computer's audio thread (the FIRMWARE line reads VERSION) |

The reverbs, in floating point at the full rate (some 0.7 MB):

- **CONCERT**: HALL without the device's economies: 16 lines instead of 8, at 44.1 kHz instead of 22.05 (the air up to
  the top), 4 diffusers, every line drifting. SIZE: RT60 0.5 .. 9 s (~4 s at 90, as HALL); DAMP: 16 .. 1.5 kHz.
- **CAVERN**: a cave: a 70 ms gap, the walls' first six echoes (60 .. 330 ms, left and right in turn), then a slow, dark
  tail from lines three times as long. SIZE: RT60 1.5 .. 30 s (~12 s at 90); DAMP: 8 .. 0.9 kHz.
- **SHIMMER**: a long hall whose tail climbs an octave each pass (a pitch shifter in the loop, low-cut at 400 Hz,
  soft-limited). SIZE: RT60 1 .. 15 s (~7 s at 90); DAMP: the lines' top and the shimmer's.

All three send out what HALL does (a burst of noise, the first 2 s, within 0.1 dB at SIZE 90) and end in exact silence.

The native engines (`engine_ids.h` marks them `/* native: */`; `tools/gen_dist.py` lets only a `NATIVE` distribution
list them, and `core.h` refuses a device build of one) share the controls of the engine they grew from, their
substitute, so a sound made with them loads on the FM-1 as that engine:

- **NEBULA** (id 57, PARTICLE's): the cloud whole: 64 grains at once a voice (PARTICLE drops any past 8), 12 voices, the
  chirp followed every 8 samples, the level from the overlap's square root (a dense cloud keeps its loudness). PARTICLE's
  12 presets and 8 of its own: NEBULA, CHOIR CLOUD, OCEAN, FIFTHS HAZE, BELL MIST, SHEPARD SKY, DEEP SWARM, STATIC FIELD.
- **CANYON** (id 58, ECHOES'): up to 64 repeats (COUNT; ECHOES 16, a CANYON sound with more plays 16 there), all of
  them ringing (ECHOES cuts all but 6), 8 voices. ECHOES' 24 presets and 8 of its own: CANYON, BELL CASCADE, PLUCK
  STORM, ENDLESS DUB, LONG BOUNCE, FM RAIN, WHOLE TONE, AVALANCHE.

```
native/build.sh idm_native
open "build/native/Fel-Tracker IDM NATIVE.app"
```

Songs, sounds and presets are stored as on the device: a song made here plays on the FM-1 with the device's voices,
CONCERT, CAVERN and SHIMMER as HALL, NEBULA as PARTICLE, CANYON as ECHOES, and a delay longer than 1.49 s cut short.
`tests/native_test.c` checks all of it in each distribution and prints how fast the computer renders every track full
(about 30 times real time on an M1 Pro).

## Plugin

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

## Tests

```
tests/run_tests.sh
```

Runs the host tests and, with Node.js, the web page tests (and, with Emscripten too, the browser emulator's). Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (SPEECH, the regression renders of each preset), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
