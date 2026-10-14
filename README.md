# Fel-Tracker

**Fel-Tracker** is a fork of [Felucca](https://github.com/hugelton/Felucca), the multi-engine synthesizer firmware
for the M-VAVE FM-1 by Leo Kuroshita ([Hügelton Instruments](https://hugelton.com)). It is developed by
**Tristano Suriani** (<https://github.com/TristanoSuriani/fel-tracker>). It is an independent fork, not an official
Felucca release. Version **0.10.0** (its own numbering; it grew from Felucca 1.4.1). On the device, the editor and
the browser emulator it introduces itself as Fel-Tracker, naming Felucca and its makers as what it is built on.

What Fel-Tracker adds, all by Tristano Suriani:

- **The tracker**, in place of the step sequencer.
- **Distributions:** builds of any set of up to 16 engines, with stable engine ids, on 4 or 8 tracks, each with its
  own palette (the device's and the emulator page's: IDM electric cyan and magenta on indigo, FOUNDRY molten orange,
  WAREHOUSE acid green and UV violet). The **IDM**
  distribution is the default, with **8 tracks**: the tracker, the MIXER and the quick layers show the selected
  track's four (1 .. 4 or 5 .. 8), every black key of the GLO and FX layers mutes its track, songs of 4 tracks load
  into its first four (BUILDING.md "Distributions"). At power-on the rhythm section comes first and sits in the
  centre: tracks 1–4 the drums (KIT, BREAKS) and the basses (SID BASS, WASP's FAT BASS); 5–8 the chords (DUBCHORD),
  the pad (VOSIM's FORMANT PAD), the lead (SID SYNC LEAD) and the siren, panned left and right in turn. On 4 tracks:
  the kit and the bass, then the pad and the lead apart.
- **Twenty-three new engines:** WAVE, BYTE, WEST, HOOVER, SPEECH, FMPERC, KARPLUS, VOSIM, PARTICLE, CHAOS, SID, DUBSIREN,
  DUBSIREN8 (SIREN8), DUBCHORD, BLISS, WASP, MODAL, VECTOR, ECHOES, GLITCH, KIT, BREAKS and RISERS.

Everything else is Felucca, Leo Kuroshita's work. [AUTHORS.md](AUTHORS.md) says in detail whose work is whose. Below,
Fel-Tracker's parts are marked *(Fel-Tracker)*.

The rest of this README started as Felucca's. Its install links, site, sponsorship and issue tracker are Felucca's
(Hügelton Instruments). They install official Felucca, not Fel-Tracker; to build Fel-Tracker, see
[BUILDING.md](BUILDING.md).

---

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

- **Native image** *(Fel-Tracker)*: Fel-Tracker as a desktop application for each distribution (`native/build.sh`:
  `Fel-Tracker IDM.app` on macOS), the same firmware in a window with the panel, the computer's sound out and MIDI in,
  its saves in a file: the base for heavier engines and effects than the FM-1 can hold. Every track has its own 16
  voices there, three more reverbs (CONCERT: HALL at the full rate and twice the lines; CAVERN: a cave's echoes and
  a 30 s tail; SHIMMER: a tail climbing in octaves), a 23.8 s delay line, and a distribution of its own, IDM NATIVE,
  with the native engines NEBULA and CANYON (BUILDING.md "Native image")

- **Sixteen engines** in a distribution (below): IDM, the one a plain build makes, FOUNDRY and WAREHOUSE (BUILDING.md
  "Distributions"), each with its own factory presets; every factory sound has a category (BASS, LEAD,
  PAD, PLUCK, KEYS, DRUM, FX, OTHER) to browse by
- **Four tracks**, one synth part each with its own engine and sound (drums are KIT, BREAKS or WAR);
  12 voices shared between them (Fel-Tracker; 8 when the device is busy: voices 9 .. 12 start only while the audio's
  measured load is under 75 %, a heavy song keeps to 8 on its own). ALGORITHM selects the track on every page
- **Tracker** *(Fel-Tracker, by Tristano Suriani; replaces Felucca's step sequencer)*: 24 patterns of up to 64 rows (a row is a 1/16), a column per
  track; each cell a note (or OFF) with an optional chord (up to three notes: a shape such as MAJ or MIN7, or two
  intervals), an instrument (01..3F, changing the track's sound on any row) and one FX (a parameter set for that row only: LEVEL,
  ATTACK, DECAY, SUSTAIN, RELEASE, CUTOFF, RESONANCE, FLT ENV, PAN, DIST, CHORUS, DELAY, REVERB, GLIDE). PLAY on
  PATTERN loops the pattern; PLAY on SONG plays the order list (up to 128 positions), each position a pattern in one
  of its variations (A..D: its own tempo, mutes, note chance, sends and SLICER over the tracks'). The instruments are factory or
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
- **SPREAD** (EDIT > VOICE 3): the voices of a POLY or UNISON track alternate left and right of its PAN; **OCT** beside it moves
  the whole sound up or down by octaves (a preset can set it: FMPERC's kicks play 3 octaves under the note)
- **Effects:** distortion and the SLICER per track; chorus, delay and reverb sends (the reverb as
  **HALL**, ROOM or SPRING; HALL is a smooth stereo reverb, the default on a fresh start; the delay **CLEAN**,
  **TAPE**, **WORN** or **PING** on DLY 2: TAPE and WORN a tape echo with wow and flutter, repeats that darken and
  saturate and run away into a howl past FDBK ~95; PING echoes left, then right, and so on, up to 0.74 s); master
  limiter. *(Fel-Tracker)* The chorus and ROOM are stereo (the chorus's two sides sweep in opposition; SPRING and the
  tape echoes stay mono, as the real ones are), and the delay's and the reverb's sends lose their lows below about
  220 Hz first, so the bass and the kick stay dry and the tails stay clear. A gentle bus compressor (3:1, slow) sits in
  front of the master limiter, so a full mix keeps its punch at the top of MASTER instead of being held down all the time
- **FX layer:** hold FX for repeat, reverse, filter sweeps, tape stop, freeze, a harmonizer
  (OCT UP / OCT DN with shimmer), and mutes on the black keys;
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
- (Fel-Tracker: FM6 and DRUM are gone, with the bank and the DRUM quick actions; KIT has the lane mutes on the black
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
| EDIT | EDIT 1, EDIT 2, VOICE, VOICE 2, VOICE 3 (FM6 adds OPERATOR, OP ENV, OPERATOR 2; DRUM LANES) | the EDIT layer (the engines; on DRUM the black keys mute lanes) |
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
or UNISON track plays the note alone. The tracks share 12 voices (8 when the device is busy), so chords on every track
at once take voices from each other.

**Changing the instrument on a step**: on PATTERN, KNOB 1 to the row, ALGORITHM to the column, KNOB 3 the instrument
(`01`..`3F`; `..` keeps the one the column played last). The instrument picked is also the one new notes are entered
and recorded with. What each number plays is set on INSTR (below).

**SONG** (SEQ again, or GLO): the order list, each position a pattern and its variation (`PATTERN 03B`). KNOB 1 the
position (past the last: + ADD), KNOB 2 its pattern (on + ADD: a new position), KNOB 3 its variation (past the
pattern's last: a new one, a copy of A; up to D), KNOB 4 right a copy after it, left deletes it (EDIT too). A
pattern's length is SEQ held + KNOB 2. PLAY plays the song from the position picked, round and round; the PATTERN page
then shows the pattern playing.

**VARIATION** (SEQ after SONG) *(Fel-Tracker)*: a pattern plays the same notes in up to four variations, A..D, each
setting over the tracks while its position plays:

| Row | Sets | KEEP / default |
|---|---|---|
| TEMPO | the tempo, 40..240 BPM (KNOB 1) | SONG: the song's |
| MUTE | a track silent (KNOB 1–4: right mutes, left plays) | PLAY |
| CHANCE | the chance each of the track's notes plays, 0..100 % (the same notes at every PLAY) | 100 % |
| DIST, CHORUS, DELAY, REVERB | the track's send | KEEP: the track's own |
| SLICER, SL PAT, SL RATE | the track's SLICER mode, pattern and rate | KEEP: the track's own |

It edits the variation of the SONG page's position (past the last position: the one LOOP plays). PRESETS picks the
row; KNOB 1–4 set it for the tracks in view; EDIT puts the selected track's value back (TEMPO: SONG; a send or the
SLICER: KEEP). Turning a value up from KEEP starts at the track's own. An edit while the variation plays is heard at
once, and PLAY on this page loops its pattern in it. The tracks' own settings never change: STOP returns to them. A
new variation starts as a copy of A; the web editor also removes them (its entries then play A).

**SETTINGS**: each track's ATTACK, DECAY, SUSTAIN, RELEASE, CUTOFF, RESO, FLT ENV, DIST, CHORUS, DELAY, REVERB, LEVEL,
PAN, WIDTH and LOW CUT. PRESETS picks the row, KNOB 1–4 set it for tracks 1–4. CUTOFF and RESO hold the engine's filter
over every instrument; INST (EDIT) leaves it to the instrument. *(Fel-Tracker)* **WIDTH** (OFF, 1–20 ms; the last four
tracks only, or the last two of four, so the rhythm section stays in the middle) is the Haas effect: the track's highs,
above about 250 Hz, play a little late on one side and its lows as they are on both, so it widens without hollowing
the bass in a mono sum; the late side changes track by track (track 5 sits left, 6 right, and so on, as their pans).
**LOW CUT** (OFF, 40, 60, 80, 120, 160, 240, 320 Hz) takes a track's lows away after DIST and the SLICER, so pads and
leads leave the bass its room. At power-on the wide tracks have WIDTH 12 ms and LOW CUT 120 Hz, the rhythm section
neither; EDIT puts that back. The song keeps them.

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
- **Song:** the order list: a pattern and its variation per position (`+`: a new variation), ▶ plays the song from
  there, × removes it, + ADD appends one. Picking another pattern while a loop plays cues it, as on the device
- **Variations:** a pattern's A..D: + Variation adds one (a copy of A), *Remove the last* removes one; the tempo (empty:
  the song's), and per track MUTE, CHANCE, the sends and the SLICER (empty / KEEP: the track's own)
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
  brighter); not on DRUM tracks
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

Felucca's own engines, by Leo Kuroshita (ANALOG, FM6, PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE and DRUM), are not in
Fel-Tracker any more (below); what other engines build on of them stays (PHYS's resonators in MODAL and ANVIL, DRUM's
lanes and General MIDI map in KIT, WAR and GLITCH).

Fel-Tracker's engines, by Tristano Suriani, marked *(Fel-Tracker)* below: WAVE, BYTE, WEST, HOOVER, SPEECH, FMPERC,
KARPLUS, VOSIM, PARTICLE, CHAOS, SID, DUBSIREN, DUBSIREN8 (SIREN8), DUBCHORD, BLISS, WASP, MODAL, VECTOR, ECHOES,
GLITCH, KIT, BREAKS and RISERS.

A build holds the engines of its distribution (BUILDING.md "Distributions"). In the order the device lists them:

- **WAVE** *(Fel-Tracker)*: wavetables. TBL picks one of 8 tables of 8 frames each (BASIC: sine to narrow pulses;
  ADDTV: one partial to many; SYNC: a hard-sync sweep; RESO: a resonant peak climbing; VOX: vowels; FM: two-operator
  FM; DIGI: sparse, metallic spectra; BITS: stepped and 1-bit shapes), POS the place between its frames; the SHP
  routings move POS (ENV DEST / LFO DEST SHP, the matrix's SHP), so an envelope or the LFO scans the table. SYNC reads
  the frame up to 8x faster (windowed sync), DTN adds a detuned second oscillator, then DRV, a resonant low-pass and
  CRSH (sample-rate and bit reduction). The tables are band-limited per octave: high notes do not alias
- **WEST** *(Fel-Tracker)*: west coast. A complex oscillator, a wavefolder and a low-pass gate: a modulating sine at RATIO
  times the note (whole and half ratios, and 1.41, 1.73, 2.76, 3.55, 4.61 for bells and metal) moves the carrier's
  phase by FM (FDBK feeds the modulator back into itself, up to noise); FOLD folds the carrier over (the SHP routings
  move it), SYM shifts the fold off centre; the low-pass gate's cutoff follows the envelope through a vactrol-like lag,
  LPG of the way down from CUT, so a note darkens as it fades (the plucked, struck sound); TMBR opens FM and FOLD with
  the same envelope. FM and FOLD ease off on high notes so they do not alias. Its plucks (LPG HARP, NYLON GATE, KOTO
  FOLD, THUMB WOOD, BUZZ PLUCK, PIZZ GATE, GLASS PLUCK, RUBBER PLUCK) take KARPLUS's place in the IDM distribution
- **HOOVER** *(Fel-Tracker)*: the early-90s rave lead, after the Alpha Juno's patch. Two oscillators, each a saw plus the
  same saw PW of a cycle on (a pulse-width-modulated saw: sweeping PW moves notches through the spectrum), PWM sweeping
  PW with the voice's own LFO at RATE (the SHP routings move PW too), DTN detuning the second, SUB a square an octave
  down; BEND starts each note up to two octaves away and slides it in over BTIM (the swoop); CUT, a low-pass. Its
  presets send plenty to the chorus, which makes the rest of the sound
- **FMPERC** *(Fel-Tracker; in no distribution since IDM gave its slot to DUBSIREN8: its voices play in KIT and BREAKS,
  and a stored FMPERC sound loads as KIT)*: FM percussion. A modulating sine at RATIO (whole, half and inharmonic ratios)
  moves a carrier's phase by INDEX, and every hit sweeps both down over FDEC: the pitch from SWEEP semitones above the
  note, the index to a tenth, so a hit starts bright and high and settles. FDBK (towards noise); NOISE mixes in noise
  of the kind NTYP sets (SNAP a short burst, CLAP four bursts and a tail, HISS as long as the note); TONE darkens below
  0, thins above. 23 presets: six kicks (deep, sub, hard, distorted, punchy), snare, two claps, rim, closed and open
  hats, crash, ride, toms, conga, clave, cowbell, metal, a zap, a bell, a glitch
- **KARPLUS** *(Fel-Tracker; in no distribution since IDM gave its slot to DUBCHORD: a stored KARPLUS sound loads as
  PHYS where there is one, else WEST)*: a plucked string, Karplus-Strong. EXCT fills the string at the pluck (NOIS,
  PLUK filtered by BRIGHT, SAW, CLIK), POS plucks nearer the bridge or the middle, DAMP lets the highs die first, DECAY
  rings 0.1 .. 10 s, STIFF sharpens the overtones towards a bell, MUTE damps it on release, DRIVE saturates. Presets:
  nylon, steel, harp, koto, pizzicato, muted bass, dub and acid plucks, a bell string, a dulcimer
- **VOSIM** *(Fel-Tracker; the IDM distribution)*: VOice SIMulation, two formants. Each period starts a train of PULS sin^2 pulses,
  each DECAY times the last; their width sets the formants F1 and F2, which stay put whatever the note (KTRK makes them
  follow it). SWEEP moves them at each note and lets them glide back (wah, brass, "yow"); BAL mixes them; BRTH adds
  breath. Presets: four vowels, talk lead, basses, brass, yow, choir, reed, whistle, growl, formant pad
- **PARTICLE** *(Fel-Tracker; the IDM distribution)*: a cloud of synthesized grains. DENS grains a second (1 .. 400), each SIZE long
  (2 .. 200 ms), at the note give or take SCAT semitones (QNT: free, semitones, octaves, fifths), gliding CHIRP; WAVE
  sine, triangle, FM or noise under SHAPE (a hit, a swell, backwards); TONE. Rain, bubbles, crackle, swarms, stardust,
  grain pads, drips, insects
- **CHAOS** *(Fel-Tracker; the IDM distribution)*: chaotic systems as oscillators. MODEL: the Lorenz and Rossler attractors (flows
  run at the note's speed: tones that wander) or the logistic and Henon maps (a new value each period: buzz that
  changes every cycle). CHAOS is the system's parameter, from order to wild; OUT the variable heard; SMTH; FOLD; SINE
  mixes in the note's sine; SEED: the same attack every note or a new one; TONE
- **SID** *(Fel-Tracker; the IDM distribution)*: a voice of the C64's sound chip, in its manner. WAVE TRI SAW PUL NOIS (the chip's
  pitched shift-register noise) or the chip's combined waves (TS PT PS PTS); PW, PWM; MOD RING / SYNC / both against a
  master at the note, the oscillator MDTN semitones above it (the sync lead, ring bells); FILT the 12 dB multimode
  filter (LP BP HP) at CUT with RES, a little gritty. The SHP routings move PW and MDTN
- **DUBSIREN** *(Fel-Tracker; the IDM distribution)*: the sound-system siren box with its own tape echo. WAVE, an LFO on the pitch
  (RATE, DEPTH, SHAPE: two-tone, whoop up or down, wail), SWEEP (the dive or the rise), TIME (turned while it sounds,
  the echoes bend), FDBK (runs away while held, settles when let go), MIX. ATK and REL are its envelope; the voice
  lasts until the echoes die away. It plays on the last track only (track 4; its 16-bit echo is that track's): on
  another track it shows dimmed, and a DUBSIREN sound loaded there (a pick, a song, an instrument, a user preset)
  becomes SIREN8 with the same preset and values where the build has it (IDM no longer does: there it plays dry)
- **SIREN8** (DUBSIREN8) *(Fel-Tracker; in no distribution since IDM gave its slot to MODAL)*: DUBSIREN's 8-bit cousin, on every track. The same siren
  and the same 8 controls, made of fewer bits: the siren held a sample in two and cut to 8 bits after its envelope (a
  fading siren grows grittier), the echo 8 bits a sample (464 ms in 5 KB a track), each pass rougher. Its first 12
  presets are DUBSIREN's 12 in 8 bits, then ARCADE ZAP, DIRTY DUB, POCKET HORN, BROKEN TAPE
- **DUBCHORD** *(Fel-Tracker; the IDM distribution)*: the dub-techno chord stab, a whole chord in one voice: a key plays
  CHORD on its note (MIN7, MIN9, MIN11, MAJ7, SUS2, SUS4, 5THS, MIN; the same quality wherever the root goes), four notes
  through one resonant low-pass, so a chord costs one voice of the budget. VOIC CLOSE, OPEN, SPREAD; WAVE SAW, SQR, SINE,
  ORGAN; DTN detunes the notes in turn; CUT, RES, and the filter's ENV and DEC (the stab's opening). Its presets send
  plenty to the delay (TAPE suits it) and the reverb; up to 4 chords a track
- **BLISS** *(Fel-Tracker; FMKEYS until 2026-10; the FOUNDRY distribution; elsewhere a stored BLISS sound loads as
  WAVE)*: FM keyboards, four operators as two pairs: the body (RATIO, INDEX, FDBK; the
  index falls over DECAY, harder notes brighter by VEL) and the tine (the metallic strike of the attack, TINE; DTUNE
  its beating against the body); KSCL softens the top of the keyboard. Electric pianos, bells, tubular bells, marimba,
  vibes, clav, organ, harpsichord, kalimba, celesta, music box; Rhodes, Wurly, the DX and LATELY basses, FM bells,
  crystal, an FM pad, brass, flute, a harp pluck
- **WASP** *(Fel-Tracker; the IDM distribution)*: two oscillators into a gritty multimode filter, after the little
  black-and-yellow synth of 1978. WAVE pairs them (SAW, SQR, a 25 % PUL, NOIS), OSC2 sits -24 .. +24 semitones away,
  DTN detunes them (each voice drifts a little too), MIX balances them. A 12 dB state-variable filter (MODE LP, BP, HP,
  NOTCH; CUT, the key half tracked) keeps its lows however hard RES is pushed (its passband stays put while the peak
  rises: no thinning); from ~115 it sings by itself, past that a saturating, unsteady self-oscillation. GRIT drives
  the filter and cuts its integrators to fewer bits (16 .. 8): steps, fizz and limit cycles as it rings. 30 presets:
  leads, basses resonant and not, reeses, pads, keys, a clav, self-oscillating and gritty oddities
- **MODAL** *(Fel-Tracker; the IDM distribution; its resonators are PHYS's, DaisySP's)*: a modal resonator. An exciter
  strikes 12 tuned resonators, the modes of a material: MATRL BAR, MRMBA, BELL, GLASS, PLATE, DRUM, TUBE, STRING; EXCIT
  MALLET, PLUCK, BOW (noise while the key is held), CLICK. STRUCT stretches the modes (inharmonic), POS the strike's
  place, DECAY the ring (50 ms .. 8 s), DAMP how much sooner the highs die, BRIGHT the strike's hardness, BEAT detunes
  the modes a little each (a bell's beating). Presets: marimba, vibes, bells, glass, plates, drums, harp, kalimba,
  gamelan, bowed glass and bells, a drone pipe, clanks and glitches
- **VECTOR** *(Fel-Tracker; the IDM distribution)*: vector synthesis. Four of WAVE's frames at the corners of a square
  (SET: VOXSTR, GLASS, ORGAN, DIGI, AIR, METAL, CHOIR, SYNTH), a point moving over it: PATH HOLD (at X, Y), ENV (each
  note round the corners, then to X, Y), LOOP, CIRCLE, EIGHT, RANDOM, RATE ~0.02 .. 5 Hz; the SHP routings move X too.
  DTN detunes A, C against B, D; CUT, RES. Presets: evolving pads, morphing choirs and organs, keys, a moving bass
- **ECHOES** *(Fel-Tracker; the IDM distribution)*: a note and its echoes as one instrument; every repeat is played
  anew, so each can differ. SRC (BLIP, PLUCK, STAB, NOISE, FM), then COUNT repeats at the tempo: TIME (1/4 .. 1/64,
  dotted, triplets), SHAPE (EVEN, BOUNCE: each shorter, SLOW, SWING, RANDOM), DECAY (what each keeps), PITCH (semitones
  a repeat), TONE (each darker or thinner), SCAT (chance in time and pitch). The echoes go on after the key is let go
- **GLITCH** *(Fel-Tracker; the IDM distribution)*: a kit of micro-sounds, a key each from C2 (as DRUM): CLICK, POP,
  TICK, ZAP, BITERR, DATA, DROP, FAX, MODEM, CRACKL, SPARK, BLIP, SHARD, BUZZ, SKIP, PING (the octave above: the same,
  an octave up). KIT CLEAN, HARSH, LOFI, BROKEN; PITCH, DECAY, ROLL (a ratchet at the tempo while held, 1/32 ..
  1/128), CRUSH, VARY (each hit different), DENS (extra micro-hits), TONE
- **WAREHOUSE** *(Fel-Tracker; the warehouse raves: four on the floor, acid, hoovers, sirens, chords and voices; 8
  tracks, its own palette RAVE, acid green and UV violet on concrete)*. The engines no other distribution has (but KARPLUS
  and HONKS; BYTE gave its place to three of its own: LINNKIT, STRINGS and FLOOR, below), with BREAKS, VOSIM,
  DUBCHORD and KIT: LINNKIT, KIT, FLOOR, BREAKS, FMPERC, CHOKE, GUTS, VENOM, HOOVER, DUBCHORD, STRINGS, VOSIM, SIREN8, ABYSS, SHRED,
  RISERS.
  - **LINNKIT**: the 80s sampled drum machine, after the LinnDrum (synthesized, no samples): a key each from C2, KICK,
    SNARE, RIM, CLAP, HAT, OPEN, TOM L, TOM M, TOM H, CONGA, COWBELL, TAMB (the octave above the same again); each a
    body with a falling pitch and noise; TUNE, DECAY, TONE, SNAP (noise against body), BODY (the pitch's fall), GATE
    (the 80s gated tail), CHAR (the sampler's grit: down towards 12 kHz and 8 bits), VEL
  - **STRINGS**: a string ensemble, after the Waldorf Streichfett: saws at 16', 8' and 4' and a square, blended by REG
    from contrabass through cello, viola and violin to organ (BODY the 16' on top, DTN their looseness), a gentle
    low-pass (BRITE; ANIM sweeps it slowly), TREM; then the ensemble on the part: three reads of a short line moved by
    a slow and a fast LFO (SPEED), ENSEM its share. A string machine's shimmer; SPREAD on the track leaves it out
  - **FLOOR**: four-on-the-floor beats, as BREAKS is the breaks: the key picks one of 12 one-bar grooves (C TECHNO,
    C# HOUSE, D DEEP HOUSE, D# TECH HOUSE, E MINIMAL, F DUB TECHNO, F# TRANCE, G PROGRESSIVE, G# GABBER, A HARD TECHNO,
    A# EDM, B DISCO), looping while held at the song's tempo, each with its own kick (a gabber kick is no disco kick);
    kick, clap, hats (closed, open) and percussion on FMPERC's voices; SWING, the lanes' levels (KICK, CLAP, HATS,
    PERC), TUNE, DECAY, DRIVE SIREN8 plays on every
  track (there is no DUBSIREN to keep the last one). At power-on: KIT, FLOOR, GUTS' GUT ACID, HOOVER's REESE BASS,
  DUBCHORD, VOSIM's FORMANT PAD, HOOVER and SIREN8's BIT SIREN
- **IDM NATIVE** *(Fel-Tracker; the native image's alone: `native/build.sh idm_native`, no FM-1 build)*: IDM with its
  two most CPU-bound engines as the native engines grown from them, which share their controls (a sound loads on the
  FM-1 as the engine it grew from): SID, WASP, WAVE, BREAKS, WEST, VECTOR, DUBSIREN, MODAL, DUBCHORD, VOSIM, NEBULA,
  CHAOS, CANYON, KIT, SPEECH, GLITCH.
  - **NEBULA**: PARTICLE's cloud whole: 64 grains at once a voice (PARTICLE: 8), 12 voices, the level from the
    overlap's square root (a dense cloud stays loud); PARTICLE's presets and NEBULA, CHOIR CLOUD, OCEAN, FIFTHS HAZE,
    BELL MIST, SHEPARD SKY, DEEP SWARM, STATIC FIELD
  - **CANYON**: ECHOES' repeats, all of them: COUNT up to 64, every repeat ringing to its end (ECHOES keeps 6), 8
    voices; ECHOES' presets and CANYON, BELL CASCADE, PLUCK STORM, ENDLESS DUB, LONG BOUNCE, FM RAIN, WHOLE TONE,
    AVALANCHE
- **FOUNDRY** *(Fel-Tracker; industrial, neuro, dark bass, and the clean sounds it lives of in contrast; 8 tracks,
  its own palette, molten orange)*. Its 16 engines, in their order on the device: SCREAM, DARK, VOSIM, WAR, SCRATCH,
  FLUIDS, RAZOR, DUBCHORD, RUST, ANVIL, DRILL, SLAG, MODAL, BLISS, ECHOES, WAVE. Every one has at least 24 presets.
  The clean side is VOSIM's choirs and voices, DUBCHORD's sine and organ chords, MODAL's chimes and bars, BLISS's FM
  pianos and bells, ECHOES' soft repeats and WAVE's tables (each described above); DARK, ANVIL and FLUIDS each have a
  clean block too (pure subs, true bells, still water). At power-on: WAR's kits, a DARK and a RAZOR bass, DUBCHORD,
  VOSIM's FORMANT PAD, RAZOR LEAD and SCREAM's SIREN CRY. Its own engines:
  - **DARK**: sub-bass from sines: SUB1 and SUB2 octaves under the note, a sine folder (FOLD, FENV for each note's
    bite), PUNCH (the pitch from up to two octaves above, falling over PDEC: an 808's thud), DRIVE, TONE
  - **RAZOR**: up to 7 saws spread apart (VOICES, SPREAD: reeses, supersaws), SUB, NOISE, DRIVE into a resonant
    low-pass (CUT, RES) and EDGE, a high-pass after it (thin, icy, laser cuts)
  - **SLAG**: hot walls and drones: 2 .. 4 oscillators wandering in tune (DRIFT), HEAT's saturation, a low-pass that
    MELT lets sink over each note, NOISE sparks
  - **WAR**: an industrial kit, a key each from C2 (as DRUM): KICK, HARD, SNARE, CLAP, HAT, OPEN, TOM L, TOM H, METAL,
    ANVIL, CRASH, PEDAL (a pedal hat; the octave above the same again); each an FM pair with a fast falling pitch
    (PUNCH) and noise; TUNE, DECAY, TONE (a low-pass), METAL, NOISE, DIST, CRUSH
  - **RUST**: corroded noise: COLOR, TONE (a pulse at the key), RATE and BITS reduction, CORRUPT (bit flips, jumps),
    METAL (two resonances, RES), FILTER
  - **DRILL**: power tools: an FM tone (FM, RATIO) ramped at audio rate (RATE, DEPTH, SHAPE), ROLL (tempo-synced hits
    while held, 1/32 .. 1/128), DRIVE, TONE
  - **ANVIL**: heavy metal struck: MODAL's resonator with ANVIL, PIPE, GONG, CHAIN, BEAM, TANK, SLAB, CAST; HAMMER,
    SCRAPE, BOW, CLICK
  - **SCREAM**: a voice breaking under terror (mono): a buzzing glottal pulse at the key made unstable period by
    period: STRAIN (the effort, building while held: higher, rougher, more broken), ROUGH (30 .. 150 Hz roughness),
    BREAK (cracks up an octave or a fifth, falls into the half or third, chaos bursts), CHAOS (jitter), VOWEL (A .. I),
    SHRILL (a 2.4 .. 3.9 kHz peak), RASP (breath let through the open folds), CROWD (1 .. 3 voices out of step)
  - **FLUIDS**: liquid waves: WAVE (sine .. saw .. square) through 2 .. 8 all-passes (STAGES, DISP) mixed back,
    MOTION and DEPTH moving them, a resonant low-pass (CUT, RES), SUB
  - **SCRATCH**: a turntablist's hands on a worked-out record (SRC STAB, VOX, KICK, NOISE): MOTION BABY, FLARE, CHIRP,
    TEAR, REWIND, STOP, SCRUB at the tempo (RATE), DEPTH, FADER, CRACKLE, TONE, DRIVE
  - Left FOUNDRY for the clean ones (in no distribution now; a stored sound of one loads in FOUNDRY as RAZOR for
    GUTS and VENOM, RUST for CHOKE, ECHOES for ABYSS, SCRATCH for SHRED, VOSIM for HONKS):
    - **HONKS**: brass barks and sax stabs: a reed (BRIGHT, BRTH) through two formants (F1, F2, RES), each note scooped up
      into (SCOOP), GROWL a ~35 Hz flutter, DRIVE
    - **GUTS**: filthy acid: SAW, TRI or SQR through a lopsided sine folder (FOLD, ASYM), a resonant low-pass opened by
      ENV over DEC (accents at velocity 100 and up), DRIVE
    - **VENOM**: caustic acid: a low-pass and a band-pass that move each other's cutoff at audio rate (XMOD), RES, ENV,
      DEC, DRIVE
    - **CHOKE**: gated noise, hard limited: a band-pass at the key (TONE, Q), BODY, SNAP, held for GATE then shut over
      TAIL, SWEEP (an air-brake's fall), LIMIT
    - **ABYSS**: a cavern that rings: one voice a track strikes (SRC PULSE, BURST, TONE, DRONE) four 8-bit delay lines
      in feedback (SIZE, FDBK, DAMP, TUNE towards the key, MOD, DRIVE in the loop), MIX
    - **SHRED**: a sound torn apart as it plays: SRC (SAW, SQR, FM, NOISE) into a 35 ms ring read by two grains (GRAIN,
      STRETCH, PITCH), STUTTER, TEAR (jumps, backwards), SYNC to the grid, MIX
- **KIT** *(Fel-Tracker; the IDM distribution)*: a whole drum kit on one part, of FMPERC's voices, on DRUM's lanes and General MIDI
  map (its lane LEVELs and the EDIT layer's lane MUTEs too). KIT: FM, METAL, BIT, GLITCH, DEEP; TUNE, DECAY, PUNCH, FM,
  SNAP, TONE move every lane; VARY gives every hit its own small differences. A closed hat chokes the open one
- **BREAKS** *(Fel-Tracker; the IDM distribution)*: a synthesized breakbeat player at the song's tempo. The key picks one of 12 breaks
  (FUNKY, CLASSIC, THINK, HALFTIME, BOOM BAP, JUNGLE, TWO STEP, SHUFFLE, BUSY, DRILL, SPARSE, GALLOP), looping while
  held; SLICE the 16th it starts from (chops), RATE (half .. double time), SWING, STUTR (rolls into 32nds and 64ths),
  SHUF (jumps to other 16ths), TUNE, DECAY, CRUSH
- **RISERS** *(Fel-Tracker; in no distribution since IDM gave its slot to GLITCH: a stored RISERS sound loads as
  NOISE where there is one, else SID)*: build-ups and landings timed to the song: LEN beats (1 .. 32) of a climb RANGE
  semitones along CURVE. NOIS (a resonant noise sweep), SAW (a detuned stack, SPRD), SINE, SHEP (an endless Shepard
  rise), DOWN (a downlifter), BOOM (an impact, a sub drop); BUILD gates it faster and faster, NOISE mixes noise in
- **SPEECH** *(Fel-Tracker; the IDM distribution)*: words, as the LPC speech chips of the late 70s spoke them: a buzz or a hiss
  through a 10-pole lattice filter at 8.8 kHz whose coefficients glide from sound to sound; no recordings, the words
  are phonemes (`tools/speech/`). BANK: NUM (the numbers 0 .. 10), TECH (machines and technology, the Latin-rooted
  words of the control room: IGNITION, REACTOR, OVERRIDE ..), COSMOS (the planets, the bright stars, NEBULA, QUASAR,
  SUPERNOVA ..), FEEL (feelings: EUPHORIA, RAPTURE, DESPAIR, PARANOIA ..), DARK (darkness, intense events, overwhelm and
  mystery: ABYSS, EXPLOSION, CATACLYSM, ENIGMA, OVERWHELM ..), SYLL (vowels and sung syllables: AA, EE, OOH, PA, PARA,
  DOO, YEAH ..), PHON (single phonemes); WORD picks one. Presets: sung vowels, syllables, the keys as words, screams MODE TONE plays WORD at the keys' pitch, WORDS makes every key a word; RATE, INTN (the intonation, 0
  monotone), BUZZ (to a whisper), FORM (the filter's clock: every formant up or down), END (STOP, HOLD the last vowel
  while held, LOOP)
- **BYTE** *(Fel-Tracker)*: bytebeat. A counter restarts at every note and runs at 8 kHz on C4 (following the key; RATE
  shifts it); FORM picks a formula of it (SAW, PWM, SIERP, XOR, OR, CHIRP, 42, MELO, OCTS, GLTCH, NOISE, MASK), A B C
  are its constants (A mostly the pitch, B how fast the pattern moves, C a mask or a melody); LOOP restarts the counter
  (1K .. 256K steps) so a pattern repeats while held; BITS and TONE shape the 8-bit output

**This fork removed DIGITAL, SAMPLE, GRAIN and SLICE** (1.5): DIGITAL (four-operator FM) had already been replaced by
FM6, and the other three played recorded material. The firmware has no built-in samples and no user sample slots, which
frees about 11 KB of RAM and 28 KB of the buffer pool. It is a clean break for what was saved with them: their engine
numbers (1, 4, 8, 13) are retired, so a project with a track on one of them is refused, a user preset of one shows as
an empty slot, a song's instrument of one is dropped, and a library file's patch of one is skipped. A complete backup
restores without its samples.

**Fel-Tracker removed Felucca's own engines** (ANALOG, FM6, PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE, DRUM) and the
default distribution that held them, with FM6's whole patch editing: the 6-OP FM tab of the web editor, its voice
bank (the B slots, its flash sector) and the user presets' FM6 patches. Their ids (0, 2, 3, 5 .. 7, 9 .. 12) are
retired as the four above, with the same clean break: a project with a track on one is refused, a user preset of one
shows empty, a song's instrument of one is dropped. The plain build is IDM.

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
| `tools/` | build script, generators, package maker and installer |
| `assets/` | UI font, icon names |
| `web/` | the web installer, the editor with the tracker (`editor.html`, protocol in `EDITOR_PROTOCOL.md`) and the browser emulator (`web/emu/`); the site's editor: [Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp) |
| `native/` | *(Fel-Tracker)* the native image: Fel-Tracker as a desktop application (SDL2), BUILDING.md "Native image" |
| `plugin/` | *(Fel-Tracker)* the plugin, in progress: its cores (a library a distribution) and their instances, BUILDING.md "Plugin" |
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

Fel-Tracker: issues and ideas go to [TristanoSuriani/fel-tracker](https://github.com/TristanoSuriani/fel-tracker/issues).
Please don't report Fel-Tracker problems to Felucca's tracker.

Felucca: if Felucca is useful to you, [sponsoring on GitHub](https://github.com/sponsors/hugelton) or a donation
on [itch.io](https://hugelton.itch.io/felucca) helps keep its development going.

Issues are for reproducible bugs (one per issue). Ideas and requests go to
[Discussions](https://github.com/hugelton/Felucca/discussions), and feature requests posted as issues will be
moved there. Pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

## AI disclaimer

Fel-Tracker is developed by Tristano Suriani with the help of AI coding agents (for coding, testing and
documentation). He decides its design, features, sound design and direction.

Felucca (Leo Kuroshita's disclaimer): Felucca is developed with the assistance of AI coding agents. These tools are used for coding, testing, documentation, translation, and maintenance.
The instrument's design, features, sound design, and overall direction are determined by the maintainer or community.
No generative AI is used to create music, icons, or visual artwork for this project.
For more details, see [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits

- **Tristano Suriani** ([@TristanoSuriani](https://github.com/TristanoSuriani)): Fel-Tracker, meaning the tracker,
  the distributions and engine ids, the engines WAVE, BYTE, WEST, HOOVER, SPEECH, FMPERC, KARPLUS, VOSIM, PARTICLE,
  CHAOS, SID, DUBSIREN, DUBSIREN8 (SIREN8), DUBCHORD, BLISS, WASP, MODAL, VECTOR, ECHOES, GLITCH, KIT, BREAKS and RISERS,
  and the changes to
  Felucca listed in
  [AUTHORS.md](AUTHORS.md)
- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  Felucca itself, its own engines (removed in Fel-Tracker) and the DRUM lanes' General MIDI map KIT uses; the
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

M-VAVE and FM-1 are trademarks of their respective owners. Neither Felucca nor Fel-Tracker is affiliated with or
endorsed by them. "Felucca" is Hügelton Instruments' name for its firmware; Fel-Tracker is an independent fork.

Felucca: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
Fel-Tracker's additions and changes: Copyright (C) 2026 Tristano Suriani (see [AUTHORS.md](AUTHORS.md))
