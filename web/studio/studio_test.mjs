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
  const missing = [...new Set(ids)].filter((id) => !html.includes(`id="${id}"`) && !js.includes(`.id = "${id}"`));
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
const P = d.one("stu_pcount"), G = d.one("stu_gcount"), PE0 = d.one("stu_pe0"), NE = d.one("stu_nengines");
const names = [...Array(NE).keys()].map((e) => d.str("stu_engine_name", e));
check(`the engines: ${names.join(" ")}`, ["SID", "WAVE", "BLISS", "WEST", "MODAL", "ECHOES"].every((n) => names.includes(n)));
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
  groups.map((g) => g.key).join("") === "EANLDMVSYRCK");
const descOk = groups.every((g) => g.items.every(([s, id]) => {
  const [fmt, min, max, def] = d.call([{ fn: "stu_desc", args: [s, id], ret: ["i32", 5] }])[0];
  return id < (s ? G : P) && min <= def && def <= max && d.str("stu_desc_label", s, id) !== "" && fmt >= 0;
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
