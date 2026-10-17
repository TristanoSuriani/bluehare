// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tristano Suriani, Blue Hare: see AUTHORS.md
// The sound design studio's WebAssembly (build/studio/bluehare.wasm, web/studio/studio_web.c) in Node, through the
// worklet the page runs (web/emu/worklet.js: its "call" messages, as studio.js sends them).
//   node web/studio/studio_test.mjs build/studio/bluehare.wasm
// Checks: the page's scripts parse; the engines, presets, layout and descriptions it builds from; a preset load and
// a parameter set (clamped, shown as the device shows it); a note on the selected track sounds and stops; a sound
// stored in a user slot reaches the flash, and the flash kept as the page keeps it brings it back in a fresh
// instance, where it loads onto another track; a sound file's values load as a user preset loads (the track's own
// parameters stay); the UP_PUT the page sends an FM-1 (sysex.js) is one the firmware's parser takes.
import fs from "fs";
import { spawnSync } from "child_process";

const wasmPath = process.argv[2] || "build/studio/bluehare.wasm";
const bytes = fs.readFileSync(wasmPath);
let fails = 0;
const check = (what, ok) => { console.log(`studio: ${what.padEnd(80)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

for (const f of ["studio.js", "sysex.js"]) {
  const r = spawnSync(process.execPath, ["--check", new URL("./" + f, import.meta.url).pathname], { encoding: "utf8" });
  check(`${f} parses`, !r.status);
  if (r.status) console.log(r.stderr);
}
{
  const html = fs.readFileSync(new URL("./index.html", import.meta.url), "utf8");
  const ids = [...fs.readFileSync(new URL("./studio.js", import.meta.url), "utf8").matchAll(/\$\("([\w-]+)"\)/g)].map((m) => m[1]);
  const js = fs.readFileSync(new URL("./studio.js", import.meta.url), "utf8");
  const missing = [...new Set(ids)].filter((id) => !html.includes(`id="${id}"`) && !js.includes(`.id = "${id}"`) && !js.includes(`"${id}")`) && !js.includes(`id: "${id}"`));
  check("every element studio.js looks up is in index.html", !missing.length);
  if (missing.length) console.log("  missing:", missing.join(" "));
}

// the worklet as the browser runs it: its processor class, a port whose replies land here
const registered = {};
globalThis.registerProcessor = (name, cls) => { registered[name] = cls; };
globalThis.AudioWorkletProcessor = class { constructor() { this.port = { postMessage: (m) => this.port.sent.push(m), sent: [] }; } };
globalThis.currentTime = 0;
const { storeSectors, restoreSectors } = await import("../emu/worklet.js");
const { upPutArgs, frame, reply, parseInfo, parseUpList, ED } = await import("./sysex.js");

async function device(sectors) {
  const w = new registered.bluehare();
  await w.onMessage({ type: "load", wasm: bytes, sectors, master: 700 });
  const ready = w.port.sent.find((m) => m.type === "ready");
  if (!ready) throw new Error(JSON.stringify(w.port.sent));
  let id = 1;
  const d = {
    w, ex: w.ex,
    call(calls) {
      const n = id++;
      w.port.sent.length = 0;
      w.onMessage({ type: "call", id: n, calls });
      const r = w.port.sent.find((m) => m.type === "ret" && m.id === n);
      if (r.error) throw new Error(r.error);
      return r.values;
    },
    one(fn, ...args) { return d.call([{ fn, args }])[0]; },
    str(fn, ...args) { return d.call([{ fn, args, ret: "str" }])[0]; },
    render(ms) {           // the worklet's process(): 128 frames a quantum; the peak heard
      let peak = 0;
      const out = [[new Float32Array(128), new Float32Array(128)]];
      for (let k = 0; k < Math.round(ms * 44.1 / 128); k++) {
        w.process([], out);
        for (const ch of out[0]) for (const v of ch) peak = Math.max(peak, Math.abs(v));
      }
      return peak;
    },
  };
  d.render(800);           // the splash: 430 ms, then the main loop
  return d;
}

const d = await device({});
const P = d.one("stu_pcount"), G = d.one("stu_gcount"), X = d.one("stu_xcount"), PE0 = d.one("stu_pe0"), NE = d.one("stu_nengines");
const names = [...Array(NE).keys()].map((e) => d.str("stu_engine_name", e));
check(`the engines: ${names.join(" ")}`, ["SID", "WAVE", "VOSIM", "BLISS", "MODAL", "STRINGS"].every((n) => names.includes(n)));
check("every engine has presets, each named", names.every((_, e) => {
  const n = d.one("stu_npresets", e);
  return n > 0 && [...Array(n).keys()].every((k) => d.str("stu_preset_name", e, k).length > 0);
}));
const lay = d.call([{ fn: "stu_layout", ret: ["u8", d.one("stu_layout_size")] }])[0];
const groups = [];
for (let i = 0; i < lay.length && lay[i];) {
  const g = { key: String.fromCharCode(lay[i++]), items: [] };
  while (lay[i] !== 0xFF) { g.items.push([lay[i], lay[i + 1]]); i += 2; }
  i++;
  groups.push(g);
}
check(`the layout: ${groups.map((g) => g.key).join("")} (${groups.reduce((n, g) => n + g.items.length, 0)} parameters)`,
  groups.map((g) => g.key).join("") === "EANLDMVZFSXWJUYRCPQKGH");
const descOk = groups.every((g) => g.items.every(([s, id]) => {
  const [fmt, min, max, def] = d.call([{ fn: "stu_desc", args: [s, id], ret: ["i32", 5] }])[0];
  return id < [P, G, X][s] && min <= def && def <= max && d.str("stu_desc_label", s, id) !== "" && fmt >= 0;
}));
check("every parameter of the layout is described (label, range, default inside it)", descOk);

// a preset, a parameter
const WAVE = names.indexOf("WAVE");
d.call([{ fn: "stu_set_engine", args: [WAVE] }, { fn: "stu_load_preset", args: [3] }]);
check("an engine and a preset load on the selected track", d.one("stu_engine") === WAVE && d.one("stu_preset") === 3);
const ATK = 1;
check("a set is clamped to the range", d.one("stu_set", 0, ATK, 500, 7) === 127 && d.one("stu_get", 0, ATK) === 127);
d.one("stu_set", 0, ATK, 0, 7);
check(`a value is shown as the device shows it (ATK 0: "${d.str("stu_format", 0, ATK, 0)}")`, /ms$/.test(d.str("stu_format", 0, ATK, 0)));
const dump = d.call([{ fn: "stu_dump", ret: ["i16", P + G + 4] }])[0];
check("the dump holds the track's values, then the engine, preset, slot, selection",
  dump[ATK] === 0 && dump[P + G] === WAVE && dump[P + G + 1] === 3 && dump[P + G + 3] === 0);
d.one("stu_set", 1, 9, 30, 0);     // (G_RSIZE)
check("a global (the FX chain's) is set", d.one("stu_get", 1, 9) === 30);

// the effects of scope 2 (docs/SoundAndDSP.md section 3): the track's inserts, the speaker EQ, the macros, MASTER
const SX = Object.fromEntries([...Array(X).keys()].map((i) => [d.str("stu_desc_label", 2, i), i]));
check(`scope 2: ${Object.keys(SX).join(" ")}`, ["LAYER", "KEY LO", "KEY HI", "VEL LO", "VEL HI", "TRANS", "LFILT", "LCUT", "LRES", "MODE", "XFADE", "XPOS", "MORPH", "LOWCUT", "PANMODE", "SPK EQ", "FILTER", "CRUSH", "THROW", "DEPTH", "MASTER"]
  .every((n) => n in SX));
d.one("stu_set", 2, SX.LOWCUT, 3, 0);
d.one("stu_set", 2, SX.PANMODE, 1, 0);
d.one("stu_select", 1);
const other = d.one("stu_get", 2, SX.LOWCUT);
d.one("stu_select", 0);
check(`LOW CUT ("${d.str("stu_format", 2, SX.LOWCUT, 3)}") and PAN MODE: the selected layer's (layer 2 keeps ${other})`,
  d.one("stu_get", 2, SX.LOWCUT) === 3 && d.one("stu_get", 2, SX.PANMODE) === 1 && other !== 3);
d.one("stu_set", 2, SX.LOWCUT, 0, 0);
d.one("stu_set", 2, SX.PANMODE, 0, 0);

// the layers (core.h lay): ON layers play the notes their ranges hold; ROUND ROBIN one each in turn; none ON: the
// selected one alone
{
  const voices = () => [0, 1, 2, 3].map((t) => d.one("stu_track_voices", t));
  const hit = (note, vel) => { d.one("stu_note", note, vel); d.render(60); const v = voices(); d.one("stu_note", note, 0); d.render(3000); return v; };
  d.one("stu_all_off");
  d.render(3000);
  d.one("stu_select", 0); d.one("stu_set", 2, SX.LAYER, 1, 0);
  d.one("stu_select", 1); d.one("stu_set", 2, SX.LAYER, 1, 0); d.one("stu_set", 2, SX["KEY HI"], 59, 0);
  d.one("stu_select", 0);
  const low = hit(48, 100), high = hit(72, 100);
  check(`a key split: C3 on layers 1 and 2 (${low.join(" ")}), C5 on layer 1 alone (${high.join(" ")}); "${d.str("stu_format", 2, SX["KEY HI"], 59)}"`,
    low[0] > 0 && low[1] > 0 && !low[2] && !low[3] && high[0] > 0 && !high[1]);
  d.one("stu_select", 1); d.one("stu_set", 2, SX["KEY HI"], 127, 0); d.one("stu_set", 2, SX["VEL LO"], 100, 0);
  d.one("stu_select", 0);
  const soft = hit(60, 60), hard = hit(60, 110);
  check(`a velocity split: soft on layer 1 alone (${soft.join(" ")}), hard on both (${hard.join(" ")})`, soft[0] > 0 && !soft[1] && hard[0] > 0 && hard[1] > 0);
  d.one("stu_select", 1); d.one("stu_set", 2, SX["VEL LO"], 1, 0);
  d.one("stu_set", 2, SX.MODE, 1, 0);
  const a = hit(60, 100), b = hit(62, 100), c = hit(64, 100);
  check(`ROUND ROBIN: one layer a note, in turn (${a.join(" ")} / ${b.join(" ")} / ${c.join(" ")})`,
    a[0] + a[1] === 1 && b[0] + b[1] === 1 && a[0] !== b[0] && c[0] === a[0]);
  d.one("stu_set", 2, SX.MODE, 0, 0);
  d.one("stu_set", 2, SX.LAYER, 0, 0); d.one("stu_select", 0); d.one("stu_set", 2, SX.LAYER, 0, 0);
  d.one("stu_select", 2);
  const sel = hit(60, 100);
  check(`no layer ON: the selected layer alone (${sel.join(" ")})`, sel[2] > 0 && !sel[0] && !sel[1] && !sel[3]);
  d.one("stu_select", 0);
  const level = (ft) => { d.one("stu_set", 2, SX.LFILT, ft, 0); d.one("stu_set", 2, SX.LCUT, 10, 0); d.one("stu_note", 84, 110);
    d.render(100); const p = d.render(300); d.one("stu_note", 84, 0); d.render(3000); return p; };
  const open = level(0), lp = level(1);
  d.one("stu_set", 2, SX.LFILT, 0, 0);
  check(`the layer's filter: LP at its lowest cutoff takes a C6 from ${open.toFixed(3)} to ${lp.toFixed(3)}`, lp < open / 4);

  // (2026-10-17) the crossfade: layers 1 and 2 placed at 20 and 100 along VELOCITY, then KEY, then the WHEEL
  const xg = (note, vel) => { d.one("stu_note", note, vel); d.render(30); const g = [0, 1].map((t) => d.one("stu_track_xg", t));
    d.one("stu_note", note, 0); d.render(2000); return g; };
  d.one("stu_all_off"); d.render(2000);
  d.one("stu_set", 2, SX.LAYER, 1, 0); d.one("stu_set", 2, SX.XPOS, 20, 0);
  d.one("stu_select", 1); d.one("stu_set", 2, SX.LAYER, 1, 0); d.one("stu_set", 2, SX.XPOS, 100, 0);
  d.one("stu_select", 0);
  d.one("stu_set", 2, SX.XFADE, 1, 0);
  const v1 = xg(60, 10), v2 = xg(60, 60), v3 = xg(60, 120);
  check(`XFADE VELOCITY: soft layer 1 alone (${v1}), middle both at -3 dB (${v2}), hard layer 2 alone (${v3})`,
    v1[0] === 32767 && v1[1] === 0 && Math.abs(v2[0] - 23170) < 400 && Math.abs(v2[1] - 23170) < 400 && v3[0] === 0 && v3[1] === 32767);
  d.one("stu_set", 2, SX.XFADE, 2, 0);
  const k1 = xg(12, 100), k2 = xg(110, 100);
  check(`XFADE KEY: low layer 1 (${k1}), high layer 2 (${k2}); "${d.str("stu_format", 2, SX.XPOS, -1)}" for no place`,
    k1[0] === 32767 && k1[1] === 0 && k2[0] === 0 && k2[1] === 32767 && d.str("stu_format", 2, SX.XPOS, -1) === "OFF");
  d.one("stu_set", 2, SX.XFADE, 3, 0);
  d.one("stu_set", 2, SX.MORPH, 0, 0); d.render(50);
  const w0 = [0, 1].map((t) => d.one("stu_layer_wg", t));
  d.one("stu_set", 2, SX.MORPH, 127, 0); d.render(50);
  const w1 = [0, 1].map((t) => d.one("stu_layer_wg", t));
  d.one("stu_select", 1); d.one("stu_set", 2, SX.XPOS, -1, 0); d.one("stu_select", 0); d.render(50);
  const w2 = d.one("stu_layer_wg", 1);
  check(`XFADE WHEEL: MORPH 0 layer 1 (${w0}), 127 layer 2 (${w1}); a layer with no place at full level (${w2})`,
    w0[0] === 32767 && w0[1] === 0 && w1[0] === 0 && w1[1] === 32767 && w2 === 32767);
  d.one("stu_set", 2, SX.XFADE, 0, 0); d.one("stu_set", 2, SX.MORPH, 0, 0); d.render(50);
  check("XFADE OFF: every layer at full level", d.one("stu_layer_wg", 0) === 32767 && d.one("stu_layer_wg", 1) === 32767);

  // a MONO layer under a POLY one: a chord's notes change its pitch, no new attack each (the stutter); a phrase retriggers
  const VOICE = Object.fromEntries([...Array(P).keys()].map((i) => [d.str("stu_desc_label", 0, i), i]));
  const VMODE = VOICE.VCE;
  d.one("stu_set", 0, VMODE, 0, 0);                   /* (layer 1 POLY, layer 2 MONO) */
  d.one("stu_select", 1); d.one("stu_set", 0, VMODE, 1, 0); d.one("stu_select", 0);
  d.one("stu_note", 60, 100); d.render(10);
  const c0 = d.one("stu_track_age", 1);
  d.one("stu_note", 64, 100); d.one("stu_note", 67, 100); d.render(30);
  const chord = [d.one("stu_track_age", 1), d.one("stu_track_note", 1), d.one("stu_track_voices", 0)];
  for (const n of [60, 64, 67]) d.one("stu_note", n, 0);
  d.render(2000);
  d.one("stu_note", 60, 100); d.render(200); const a1 = d.one("stu_track_age", 1);
  d.one("stu_note", 64, 100); d.render(30); const a2 = d.one("stu_track_age", 1);
  d.one("stu_note", 60, 0); d.one("stu_note", 64, 0); d.render(2000);
  check(`a MONO layer under a POLY one (${chord[2]} voices): a chord moves it to its last note (${chord[1]}) without a new attack (start ${c0} -> ${chord[0]}); a phrase retriggers (${a1} -> ${a2})`,
    chord[1] === 67 && chord[0] === c0 && chord[2] === 3 && a2 > a1);
  d.one("stu_select", 1); d.one("stu_set", 0, VMODE, 0, 0); d.one("stu_set", 2, SX.LAYER, 0, 0);
  d.one("stu_select", 0); d.one("stu_set", 2, SX.LAYER, 0, 0); d.one("stu_set", 2, SX.XPOS, -1, 0);
}

// (2026-10-17) the patch's EQ, PLATE and AMB
{
  const G_DTYPE = 25, G_RTYPE = 24, G_EQLO = 27, G_EQHI = 30, P_DLY = 35, P_REV = 36;
  check(`EQ: four bands of the patch, ±12 dB ("${d.str("stu_format", 1, G_EQLO, 12)}", "${d.str("stu_format", 1, G_EQHI, -12)}"; their response: tests/baseline/float_fx_test.c)`,
    d.str("stu_format", 1, G_EQLO, 12).includes("dB") && d.one("stu_set", 1, G_EQHI, 40, 0) === 12 && d.one("stu_set", 1, G_EQHI, 0, 0) === 0);
  const tail = (sendId, gid, type) => {
    d.one("stu_set", 1, gid, type, 0); d.one("stu_set", 0, sendId, 100, 0); d.render(200);
    d.one("stu_note", 60, 110); d.render(150); d.one("stu_note", 60, 0); d.render(400);
    const t1 = d.render(400), t2 = (d.render(8000), d.render(4000));
    d.one("stu_set", 0, sendId, 0, 0); d.render(6000);
    return [t1, t2];
  };
  const pl = tail(P_REV, G_RTYPE, 5), am = tail(P_DLY, G_DTYPE, 5);
  d.one("stu_set", 1, G_DTYPE, 0, 0); d.one("stu_set", 1, G_RTYPE, 2, 0); d.render(500);
  check(`PLATE: a tail after the note (${pl[0].toFixed(3)}), dying away (${pl[1].toFixed(4)}); "${d.str("stu_format", 1, G_RTYPE, 5)}"`,
    pl[0] > 0.005 && pl[0] < 1 && pl[1] < pl[0] / 4 && d.str("stu_format", 1, G_RTYPE, 5) === "PLATE");
  check(`AMB: echoes after the note (${am[0].toFixed(3)}), not running away (${am[1].toFixed(4)}); "${d.str("stu_format", 1, G_DTYPE, 5)}"`,
    am[0] > 0.005 && am[0] < 1 && am[1] < 1 && d.str("stu_format", 1, G_DTYPE, 5) === "AMB");
}
d.one("stu_set", 2, SX.SPKEQ, 2, 0);
check("SPEAKER EQ BASS+, back to FLAT", d.one("stu_get", 2, SX.SPKEQ) === 2 && d.one("stu_set", 2, SX.SPKEQ, 0, 0) === 0);
d.one("stu_set", 2, SX.DEPTH, 60, 0);
check("DEPTH 60 (perform.c's cut 40), CRUSH 50", d.one("stu_get", 2, SX.DEPTH) === 60 && d.one("stu_set", 2, SX.CRUSH, 50, 0) === 50);
d.one("stu_perf_off");
// the performance layer: a hold effect changes the sound while held, and only then
const hold = (e, latch, ms = 500) => {
  d.one("stu_note", 48, 100);
  d.render(200);
  const ref = d.render(300);
  d.one("stu_perf", e, 1, latch);
  const on = d.one("stu_perf_on");
  d.render(ms);
  const wet = d.render(300);
  if (!latch) d.one("stu_perf", e, 0, 0); else d.one("stu_perf", e, 1, 1);
  const off = d.one("stu_perf_on");
  d.one("stu_note", 48, 0);
  d.render(2500);
  return { on, off, ref, wet };
};
const lpf = hold(4, 0, 2000);   // (the LPF closes over a bar: 2 s at 120 BPM)
check(`LPF held: on while held, off when let go (peak ${lpf.ref.toFixed(3)} -> ${lpf.wet.toFixed(3)})`,
  lpf.on === 1 << 4 && lpf.off === 0 && lpf.wet < lpf.ref * 0.8);
const oup = hold(8, 1);
check(`OCT UP latched: on after a press, off after the next (peak ${oup.ref.toFixed(3)} -> ${oup.wet.toFixed(3)})`,
  oup.on === 1 << 8 && oup.off === 0 && Math.abs(oup.wet - oup.ref) > 0.001);
const mt = d.call([{ fn: "stu_meters", ret: ["i32", 2] }])[0];
check(`the master's meters read (compressor ${mt[0]}, limiter ${mt[1]} of 32768)`, mt[0] > 0 && mt[0] <= 32768 && mt[1] > 0 && mt[1] <= 32768);

// GEN: started and stopped as ARP does, its state; a preset picked loads its sounds and tempo (ui.c gen_poll)
{
  const G_GPRE = 12, G_BPM = 0;
  d.one("stu_set", 1, G_GPRE, 3, 0);                 /* AFTERNOON: WAVE, WAVE (SUB BASS), BLISS, MODAL; 84 BPM */
  d.render(100);
  const eng = [0, 1, 2, 3].map((t) => names[d.one("stu_track_engine", t)]);
  check(`a GEN piece picked (AFTERNOON) loads its four sounds and tempo: ${eng.join(" ")}, ${d.one("stu_get", 1, G_BPM)} BPM`,
    eng.join() === "WAVE,WAVE,BLISS,MODAL" && d.one("stu_get", 1, G_BPM) === 84);
  d.one("stu_gen", 1);
  d.render(6000);
  const st = d.call([{ fn: "stu_gen_state", ret: ["i32", 8] }])[0], chord = d.str("stu_gen_chord");
  check(`GEN plays: bar ${st[1] + 1}, notes (pad bass bells pulse) ${st.slice(4).join(" ")}, the chord ${chord}`,
    st[0] === 1 && st[4] > 0 && st[5] > 0 && st[6] + st[7] > 0 && chord.length > 0);
  d.one("stu_gen", 0);
  d.render(100);
  check("GEN stops", d.call([{ fn: "stu_gen_state", ret: ["i32", 8] }])[0][0] === 0);
  d.render(6000);
}

// amb-1's processing (fx_amb.c) against Blue Hare's, on the same note with its sends: both sound, they differ, the
// plate and the tape echo ring on after the note; switching back works
{
  const tail = (proc) => {
    d.one("stu_set", 2, SX.PROC, proc, 0);
    d.one("stu_set", 0, 35, 100, 0);                 /* (P_DLY, P_REV: the sends up) */
    d.one("stu_set", 0, 36, 100, 0);
    d.render(4000);
    d.one("stu_note", 64, 110);
    const on = d.render(400);
    d.one("stu_note", 64, 0);
    d.render(1500);
    const late = d.render(1000);                     /* (1.9 .. 2.9 s after the note: the tails) */
    d.render(8000);
    return { on, late };
  };
  const bh = tail(0), amb = tail(1), back = tail(0);
  check(`amb-1's processing sounds (peak ${amb.on.toFixed(3)}), rings on (${amb.late.toFixed(4)}), and is not Blue Hare's (${bh.on.toFixed(3)}, ${bh.late.toFixed(4)})`,
    amb.on > 0.02 && amb.on < 1.0 && amb.late > 0.001 && Math.abs(amb.late - bh.late) > 1e-4 && Number.isFinite(amb.on));
  check("back to Blue Hare's: as before", Math.abs(back.on - bh.on) < 0.05 && d.one("stu_get", 2, SX.PROC) === 0);
}

// a note
d.one("stu_note", 60, 100);
const on = d.render(300);
d.one("stu_note", 60, 0);
d.render(3000);
const after = d.render(200);
check(`a note on the selected track sounds (peak ${on.toFixed(3)}) and stops (${after.toFixed(4)})`, on > 0.02 && after < on / 20);

// a user preset: stored, kept in the flash, back in a fresh instance, loaded onto track 2
d.one("stu_set", 0, PE0 + 1, 77, 9);
const want = d.call([{ fn: "stu_dump", ret: ["i16", P] }])[0];
const rc = d.call([{ fn: "stu_up_store", args: [5, 3], str: "TEST PAD" }])[0];
d.render(100);
check("a sound is stored in a user slot", rc === 0 && d.one("stu_up_used", 5) === 1 && d.str("stu_up_name", 5) === "TEST PAD" &&
  d.one("stu_up_cat", 5) === 3 && d.one("stu_up_engine", 5) === WAVE);
check("a bad name is refused", d.call([{ fn: "stu_up_store", args: [6, 0], str: "" }])[0] === 1 && !d.one("stu_up_used", 6));
const sectors = storeSectors(new Uint8Array(d.ex.memory.buffer, d.ex.web_nor(), d.ex.web_nor_size()));
const d2 = await device(JSON.parse(JSON.stringify(sectors, (k, v) => v instanceof Uint8Array ? Array.from(v) : v)));
check("the slot is back in a fresh instance (the flash as the page keeps it)", d2.str("stu_up_name", 5) === "TEST PAD");
d2.one("stu_select", 1);
const kept = new Set([...Array(P).keys()].filter((i) => d2.one("stu_kept", i)));
const before2 = d2.call([{ fn: "stu_dump", ret: ["i16", P] }])[0];
d2.one("stu_up_load", 5);
const got = d2.call([{ fn: "stu_dump", ret: ["i16", P + G + 4] }])[0];
check("it loads onto track 2: its engine and sound, the track's own parameters kept",
  got[P + G] === WAVE && got[P + G + 2] === 6 && got[P + G + 3] === 1 &&
  [...Array(P).keys()].every((i) => (kept.has(i) ? got[i] === before2[i] : got[i] === want[i])));

// a sound file's values (studio.js loadSoundFile)
const SID = names.indexOf("SID");
d2.one("stu_set", 0, 0, 90, 0);          // (LEVEL: the track's own)
const vals = Object.fromEntries([...Array(P).keys()].filter((i) => !kept.has(i)).map((i) => [i, i === ATK ? 55 : i === PE0 ? 2 : want[i]]));
d2.call([{ fn: "stu_stage_begin" }, ...Object.entries(vals).map(([k, v]) => ({ fn: "stu_stage_set", args: [+k, v] })),
         { fn: "stu_stage_load", args: [SID] }]);
check("a sound file loads: engine and values; the track's LEVEL stays",
  d2.one("stu_engine") === SID && d2.one("stu_get", 0, ATK) === 55 && d2.one("stu_get", 0, PE0) === 2 && d2.one("stu_get", 0, 0) === 90);
d2.one("stu_undo");
check("the device's undo takes the load back", d2.one("stu_engine") === WAVE);

// the FM-1's UP_PUT as the page builds it, through the firmware's parser
const args = upPutArgs(9, d2.one("stu_engine_id", SID), "BASS ONE", want.slice(0, P), 1);
const f = new Uint8Array(d2.ex.memory.buffer, d2.ex.stu_frame(), 640);
f.set(args);
const pr = d2.one("stu_up_parse", args.length);
check("the page's UP_PUT is one the firmware takes (slot, engine id, category)",
  pr >= 0 && (pr & 255) === 9 && ((pr >> 8) & 255) === d2.one("stu_engine_id", SID) && (pr >> 16) === 1);
f.set(upPutArgs(9, 27, "BAD", want.slice(0, P), 1));    // (27: a retired id)
check("an UP_PUT of a retired engine is refused by it", d2.one("stu_up_parse", args.length - 5) === -1);
const fr = frame(ED.UP_PUT, [9, 0]);
check("frames: header, command, end; a reply's arguments read back",
  fr[0] === 0xF0 && fr[4] === 18 && fr[fr.length - 1] === 0xF7 && reply(Uint8Array.from(fr), ED.UP_PUT).join() === "9,0");
check("INFO and UP_LIST replies parse", parseInfo([66, 72, 0, 54, P, 33]).pcount === P &&
  JSON.stringify(parseUpList([0, 2, 32, 1, 14, 65, 0, 0, 0, 0])) === '[{"used":1,"engine":14,"name":"A"},{"used":0,"engine":0,"name":""}]');

void restoreSectors;
console.log(fails ? `studio: ${fails} FAILED` : "studio: all passed");
process.exit(fails ? 1 : 0);
