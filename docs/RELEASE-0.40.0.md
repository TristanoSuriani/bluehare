# Blue Hare 0.40.0 (beta)

The first public release of **Blue Hare**, an alternative firmware for the **M-VAVE FM-1**: a three-part synthesizer
for live playing from an external sequencer or the FM-1's own keys. It is a fork of
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments), not an official Felucca or
M-VAVE release. The [manual](MANUAL.md) covers everything below.

## What is in it

- **Three tracks, each with its own engines:** the synth (VA, SANDSTORM, HOOVER, SID, OBIE), the drums (KIT, RHYTHM,
  VAKIT, LINNKIT, WAR) and the voices (SPEECH, ONYX, VOCODER, VOSIM, DUBSIREN, RISERS, NOISE). Everything is
  synthesized; 186 factory sounds, 80 slots of your own.
- **MIDI:** an input (USB, TRS or both) and channel per track; follows the sequencer's clock, Start and Stop.
- **REC:** loops of what the sequencer plays, per track.
- **An arpeggiator** per track, **quick layers** (FX, GLO, SCL, EDIT), **effects** (reverbs, delays, chorus, EQ,
  distortion) and **USB audio**.
- **The second processor core** mixes the busiest track (MENU > SYSTEM > CORES).
- **Boot protection** (from ChoralRoot FM-1): an interrupted update comes back in update mode; OCT− held at power-on
  enters it on purpose.
- **MENU > SYSTEM > INFO** shows the CPU load, the cores, and what went wrong since power-on: AUDIO (late, voices
  cut) and MIDI IN (lost).
- **Two games:** HARE CROSSING (SEQ held a second) and PONG (PLAY held a second; one or two players).

## Installing

```
pip install mido python-rtmidi
python3 tools/fm1_install.py bluehare-0.40.0.fwsc
```

Check the download against `SHA256SUMS`. Back to the stock firmware: `python3 tools/fm1_install.py FM-1.fwsc` with
M-VAVE's official package.

## Please note

- **The first install writes the boot protection itself:** for about a second at its start an interruption cannot be
  repaired from the FM-1. Use a direct cable and a charged FM-1. Every install after it is protected.
- **Songs and sounds from Felucca or earlier Blue Hare builds do not load.**
- **The second core runs with the chip's core supply raised** to the maker's levels for its clock (1.35 V and
  1.50 V), as ChoralRoot and Melodee do; never higher. To avoid it, set MENU > SYSTEM > CORES to 1 and restart.
- **Known issue, under investigation:** in a long session sequenced over TRS MIDI with two cores, notes were seen
  missed, cut short, late, or hanging at stop. It has not been seen on one core. If it happens to you, set CORES to 1
  and restart, and please report what INFO showed (CORES, AUDIO, MIDI IN).
- **USB IDs:** the FM-1 presents itself with pid.codes' test IDs (1209:0001); a computer that saw Felucca before may
  still call it "Felucca".
- Installing is at your own risk, as with any alternative firmware.

## Files

- `bluehare-0.40.0.fwsc`: the package to install (identity FM-1_940)
- `bluehare-0.40.0-app.bin`: the application image, for reference
- `SHA256SUMS`, `LICENSE`, `LICENSING.md`, `LICENSES/`: the checksums and the licences (GPL-3.0)
