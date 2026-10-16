# Archived engines

Blue Hare's engines that are not in its one distribution, HARE FM1 (2026-10-16, Milestone 1: `docs/Milestones.md`), by
Tristano Suriani; DaisySP-derived parts keep their MIT notices. The sources are here, out of every build and test:
`eng_*.c`, and `drum_map.c` (the drum lanes and General MIDI map the kits shared, from Felucca's DRUM). Their tests are
in `archive/tests/`, the SPEECH word generator in `archive/tools/`, the distributions that held them in
`archive/distributions/`.

Their ids stay theirs (`firmware/src/engine_ids.h`, `ARCHIVED` lines): a stored sound of one is refused. To bring one
back: move its `eng_*.c` back to `firmware/src/`, turn its `ARCHIVED` line into an `ENGINE` line (with a substitute),
add its `#include` to `engines.c` (and its block to `ENG_SLAB_BYTES` if it borrows the track's block), list it in a
distribution, and restore its tests and golden renders from the git history before 2026-10-16.

What they were (from the README of before; the distributions as they were then):

- **HOOVER** *(Blue Hare)*: the early-90s rave lead, after the Alpha Juno's patch. Two oscillators, each a saw plus the
  same saw PW of a cycle on (a pulse-width-modulated saw: sweeping PW moves notches through the spectrum), PWM sweeping
  PW with the voice's own LFO at RATE (the SHP routings move PW too), DTN detuning the second, SUB a square an octave
  down; BEND starts each note up to two octaves away and slides it in over BTIM (the swoop); CUT, a low-pass. Its
  presets send plenty to the chorus, which makes the rest of the sound
- **FMPERC** *(Blue Hare; in no distribution since IDM gave its slot to DUBSIREN8: its voices play in KIT and BREAKS,
  and a stored FMPERC sound loads as KIT)*: FM percussion. A modulating sine at RATIO (whole, half and inharmonic ratios)
  moves a carrier's phase by INDEX, and every hit sweeps both down over FDEC: the pitch from SWEEP semitones above the
  note, the index to a tenth, so a hit starts bright and high and settles. FDBK (towards noise); NOISE mixes in noise
  of the kind NTYP sets (SNAP a short burst, CLAP four bursts and a tail, HISS as long as the note); TONE darkens below
  0, thins above. 23 presets: six kicks (deep, sub, hard, distorted, punchy), snare, two claps, rim, closed and open
  hats, crash, ride, toms, conga, clave, cowbell, metal, a zap, a bell, a glitch
- **KARPLUS** *(Blue Hare; in no distribution since IDM gave its slot to DUBCHORD: a stored KARPLUS sound loads as
  PHYS where there is one, else WEST)*: a plucked string, Karplus-Strong. EXCT fills the string at the pluck (NOIS,
  PLUK filtered by BRIGHT, SAW, CLIK), POS plucks nearer the bridge or the middle, DAMP lets the highs die first, DECAY
  rings 0.1 .. 10 s, STIFF sharpens the overtones towards a bell, MUTE damps it on release, DRIVE saturates. Presets:
  nylon, steel, harp, koto, pizzicato, muted bass, dub and acid plucks, a bell string, a dulcimer
- **VOSIM** *(Blue Hare; the IDM distribution)*: VOice SIMulation, two formants. Each period starts a train of PULS sin^2 pulses,
  each DECAY times the last; their width sets the formants F1 and F2, which stay put whatever the note (KTRK makes them
  follow it). SWEEP moves them at each note and lets them glide back (wah, brass, "yow"); BAL mixes them; BRTH adds
  breath. Presets: four vowels, talk lead, basses, brass, yow, choir, reed, whistle, growl, formant pad
- **PARTICLE** *(Blue Hare; the IDM distribution)*: a cloud of synthesized grains. DENS grains a second (1 .. 400), each SIZE long
  (2 .. 200 ms), at the note give or take SCAT semitones (QNT: free, semitones, octaves, fifths), gliding CHIRP; WAVE
  sine, triangle, FM or noise under SHAPE (a hit, a swell, backwards); TONE. Rain, bubbles, crackle, swarms, stardust,
  grain pads, drips, insects
- **CHAOS** *(Blue Hare; the IDM distribution)*: chaotic systems as oscillators. MODEL: the Lorenz and Rossler attractors (flows
  run at the note's speed: tones that wander) or the logistic and Henon maps (a new value each period: buzz that
  changes every cycle). CHAOS is the system's parameter, from order to wild; OUT the variable heard; SMTH; FOLD; SINE
  mixes in the note's sine; SEED: the same attack every note or a new one; TONE
- **DUBSIREN** *(Blue Hare; the IDM distribution)*: the sound-system siren box with its own tape echo. WAVE, an LFO on the pitch
  (RATE, DEPTH, SHAPE: two-tone, whoop up or down, wail), SWEEP (the dive or the rise), TIME (turned while it sounds,
  the echoes bend), FDBK (runs away while held, settles when let go), MIX. ATK and REL are its envelope; the voice
  lasts until the echoes die away. It plays on the last track only (track 4; its 16-bit echo is that track's): on
  another track it shows dimmed, and a DUBSIREN sound loaded there (a pick, a song, an instrument, a user preset)
  becomes SIREN8 with the same preset and values where the build has it (IDM no longer does: there it plays dry)
- **SIREN8** (DUBSIREN8) *(Blue Hare; in no distribution since IDM gave its slot to MODAL)*: DUBSIREN's 8-bit cousin, on every track. The same siren
  and the same 8 controls, made of fewer bits: the siren held a sample in two and cut to 8 bits after its envelope (a
  fading siren grows grittier), the echo 8 bits a sample (464 ms in 5 KB a track), each pass rougher. Its first 12
  presets are DUBSIREN's 12 in 8 bits, then ARCADE ZAP, DIRTY DUB, POCKET HORN, BROKEN TAPE
- **DUBCHORD** *(Blue Hare; the IDM distribution)*: the dub-techno chord stab, a whole chord in one voice: a key plays
  CHORD on its note (MIN7, MIN9, MIN11, MAJ7, SUS2, SUS4, 5THS, MIN; the same quality wherever the root goes), four notes
  through one resonant low-pass, so a chord costs one voice of the budget. VOIC CLOSE, OPEN, SPREAD; WAVE SAW, SQR, SINE,
  ORGAN; DTN detunes the notes in turn; CUT, RES, and the filter's ENV and DEC (the stab's opening). Its presets send
  plenty to the delay (TAPE suits it) and the reverb; up to 4 chords a track
- **WASP** *(Blue Hare; the IDM distribution)*: two oscillators into a gritty multimode filter, after the little
  black-and-yellow synth of 1978. WAVE pairs them (SAW, SQR, a 25 % PUL, NOIS), OSC2 sits -24 .. +24 semitones away,
  DTN detunes them (each voice drifts a little too), MIX balances them. A 12 dB state-variable filter (MODE LP, BP, HP,
  NOTCH; CUT, the key half tracked) keeps its lows however hard RES is pushed (its passband stays put while the peak
  rises: no thinning); from ~115 it sings by itself, past that a saturating, unsteady self-oscillation. GRIT drives
  the filter and cuts its integrators to fewer bits (16 .. 8): steps, fizz and limit cycles as it rings. 30 presets:
  leads, basses resonant and not, reeses, pads, keys, a clav, self-oscillating and gritty oddities
- **VECTOR** *(Blue Hare; the IDM distribution)*: vector synthesis. Four of WAVE's frames at the corners of a square
  (SET: VOXSTR, GLASS, ORGAN, DIGI, AIR, METAL, CHOIR, SYNTH), a point moving over it: PATH HOLD (at X, Y), ENV (each
  note round the corners, then to X, Y), LOOP, CIRCLE, EIGHT, RANDOM, RATE ~0.02 .. 5 Hz; the SHP routings move X too.
  DTN detunes A, C against B, D; CUT, RES. Presets: evolving pads, morphing choirs and organs, keys, a moving bass
- **GLITCH** *(Blue Hare; the IDM distribution)*: a kit of micro-sounds, a key each from C2 (as DRUM): CLICK, POP,
  TICK, ZAP, BITERR, DATA, DROP, FAX, MODEM, CRACKL, SPARK, BLIP, SHARD, BUZZ, SKIP, PING (the octave above: the same,
  an octave up). KIT CLEAN, HARSH, LOFI, BROKEN; PITCH, DECAY, ROLL (a ratchet at the tempo while held, 1/32 ..
  1/128), CRUSH, VARY (each hit different), DENS (extra micro-hits), TONE
- **WAREHOUSE** *(Blue Hare; the warehouse raves: four on the floor and breakbeats, acid, chord stabs and strings,
  hoovers and the trance lead, build-ups; 4 tracks, its own palette RAVE, acid green and UV violet on concrete)*. Its 8
  engines: FLOOR, BREAKS, VENOM, DUBCHORD, STRINGS, HOOVER, SANDSTORM, RISERS. At power-on: FLOOR, VENOM ACID,
  DUBCHORD's DUB STAB, SANDSTORM's LEAD. (LINNKIT, KIT, FMPERC, CHOKE, GUTS, VOSIM, SIREN8 and ABYSS left it in 2026-10;
  their descriptions stay below.)
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
    PERC), TUNE, DECAY, DRIVE
  - **SANDSTORM** (SANDSTRM on the device): the late-90s trance lead, named after Darude's track of 1999 (nothing of
    the record): five saws SPRD apart into two resonant low-passes (24 dB, CUT, RES) that every note opens by ENV and
    lets close over DEC (the pluck); GATE (OFF, 1/8, 1/16, 1/32 at the tempo) plucks it again and chops it every step
    while the key is held; DIVE lets the pitch fall once the key is let go (up to two octaves over ~400 ms); DRIVE.
    24 presets (LEAD, STAB, GATE 16, GATE 32, DIVE, ANTHEM, SUPERSAW ..). SIREN8 plays on every
  track (there is no DUBSIREN to keep the last one). At power-on: KIT, FLOOR, GUTS' GUT ACID, HOOVER's REESE BASS,
  DUBCHORD, VOSIM's FORMANT PAD, SANDSTORM's LEAD and SIREN8's BIT SIREN
- **CHIP** *(Blue Hare; chiptune, the 8-bit consoles' four channels: 4 tracks, its own palette CHIP, the Game Boy's
  four greens)*. The engines already in the tree that sound the part, and CHIPKIT: CHIPKIT, SID, WAVE, BYTE, ECHOES,
  SPEECH, SIREN8 (seven since 2026-10: KIT, GLITCH, WASP, VECTOR, VOSIM, CHAOS and RISERS left it).
  - **CHIPKIT**: the drums of the 8-bit consoles, made as their sound chips made them (no chip emulated, no samples):
    a key each from C2, KICK, SNARE, HAT, OPEN, TOM L, TOM M, TOM H, CRASH, CLAP, ZAP, COIN, NOISE (the octave above
    the same again); each a 4-bit stepped triangle, a pulse (12.5, 25, 50 %) and the noise register (long: hiss; short:
    metallic), their pitch, volume (16 steps) and noise rate set once a frame. CHIP the console: NES, GB (darker
    noise), SMS (squares only, 2 dB volume steps, three noise rates and its tonal periodic noise), C64 (smooth);
    TUNE, DECAY, SWEEP (the pitch falls), NOISE, RATE (the frames: a quarter .. four times the console's), VARY, VEL.
    At power-on: CHIPKIT's NES KIT, SID BASS, BYTE ARP and SID's SYNC LEAD
- **IDM NATIVE** *(Blue Hare; the native image's alone: `native/build.sh idm_native`, no FM-1 build)*: IDM with its
  two most CPU-bound engines as the native engines grown from them, which share their controls (a sound loads on the
  FM-1 as the engine it grew from): SID, WASP, WAVE, BREAKS, WEST, VECTOR, DUBSIREN, MODAL, DUBCHORD, VOSIM, NEBULA,
  CHAOS, CANYON, KIT, SPEECH, GLITCH.
  - **NEBULA**: PARTICLE's cloud whole: 64 grains at once a voice (PARTICLE: 8), 12 voices, the level from the
    overlap's square root (a dense cloud stays loud); PARTICLE's presets and NEBULA, CHOIR CLOUD, OCEAN, FIFTHS HAZE,
    BELL MIST, SHEPARD SKY, DEEP SWARM, STATIC FIELD
  - **CANYON**: ECHOES' repeats, all of them: COUNT up to 64, every repeat ringing to its end (ECHOES keeps 6), 8
    voices; ECHOES' presets and CANYON, BELL CASCADE, PLUCK STORM, ENDLESS DUB, LONG BOUNCE, FM RAIN, WHOLE TONE,
    AVALANCHE
- **FOUNDRY** *(Blue Hare; industrial and EBM: war drums and struck metal, sub bass and saw stacks, molten walls and
  corroded noise, screams and power tools; 4 tracks, its own palette, molten orange)*. Its 8 engines, in their order on
  the device: WAR, ANVIL, DARK, RAZOR, SLAG, RUST, SCREAM, DRILL. Every one has at least 24 presets. At power-on: WAR
  KIT, DARK's SUB WEIGHT, SLAG's EMBER PAD, RAZOR LEAD. (VOSIM, SCRATCH, FLUIDS, DUBCHORD, MODAL, BLISS, ECHOES and WAVE
  left it in 2026-10; SCRATCH, FLUIDS and BLISS are in no build now, their code kept.) Its own engines:
  - **DARK**: sub-bass from sines: SUB1 and SUB2 octaves under the note, a sine folder (FOLD, FENV for each note's
    bite), PUNCH (the pitch from up to two octaves above, falling over PDEC: an 808's thud), DRIVE, TONE
  - **RAZOR**: up to 7 saws spread apart (VOICES, SPREAD: reeses, supersaws), SUB, NOISE, DRIVE into a resonant
    low-pass (CUT, RES) and EDGE, a high-pass after it (thin, icy, laser cuts)
  - *(Blue Hare)* RAZOR and DARK each have three EBM bass sounds: EBM OCTAVES, EBM ROOT, EBM GALLOP (until 2026-10
    they set the arpeggiator's figure too)
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
- **KIT** *(Blue Hare; the IDM distribution)*: a whole drum kit on one part, of FMPERC's voices, on DRUM's lanes and General MIDI
  map (its lane LEVELs and the EDIT layer's lane MUTEs too). KIT: FM, METAL, BIT, GLITCH, DEEP; TUNE, DECAY, PUNCH, FM,
  SNAP, TONE move every lane; VARY gives every hit its own small differences. A closed hat chokes the open one
- **BREAKS** *(Blue Hare; the IDM distribution)*: a synthesized breakbeat player at the song's tempo. The key picks one of 12 breaks
  (FUNKY, CLASSIC, THINK, HALFTIME, BOOM BAP, JUNGLE, TWO STEP, SHUFFLE, BUSY, DRILL, SPARSE, GALLOP), looping while
  held; SLICE the 16th it starts from (chops), RATE (half .. double time), SWING, STUTR (rolls into 32nds and 64ths),
  SHUF (jumps to other 16ths), TUNE, DECAY, CRUSH
- **RISERS** *(Blue Hare; in no distribution since IDM gave its slot to GLITCH: a stored RISERS sound loads as
  NOISE where there is one, else SID)*: build-ups and landings timed to the song: LEN beats (1 .. 32) of a climb RANGE
  semitones along CURVE. NOIS (a resonant noise sweep), SAW (a detuned stack, SPRD), SINE, SHEP (an endless Shepard
  rise), DOWN (a downlifter), BOOM (an impact, a sub drop); BUILD gates it faster and faster, NOISE mixes noise in
- **SPEECH** *(Blue Hare; the IDM distribution)*: words, as the LPC speech chips of the late 70s spoke them: a buzz or a hiss
  through a 10-pole lattice filter at 8.8 kHz whose coefficients glide from sound to sound; no recordings, the words
  are phonemes (`tools/speech/`). BANK: NUM (the numbers 0 .. 10), TECH (machines and technology, the Latin-rooted
  words of the control room: IGNITION, REACTOR, OVERRIDE ..), COSMOS (the planets, the bright stars, NEBULA, QUASAR,
  SUPERNOVA ..), FEEL (feelings: EUPHORIA, RAPTURE, DESPAIR, PARANOIA ..), DARK (darkness, intense events, overwhelm and
  mystery: ABYSS, EXPLOSION, CATACLYSM, ENIGMA, OVERWHELM ..), SYLL (vowels and sung syllables: AA, EE, OOH, PA, PARA,
  DOO, YEAH ..), PHON (single phonemes); WORD picks one. Presets: sung vowels, syllables, the keys as words, screams MODE TONE plays WORD at the keys' pitch, WORDS makes every key a word; RATE, INTN (the intonation, 0
  monotone), BUZZ (to a whisper), FORM (the filter's clock: every formant up or down), END (STOP, HOLD the last vowel
  while held, LOOP)
- **BYTE** *(Blue Hare)*: bytebeat. A counter restarts at every note and runs at 8 kHz on C4 (following the key; RATE
  shifts it); FORM picks a formula of it (SAW, PWM, SIERP, XOR, OR, CHIRP, 42, MELO, OCTS, GLTCH, NOISE, MASK), A B C
  are its constants (A mostly the pitch, B how fast the pattern moves, C a mask or a melody); LOOP restarts the counter
  (1K .. 256K steps) so a pattern repeats while held; BITS and TONE shape the 8-bit output
