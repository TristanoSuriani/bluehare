# Felucca

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)
[![Sponsor](https://img.shields.io/badge/Sponsor-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/hugelton)

![Felucca 1.0](docs/felucca-1.0.png)

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
- Editor: `web/editor.html` in this repository edits the tracker (see [Tracker in the web editor](#tracker-in-the-web-editor));
  the [web editor](https://hugelton.github.io/Felucca/webapp/editor/) on the site
  ([Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp)) edits the sounds but not the tracker
- Build: [BUILDING.md](BUILDING.md)

<a href="https://hugelton.itch.io/felucca"><img src="https://static.itch.io/images/badge-color.svg" alt="Available on itch.io" width="74"></a>

## Features

- **Thirteen engines** (below; SAMPLE, GRAIN and SLICE are left out of this fork's build), each with its own factory presets; every factory sound has a category (BASS, LEAD,
  PAD, PLUCK, KEYS, DRUM, FX, OTHER) to browse by
- **Four tracks**, one synth part each with its own engine and sound (drums are the DRUM engine);
  8 voices shared between them. ALGORITHM selects the track on every page
- **Tracker** (this fork, replacing the step sequencer): 24 patterns of up to 64 rows (a row is a 1/16), a column per
  track; each cell a note (or OFF) with an optional chord (up to three notes: a shape such as MAJ or MIN7, or two
  intervals), an instrument (01..3F, changing the track's sound on any row) and one FX (a parameter set for that row only: LEVEL,
  ATTACK, DECAY, SUSTAIN, RELEASE, CUTOFF, RESONANCE, FLT ENV, PAN, DIST, CHORUS, DELAY, REVERB, GLIDE). PLAY on
  PATTERN loops the pattern; PLAY on SONG plays the order list (up to 128 positions). The instruments are factory or
  user presets; each track's ENV, filter and sends stay its own (SEQ > SETTINGS). Live recording into the rows, step
  entry at the cursor (REC armed, stopped); playing, the screen follows the row playing, 8 rows a page. Undo / redo
  of pattern edits (SAVE held), copy and paste of a track or a pattern, row insert / delete, PLAY from the cursor's
  row (all on SEQ held). The web editor's Sequencer tab edits all of it. See [Tracker](#tracker)
- **Metronome:** CLICK (OFF, while recording, or always) with three levels; the click goes to the headphones
  and speaker only, never to USB audio or into a pattern. There is no count-in: PLAY starts at once, armed or not
- **Chord keys:** one finger plays an in-key chord (triads or sevenths of the scale, or fixed chord
  shapes), with voicings; on the keys, MIDI in, recording and the arpeggiator
- **Arpeggiator** with UP, DN, UPDN, RND, ORD (the notes in the order you pressed them), REPEAT and, for chords,
  DNUP, UP+8, CONV, DIVG, PINKY, THUMB, WALK and CHORD; a beat LED; 16 scales with a white-key mode, glide,
  MONO / LEGATO / UNISON
- **LFO:** RATE or **SYNC** to a note value (4 bars to 1/32, following the tempo or an external clock), **TRIG** NOTE
  or FREE, **POL** bipolar or unipolar (LFO > LFO 2)
- **Modulation matrix:** 4 slots per track, MIDI controllers as sources, **S&H** and **SLEW** (a random value per
  LFO cycle, held or gliding), and **DEPTH** as a destination, so the matrix can modulate the LFO itself
- **SPREAD** (EDIT > VOICE 3): the voices of a POLY or UNISON track alternate left and right of its PAN
- **Effects:** distortion and the SLICER per track; chorus, delay and reverb sends (the reverb as
  **HALL**, ROOM or SPRING; HALL is a smooth stereo reverb, the default on a fresh start); master limiter
- **FX layer:** hold FX for repeat, reverse, filter sweeps, tape stop, freeze, a harmonizer
  (OCT UP / OCT DN with shimmer), **FLANGER** and **PHASER** (in time with the tempo), and mutes on the black keys;
  any white key can hold any effect (hold the key in the layer and turn PRESETS); MENU > FX LATCH makes them toggle,
  so nothing has to stay held
- **MOMENTARY:** hold LFO and turn a knob on HOME or a sound / FX page; let go of LFO and the value jumps back
  (OCT+ while holding keeps it)
- **Quick layers:** hold FX, GLO, SCL, EDIT, REC or SEQ for shortcuts on the keys and knobs, or double-tap it to
  keep the layer open; one-step undo (SAVE held); REC on every page; OCT+ confirms, OCT- goes back; what can be
  pressed breathes softly instead of blinking
- **Presets:** factory presets and 32 user preset slots, named on the device (an FM6 sound keeps its own patch);
  user presets get a category. One song (the tracker's) is saved on SAVE > PROJECT and can get a random name; projects
  of Felucca 1.4 and before do not open (see [Tracker](#tracker)). The PRESETS knob changes the sound from every page
  that is not a list.
  Turn the FM-1 off and on and the music is as you left it (MENU > RESTORE LAST, an autosave of its own,
  written only when stopped and idle)
- **Screen:** flat UI with Inter Tight and Fukiai icons, FLAT or LINE style, 10 palettes including
  grayscale, black and white, high contrast and NIGHT; type in sizes by role (what you read in the larger size,
  labels and hints in the smaller); MENU > LARGE for tall knob cards with larger labels and values; the menu in
  five tabs. The header shows the track, the play state (and while a song plays, its position) and the tempo. HOME
  shows the oscilloscope or, with MENU > HOME TRACKS, **the four tracks** with their sounds, mutes and meters; the
  screen can go dark when the panel is left alone (MENU > SCREEN OFF)
- **LEDs:** idle buttons and keys glow dim so the panel can be found in the dark (MENU > LEDS: OFF,
  DIM LO, DIM HI or INV, the official firmware's look); PLAY turns green while playing; the keys show
  the notes the tracker and MIDI IN play; MENU > SCALE LEDS shows the track's scale on the keys;
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
  the tracker (patterns, song, instruments, track settings, transport), mixer, preset library, sample upload and
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

## ⚠ Coming from 1.1.5

1.4 is the first release since 1.1.5.1 and holds a lot (all of this holds for 1.4.1 too). Everything you saved loads and sounds as before, except
where marked here:

- **Projects are saved in a new format (FUN10).** 1.4 opens every older project, but **1.1.5.x and older cannot
  open a project saved by 1.4**: they show its slot as empty (the data stays in flash until you save something into
  that slot there), and refuse a backup that holds one. User presets saved by 1.4 load on 1.1.5.x without the five
  new values (LFO 2's SYNC, TRIG and POL, QUANTIZE, SPREAD).
- **ARP ORD now plays the held notes in the order you pressed them** (up to 1.1.5 it played them as UP): a sound
  or project using ORD sounds different when its notes were pressed in another order than low to high.
- **SAMPLE's PIANO is a lighter lo-fi piano** (two notes at 11,025 Hz, 1.6 s each; up to 1.1.5 five notes at
  22,050 Hz, 0.75 s), and so are the sounds that use it (GRAIN's FROZEN and SHIMMER too). For the old piano, install
  **PIANO HD** into a user slot from the web editor and set those sounds' SET (GRAIN: SRC) to that slot. SLICE's
  PIANO is unchanged.
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

![FM-1 controls with Felucca](docs/controls.jpg)

- Tap a page button for its page, again for the next; HOME returns home. Page buttons act when let go
- Hold FX, GLO, SCL, EDIT, REC or SEQ for its quick layer; hold SAVE to undo, HOME for the menu; hold LFO and turn a
  knob for a MOMENTARY change
- Double-tap a layer's button to keep its layer open without holding; tap it again to close
- On action pages and in dialogs, OCT+ does it and OCT− goes back; in the menu, OCT+ / OCT− change the value and HOME closes it
- Save a sound: stop, tap SAVE, pick a slot with KNOB 1, then OCT+ and OCT+ again (name it with the keys)

The pages by button:

| Button | Taps go round | Held |
|---|---|---|
| HOME | HOME, MIXER, CLOCK (BPM, swing, clock source) | the menu |
| SEQ | PATTERN (always first from another page), SONG, SETTINGS, INSTR | the pattern edited, its length, EDIT STEP (KNOB 1–3) |
| GLO | the tracker's SONG | the GLO layer (mutes, solos, levels, TAP tempo) |
| SAVE | USER, PROJECT (the song), TOOLS, PRESETS | UNDO |
| LFO | LFO, LFO 2, LFO DEST, MOD | MOMENTARY (with a knob) |
| EDIT | EDIT 1, EDIT 2, VOICE, VOICE 2, VOICE 3 (FM6 adds OPERATOR, OP ENV, OPERATOR 2; DRUM LANES; SLICE SLICES) | the EDIT layer (the engines; on DRUM the black keys mute lanes) |
| FX, SCL, ENV, ARP | their pages, as before | FX and SCL: their layers |
| REC | arms / disarms the selected track (never starts PLAY: PLAY then records) | the REC layer (CLEAR, CLICK, CLICK LEVEL) |

## Tracker

The step sequencer and everything that came with it are gone, code and all: the STEP page and the DRUM grid, the
AUTOMATION list (automation, parameter locks, CHANCE, RATCH, NUDGE), the old SEQ TOOLS, PHRASES (the factory patterns,
a preset's suggested pattern), the A–D song and MENU > CHORD ENTRY. A tracker replaces them. Projects of Felucca 1.4
and before do not open here; a user preset still loads its sound (a pattern stored with it is kept but never played,
and a new one stores none).

**PATTERN** (SEQ): 24 patterns (00..17), 8 rows at a time, a column per track (ALGORITHM picks the cursor's column).
Each cell shows the note (`C-4`, `OFF`, `---` none), the instrument (`01`..`3F`, `..` none) and the FX (a letter and a
value `00`..`7F`, `...` none); a mark between the note and the instrument says the cell has a chord. The footer spells
out the cell under the cursor ("C-4 MAJ  SAW LEAD  REVERB 7F") and names the pattern ("PATTERN 05").

The column headers ("1 05 ANLG") show the instrument each track sounds *at that moment* and its engine: any cell's
instrument can change it. A track whose sound isn't an instrument (the power-on sound, or one loaded with PRESETS
since, or an instrument changed on INSTR) shows its engine alone ("1 ANLG"). A silent track's header is dim with "M"
for its number: muted, or another track soloed (hold GLO: the black keys F#3..A#3 mute tracks 1–4, the white keys
F3..B3 solo them while held, C4 unmutes all). KNOB 1 moves the cursor at half speed: the first click of a turn moves a
row at once, then a row every second click.

| Control | On PATTERN |
|---|---|
| KNOB 1 | the row (playing: the cursor is the row playing) |
| KNOB 2 | the note (from none: the last one entered; below the lowest: OFF, then none) |
| KNOB 3 | the instrument (the keys then record with it) |
| KNOB 4 | the FX value (a new FX starts at the track's own value) |
| PRESETS | the FX command (V level, A D S R the envelope, C cutoff, Q resonance, F filter envelope, P pan, X dist, H chorus, E delay, W reverb, G glide; NO FX clears it) |
| EDIT | clears the cell (stopped: the cursor moves EDIT STEP rows on) |
| SAVE held | undoes the last edit of a pattern; held again, redoes it (below) |
| keys | play; with the track armed (REC) and stopped they write their note at the cursor; armed and playing they record into the rows. Keys pressed while the first is held add to its cell's chord (stopped or playing) |
| SEQ held | KNOB 1 the pattern edited (while LOOP plays, it starts after the one playing), KNOB 2 its length, KNOB 3 EDIT STEP, KNOB 4 the chord of the cursor's cell; the white keys the tools (below); PLAY loops the pattern from the cursor's row; OCT− puts the pattern back as it was when SEQ went down |
| PLAY | loops the pattern from row 00 |

**Undo**: SAVE held on a tracker page puts back the pattern edited last as it was before that edit (its cells and its
length); held again, it redoes. One edit is one undo: a single turn of a knob on a cell (until you pause for 1.5 s or
move), an EDIT, a key's note, a tool. Undo works while playing. It covers the patterns, not the song order,
instruments or settings, and edits from the web editor or live recording are not undone.

**Tools** (hold SEQ, then a white key; the map shows them, a paste dim while nothing is copied):

| Key | Tool |
|---|---|
| F3 COPY TRK | copies the cursor's track (its column of the pattern) |
| G3 PASTE TRK | pastes it into the cursor's track (any pattern, any track) |
| A3 COPY PAT | copies the whole pattern, with its length |
| B3 PASTE PAT | pastes it over the pattern edited (SEQ + KNOB 1 picks it: copy, turn, paste) |
| C4 INS ROW | inserts an empty cell at the cursor in its track; the rows below move down, the last one of the length goes |
| D4 DEL ROW | deletes the cursor's cell in its track; the rows below move up, an empty one at the end |
| E4 INS ALL | INS ROW on every track |
| F4 DEL ALL | DEL ROW on every track |

Each tool is one undo. The clipboard keeps until power-off.

**Chords**: a cell's note can carry two more notes, 1 to 15 semitones above it, so a track plays up to three notes on a
row. SEQ held + KNOB 4 picks a shape: NONE, MAJ, MIN, SUS2, SUS4, DIM, AUG, MAJ6, MAJ7 (1 3 7), DOM7 (1 3 ♭7), MIN7
(1 ♭3 ♭7), 5TH, 5+OCT, OCT. Holding keys together writes any chord: the first key is the note, the next two (higher, at
most 15 semitones up) its intervals; this works for step entry and live recording. KNOB 2 moves the chord with its
note; OFF or no note drops it. A chord sounds only when the track is POLY (the instrument's voice mode): a MONO, LEGATO
or UNISON track plays the note alone. The four tracks share 8 voices, so chords on every track at once take voices from
each other.

**Changing the instrument on a step**: on PATTERN, KNOB 1 to the row, ALGORITHM to the column, KNOB 3 the instrument
(`01`..`3F`; `..` keeps the one the column played last). The instrument picked is also the one new notes are entered
and recorded with. What each number plays is set on INSTR (below).

**SONG** (SEQ again, or GLO): the order list. KNOB 1 the position (past the last: + ADD), KNOB 2 its pattern (on
+ ADD: a new position), KNOB 3 that pattern's length, KNOB 4 right a copy after it, left deletes it (EDIT too). PLAY
plays the song from the position picked, round and round; the PATTERN page then shows the pattern playing.

**SETTINGS**: each track's ATTACK, DECAY, SUSTAIN, RELEASE, CUTOFF, RESO, FLT ENV, DIST, CHORUS, DELAY, REVERB, LEVEL
and PAN. PRESETS picks the row, KNOB 1–4 set it for tracks 1–4. CUTOFF and RESO hold the engine's filter over every
instrument; INST (EDIT) leaves it to the instrument.

**INSTR**: the instruments 01..3F. KNOB 1 the slot, KNOB 2 its engine (or USER), KNOB 3 its preset; the selected
track plays it at once. EDIT empties the slot. A row's instrument sets the track's engine, its EDIT values and voice
mode from that row on; the track's settings stay.

**The song store** (SAVE > PROJECT): one song, saved over the four project storage areas; a save cut short leaves
the song saved before it whole. About 3,300 cells fit, about 2,200 if every one has a chord (SONG TOO BIG beyond;
songs saved before the chords do not open); RESTORE LAST keeps a song that fits one
storage area (a bigger one says SONG NOT RESTORED at power-on: save it). The full backup (web editor, Projects tab)
holds the song too.

**Swing**: CLOCK's SWING swings the rows (up to 1.4 × / 0.6 × at 100); a track's own SWG of the step sequencer is
not used any more. The SLICER follows the rows.

### Tracker in the web editor

`web/editor.html` (open it in Chrome or Edge; `?mock=1` tries it without a device) shows the tracker on its
**Sequencer** tab when the firmware has one:

- **Pattern:** the pattern, its length, PLAY LOOP / STOP, CLEAR. Every cell is a text field: `C-4 01 C40` (note,
  instrument, FX), `C-4 MAJ 01 C40` or `C-4 +3+10 01` (a chord after the note: a shape or two intervals), `OFF`,
  `---` / `..` / `...` for none; an empty field clears the cell. Fields can be left out from the front (`01 C40` sets
  the instrument and FX only). Up / Down and Enter move
  between rows. While playing, the row playing is highlighted and, with *Follow playback*, the view keeps up with it,
  8 rows at a time, and switches to each new pattern
- **Song:** the order list: a pattern per position, ▶ plays the song from there, × removes it, + ADD appends one.
  Picking another pattern while a loop plays cues it, as on the device
- **Instruments:** 01..3F, each none, an engine's factory preset, or a user preset
- **Track settings:** CUTOFF and RESO over the instruments (empty: INST), and each track's ATTACK, DECAY, SUSTAIN,
  RELEASE, FLT ENV, DIST, CHORUS, DELAY, REVERB, LEVEL and PAN
- What changes on the device (the panel, live recording, a load) shows up in the editor by itself
- **Projects** has the one song (load / save) instead of the slots A–D; the step grid, AUTOMATION and the A–D song chain
  are gone from the editor with a tracker firmware (an older firmware still gets them)

The protocol (SysEx commands 78 TRACKER and 79 TRACKER_CHANGED) is in [web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md).

## Menu

Hold **HOME** for the menu, in five tabs: **DISPLAY** (COLOR, STYLE, LARGE, ANIM, LEDS, SCREEN OFF, SCOPE, HOME),
**CONTROL** (HOLD, KNOB ACCEL, FX LATCH, BPM LOCK, SCALE LEDS, STEP PREVIEW), **AUDIO** (SPEAKER EQ,
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
  larger labels and values about twice the size; the graph below becomes a strip. Lists, the tracker's pages
  and the quick layers keep their layout with larger labels
- **ANIM:** ON, OFF or IDLE. OFF shows every change at once, without rolling digits or
  the LED sweep at power-on. IDLE is ON plus a gentle LED animation after a minute without input: a soft light drifts
  over the keys and the buttons breathe; any key, button or knob ends it and does what it always does. It never runs
  while playing, with a track armed, in a layer, the menu or a dialog
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
  brighter); not on DRUM or SLICE tracks
- **STEP PREVIEW:** OFF (default) or ON: on SEQ > PATTERN with the transport stopped, moving the cursor plays the
  cell it lands on once (its note, through its instrument)
- **SPEAKER EQ:** FLAT, LOWCUT or BASS+, a tone setting for the built-in speaker (not a switch). It also
  shapes the headphone out and USB audio, so keep it on FLAT when recording. The firmware cannot turn
  the speaker off, but headphones in the jack do
- **USB LEVEL:** MASTER (the MASTER knob sets the USB audio level too) or FIXED (USB always at full
  level, MASTER sets only the speaker and headphones)
- **CLICK:** OFF (default), REC (while a track is armed) or ON: a click on every beat while playing, the
  bar's first beat higher. **CLICK LEVEL:** LOW, MID (default) or HIGH; MASTER sets it too
- **TUNE:** the global tuning, −50 to +50 cents. It is the song's, saved and loaded with it
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

In the order the device lists them:

- **ANALOG**: virtual analog; two oscillators (WAVE SYNC: the second hard-synced to the first; SUB: a square an
  octave down), noise, drive, resonant low-pass filter
- **FM6**: classic 6-operator FM (Dexed-based): 32 algorithms, a full patch per track, edited in the
  web editor (which imports .syx files) or on the device: EDIT > OPERATOR (ratio or fixed frequency, fine, level),
  OP ENV (each stage's rate and level, drawn as the envelope) and OPERATOR 2 (mode, detune, velocity), one
  operator at a time on the algorithm chart; macros on top; SLOT picks a factory patch (F1–F8), the track's own (OWN) or a voice of the bank the web editor sent (B1–B32)
- **WAVE** (this fork): wavetables. TBL picks one of 8 tables of 8 frames each (BASIC: sine to narrow pulses;
  ADDTV: one partial to many; SYNC: a hard-sync sweep; RESO: a resonant peak climbing; VOX: vowels; FM: two-operator
  FM; DIGI: sparse, metallic spectra; BITS: stepped and 1-bit shapes), POS the place between its frames; the SHP
  routings move POS (ENV DEST / LFO DEST SHP, the matrix's SHP), so an envelope or the LFO scans the table. SYNC reads
  the frame up to 8x faster (windowed sync), DTN adds a detuned second oscillator, then DRV, a resonant low-pass and
  CRSH (sample-rate and bit reduction). The tables are band-limited per octave: high notes do not alias
- **WEST** (this fork): west coast. A complex oscillator, a wavefolder and a low-pass gate: a modulating sine at RATIO
  times the note (whole and half ratios, and 1.41, 1.73, 2.76, 3.55, 4.61 for bells and metal) moves the carrier's
  phase by FM (FDBK feeds the modulator back into itself, up to noise); FOLD folds the carrier over (the SHP routings
  move it), SYM shifts the fold off centre; the low-pass gate's cutoff follows the envelope through a vactrol-like lag,
  LPG of the way down from CUT, so a note darkens as it fades (the plucked, struck sound); TMBR opens FM and FOLD with
  the same envelope. FM and FOLD ease off on high notes so they do not alias
- **PHASE**: phase distortion (ported from CrispyZebra)
- **LOFI**: chiptune; pulse, triangle, saw, noise and a 4-bit wave RAM, stepped envelope, sweep, arpeggio
- **SAMPLE** (left out, `FELUCCA_SAMPLE=1`): multisampled instruments (PIANO, a light lo-fi piano; FLUTE; SAX) and
  3 user sample slots. PIANO HD, the fuller piano of 1.0 to 1.1.5, installs into a user slot from the web editor
- **VOICE**: formant oscillator, sung vowels
- **TRIO**: 3 oscillators with ring modulation and sync, multimode filter
- **WHEEL**: tonewheel-style organ; drawbar registrations, percussion, key click, drive, rotary speaker
- **GRAIN** (left out, `FELUCCA_GRAIN=1`): granular textures from the built-in samples or a user slot
- **PHYS**: physical models: modal resonators, strings, struck membranes, sympathetic strings
- **NOISE**: noise from analog to digital: colours, crackle, shift-register and metallic tones
- **BYTE** (this fork): bytebeat. A counter restarts at every note and runs at 8 kHz on C4 (following the key; RATE
  shifts it); FORM picks a formula of it (SAW, PWM, SIERP, XOR, OR, CHIRP, 42, MELO, OCTS, GLTCH, NOISE, MASK), A B C
  are its constants (A mostly the pitch, B how fast the pattern moves, C a mask or a melody); LOOP restarts the counter
  (1K .. 256K steps) so a pattern repeats while held; BITS and TONE shape the 8-bit output
- **SLICE** (left out, `FELUCCA_SLICE=1`): a drum break, a piano note (both built in) or your own sample cut into
  slices, one per key; set the slices by hand on the SLICES page
- **DRUM**: an 8-lane kit of Felucca's own drum voices on the General MIDI key map. KIT picks the
  standard kit or one of five virtual-analog (VA) kits in the manner of
  classic analog drum machines (the numbers are a hint), every lane its own voice: **80** (deep sine kick with a long decay, noisy snare,
  six-square hats), **10** (swept kick, white-noise snare, long-tailed claps, a cymbal), **66** (soft
  round kick, bright snare, noise hats, a conga), **55** (dropping kick, high metal hats, a metal bell)
  and **77** (swelling kick, multi-burst claps, claves, a cymbal). All of them are synthesized by
  Felucca, no samples. EDIT > LANES and LANES 2 set each lane's level

The DIGITAL engine of 0.9 has been replaced by FM6: projects and presets with DIGITAL sounds load
as FM6 sounds converted from them. The SAMPLE engine's PERC kit was removed in 1.0.2: sounds and
projects that used it load as the DRUM engine's kit, on the same key map. FM6's patch bank (the B
slots) was removed in 1.0.3: user presets keep their own FM6 patch, and presets that used a B slot get
that patch on the first start of 1.0.3. 1.4.1 brings B slots back as a voice bank sent from the web editor
(B1–B32): a voice picked from it becomes the track's own patch, so nothing saved depends on the bank. SLICE gained a second built-in sound in 1.0.4, PIANO (the SAMPLE
engine's middle C as it was then), next to BREAK. Since 1.0.4 a missing sample (an empty user slot, or a set missing
from the build) plays a plain sine at the note's pitch on SAMPLE, GRAIN and SLICE, and the screen says
NO SAMPLE once. DRUM's KIT variants HAND, CYM and H+CYM were retired in 1.0.5: sounds and projects that
used them play the 66, 10 and 77 kits. SAMPLE's PIANO became a lo-fi piano in 1.4 (PIANO HD above).

**This fork leaves out the engines that play recorded material** (SAMPLE, GRAIN, SLICE): the firmware has no built-in
samples and no user sample slots, which frees about 11 KB of RAM and 28 KB of the buffer pool (and the flash the
wavetables now use). Their engine numbers stay reserved, so nothing saved breaks: a sound of one (a project, a user
preset, a song's instrument, the web editor) loads as the nearest engine built, with its first preset: SAMPLE as FM6
(TINE EP), GRAIN as WAVE (DUB CHORD), SLICE as DRUM. A complete backup restores without its samples. Build them back
with `FELUCCA_SAMPLE=1`, `FELUCCA_GRAIN=1`, `FELUCCA_SLICE=1` (BUILDING.md); the paragraphs above about them apply
to such builds.

**SLICER** (FX page, every track): a tempo-synced 16-step gate or stutter, with 16 patterns.

**Reverb** (FX > REVERB, TYPE): **HALL** (since 1.4, the default on a fresh start: eight delay lines feeding each
other with slowly drifting taps, stereo, SIZE the decay up to about 4.5 s, DAMP how fast the highs die), **ROOM** (the
default up to 1.1.5) and **SPRING**. A project plays the TYPE it was saved with.

## Scale keyboard

On the **SCL** page, set **QNT** to WHITE to play the selected scale using only the
white keys (SNAP keeps every key and rounds it down to the scale). C4 plays **ROOT**; consecutive white keys play consecutive scale notes
above and below it. Black keys are silent, including during live recording and
step entry. **TRN** transposes the resulting notes; the octave buttons shift them
by full octaves. Set QNT to OFF for the normal chromatic keyboard. QNT SEQ snaps the keys like SNAP and
also maps the tracker's notes onto the current ROOT / SCALE as they play (the cells are not changed; drum
kits are never quantized).

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
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | UI font, icon names, CC0 instrument samples |
| `web/` | the web installer, the editor with the tracker (`editor.html`, protocol in `EDITOR_PROTOCOL.md`) and the browser emulator (`web/emu/`); the site's editor: [Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp) |
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

If Felucca is useful to you, [sponsoring on GitHub](https://github.com/sponsors/hugelton) or a donation
on [itch.io](https://hugelton.itch.io/felucca) helps keep its development going.

Issues are for reproducible bugs (one per issue). Ideas and requests go to
[Discussions](https://github.com/hugelton/Felucca/discussions), and feature requests posted as issues will be
moved there. Pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

## AI disclaimer

Felucca is developed with the assistance of AI coding agents. These tools are used for coding, testing, documentation, translation, and maintenance.
The instrument's design, features, sound design, and overall direction are determined by the maintainer or community.
No generative AI is used to create music, icons, or visual artwork for this project.
For more details, see [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits

- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  Felucca itself; the PHASE engine's waveforms (a C port of the oscillator of
  [CrispyZebra](https://github.com/hugelton/CrispyZebra), GPL-3.0); the DRUM voices and kits; the Hügelton Sample
  Pack (the drum samples, GPL-3.0-only, not CC0); the [Fukiai](https://github.com/hugelton/Fukiai) icon
  font ([MIT](LICENSES/MIT-Fukiai.txt))
- Fonts: [Inter Tight](https://github.com/rsms/inter-tight) by The Inter Project Authors, [SIL OFL 1.1](LICENSES/OFL-InterTight.txt);
  the browser emulator's labels: [DotGothic16](https://github.com/fontworks-fonts/DotGothic16) by The DotGothic16 Project Authors, [SIL OFL 1.1](LICENSES/OFL-DotGothic16.txt)
- Samples: [Versilian Studios](https://versilian-studios.com/) [VSCO-2 Community Edition](https://github.com/sgossner/VSCO-2-CE) and [VCSL](https://github.com/sgossner/VCSL), CC0 1.0: the SAMPLE sets, also SLICE's PIANO and PIANO HD ([attribution](assets/samples-cc0/ATTRIBUTION.txt))
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- PHYS engine: models ported from [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith and Emilie Gillet ([MIT](LICENSES/MIT-DaisySP.txt)) and from Emilie Gillet's [eurorack](https://github.com/pichenettes/eurorack) code ([MIT](LICENSES/MIT-Rings.txt))
- FM6 engine: msfa from [Dexed](https://github.com/asb2m10/dexed) by Google Inc. and Pascal Gauthier ([Apache-2.0](LICENSES/Apache-2.0-msfa.txt))
- Browser emulator: after [X0X](https://github.com/charlesvestal/fm1-x0x) by [charlesvestal](https://github.com/charlesvestal) (GPL-3.0), a Felucca fork whose browser build showed the way
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) ([Apache-2.0](LICENSES/Apache-2.0.txt); three of its files are in every package, none in this tree)
- Contributions: [keremimo](https://github.com/keremimo) (white-key scales, #2), [ChanceTheMaker](https://github.com/ChanceTheMaker)
  (TRS MIDI, bend, sustain and clock, palettes, favourites, editor display settings: #8, #10, #11, #12),
  [andreahaku](https://github.com/andreahaku) (sample recording and trim, #29; SLICE manual slices and tests, #27, #22),
  [spinkham](https://github.com/spinkham) (the boot fix for 0.9 projects, #111),
  [zednaked](https://github.com/zednaked) (ratchets, #100),
  [jasonpersinger](https://github.com/jasonpersinger) (the ROOM reverb click fix, #121)

## Licence

Free software: [GPL-3.0-only](LICENSE), the Hügelton Sample Pack included. The bundled fonts and the
ported DSP keep their own licences ([LICENSES/](LICENSES/)); details in [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
