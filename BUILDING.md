# Building Blue Hare (and Felucca)

Blue Hare (Tristano Suriani's fork of Felucca by Leo Kuroshita) builds the same way Felucca does, and still under
Felucca's file names (`bluehare.bin`, `bluehare.fwsc`). The distributions (below) are Blue Hare's. Whose work is
whose: [AUTHORS.md](AUTHORS.md).

**One command per target:** `make help` lists them. `make firmware`, `make emu`,
`make native`, `make quick` (the fast tests, about a minute), `make test` (the whole suite, its
steps in parallel). The Makefile sets the environment each needs (the venv, `.local-bin`, Emscripten); the scripts
below still work on their own.

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `bluehare.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `bluehare.fwsc` | the installable package (app + loader) |

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
- Emscripten (optional, `emcc`: the browser emulator, `web/emu/`, and the sound design studio, `web/studio/`).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 1.0` makes a release build: the package identity becomes `FM-1_910`
and the version string `v1.0`; the package is `build/bluehare-1.0.fwsc`, and
`build/release-1.0/` holds what a release ships: the package, the app
(`bluehare-1.0-app.bin`), `SHA256SUMS`, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

Build options (environment, `0` or `1`; defaults in `firmware/src/bluehare.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `BLUEHARE_FLASH` | 1 | settings, presets and projects in flash |
| `BLUEHARE_OTA` | 1 | update entry (needs `BLUEHARE_FLASH`) |
| `BLUEHARE_CDC` | 1 | USB serial console |
| `BLUEHARE_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `BLUEHARE_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `BLUEHARE_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/src/usb.c`) |
| `BLUEHARE_UART` | 1 | TRS MIDI IN |
| `BLUEHARE_ICONS` | 1 | parameter icons on the knob cards |

`BLUEHARE_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/src/usb.c`.

## Distributions

The tree holds every engine (`firmware/src/engine_ids.h`); a **distribution** is the set one build holds, at most 16,
in the order the device lists them: `distributions/<name>.txt`, one engine a line, and optionally `TRACKS 4` or
`TRACKS 8`, the build's track count (4 without the line), and `PALETTE <name>`, the palette a device starts with
(tools/gen_ui_palettes.py; GREY without the line).

```
./build.sh                          # the distribution distributions/BUILD names: HARE FM1 (build/bluehare.fwsc, "v0.10.0 HARE FM1")
BLUEHARE_DIST=<name> ./build.sh      # another one (build/bluehare-<name>.fwsc)
```

| Distribution | Tracks | Engines |
| --- | --- | --- |
| `hare_fm1` | 4 | SID WAVE BLISS WEST MODAL ECHOES: Blue Hare's one distribution (2026-10-16, Milestone 1), palette ICE |
| `hare_native` | 4 | HARE FM1 with ECHOES as its native engine CANYON (`NATIVE`: the native image's alone, "Native image" below) |
| `test_subset` | 4 | the host tests' build without some engines, never an image: SID WAVE WEST MODAL (`tests/engines_test.c`) |

*(2026-10-16)* Blue Hare builds one distribution; the infrastructure for several stays (a new `distributions/*.txt` is
a new build). The earlier ones (IDM, FOUNDRY, WAREHOUSE, CHIP, IDM NATIVE, REF) are in `archive/distributions/`, with
the engines only they held in `archive/engines/` (its README says how one comes back). The host tests build the
primary distribution (`distributions/BUILD`: HARE FM1) with 4 tracks unless they name one.

`VOICES 12` keeps the voice budget at 12 always; without it 8, and 12 while the audio's load stays under 75 % (HARE
FM1). The code still builds with 8 tracks (`TRACKS 8`, or the tests' `-DBLUEHARE_NTRK=8`): the places four wide (the
MIXER, HOME's rows, the GLO and FX layers' cards) show the selected track's four, 1 .. 4 or 5 .. 8; projects hold 16
chunks; MIDI IN routes in blocks of 8 (CH1-8, SEL, CH9-16).

Felucca's own engines (ANALOG FM6 PHASE LOFI VOICE TRIO WHEEL PHYS NOISE DRUM) and its default distribution are gone
from Blue Hare: their ids are retired (`core.h` `EID_RETIRED`, which the archived engines' ids join); what other
engines used of them lives in `firmware/src/shared/` (PHYS's resonators for MODAL).

The engines a distribution leaves out are not linked. A build numbers its engines 0, 1, .. in the distribution's
order (`core.h` `ENGI_*`, `engines.c` `ENGINES[]`), but everything stored and the editor protocol name an engine by its
fixed id (`EID_*`), so a project, a user preset, a song or a backup moves between distributions:

- a sound of an engine the distribution lacks loads as its substitute (`engine_ids.h`), with the substitute's first
  preset (a project's track keeps the rest of its sound); if that one is missing too, down the same chain, else the
  distribution's first engine;
- a user preset of such an engine is kept as it is but listed nowhere and loads nothing (the editor still reads and
  writes it);
- the power-on parts (WAVE's WARM PAD, SID BASS, BLISS's E.PIANO, MODAL's KALIMBA) whose engine is missing start as
  their second pick, else the first engine;
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

`web/emu/build.sh` builds the distribution `distributions/BUILD` names (HARE FM1) into `build/emu/`;
`BLUEHARE_DIST=<name> web/emu/build.sh` another one into `build/emu-<name>/` (serve that folder instead). The page
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

`web/emu/bluehare_web.c` builds the firmware's sources as the host tests do, against a simulated FM-1, and
Emscripten compiles it to WebAssembly (`build/emu/bluehare.wasm`, with `index.html`, `worklet.js` and the font in `fonts/`). The page
keeps the flash's storage sectors (projects, user presets, settings) in the browser. Not emulated: USB (audio,
MIDI out, the editor, updates) and TRS MIDI. A build of it is on the site:
<https://hugelton.github.io/Felucca/webapp/try/>.

## Sound design studio

```
./build.sh
make studio                    # or web/studio/build.sh
make serve-studio              # open http://localhost:8001/
```

The studio (`web/studio/`, Milestone 1) designs sounds with the firmware's own engines and FX chain: the browser
emulator's build (`web/emu/bluehare_web.c`) with the studio's exports (`web/studio/studio_web.c`: the engines and
presets, every parameter of the selected track and of the FX chain, described, set and shown as the device shows
them, the user preset slots), in the emulator's worklet (`web/emu/worklet.js`, its "call" messages). The page
(`index.html`, `studio.js`) has the engines, their presets by category and the sounds saved here; the parameters
in groups (the engine, its envelope first; LFO, mod matrix, voice and key folded); every effect of
`docs/SoundAndDSP.md` section 3 (the track's inserts DIST, LOW CUT, SPREAD, PAN MODE, WIDTH; its sends; the shared
delay, reverb and chorus; the performance layer's hold effects, latched or held, and its macros; the master's SPEAKER
EQ and MASTER, its always-on stages with the compressor's and the limiter's gain reduction); 4 tracks; the keys (mouse, computer keys A W S E .., Z X octave, Esc all off, Hold), Web MIDI in to the
selected track; undo and redo of every change (Ctrl/⌘ Z, Ctrl/⌘ ⇧ Z); a scope and a peak meter; a WAV recording
of what plays; the FM-1's screen. A sound goes to a slot here (kept in the browser, as the emulator keeps its
flash), to a file (`.bluehare-sound.json`: the engine by name and id, every parameter by its id, as user presets
and the editor protocol number them) or to a connected FM-1 (Connect, then Send: the editor protocol's `UP_PUT`,
`web/studio/sysex.js`; Chrome or Edge, Blue Hare on the FM-1 with the same parameter count). The FX chain goes to a
file of its own (`.bluehare-fx.json`). `web/studio/studio_test.mjs` (in the suite with Emscripten) checks it all
through the worklet, and the page's `UP_PUT` against the firmware's own parser.

## Native image

```
./build.sh
native/build.sh                # or native/build.sh hare_native
open "build/native/Blue Hare HARE FM1.app" # or run build/native/blue-hare-hare_fm1
```

Blue Hare as a desktop application (`native/bluehare_native.c`): the same simulated FM-1 the browser emulator runs
(`web/emu/bluehare_web.c`), compiled natively, in a window of its own (SDL2; on macOS linked into the program, so the
`.app` needs nothing installed), the computer's sound out at 44.1 kHz, MIDI in from every source (CoreMIDI on macOS),
its saves in a file (`~/Library/Application Support/Blue Hare/<distribution>/flash.bin` on macOS). The window: the
screen at three times its size, the knobs (MASTER the level), the buttons and the
keys (click and hold), lit as the device lights them; a knob turns when dragged up or down, or with the wheel or the
trackpad over it; the computer keyboard as on the emulator page (Esc quits). Needs
SDL2: `brew install sdl2` (Linux: libsdl2-dev). It is the base for what the device cannot hold: heavier engines and
effects run on the computer's CPU and memory, not the FM-1's; a VST built on it is planned.

It is built with `BLUEHARE_NATIVE` (`firmware/src/core.h`), which lifts the limits that were the device's. The
device's engines stay exactly as they are (a sound plays as on the FM-1: 0 of their golden renders differ); what the
native image adds is its own:

| | FM-1 (and the browser emulator) | native image |
|---|---|---|
| voices | 12 shared by all the tracks (8 when the CPU is busy); a new note takes one from another track | 16 on every track, its own: nothing taken from another track |
| reverb (FX > REVERB TYPE) | ROOM, SPRING, HALL (8 lines at half the rate), GATED | and CONCERT, CAVERN, SHIMMER (`firmware/src/fx_native.c`, below) |
| delay line | 1.49 s (1/1, 2BAR, 4BAR cut short at most tempos) | 23.8 s: 4BAR down to 41 BPM; PING's right side 11.9 s |
| engines | the distribution's | and the native engine, in a `NATIVE` distribution (`hare_native`): CANYON |
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

- **CANYON** (id 58, ECHOES'): up to 64 repeats (COUNT; ECHOES 16, a CANYON sound with more plays 16 there), all of
  them ringing (ECHOES cuts all but 6), 8 voices. ECHOES' 24 presets and 8 of its own: CANYON, BELL CASCADE, PLUCK
  STORM, ENDLESS DUB, LONG BOUNCE, FM RAIN, WHOLE TONE, AVALANCHE.

```
native/build.sh hare_native
open "build/native/Blue Hare HARE NATIVE.app"
```

Songs, sounds and presets are stored as on the device: a song made here plays on the FM-1 with the device's voices,
CONCERT, CAVERN and SHIMMER as HALL, CANYON as ECHOES, and a delay longer than 1.49 s cut short.
`tests/native_test.c` checks all of it in each distribution and prints how fast the computer renders every track full
(about 30 times real time on an M1 Pro).

## Plugin (archived)

The VST3 / AU / CLAP plugin is archived in [archive/plugin](archive/plugin/README.md) (2026-10: it is not clear it
works properly). It is out of the build and the tests; its build notes are kept there. The firmware's host side
(`firmware/src/target_host.c`: a DAW's transport and tempo, the tracks' own outputs) stays, shared with the native
image.

## Tests

```
tests/run_tests.sh
```

Runs the host tests and, with Node.js, the web page tests (and, with Emscripten too, the browser emulator's). Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (the regression renders of each preset), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
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
python3 tools/fm1_install.py build/bluehare.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/bluehare.fwsc dev /tmp/bluehare-site
cd /tmp/bluehare-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
