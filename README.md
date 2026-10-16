# Blue Hare

**Blue Hare** is a fork of [Felucca](https://github.com/hugelton/Felucca), the multi-engine synthesizer firmware
for the M-VAVE FM-1 by Leo Kuroshita ([Hügelton Instruments](https://hugelton.com)). It is developed by
**Tristano Suriani** (<https://github.com/TristanoSuriani/fel-tracker>). It is an independent fork, not an official
Felucca release. Version **0.10.0** (its own numbering; it grew from Felucca 1.4.1). On the device, the editor and
the browser emulator it introduces itself as Blue Hare, naming Felucca and its makers as what it is built on.

What Blue Hare adds, all by Tristano Suriani:

- **The sequencer:** a placeholder for now (SEQ shows TODO). Felucca's step sequencer is gone, and so is the tracker
  that took its place until 2026-10.
- **One distribution, HARE FM1** *(2026-10-16)*: the base synthesizer engines SID, SH101, WAVE, BLISS, WEST, MODAL and
  ECHOES on **4 tracks** (at power-on WAVE's WARM PAD and SH101's 101 BASS, centred; BLISS's E.PIANO on the left and MODAL's
  KALIMBA on the right). The build still takes any number of distributions of up to 16 engines, with stable engine
  ids and a palette each (HARE FM1: ICE); the earlier ones are archived (BUILDING.md "Distributions").
- **Blue Hare's engines:** of the twenty-four Blue Hare wrote, SID, SH101, WAVE, BLISS, WEST, MODAL and ECHOES (and
  CANYON in the native image) are in the build; the others are in `archive/engines/`. **SH101** *(2026-10)*: saw, pulse
  with PWM, a locked sub-oscillator and noise into a self-oscillating 24 dB ladder.
- **A synthesizer, not a groovebox** *(2026-10-16)*: the SLICER, DUCK and the FX layer's REPEAT, REVERSE and TAPE STOP
  are archived; the design and the plan are in [docs/](docs/) (DesignPrinciples.md, Milestones.md, SoundAndDSP.md).
- **The sound design studio** *(2026-10-16)*: the firmware's engines and FX chain in the browser, to design sounds,
  try them on 4 tracks with every effect (track inserts, the shared buses, the performance layer, the master) and
  save them to a file, to the studio's slots or straight to the FM-1's user slots
  (`web/studio/`, `make studio`, BUILDING.md "Sound design studio").

Everything else is Felucca, Leo Kuroshita's work. [AUTHORS.md](AUTHORS.md) says in detail whose work is whose. Below,
Blue Hare's parts are marked *(Blue Hare)*.

The rest of this README started as Felucca's. Its install links, site, sponsorship and issue tracker are Felucca's
(Hügelton Instruments). They install official Felucca, not Blue Hare; to build Blue Hare, see
[BUILDING.md](BUILDING.md).

---

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)
[![Sponsor](https://img.shields.io/badge/Sponsor-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/hugelton)

**TL;DR:** Felucca 1.4.1 — 1.4 plus DRUM lane mutes on the EDIT layer and an FM6 voice bank, field testing. Connect your FM-1 to a computer by USB,
open the [web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge, and press Install;
no extra hardware is needed. Installing is at your own risk: M-VAVE's updater or the installer's
**Return to official V15** takes you back. Coming from 1.1.5.x? Read [⚠ Coming from 1.1.5](#-coming-from-115) first:
projects saved by 1.4 or 1.4.1 do not open on 1.1.5.x. Want to look around first?
[Try it in your browser](https://hugelton.github.io/Felucca/webapp/try/), no FM-1 needed.

Multi-engine synthesizer firmware for the M-VAVE FM-1. Please report what you find in
[Issues](https://github.com/hugelton/Felucca/issues).

- Install: [web installer](https://hugelton.github.io/Felucca/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Try: [Felucca in your browser](https://hugelton.github.io/Felucca/webapp/try/): the same firmware compiled to
  WebAssembly, with the panel on screen (mouse, touch, computer keyboard, Web MIDI in)
- Editor: `web/editor.html` in this repository (its Sequencer tab says TODO with this firmware); the
  [web editor](https://hugelton.github.io/Felucca/webapp/editor/) on the site
  ([Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp)) edits the sounds
- Build: [BUILDING.md](BUILDING.md)

<a href="https://hugelton.itch.io/felucca"><img src="https://static.itch.io/images/badge-color.svg" alt="Available on itch.io" width="74"></a>

## Features

- **Native image** *(Blue Hare)*: Blue Hare as a desktop application for each distribution (`native/build.sh`:
  `Blue Hare HARE FM1.app` on macOS), the same firmware in a window with the panel, the computer's sound out and MIDI in,
  its saves in a file: the base for heavier engines and effects than the FM-1 can hold. Every track has its own 16
  voices there, three more reverbs (CONCERT: HALL at the full rate and twice the lines; CAVERN: a cave's echoes and
  a 30 s tail; SHIMMER: a tail climbing in octaves), a 23.8 s delay line, and a distribution of its own, HARE NATIVE,
  with the native engine CANYON (BUILDING.md "Native image")

- **Six engines** in HARE FM1 (below), each with its own factory presets; every factory sound has a category (BASS, LEAD,
  PAD, PLUCK, KEYS, DRUM, FX, OTHER) to browse by
- **Four tracks**, one synth part each with its own engine and sound;
  12 voices shared between them (Blue Hare; 8 when the device is busy: voices 9 .. 12 start only while the audio's
  measured load is under 75 %, a heavy song keeps to 8 on its own). ALGORITHM selects the track on every page
- **Sequencer:** a placeholder for now: SEQ (and GLO) show TODO. PLAY runs the transport (the click and the synced
  LFOs follow it). See [Sequencer](#sequencer)
- **Metronome:** CLICK (OFF or ON) with three levels; the click goes to the headphones and speaker only, never to
  USB audio. There is no count-in: PLAY starts at once
- **Chord keys:** one finger plays an in-key chord (triads or sevenths of the scale, or fixed chord
  shapes), with voicings; on the keys and MIDI in
- **Scales and voices:** 16 scales with a white-key mode, glide, MONO / LEGATO / UNISON. The arpeggiator is gone
  (2026-10): ARP shows a TODO page for now
- **LFO:** RATE or **SYNC** to a note value (4 bars to 1/32, following the tempo or an external clock), **TRIG** NOTE
  or FREE, **POL** bipolar or unipolar (LFO > LFO 2)
- **Modulation matrix:** 4 slots per track, MIDI controllers as sources, **S&H** and **SLEW** (a random value per
  LFO cycle, held or gliding), and **DEPTH** as a destination, so the matrix can modulate the LFO itself
- **SPREAD** (EDIT > VOICE 3): the voices of a POLY or UNISON track alternate left and right of its PAN (or land on a
  side at random: SETTINGS' **PAN MODE** RND, below); **OCT** beside it moves
  the whole sound up or down by octaves (a preset can set it)
- **Effects:** distortion per track; chorus, delay and reverb sends (the reverb as
  **HALL**, ROOM, SPRING or *(Blue Hare)* **GATED** or **CONCERT**; HALL is a smooth stereo reverb, the default on a
  fresh start; the delay **CLEAN**, **TAPE**, **WORN**, **PING** or **DUAL** on DLY 2: TAPE and WORN a tape echo with
  wow and flutter, repeats that darken and saturate and run away into a howl past FDBK ~95; PING echoes left, then
  right, and so on, up to 0.74 s; DUAL a true stereo delay, the left at TIME and the right at three quarters of it,
  each its own loop, a quarter fed across); master limiter. *(Blue Hare)* The chorus and ROOM are stereo (the chorus's two sides sweep in opposition; SPRING and the
  tape echoes stay mono, as the real ones are), and the delay's and the reverb's sends lose their lows below about
  220 Hz first, so the bass and the kick stay dry and the tails stay clear. A gentle bus compressor (3:1, slow) sits in
  front of the master limiter, so a full mix keeps its punch at the top of MASTER instead of being held down all the time.
  *(Blue Hare, 2026-10)* **2x oversampling** around a shaper (`dsp.c` os2_*): what a hard drive adds above the band no
  longer folds back into it (6 to 7 dB fewer aliases; its engines are archived for now, the building block stays).
  *(Blue Hare, 2026-10)* **MONO LOWS**: before it, the mix's stereo difference under about 120 Hz is taken out (40 Hz
  hard left plays on both sides), as a club's system wants the bass; the width above stays
- **FX layer:** hold FX for filter sweeps (LPF, HPF), freeze, a harmonizer (OCT UP / OCT DN with shimmer), and mutes
  on the black keys (REPEAT, REVERSE and TAPE STOP: archived 2026-10-16);
  any white key can hold any effect (hold the key in the layer and turn PRESETS); MENU > FX LATCH makes them toggle,
  so nothing has to stay held
- **MOMENTARY:** hold LFO and turn a knob on HOME or a sound / FX page; let go of LFO and the value jumps back
  (OCT+ while holding keeps it)
- **Quick layers:** hold FX, GLO, SCL or EDIT for shortcuts on the keys and knobs, or double-tap it to
  keep the layer open; one-step undo (SAVE held); OCT+ confirms, OCT- goes back; what can be
  pressed breathes softly instead of blinking
- **Presets:** factory presets and 32 user preset slots, named on the device (an FM6 sound keeps its own patch);
  user presets get a category. One project (the tracks' sounds and settings) is saved on SAVE > PROJECT and can get a
  random name; projects of earlier versions do not open (see [Sequencer](#sequencer)). The PRESETS knob changes the sound from every page
  that is not a list.
  Turn the FM-1 off and on and the music is as you left it (MENU > RESTORE LAST, an autosave of its own,
  written only when stopped and idle)
- **Screen:** flat UI with Inter Tight and Fukiai icons, FLAT or LINE style, 10 palettes including
  grayscale, black and white, high contrast and NIGHT; type in sizes by role (what you read in the larger size,
  labels and hints in the smaller); MENU > LARGE for tall knob cards with larger labels and values; the menu in
  five tabs. The header shows the track, the play state and the tempo. HOME
  shows the oscilloscope or, with MENU > HOME TRACKS, **the four tracks** with their sounds, mutes and meters; the
  screen can go dark when the panel is left alone (MENU > SCREEN OFF)
- **LEDs:** idle buttons and keys glow dim so the panel can be found in the dark (MENU > LEDS: OFF,
  DIM LO, DIM HI or INV, the official firmware's look); PLAY turns green while playing; the keys show
  the notes MIDI IN plays; MENU > SCALE LEDS shows the track's scale on the keys;
  a soft light sweeps over the keys at power-on, and with MENU > ANIM IDLE drifts over them while the FM-1 waits
- **USB:** class-compliant MIDI in and out, and a stereo audio input ("Felucca") at 44.1 or 48 kHz
  (the computer picks) that records the master output on the computer, no driver needed (at a fixed level with MENU > USB LEVEL FIXED;
  on macOS 13–15, set MENU > USB SERIAL to OFF so the audio input appears)
- **MIDI:** USB and TRS MIDI in; MENU > MIDI > MIDI IN: CH1-4 (channels 1–4 play tracks 1–4), CH5-8, CH9-12 or
  CH13-16 (another block of four, e.g. for two FM-1s on one cable), or SEL (every channel plays the selected track);
  the keys send on the track's channel; pitch bend, sustain, panic; clock from internal, USB or TRS. MIDI CCs set
  track parameters: 5 GLIDE, 7 LEVEL, 10 PAN, 71 resonance, 72 / 73 / 75 release / attack / decay, 74 brightness,
  91 / 93 / 94 the reverb, chorus and delay sends
- **Web:** editor for every parameter (with a 6-operator FM patch editor that also sends a whole 32-voice bank),
  mixer, preset library, sample upload and
  recording with trim, the MENU settings; full backup and restore
  (everything but the FM6 voice bank: keep its .syx file);
  return to the official firmware; Felucca itself running in the browser

## New in 1.4.1

- **Engine quick actions on the EDIT layer's black keys.** Hold EDIT: the black keys do what the selected track's
  engine offers, and the map shows it. On a DRUM track, black keys 1–8 (F#3 … A#4) mute its lanes KICK, SNARE, CLAP,
  HATCL, HATOP, TOM, RIM and BELL, one press to mute, the next to unmute. The LEDs show which lanes sound, the drum
  grid dims a muted lane. A muted lane keeps playing unheard, so unmuting brings it back in time. The mutes are for
  live play: never saved, not recorded, not undone; a sound or project load clears them, OCT− in the layer puts them
  back as they were when it opened. Other engines have no quick actions yet.
- **FM6 voice bank** (Discussion #168). The web editor's 6-OP FM tab sends a whole 32-voice SysEx bank (the standard
  4096-byte file) to the FM-1 in one go, with its name. On an FM6 track SLOT (EDIT 2, KNOB 4) now goes F1–F8, OWN,
  then **B1–B32**, the bank's voices; the algorithm chart shows the voice's name. Picking a voice copies it into the
  track, as F1–F8 do, so a project or user preset saved with it keeps that sound after you send another bank (it
  loads with SLOT OWN). With no bank sent, SLOT stops at OWN as before. The bank has its own flash sector and is only
  replaced once a new one arrived whole; projects, presets, samples and settings are not touched. **The full backup
  does not include the bank: keep the .syx file.**
- 1.4.1 opens and saves everything 1.4 does, in the same formats.
- (Blue Hare: FM6 and DRUM are gone, with the bank and the DRUM quick actions; KIT has the lane mutes on the black
  keys now.)

## ⚠ Coming from 1.1.5

1.4 is the first release since 1.1.5.1 and holds a lot (all of this holds for 1.4.1 too). Everything you saved loads and sounds as before, except
where marked here:

- **Projects are saved in a new format (FUN10).** 1.4 opens every older project, but **1.1.5.x and older cannot
  open a project saved by 1.4**: they show its slot as empty (the data stays in flash until you save something into
  that slot there), and refuse a backup that holds one. User presets saved by 1.4 load on 1.1.5.x without the five
  new values (LFO 2's SYNC, TRIG and POL, QUANTIZE, SPREAD).
- **ARP ORD now plays the held notes in the order you pressed them** (up to 1.1.5 it played them as UP): a sound
  or project using ORD sounds different when its notes were pressed in another order than low to high.
- **The pages moved** (Discussion #153): see Controls below. In short: SEQ is STEP and AUTOMATION; **SEQ held is SEQ
  TOOLS on every page** (it no longer opens SONG); **GLO opens SONG**; PHRASES is on SAVE; HOME goes round HOME,
  MIXER and CLOCK (BPM, swing, clock source); TUNE is MENU > AUDIO, MIDI IN (ROUT) is MENU > MIDI, and the USB state
  and CPU load are MENU > SYSTEM > INFO. REC held is the REC layer as before.
- **SONG has a lane per track**: older songs load with all four tracks on each row's slot and play as before; its
  knobs are KNOB 1 SECTION, 2 TRACK, 3 PAT, 4 REPS, and on SONG the white keys A3 B3 C4 D4 set the slot.
- **A fresh start uses the HALL reverb** (up to 1.1.5: ROOM). Saved projects keep their reverb TYPE.
- **BASS+ (MENU > SPEAKER EQ) is softer:** its added harmonics are at half the level, so a full-scale kick no longer
  distorts (#180).
- **The web editor** needs its 1.4 update for the new features; use the one on the site.

## Controls

- Tap a page button for its page, again for the next; HOME returns home. Page buttons act when let go
- Hold FX, GLO, SCL or EDIT for its quick layer; hold SAVE to undo, HOME for the menu; hold LFO and turn a
  knob for a MOMENTARY change
- Double-tap a layer's button to keep its layer open without holding; tap it again to close
- On action pages and in dialogs, OCT+ does it and OCT− goes back; in the menu, OCT+ / OCT− change the value and HOME closes it
- Save a sound: stop, tap SAVE, pick a slot with KNOB 1, then OCT+ and OCT+ again (name it with the keys)

The pages by button:

| Button | Taps go round | Held |
|---|---|---|
| HOME | HOME, MIXER, CLOCK (BPM, swing, clock source) | the menu |
| SEQ | the sequencer's placeholder (TODO) | (as a tap) |
| GLO | the sequencer's placeholder (TODO) | the GLO layer (mutes, solos, levels, TAP tempo) |
| SAVE | USER, PROJECT, TOOLS (INIT SOUND), PRESETS | UNDO |
| LFO | LFO, LFO 2, LFO DEST, MOD | MOMENTARY (with a knob) |
| EDIT | EDIT 1, EDIT 2, VOICE, VOICE 2, VOICE 3 (FM6 adds OPERATOR, OP ENV, OPERATOR 2; DRUM LANES) | the EDIT layer (the engines; on DRUM the black keys mute lanes) |
| FX, SCL, ENV | their pages, as before | FX and SCL: their layers |
| ARP | a placeholder (TODO): the arpeggiator is gone | (as a tap) |
| REC | nothing for now (no recording) | nothing |

## Sequencer

A placeholder for now: SEQ (and GLO, which opened the song) show a page that says TODO. Felucca's step sequencer, its
automation, PHRASES and the A–D song are gone, and so is the tracker that took their place until 2026-10 (git
history keeps it). PLAY still runs the transport: the click and the synced LFOs follow it, on the internal, USB or
TRS clock. REC does nothing for now.

A project (SAVE > PROJECT, the autosave) holds the tracks' sounds, the globals and each track's
place in the mix (WIDTH, LOW CUT, PAN MODE: their power-on values, as no page sets them for now; version 9: DUCK is
archived). Projects of
earlier versions, the tracker's songs among them, do not open.

The web editor's Sequencer tab says TODO with this firmware (an older Felucca firmware still gets its step grid).

## Menu

Hold **HOME** for the menu, in five tabs: **DISPLAY** (COLOR, STYLE, LARGE, ANIM, LEDS, SCREEN OFF, SCOPE, HOME),
**CONTROL** (HOLD, KNOB ACCEL, FX LATCH, BPM LOCK, SCALE LEDS), **AUDIO** (SPEAKER EQ,
USB LEVEL, CLICK, CLICK LEVEL, TUNE), **MIDI** (MIDI IN) and **SYSTEM** (USB SERIAL, RESTORE LAST,
HARDWARE CALIBRATION, INFO, ABOUT).
ALGORITHM moves between the tabs and PRESETS through the rows of one; any of KNOB 1–4, or OCT+ / OCT−,
changes the value (OCT+ opens HARDWARE CALIBRATION, INFO and ABOUT); press HOME to close the menu. Every new
setting defaults to the earlier behaviour. The web editor's Settings tab reads and changes them too, saved the same
way as from the menu, in the same tabs.

- **COLOR:** the palette. GREY (grayscale, called MONO up to 1.0.1), GREEN, AMBER, ICE, VIOLET, ROSE,
  PAPER (light), HI-CON (high contrast), NIGHT (the 0.9 look: true black, green-tinted text) and MONO
  (black and white)
- **STYLE:** FLAT (filled cards) or LINE (areas divided by thin lines)
- **LARGE:** OFF or ON: on HOME and the value pages the four knob cards grow tall, marked K1–K4, with
  larger labels and values about twice the size; the graph below becomes a strip. Lists, SEQ's placeholder
  and the quick layers keep their layout with larger labels
- **ANIM:** ON, OFF or IDLE. OFF shows every change at once, without rolling digits or
  the LED sweep at power-on. IDLE is ON plus a gentle LED animation after a minute without input: a soft light drifts
  over the keys and the buttons breathe; any key, button or knob ends it and does what it always does. It never runs
  while playing, in a layer, the menu or a dialog
- **LEDS:** OFF (no glow), DIM LO, DIM HI (default) or INV (the idle LEDs lit, the active ones dark).
  What can be pressed breathes up to about 60 % of a lit LED, about 30 % with DIM LO
- **SCREEN OFF:** NEVER (default), 5 MIN, 15 MIN, 30 MIN or 60 MIN: after that long with no button, key or knob
  touched, the screen goes dark; the sound, the sequencer, MIDI and USB go on. The backlight stays on (on the FM-1
  its line also enables the buttons and keys). The next button, key or knob only turns the screen back on
- **SCOPE:** OUT (default) or MIX: what HOME's oscilloscope shows. OUT the sound after MASTER (the picture shrinks
  with the volume), MIX the mix before MASTER, the same size at any volume
- **HOME:** SCOPE (default) or TRACKS: under HOME's four cards, the oscilloscope or the four tracks, a row each with
  its sound, MUTE and a meter. Only the look changes: the knobs work as on SCOPE
- **HOLD:** how long a layer's button is held before its map shows
- **KNOB ACCEL:** OFF (one step per click) or ON: a fast, steady turn of a wide value moves 2 to 4 steps
  per click, up to 8 on values of more than 64 steps, the FX and GLO layers' knobs included; lists never
  jump. ON or OFF, every click of a fast turn counts
- **FX LATCH:** ON, FX + an effect key turns the effect on until pressed again, the knob macros stay
  where you leave them, and FX + OCT− turns everything off
- **BPM LOCK:** ON, SELECT no longer changes the tempo; hold GLO and turn SELECT, tap F4 in the GLO
  layer or use HOME > CLOCK instead
- **SCALE LEDS:** OFF or ON: the keys show the selected track's scale all the time (the root a little
  brighter); not on DRUM tracks
- **SPEAKER EQ:** FLAT, LOWCUT or BASS+, a tone setting for the built-in speaker (not a switch). It also
  shapes the headphone out and USB audio, so keep it on FLAT when recording. The firmware cannot turn
  the speaker off, but headphones in the jack do
- **USB LEVEL:** MASTER (the MASTER knob sets the USB audio level too) or FIXED (USB always at full
  level, MASTER sets only the speaker and headphones)
- **CLICK:** OFF (default) or ON: a click on every beat while playing, the
  bar's first beat higher. **CLICK LEVEL:** LOW, MID (default) or HIGH; MASTER sets it too
- **TUNE:** the global tuning, −50 to +50 cents. It is the project's, saved and loaded with it
- **MIDI IN:** CH1-4 (default), SEL, CH5-8, CH9-12 or CH13-16 (see MIDI above). The project's too
- **USB SERIAL:** ON or OFF, applied when the menu closes (the FM-1 reconnects). OFF leaves out the
  serial console, a developer tool, so the FM-1 is a plain audio + MIDI device; this lets macOS 13–15
  see its USB audio input. MIDI, the editor and the installer work either way
- **RESTORE LAST:** ON (default) or OFF. ON, the FM-1 starts with the music you left (the header says
  RESTORED), kept in an autosave apart from the saved song; it is written when the music changed, the
  transport is stopped and nothing has been touched for 10 s, at most once a minute. OFF starts with the
  power-on sounds
- **HARDWARE CALIBRATION**, **INFO** (the version, the USB link and the CPU load) and **ABOUT**

## Engines

Felucca's own engines, by Leo Kuroshita (ANALOG, FM6, PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE and DRUM), are not in
Blue Hare any more (below); what other engines build on of them stays (PHYS's resonators in MODAL).

*(Blue Hare, 2026-10-16: Milestone 1)* Blue Hare has one distribution, **HARE FM1**, with the base engines its
synthesizer is built on; the other engines (BYTE, HOOVER, SPEECH, FMPERC, KARPLUS, VOSIM, PARTICLE, CHAOS, DUBSIREN,
KIT, BREAKS, RISERS, SIREN8, DUBCHORD, WASP, VECTOR, GLITCH and the FOUNDRY, WAREHOUSE and CHIP engines) are archived
in `archive/engines/` with their descriptions ([archive/engines/README.md](archive/engines/README.md)); their ids stay
theirs, and a sound of one is refused. An SH-101-style engine joins them next ([docs/Milestones.md](docs/Milestones.md)).
[docs/SoundAndDSP.md](docs/SoundAndDSP.md) describes every engine's and effect's algorithm.

In the order the device lists them:

- **SID** *(Blue Hare)*: a voice of the C64's sound chip, in its manner. WAVE TRI SAW PUL NOIS (the chip's
  pitched shift-register noise) or the chip's combined waves (TS PT PS PTS); PW, PWM; MOD RING / SYNC / both against a
  master at the note, the oscillator MDTN semitones above it (the sync lead, ring bells); FILT the 12 dB multimode
  filter (LP BP HP) at CUT with RES, a little gritty. The SHP routings move PW and MDTN
- **SH101** *(Blue Hare, 2026-10)*: after the 1982 monosynth. One oscillator's waves at once, by level: SAW, PULS (PW
  50 % down to ~5 %; ENV DEST or LFO DEST SHP is its PWM), SUB locked to the oscillator (SUBT -1 SQ, -2 SQ, -2 PU) and
  NOIS; into a 24 dB ladder low-pass at CUT (half following the keys) with RES, its input saturating as the mix gets
  louder, singing by itself past ~110. Basses, acid lines, PWM and saw leads, a singing lead, plucks, a pad, brass, a
  kick, a hat, a zap, wind
- **WAVE** *(Blue Hare)*: wavetables. TBL picks one of 8 tables of 8 frames each (BASIC: sine to narrow pulses;
  ADDTV: one partial to many; SYNC: a hard-sync sweep; RESO: a resonant peak climbing; VOX: vowels; FM: two-operator
  FM; DIGI: sparse, metallic spectra; BITS: stepped and 1-bit shapes), POS the place between its frames; the SHP
  routings move POS (ENV DEST / LFO DEST SHP, the matrix's SHP), so an envelope or the LFO scans the table. SYNC reads
  the frame up to 8x faster (windowed sync), DTN adds a detuned second oscillator, then DRV, a resonant low-pass and
  CRSH (sample-rate and bit reduction). The tables are band-limited per octave: high notes do not alias
- **BLISS** *(Blue Hare; FMKEYS until 2026-10)*: FM keyboards, four operators as two pairs: the body (RATIO, INDEX, FDBK; the
  index falls over DECAY, harder notes brighter by VEL) and the tine (the metallic strike of the attack, TINE; DTUNE
  its beating against the body); KSCL softens the top of the keyboard. Electric pianos, bells, tubular bells, marimba,
  vibes, clav, organ, harpsichord, kalimba, celesta, music box; Rhodes, Wurly, the DX and LATELY basses, FM bells,
  crystal, an FM pad, brass, flute, a harp pluck
- **WEST** *(Blue Hare)*: west coast. A complex oscillator, a wavefolder and a low-pass gate: a modulating sine at RATIO
  times the note (whole and half ratios, and 1.41, 1.73, 2.76, 3.55, 4.61 for bells and metal) moves the carrier's
  phase by FM (FDBK feeds the modulator back into itself, up to noise); FOLD folds the carrier over (the SHP routings
  move it), SYM shifts the fold off centre; the low-pass gate's cutoff follows the envelope through a vactrol-like lag,
  LPG of the way down from CUT, so a note darkens as it fades (the plucked, struck sound); TMBR opens FM and FOLD with
  the same envelope. FM and FOLD ease off on high notes so they do not alias. Its plucks (LPG HARP, NYLON GATE, KOTO
  FOLD, THUMB WOOD, BUZZ PLUCK, PIZZ GATE, GLASS PLUCK, RUBBER PLUCK) took KARPLUS's place when it was archived
- **MODAL** *(Blue Hare; its resonators are PHYS's, DaisySP's)*: a modal resonator. An exciter
  strikes 12 tuned resonators, the modes of a material: MATRL BAR, MRMBA, BELL, GLASS, PLATE, DRUM, TUBE, STRING; EXCIT
  MALLET, PLUCK, BOW (noise while the key is held), CLICK. STRUCT stretches the modes (inharmonic), POS the strike's
  place, DECAY the ring (50 ms .. 8 s), DAMP how much sooner the highs die, BRIGHT the strike's hardness, BEAT detunes
  the modes a little each (a bell's beating). Presets: marimba, vibes, bells, glass, plates, drums, harp, kalimba,
  gamelan, bowed glass and bells, a drone pipe, clanks and glitches
- **ECHOES** *(Blue Hare)*: a note and its echoes as one instrument; every repeat is played
  anew, so each can differ. SRC (BLIP, PLUCK, STAB, NOISE, FM), then COUNT repeats at the tempo: TIME (1/4 .. 1/64,
  dotted, triplets), SHAPE (EVEN, BOUNCE: each shorter, SLOW, SWING, RANDOM), DECAY (what each keeps), PITCH (semitones
  a repeat), TONE (each darker or thinner), SCAT (chance in time and pitch). The echoes go on after the key is let go

**This fork removed DIGITAL, SAMPLE, GRAIN and SLICE** (1.5): DIGITAL (four-operator FM) had already been replaced by
FM6, and the other three played recorded material. The firmware has no built-in samples and no user sample slots, which
frees about 11 KB of RAM and 28 KB of the buffer pool. It is a clean break for what was saved with them: their engine
numbers (1, 4, 8, 13) are retired, so a project with a track on one of them is refused, a user preset of one shows as
an empty slot, a song's instrument of one is dropped, and a library file's patch of one is skipped. A complete backup
restores without its samples.

**Blue Hare removed Felucca's own engines** (ANALOG, FM6, PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE, DRUM) and the
default distribution that held them, with FM6's whole patch editing: the 6-OP FM tab of the web editor, its voice
bank (the B slots, its flash sector) and the user presets' FM6 patches. Their ids (0, 2, 3, 5 .. 7, 9 .. 12) are
retired as the four above, with the same clean break: a project with a track on one is refused, a user preset of one
shows empty, a song's instrument of one is dropped.

*(Blue Hare, 2026-10-16)* **Archived:** the engines above that are not HARE FM1's, the distributions IDM, FOUNDRY,
WAREHOUSE, CHIP, IDM NATIVE and REF (`archive/distributions/`), the SLICER and DUCK, and the FX layer's REPEAT, REVERSE
and TAPE STOP (`archive/fx/`): the groovebox's parts, out of a synthesizer's way.

**Reverb** (FX > REVERB, TYPE): **HALL** (since 1.4, the default on a fresh start: eight delay lines feeding each
other with slowly drifting taps, stereo, SIZE the decay up to about 4.5 s, DAMP how fast the highs die), **ROOM** (the
default up to 1.1.5), **SPRING** and *(Blue Hare)* **GATED**: HALL cut off sharply, the 80s gated snare. A hit in
the send opens it; it holds for SIZE (about 60 ms at 0 to 500 ms at 127) after the last one, then shuts in about 10 ms.
*(Blue Hare, 2026-10)* **CONCERT**: HALL grown into the memory four tracks freed, sixteen longer lines instead of
eight, a 12 ms pre-delay and four diffusers: a denser, smoother, wider tail at HALL's level (SIZE and DAMP as HALL's;
the native image plays its own floating-point CONCERT).
A project plays the TYPE it was saved with (one of the native image's own, on the FM-1: HALL).

## Scale keyboard

On the **SCL** page, set **QNT** to WHITE to play the selected scale using only the
white keys (SNAP keeps every key and rounds it down to the scale). C4 plays **ROOT**; consecutive white keys play consecutive scale notes
above and below it. Black keys are silent. **TRN** transposes the resulting notes; the octave buttons shift them
by full octaves. Set QNT to OFF for the normal chromatic keyboard.

Available scales: chromatic (CHR), major (MAJ), natural minor (MIN), Dorian (DOR),
Mixolydian (MIX), major pentatonic (PEN), minor pentatonic (MPEN), harmonic minor
(HARM), Phrygian (PHRY), Lydian (LYD), Locrian (LOC), ascending melodic minor (MEL),
minor blues (BLUES), whole tone (WHOLE), half-whole diminished (DIMHW), and
whole-half diminished (DIMWH). Scales with other than seven notes continue across
the white keys without repeating notes; their roots need not fall on every C key.
Drum kits and incoming MIDI keep their own note mapping.

Press **SCL** again for the **CHORD** page: CHRD picks the chord keys (OFF, the scale's triads or
sevenths, or a fixed shape) and VOIC the voicing.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build script, generators, package maker and installer |
| `assets/` | UI font, icon names |
| `web/` | the web installer, the editor (`editor.html`, protocol in `EDITOR_PROTOCOL.md`) and the browser emulator (`web/emu/`), the sound design studio (`web/studio/`); the site's editor: [Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp) |
| `native/` | *(Blue Hare)* the native image: Blue Hare as a desktop application (SDL2), BUILDING.md "Native image" |
| `archive/` | *(Blue Hare)* what is kept but neither built nor tested: the plugin (VST3, AU, CLAP by DPF), `archive/plugin/README.md` |
| `tests/` | tests that run on the build machine |
| `LICENSES/` | licence texts of the bundled fonts, icons, ported DSP and SDK files |

## If the FM-1 does not start

If an update is interrupted and the FM-1 stays black, check whether a computer sees it as a USB device named
**WL80UBOOT** (or a USB mass-storage device with ID 4C4A:8057). That is the chip's built-in boot mode, and
the FM-1 can be brought back:

- First try another USB data cable, and close every other app that uses MIDI, then run the web installer again.
- If it stays in boot mode, [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter) reads and
  writes the FM-1's flash from a Mac through a Seeed XIAO RP2040 (three wires to the FM-1's USB lines). Back up
  the flash first, then write the official firmware (M-VAVE's FM-1.fwsc).
- Questions: [Issues](https://github.com/hugelton/Felucca/issues).

## Support

Blue Hare: issues and ideas go to [TristanoSuriani/fel-tracker](https://github.com/TristanoSuriani/fel-tracker/issues).
Please don't report Blue Hare problems to Felucca's tracker.

Felucca: if Felucca is useful to you, [sponsoring on GitHub](https://github.com/sponsors/hugelton) or a donation
on [itch.io](https://hugelton.itch.io/felucca) helps keep its development going.

Issues are for reproducible bugs (one per issue). Ideas and requests go to
[Discussions](https://github.com/hugelton/Felucca/discussions), and feature requests posted as issues will be
moved there. Pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

## AI disclaimer

Blue Hare is developed by Tristano Suriani with the help of AI coding agents (for coding, testing and
documentation). He decides its design, features, sound design and direction.

Felucca (Leo Kuroshita's disclaimer): Felucca is developed with the assistance of AI coding agents. These tools are used for coding, testing, documentation, translation, and maintenance.
The instrument's design, features, sound design, and overall direction are determined by the maintainer or community.
No generative AI is used to create music, icons, or visual artwork for this project.
For more details, see [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits

- **Tristano Suriani** ([@TristanoSuriani](https://github.com/TristanoSuriani)): Blue Hare, meaning the tracker,
  the distributions and engine ids, the engines WAVE, BYTE, WEST, HOOVER, SPEECH, FMPERC, KARPLUS, VOSIM, PARTICLE,
  CHAOS, SID, DUBSIREN, DUBSIREN8 (SIREN8), DUBCHORD, BLISS, WASP, MODAL, VECTOR, ECHOES, GLITCH, KIT, BREAKS and RISERS,
  and the changes to
  Felucca listed in
  [AUTHORS.md](AUTHORS.md)
- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  Felucca itself, its own engines (removed in Blue Hare) and the DRUM lanes' General MIDI map the kits used; the
  [Fukiai](https://github.com/hugelton/Fukiai) icon font ([MIT](LICENSES/MIT-Fukiai.txt))
- Fonts: [Inter Tight](https://github.com/rsms/inter-tight) by The Inter Project Authors, [SIL OFL 1.1](LICENSES/OFL-InterTight.txt);
  the browser emulator's labels: [DotGothic16](https://github.com/fontworks-fonts/DotGothic16) by The DotGothic16 Project Authors, [SIL OFL 1.1](LICENSES/OFL-DotGothic16.txt)
- MODAL and ANVIL: the modal resonator ported from [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith and Emilie Gillet ([MIT](LICENSES/MIT-DaisySP.txt))
- Browser emulator: after [X0X](https://github.com/charlesvestal/fm1-x0x) by [charlesvestal](https://github.com/charlesvestal) (GPL-3.0), a Felucca fork whose browser build showed the way
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) ([Apache-2.0](LICENSES/Apache-2.0.txt); three of its files are in every package, none in this tree)
- Contributions: [keremimo](https://github.com/keremimo) (white-key scales, #2), [ChanceTheMaker](https://github.com/ChanceTheMaker)
  (TRS MIDI, bend, sustain and clock, palettes, favourites, editor display settings: #8, #10, #11, #12),
  [andreahaku](https://github.com/andreahaku) (sample recording and trim, #29; SLICE manual slices and tests, #27, #22),
  [spinkham](https://github.com/spinkham) (the boot fix for 0.9 projects, #111),
  [zednaked](https://github.com/zednaked) (ratchets, #100),
  [jasonpersinger](https://github.com/jasonpersinger) (the ROOM reverb click fix, #121)

## Licence

Free software: [GPL-3.0-only](LICENSE). The bundled fonts and the
ported DSP keep their own licences ([LICENSES/](LICENSES/)); details in [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Neither Felucca nor Blue Hare is affiliated with or
endorsed by them. "Felucca" is Hügelton Instruments' name for its firmware; Blue Hare is an independent fork.

Felucca: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
Blue Hare's additions and changes: Copyright (C) 2026 Tristano Suriani (see [AUTHORS.md](AUTHORS.md))
