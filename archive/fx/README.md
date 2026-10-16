# Archived effects

The groovebox's effects, archived 2026-10-16 (Milestone 1, `docs/Milestones.md`: the effects that are not groovebox
oriented stay):

- **SLICER** (`slicer.c`): a per-track, tempo-synced 16-step GATE or STUT insert after DIST, 16 patterns, rates 1/8 ..
  32T, swung by CLOCK's SWING. Its parameters' ids (45 .. 48, `core.h` `P_RSV33` .. `P_RSV36`) are kept, inert.
- **DUCK**: a key track (the kick) ducking the others by their depth (an instant attack, ~90 ms release), in
  `firmware/src/fx.c` (`duck_key`, `mix_ducked`) and the project stream (version 8; version 9 has no DUCK bytes).
- **The FX layer's REPEAT 1/8, 1/16, 1/32, REVERSE and TAPE STOP** (`firmware/src/perform.c`): a beat repeat and a
  reversed loop starting on the next 1/16, a tape stop over a beat. Their numbers in the key map are kept (`PF_GONE`)
  and hold nothing; FREEZE, OCT UP / DN, LPF, HPF, the macros and the mutes stay.

`slicer.c` is here; DUCK's and the layer effects' code is in the git history before 2026-10-16 (the commit that
archived them), as are their tests (`archive/tests/slicer_test.c`, `archive/tests/duck_test.c`) and golden renders.
