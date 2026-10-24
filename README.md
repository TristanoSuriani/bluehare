# Blue Hare

**Blue Hare** is an alternative firmware for the **M-VAVE FM-1**: a three-part synthesizer for live playing, made
to be played from an external sequencer or from the FM-1's own keys. It is developed by **Tristano Suriani** and is a
fork of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments). It is an independent
project, not an official Felucca or M-VAVE release.

**Status: beta (0.11).** It runs on the FM-1 and has a broad automatic test suite, but it has been played on few
units so far. Please report what you find in the repository's issues.

- **Manual:** [docs/MANUAL.md](docs/MANUAL.md)
- **Build:** [BUILDING.md](BUILDING.md)
- **Who made what:** [AUTHORS.md](AUTHORS.md)

## What it is

**Three tracks, each with a job and its own engines:**

| Track | Job | Engines |
|---|---|---|
| 1 | the synth | VA (the virtual analogue), SANDSTORM (the trance lead), HOOVER (the rave lead), SID (the C64 chip), OBIE (after the OB-Xa) |
| 2 | the drums | KIT (FM), RHYTHM (the 808 and the 909), VAKIT (synth drums), LINNKIT (the LinnDrum), WAR (industrial) |
| 3 | the voices | SPEECH (the speech chips), ONYX (spoken phrases and sung vowels), VOCODER, VOSIM, DUBSIREN, RISERS (build-ups), NOISE |

Everything is synthesized: there are no samples. 186 factory sounds, 80 slots of your own, favourites and
categories.

**Made for playing live:**

- **MIDI:** each track has its own input (USB, TRS or both) and channel, on GLO > MIDI IN. Blue Hare follows the
  sequencer's MIDI clock, Start and Stop, or its own tempo.
- **REC** records what the sequencer plays into a loop per track (1–16 bars, as played or quantized). A take
  replaces only the tracks that got notes.
- **An arpeggiator** per track (14 modes, hold, swing, probability), with visuals while it runs.
- **Quick layers:** hold FX for performance effects and macros, GLO for mutes, levels and tap tempo, SCL for scales
  and chords, EDIT to switch engines.
- **Effects:** a shared reverb (spring, gated, plate), a delay per track, chorus, EQ and distortion.
- **USB audio:** the FM-1 is an audio input on the computer, carrying Blue Hare's output.

**Under the hood:**

- The synth engines run oversampled in the desktop version and from the chip's fast RAM on the FM-1.
- The FM-1's **second processor core** mixes the busiest track (MENU > SYSTEM > CORES; see the manual).
- **Boot protection** (from ChoralRoot FM-1): an interrupted update brings the FM-1 back in update mode instead of
  leaving it dark, and holding OCT− at power-on enters update mode on purpose.
- A game, HARE CROSSING, behind SEQ held for a second.

## Installing

You need a computer with Python 3 and a USB cable:

```
pip install mido python-rtmidi
python3 tools/fm1_install.py blue-hare-0.11.fwsc
```

The FM-1 restarts with Blue Hare when the installer says `done`. **The first install of 0.11** writes the boot
protection itself: for about a second at its start an interruption cannot be repaired from the FM-1, so use a direct
cable and a charged FM-1. Every install after it is protected.

**Back to the stock firmware:** `python3 tools/fm1_install.py FM-1.fwsc` with M-VAVE's official package.

**Please note:**

- **Your songs and sounds from Felucca or earlier Blue Hare versions do not load.**
- **The second core runs with the chip's core supply raised** to the maker's levels for its clock (1.35 V and
  1.50 V), as in ChoralRoot and Melodee; never higher. If you prefer not to, set MENU > SYSTEM > CORES to 1.
- Installing is at your own risk, as with any alternative firmware.

## Desktop and browser

The same firmware also builds as:

- **A desktop application** (`native/`), with the panel on screen, the computer's sound and MIDI.
- **A browser emulator** (`web/emu/`).
- **A sound design studio** (`web/studio/`) to design sounds on a computer and send them to the FM-1.

See [BUILDING.md](BUILDING.md).

## Credits

Blue Hare stands on others' work:

- **Felucca:** Leo Kuroshita (Hügelton Instruments): the platform, the drivers, the update loader and much of the UI.
- **ChoralRoot FM-1:** its contributors: the boot stub, the second-core support, the code in RAM.
- **Melodee:** Kerem Kilic: the second core's robustness.
- **amb-1 and X0X:** Charles Vestal: the plate reverb, the tape echo, the fast maths.
- **The Kokoro-82M voices** (Apache-2.0) behind ONYX and VOCODER.

Every component and its author: [AUTHORS.md](AUTHORS.md).

## Licence

Free software: [GPL-3.0-only](LICENSE). The bundled fonts, the ported DSP, the voices and the SDK files keep their
own licences ([LICENSES/](LICENSES/)); details in [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Neither Felucca nor Blue Hare is affiliated with or
endorsed by them. "Felucca" is Hügelton Instruments' name for its firmware; Blue Hare is an independent fork.

Felucca: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
Blue Hare's additions and changes: Copyright (C) 2026 Tristano Suriani (see [AUTHORS.md](AUTHORS.md))
