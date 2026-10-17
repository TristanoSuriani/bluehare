// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tristano Suriani, Blue Hare: see AUTHORS.md
// The sound design studio's page. The FM-1 firmware runs in an AudioWorklet (worklet.js, bluehare.wasm: the browser
// emulator's build with the studio's exports, web/studio/studio_web.c); everything here asks it through "call":
// the engines and presets, every parameter's description, value and text as the device shows it, the user preset
// slots (kept in this browser as the emulator keeps its flash). Sounds go to a file, to a slot here, or to a
// connected FM-1 (the editor protocol's UP_PUT, web/EDITOR_PROTOCOL.md).
import { ED, frame, reply, parseInfo, parseUpList, upPutArgs } from "./sysex.js";

const $ = (id) => document.getElementById(id);
const h = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (v !== undefined && v !== null && v !== false) e.setAttribute(k, v === true ? "" : v);
  }
  for (const c of kids.flat()) if (c !== null && c !== undefined) e.append(c);
  return e;
};
const pref = (k, v) => { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch { return null; } };

// ------------------------------------------------------------------ the firmware
const F = { INT: 0, PCT: 1, BIPCT: 2, TIME: 3, LFOHZ: 4, CUTOFF: 5, DB: 6, SEMI: 7, ENUM: 8, BPM: 9, NOTE: 10 };
let node = null, ac = null, analyser = null, nextId = 1;
const pending = new Map();
// calls: [{fn, args, str, ret}] -> their values, in order (one round trip)
function call(calls) {
  return new Promise((res, rej) => {
    const id = nextId++;
    pending.set(id, { res, rej });
    node.port.postMessage({ type: "call", id, calls });
  });
}
const one = async (fn, ...args) => (await call([{ fn, args }]))[0];
const str = async (fn, ...args) => (await call([{ fn, args, ret: "str" }]))[0];

// what the firmware told: constants, the engines, the layout, the descriptions; then the values (polled)
const fw = { P: 0, G: 0, X: 0, PE0: 0, ntrk: 4, engines: [], cats: [], slots: 32, ENG_NO: 255, layout: [] };
const S = { p: [], g: [], x: [], engine: 0, preset: 0, user: 0, sel: 0, tracks: [] };
const desc = { 0: [], 1: [], 2: [] };    // desc[scope][id] (scope 0 the track, 1 the song, 2 the other effects) = {fmt, min, max, def, names: [..] | null, label}
const ctl = { 0: new Map(), 1: new Map(), 2: new Map() };   // controls by id: {set(v), refresh()}

async function loadConstants() {
  const [P, G, PE0, ntrk, ne, nc, slots, ENG_NO, lsz, X] = await call(
    ["stu_pcount", "stu_gcount", "stu_pe0", "stu_ntracks", "stu_nengines", "stu_ncat", "stu_up_slots", "stu_engine_none",
     "stu_layout_size", "stu_xcount"].map((fn) => ({ fn })));
  Object.assign(fw, { P, G, X, PE0, ntrk, slots, ENG_NO });
  const q = [];
  for (let e = 0; e < ne; e++)
    q.push({ fn: "stu_engine_name", args: [e], ret: "str" }, { fn: "stu_engine_id", args: [e] }, { fn: "stu_npresets", args: [e] },
           { fn: "stu_page_title", args: [e, 0], ret: "str" }, { fn: "stu_page_title", args: [e, 1], ret: "str" });
  for (let c = 0; c < nc; c++) q.push({ fn: "stu_cat_name", args: [c], ret: "str" });
  q.push({ fn: "stu_layout", ret: ["u8", lsz] });
  const r = await call(q);
  fw.engines = [];
  for (let e = 0; e < ne; e++) {
    const [name, id, np, t0, t1] = r.slice(e * 5, e * 5 + 5);
    fw.engines.push({ index: e, name, id, np, titles: [t0, t1], presets: [] });
  }
  fw.cats = r.slice(ne * 5, ne * 5 + nc);
  const lay = r[r.length - 1];
  fw.layout = [];
  for (let i = 0; i < lay.length && lay[i];) {
    const g = { key: String.fromCharCode(lay[i++]), items: [] };
    while (i < lay.length && lay[i] !== 0xFF) { g.items.push({ scope: lay[i], id: lay[i + 1] }); i += 2; }
    i++;
    fw.layout.push(g);
  }
  // the presets' names and categories
  const pq = [];
  for (const e of fw.engines)
    for (let k = 0; k < e.np; k++) pq.push({ fn: "stu_preset_name", args: [e.index, k], ret: "str" }, { fn: "stu_preset_cat", args: [e.index, k] });
  const pr = await call(pq);
  let j = 0;
  for (const e of fw.engines)
    for (let k = 0; k < e.np; k++) e.presets.push({ name: pr[j++], cat: pr[j++] });
}

// the descriptions of the ids given (scope 0 engine parameters change with the engine and its mode)
async function loadDescs(items) {
  const q = [];
  for (const { scope, id } of items)
    q.push({ fn: "stu_desc", args: [scope, id], ret: ["i32", 5] }, { fn: "stu_desc_label", args: [scope, id], ret: "str" });
  const r = await call(q);
  const named = [];
  items.forEach(({ scope, id }, i) => {
    const [fmt, min, max, def, nn] = r[2 * i];
    desc[scope][id] = { fmt, min, max, def, label: r[2 * i + 1], names: null, nn };
    if (nn) named.push({ scope, id, min, nn });
  });
  if (named.length) {             // an enum's names: its values as the device shows them
    const nq = [];
    for (const n of named) for (let k = 0; k < n.nn; k++) nq.push({ fn: "stu_enum_name", args: [n.scope, n.id, k], ret: "str" });
    const nr = await call(nq);
    let j = 0;
    for (const n of named) desc[n.scope][n.id].names = nr.slice(j, j += n.nn);
  }
}

// ------------------------------------------------------------------ the page's words for the parameters
// the tabs, and the groups each shows (STU_LAYOUT's keys)
const TABS = [["layer", "LAYER", "ZFJ"], ["sound", "SOUND", "EANSV"], ["move", "MOVEMENT", "LDM"], ["fx", "EFFECTS", "OXYRCUPQ"],
              ["gen", "GENERATIVE", "GHK"]];
const GROUPS = {
  E: { title: "Engine", wide: true },
  A: { title: "Amp envelope", names: ["Attack", "Decay", "Sustain", "Release", "Level", "Octave"] },
  N: { title: "Envelope moves", names: ["Filter", "Pitch", "Shape"] },
  Z: { title: "Layer", names: ["In the patch", "Key low", "Key high", "Vel low", "Vel high", "Transpose"], track: true, wide: true,
       note: "A layer ON is part of the patch: a note played sounds on every ON layer whose key and velocity ranges hold it (STACK: a stack, a key split or a velocity split, by the ranges), or on one of them in turn (ROUND ROBIN), at the note plus its transpose. With no layer ON, the keys play the selected layer alone." },
  F: { title: "Layer insert", names: ["Drive", "Filter", "Cutoff", "Resonance", "Low cut", "Pan mode"], track: true, wide: true,
       note: "The layer's own processing before the patch's effects: DRIVE, a 12 dB FILTER (low-, band- or high-pass), LOW CUT; PAN MODE spreads its voices (ALT left and right, RND at random)." },
  J: { title: "Crossfade", names: ["Crossfade by", "Place", "Morph"], track: true, wide: true,
       note: "The layers given a place fade into each other along VELOCITY, KEY or the WHEEL (MIDI CC1, or MORPH here), two neighbours at a time, equal power: at a layer's place that layer alone. A layer at OFF stays at full level: a bass under a morphing pad. VELOCITY and KEY set each note's level as it starts; a layer faded out for a note does not play it." },
  S: { title: "Spread, sends", names: ["Spread", "Chorus", "Delay", "Reverb"],
       note: "Saved with the sound. The delay and reverb sends lose everything under ~220 Hz: the bass stays dry." },
  V: { title: "Voice", names: ["Mode", "Glide", "Glide by", "Priority", "Allocation", "Detune", "Pan"] },
  L: { title: "LFO", names: ["Rate", "Wave", "Phase", "Fade in", "Sync", "Trigger", "Polarity"] },
  D: { title: "LFO moves", names: ["Pitch", "Filter", "Shape", "Amp"] },
  M: { title: "Mod matrix", wide: true },
  X: { title: "Sound processing", names: ["Processing"], fx: true, wide: true,
       note: "BLUE HARE: its chorus, delay and reverb buses (amb-1's plate and echo among their types: PLATE, AMB), the EQ, mono lows, compressor and limiter. AMB-1 (Charles Vestal's): a 5.6 s tape echo with two wandering heads and a 160 ms Echoplex, fed into a full-rate Dattorro plate, mixed wet, one soft clip at the end; no chorus, no compressor. The same knobs drive it: TIME, FEEDBACK, COLOR, MIX the echo; SIZE, DAMPING the plate; GEN's MOTION the heads." },
  Y: { title: "Delay", names: ["Type", "Time", "Feedback", "Color", "Mix"], fx: true,
       note: "CLEAN: low-passed repeats · TAPE, WORN: wow, flutter, a saturated loop · PING: ping-pong · DUAL: two lines · AMB: amb-1's 5.3 s tape echo with two wandering heads (GEN's MOTION) and a 160 ms Echoplex, its echoes fed into the reverb" },
  R: { title: "Reverb", names: ["Type", "Size", "Damping"], fx: true,
       note: "ROOM: combs · SPRING: a dispersive spring · HALL: an 8-line FDN · GATED: HALL cut off · CONCERT: 16 lines · PLATE: amb-1's Dattorro plate, in float. The native app adds CAVERN and SHIMMER." },
  O: { title: "FX preset", names: ["Preset", "Wet"], fx: true, wide: true,
       note: "A preset sets the delay, the reverb, the chorus and the EQ at once (the layers' sends stay the sounds'): DRY ROOM PLATE HALL VAULT TAPE DUB DRIFT WIDE GATED LOFI SPACE. Changed below, the FM-1 shows the preset with a * (with EXPERT off it shows only this). WET turns every layer's chorus, delay and reverb sends down together: 100 % as the sounds set them." },
  U: { title: "EQ", names: ["Low", "Low mid", "High mid", "High"], fx: true,
       note: "The patch's four bands on the mix, before the compressor: shelves at 100 Hz and 8 kHz, peaks at 400 Hz and 2.5 kHz, ±12 dB." },
  C: { title: "Chorus", names: ["Rate", "Depth"], fx: true },
  P: { title: "Performance", names: ["Filter", "Crush", "Throw", "Depth"], fx: true,
       note: "Hold a button, or latch it, as a white key with FX held on the FM-1. FILTER: LPF left, HPF right; CRUSH: fewer bits; THROW: the dry mix into the delay and reverb; DEPTH: the hold effects' level (OCT UP / DN: the shimmer)." },
  Q: { title: "Master", names: ["Speaker EQ", "Master"], fx: true },
  G: { title: "Generative", names: ["Piece", "Scale", "Root", "Motion", "Tempo"], wide: true,
       note: "Four tracks as an ensemble: track 1 a pad of four breathing voices, 2 the bass, 3 bells in four loops of 13, 17, 23 and 29 beats, 4 a euclidean pulse. The chords drift on their own. A piece loads the four sounds, the tempo and the effects; SCALE and ROOT change the key at once; MOTION how far the music swells and thins. On the FM-1: ARP starts and stops it." },
  H: { title: "Density", names: ["Pad", "Bass", "Bells", "Pulse / waves"], wide: true,
       note: "How busy each role is while GEN plays (KNOB 1 to 4 on the FM-1's GEN MIX page): 64 the piece as written, 0 the role silent, 127 about twice as busy. The pieces without a pulse play WAVES on track 4: swells of noise. Keys played while GEN runs are snapped to its scale and sound on the bells." },
  K: { title: "Keys", names: ["Root", "Scale", "Snap", "Transpose", "Chord", "Voicing"], track: true },
};
const ENGINE_WORDS = {
  SID: "The C64's chip voice: pulse, saw, noise, ring, sync",
  WAVE: "Wavetables, scanned and warped",
  VOSIM: "Two formants of pulse trains: voices, reeds, brass",
  BLISS: "FM keys: electric pianos, bells, mallets",
  MODAL: "Struck, plucked and bowed resonators",
  STRINGS: "A string ensemble: 16', 8', 4' and its chorus",
  CANYON: "Echoes' repeats, all of them",
};

// ------------------------------------------------------------------ history (every change can be taken back)
const hist = { undo: [], redo: [], gesture: 0, lastT: 0 };
const snapshot = () => ({ sel: S.sel, engine: S.engine, p: S.p.slice(), g: S.g.slice(), x: S.x.slice(), name: sound.name, sub: sound.sub });
function remember(gestureKey) {    // before a change: the state as it is (a gesture's further changes share it)
  const now = performance.now();
  if (gestureKey && gestureKey === hist.gesture && now - hist.lastT < 1500) { hist.lastT = now; return; }
  hist.gesture = gestureKey;
  hist.lastT = now;
  hist.undo.push(snapshot());
  if (hist.undo.length > 200) hist.undo.shift();
  hist.redo.length = 0;
  syncHistory();
}
function syncHistory() {
  $("undo").disabled = !hist.undo.length;
  $("redo").disabled = !hist.redo.length;
}
async function restore(s) {
  hist.gesture = 0;
  const q = [];
  if (s.sel !== S.sel) q.push({ fn: "stu_select", args: [s.sel] });
  q.push({ fn: "stu_stage_begin" });
  for (let i = 0; i < fw.P; i++) q.push({ fn: "stu_stage_set", args: [i, s.p[i]] });
  q.push({ fn: "stu_stage_load", args: [s.engine] });
  for (let i = 0; i < fw.P; i++) if (kept.has(i)) q.push({ fn: "stu_set", args: [0, i, s.p[i], 0xBEEF] });
  for (let i = 0; i < fw.G; i++) if (s.g[i] !== S.g[i] && desc[1][i]) q.push({ fn: "stu_set", args: [1, i, s.g[i], 0] });
  for (let i = 0; i < fw.X; i++) if (s.x[i] !== S.x[i] && s.sel === S.sel) q.push({ fn: "stu_set", args: [2, i, s.x[i], 0] });
  await call(q);
  setSound(s.name, s.sub, false);
  await poll(true);
}
async function undo() {
  if (!hist.undo.length) return;
  const s = hist.undo.pop();
  hist.redo.push(snapshot());
  await restore(s);
  syncHistory();
}
async function redo() {
  if (!hist.redo.length) return;
  const s = hist.redo.pop();
  hist.undo.push(snapshot());
  await restore(s);
  syncHistory();
}
const kept = new Set();

// ------------------------------------------------------------------ the sound's name line
const sound = { name: "", sub: "", edited: false };
function setSound(name, sub, edited = false) {
  Object.assign(sound, { name, sub, edited });
  $("sound-name").textContent = name || "—";
  $("sound-sub").textContent = sub || "";
  $("sound-edited").hidden = !edited;
  if (!edited && name) $("name").value = name.slice(0, 12);
}
const markEdited = () => { if (!sound.edited) { sound.edited = true; $("sound-edited").hidden = false; } };

// ------------------------------------------------------------------ a knob
const ARC = 270, A0 = -135;
function polar(r, deg) { const a = (deg - 90) * Math.PI / 180; return [24 + r * Math.cos(a), 24 + r * Math.sin(a)]; }
function arcPath(r, d0, d1) {
  if (Math.abs(d1 - d0) < .5) return "";
  const [x0, y0] = polar(r, Math.min(d0, d1)), [x1, y1] = polar(r, Math.max(d0, d1));
  return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${r} ${r} 0 ${Math.abs(d1 - d0) > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}
const NS = "http://www.w3.org/2000/svg";
function svg(tag, attrs) { const e = document.createElementNS(NS, tag); for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v); return e; }

// a control of parameter (scope, id): a knob, or (an enum of a few names) buttons, or a list
function control(scope, id, name) {
  const d = desc[scope][id];
  if (!d) return h("span");
  const label = name || d.label;
  let el, show;
  if (d.fmt === F.ENUM && d.names && d.names.length <= 5) {
    const seg = h("div", { class: "seg", role: "radiogroup", "aria-label": label });
    const bs = d.names.map((n, k) => {
      const b = h("button", { type: "button", "aria-pressed": "false", onclick: () => change(scope, id, d.min + k, "seg" + scope + "." + id + "." + k) }, n);
      seg.append(b);
      return b;
    });
    el = h("div", { class: "choice", title: d.label }, h("span", { class: "lbl" }, label), seg);
    show = (v) => bs.forEach((b, k) => b.setAttribute("aria-pressed", String(d.min + k === v)));
  } else if (d.fmt === F.ENUM && d.names) {
    const sel = h("select", { "aria-label": label, onchange: () => change(scope, id, +sel.value, 0) },
      d.names.map((n, k) => h("option", { value: d.min + k }, n)));
    el = h("div", { class: "choice", title: d.label }, h("span", { class: "lbl" }, label), sel);
    show = (v) => { sel.value = String(v); };
  } else {
    const fixed = d.max <= d.min;
    const bip = d.min < 0 && d.max > 0;
    const g = svg("svg", { viewBox: "0 0 48 48", "aria-hidden": "true" });
    g.append(svg("circle", { class: "face", cx: 24, cy: 24, r: 15, "stroke-width": 1 }));
    g.append(svg("path", { class: "track", d: arcPath(20, A0, A0 + ARC), fill: "none", "stroke-width": 4, "stroke-linecap": "round" }));
    const arc = svg("path", { class: "arc", fill: "none", "stroke-width": 4, "stroke-linecap": "round" });
    const ptr = svg("line", { class: "ptr", x1: 24, y1: 24, x2: 24, y2: 12, "stroke-width": 2.4, "stroke-linecap": "round" });
    g.append(arc, ptr);
    const val = h("span", { class: "val" }, "");
    el = h("div", { class: "knob" + (fixed ? " off" : ""), role: "slider", tabindex: fixed ? "-1" : "0", "aria-label": label,
                    "aria-valuemin": d.min, "aria-valuemax": d.max, title: `${d.label} · double-click: ${d.def}` },
      h("span", { class: "lbl" }, label), g, val);
    const deg = (v) => A0 + ARC * (v - d.min) / Math.max(1, d.max - d.min);
    show = (v) => {
      const a = deg(v), z = bip ? deg(0) : A0;
      arc.setAttribute("d", arcPath(20, z, a));
      ptr.setAttribute("transform", `rotate(${a} 24 24)`);
      el.setAttribute("aria-valuenow", v);
      el.classList.toggle("moved", v !== d.def);
    };
    el._val = val;
    if (!fixed) {
      let drag = null;
      el.addEventListener("pointerdown", (e) => {
        if (e.button) return;
        el.setPointerCapture(e.pointerId);
        drag = { y: e.clientY, v: cur(scope, id), key: Symbol() };
        e.preventDefault();
      });
      el.addEventListener("pointermove", (e) => {
        if (!drag) return;
        const span = d.max - d.min, px = e.shiftKey ? 600 : 180;
        const v = Math.round(drag.v + (drag.y - e.clientY) * Math.max(span, 8) / px);
        change(scope, id, clamp(v, d.min, d.max), drag.key);
      });
      const end = () => { drag = null; };
      el.addEventListener("pointerup", end);
      el.addEventListener("pointercancel", end);
      el.addEventListener("dblclick", () => change(scope, id, d.def, 0));
      el.addEventListener("wheel", (e) => {
        e.preventDefault();
        const step = e.shiftKey ? 1 : Math.max(1, Math.round((d.max - d.min) / 64));
        change(scope, id, clamp(cur(scope, id) + (e.deltaY < 0 ? step : -step), d.min, d.max), "wheel" + scope + "." + id);
      }, { passive: false });
      el.addEventListener("keydown", (e) => {
        const big = Math.max(1, Math.round((d.max - d.min) / 10));
        const k = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: big, PageDown: -big }[e.key];
        let v = null;
        if (k) v = cur(scope, id) + k;
        else if (e.key === "Home") v = d.min;
        else if (e.key === "End") v = d.max;
        else if (e.key === "Delete" || e.key === "Backspace") v = d.def;
        if (v === null) return;
        e.preventDefault();
        e.stopPropagation();
        change(scope, id, clamp(v, d.min, d.max), "key" + scope + "." + id);
      });
    }
  }
  const c = { el, show, text: (t) => { if (el._val) el._val.textContent = t; el.setAttribute?.("aria-valuetext", t); } };
  ctl[scope].set(id, c);
  return el;
}
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const vals = (scope) => (scope === 2 ? S.x : scope ? S.g : S.p);
const cur = (scope, id) => vals(scope)[id] ?? 0;

// a value changed on the page: into the firmware (the latest only, one call in flight per parameter), then its text
const inflight = new Map();
function change(scope, id, v, gestureKey) {
  if (cur(scope, id) === v) return;
  remember(gestureKey || Symbol());
  vals(scope)[id] = v;
  const c = ctl[scope].get(id);
  if (c) c.show(v);
  if (!scope) markEdited();
  const k = scope + "." + id;
  if (inflight.has(k)) { inflight.get(k).next = v; return; }
  const send = async (x) => {
    inflight.set(k, { next: null });
    try {
      const [got, text] = await call([{ fn: "stu_set", args: [scope, id, x, hashKey(gestureKey)] }, { fn: "stu_format", args: [scope, id, x], ret: "str" }]);
      vals(scope)[id] = got;
      if (c) c.text(text);
      if (!scope && id >= fw.PE0) refreshEngineLabels();
      if (!scope && id >= 1 && id <= 4) drawEnv();          // (P_ATK .. P_REL)
      if (scope === 2) procSync();
      if (scope === 2 && desc[2][id]?.label === "LAYER") refreshTracks();
    } finally {
      const nx = inflight.get(k).next;
      inflight.delete(k);
      if (nx !== null && nx !== x) send(nx);
    }
  };
  send(v);
}
const keyIds = new Map();
function hashKey(k) {                       // a gesture's tag for the firmware's own undo (ui.c edit_undo_take)
  if (!k) return 0;
  if (!keyIds.has(k)) keyIds.set(k, (keyIds.size % 60000) + 1000);
  return keyIds.get(k);
}

// ------------------------------------------------------------------ the parameter groups
let envCanvas = null;
function drawEnv() {
  if (!envCanvas) return;
  const c = envCanvas, x = c.getContext("2d"), W = c.width, H = c.height;
  const st = getComputedStyle(document.documentElement);
  x.clearRect(0, 0, W, H);
  const a = S.p[1] / 127, dcy = S.p[2] / 127, s = S.p[3] / 127, r = S.p[4] / 127;
  const seg = (W - 16) / 4, xa = 8 + a * seg, xd = xa + dcy * seg, xs = xd + seg * .8, xr = xs + r * seg;
  const y = (v) => H - 8 - v * (H - 16);
  x.strokeStyle = st.getPropertyValue("--accent");
  x.fillStyle = st.getPropertyValue("--accent").trim() + "22";
  x.lineWidth = 2;
  x.beginPath();
  x.moveTo(8, y(0)); x.lineTo(xa, y(1)); x.lineTo(xd, y(s)); x.lineTo(xs, y(s)); x.lineTo(xr, y(0));
  x.stroke();
  x.lineTo(8, y(0));
  x.fill();
}

// the performance layer's hold effects (perform.c PF_DEF's order): held while pressed, or latched (a press on, a press
// off), as on the FM-1 with MENU > FX LATCH
const PERF = [["LPF", 4], ["HPF", 5], ["FREEZE", 7], ["OCT UP", 8], ["OCT DN", 9]];
let perfLatch = pref("bluehare-studio-latch") === "1";
const perfEls = [];
function perfButtons() {
  perfEls.length = 0;
  const row = h("div", { class: "perf" });
  for (const [name, e] of PERF) {
    const b = h("button", { type: "button", class: "btn perfb", "aria-pressed": "false", "data-e": e }, name);
    b.addEventListener("pointerdown", (ev) => { ev.preventDefault(); try { b.setPointerCapture(ev.pointerId); } catch {} perfPress(e, 1); });
    const up = () => { if (!perfLatch) perfPress(e, 0); };
    b.addEventListener("pointerup", up);
    b.addEventListener("pointercancel", up);
    b.addEventListener("keydown", (ev) => { if ((ev.key === " " || ev.key === "Enter") && !ev.repeat) { ev.preventDefault(); perfPress(e, 1); } });
    b.addEventListener("keyup", (ev) => { if (ev.key === " " || ev.key === "Enter") { ev.preventDefault(); up(); } });
    perfEls.push(b);
    row.append(b);
  }
  const latch = h("button", { type: "button", class: "btn small" + (perfLatch ? " on" : ""), "aria-pressed": String(perfLatch),
    title: "Latch: a press turns an effect on, the next turns it off" }, "Latch");
  latch.addEventListener("click", async () => {
    perfLatch = !perfLatch;
    pref("bluehare-studio-latch", perfLatch ? "1" : "0");
    latch.classList.toggle("on", perfLatch);
    latch.setAttribute("aria-pressed", String(perfLatch));
    await one("stu_perf_off");
    await poll(true);
  });
  const off = h("button", { type: "button", class: "btn small", title: "Every effect off, the macros back to 0" }, "All off");
  off.addEventListener("click", async () => { await one("stu_perf_off"); await poll(true); });
  row.append(latch, off);
  return row;
}
async function perfPress(e, down) {
  await call([{ fn: "stu_perf", args: [e, down, perfLatch ? 1 : 0] }]);
  await perfShow();
}
async function perfShow() {
  const on = await one("stu_perf_on");
  for (const b of perfEls) b.setAttribute("aria-pressed", String(!!((on >>> +b.dataset.e) & 1)));
}
// the master's stages: SPEAKER EQ and MASTER are controls (above), the rest always on; the compressor's and the limiter's
// gain reduction live
function masterStages() {
  const box = h("div", { class: "stages" });
  box.append(h("div", { class: "chain" }, ["Mono lows", "Bus compressor 3:1", "Peak limiter", "Soft clip", "DC blocker"]
    .map((n) => h("span", { class: "stage" }, n))));
  const gr = (label, id) => h("div", { class: "meter gr" }, h("span", { class: "lbl" }, label), h("span", { class: "bar" }, h("i", { id })),
    h("span", { class: "mono", id: id + "-v" }, "0.0 dB"));
  box.append(gr("Compressor", "gr-comp"), gr("Limiter", "gr-lim"));
  return box;
}
async function meterPoll() {
  if (!node || document.hidden || !$("gr-comp")) return;
  const [[c, l], on] = await call([{ fn: "stu_meters", ret: ["i32", 2] }, { fn: "stu_perf_on" }]);
  for (const [id, g] of [["gr-comp", c], ["gr-lim", l]]) {
    const db = 20 * Math.log10(Math.max(1, g) / 32768);
    $(id).style.width = clamp(-db / 12 * 100, 0, 100) + "%";
    $(id + "-v").textContent = (db > -0.05 ? "0.0" : db.toFixed(1)) + " dB";
  }
  for (const b of perfEls) b.setAttribute("aria-pressed", String(!!((on >>> +b.dataset.e) & 1)));
}

let tabNow = pref("bluehare-studio-tab") || "sound";
function showTab(id) {
  tabNow = TABS.some((t) => t[0] === id) ? id : "sound";
  pref("bluehare-studio-tab", tabNow);
  for (const b of $("tabs").children) b.setAttribute("aria-selected", String(b.dataset.tab === tabNow));
  for (const p of $("groups").children) p.hidden = p.dataset.tab !== tabNow;
  if ($("fx-actions")) $("fx-actions").hidden = tabNow !== "fx";
  drawEnv();
}
async function buildGroups() {
  const root = $("groups"), tabs = $("tabs");
  root.textContent = "";
  tabs.textContent = "";
  ctl[0].clear();
  ctl[1].clear();
  ctl[2].clear();
  const items = fw.layout.flatMap((g) => g.items);
  await loadDescs(items);
  fw.SX_LMODE = desc[2].findIndex((d) => d && d.label === "MODE");
  for (const [id, title, keys] of TABS) {
    const b = h("button", { class: "tab", role: "tab", type: "button", "data-tab": id, onclick: () => showTab(id) }, title,
      id === "gen" ? h("span", { class: "live", id: "gen-live", hidden: true }) : null);
    tabs.append(b);
    const panel = h("div", { class: "groups", role: "tabpanel", "data-tab": id });
    for (const key of keys) {
      const g = fw.layout.find((x) => x.key === key);
      if (g) panel.append(groupBox(g));
    }
    root.append(panel);
  }
  tabs.append(fxActions());
  showTab(tabNow);
}
function groupBox(g) {
  const G = GROUPS[g.key] || { title: g.key };
  const box = h("section", { class: "group" + (G.wide ? " wide" : ""), "data-group": g.key });
  const info = G.note && g.key !== "G" && !G.show ? h("button", { class: "info", type: "button", title: G.note, "aria-label": "About " + G.title,
    onclick: () => { const n = box.querySelector(".gnote"); if (n) n.hidden = !n.hidden; } }, "ⓘ") : null;
  box.append(h("h3", {}, G.title, G.track ? h("span", { class: "sub" }, `layer ${S.sel + 1}`) : null,
    G.fx ? h("span", { class: "sub" }, "the patch") : null, info));
  const ctrls = h("div", { class: "ctrls" });
  if (g.key === "E") {
    box.id = "engine-group";
    engineGroup(ctrls, g.items);
  } else if (g.key === "M") {
    ctrls.append(matrix(g.items));
  } else {
    if (g.key === "G") ctrls.append(genBar());
    if (g.key === "P") ctrls.append(perfButtons());
    if (g.key === "A") {
      envCanvas = h("canvas", { id: "env-graph", width: 300, height: 112 });
      ctrls.append(envCanvas);
    }
    g.items.forEach((it, i) => ctrls.append(control(it.scope, it.id, G.names?.[i])));
    if (g.key === "Q") ctrls.append(masterStages());
    if (G.note) ctrls.append(h("p", { class: "gnote", hidden: !G.show && g.key !== "G" }, G.note));   /* (the others: ⓘ) */
  }
  box.append(ctrls);
  return box;
}
// the effects' file: at the top of the EFFECTS tab
function fxActions() {
  const file = h("input", { type: "file", accept: ".json,application/json", hidden: true });
  return h("div", { id: "fx-actions", class: "tabacts", title: "Every effect here is shared by the four tracks. An effects file keeps them all (not MASTER)." },
    h("button", { class: "btn small", type: "button", onclick: () => download("effects.bluehare-fx.json", JSON.stringify(fxFile(), null, 1)) }, "Export effects"),
    h("button", { class: "btn small", type: "button", onclick: () => pickFile(file, loadFxFile) }, "Import effects"), file);
}
// GEN: play / stop, the chord, the bar, a light for each role as it plays
let genOn = false, genPlayed = [0, 0, 0, 0];
function genBar() {
  const play = h("button", { class: "btn primary play", type: "button", id: "gen-play", onclick: async () => {
    await one("stu_gen", genOn ? 0 : 1);
    await genPoll();
  } }, "▶ Play");
  return h("div", { class: "genbar" }, play, h("span", { class: "chord", id: "gen-chord" }, "—"),
    h("span", { class: "where", id: "gen-where" }, "stopped"),
    h("span", { class: "roles" }, ["PAD", "BASS", "BELLS", "PULSE"].map((n, i) => h("span", { class: "role", id: "gen-r" + i }, n))));
}
async function genPoll() {
  if (!node || !$("gen-play")) return;
  const [st, chord] = await call([{ fn: "stu_gen_state", ret: ["i32", 8] }, { fn: "stu_gen_chord", ret: "str" }]);
  genOn = !!st[0];
  $("gen-play").textContent = genOn ? "■ Stop" : "▶ Play";
  $("gen-play").classList.toggle("on", genOn);
  $("gen-chord").textContent = genOn ? chord || "—" : "—";
  $("gen-where").textContent = genOn ? `bar ${st[1] + 1} · step ${st[2] + 1}` : "stopped";
  $("gen-live").hidden = !genOn;
  for (let r = 0; r < 4; r++) {
    $("gen-r" + r).classList.toggle("hit", genOn && st[4 + r] !== genPlayed[r]);
    genPlayed[r] = st[4 + r];
  }
}
function engineGroup(ctrls, items) {
  const e = fw.engines[S.engine];
  ctrls.textContent = "";
  ctrls.append(h("span", { class: "subhead" }, e ? e.titles[0] : ""));
  items.slice(0, 4).forEach((it) => ctrls.append(control(it.scope, it.id)));
  ctrls.append(h("span", { class: "subhead" }, e ? e.titles[1] : ""));
  items.slice(4).forEach((it) => ctrls.append(control(it.scope, it.id)));
}
let engItems = null, engLabelsBusy = false;
async function refreshEngineLabels() {   // an engine's labels follow its mode (a desc hook): re-read them, rebuild if one moved
  if (engLabelsBusy) return;
  engLabelsBusy = true;
  try {
    engItems = fw.layout.find((g) => g.key === "E").items;
    const before = engItems.map((it) => JSON.stringify(desc[0][it.id]));
    await loadDescs(engItems);
    const after = engItems.map((it) => JSON.stringify(desc[0][it.id]));
    const box = $("engine-group")?.querySelector(".ctrls");
    relabelMatrix();
    if (box && before.join() !== after.join()) {
      engineGroup(box, engItems);
      await showValues(engItems);
    }
  } finally { engLabelsBusy = false; }
}
const dstSelects = [];
function relabelMatrix() {               // the matrix's E1..E8: the engine's parameters by their names
  for (const s of dstSelects)
    for (const o of s.options) {
      const m = /^E(\d)$/.exec(o.dataset.n || "");
      if (m) o.textContent = `E${m[1]} · ${desc[0][fw.PE0 + +m[1] - 1]?.label || "-"}`;
    }
}
function matrix(items) {
  dstSelects.length = 0;
  const m = h("div", { class: "matrix" }, h("span", { class: "h" }, ""), h("span", { class: "h" }, "Source"),
    h("span", { class: "h" }, "Destination"), h("span", { class: "h" }, "Amount"));
  for (let k = 0; k < 4; k++) {
    const [src, dst, amt] = items.slice(3 * k, 3 * k + 3);
    const sel = (it) => {
      const d = desc[0][it.id];
      const s = h("select", { "aria-label": `Slot ${k + 1} ${d.label}`, onchange: () => change(0, it.id, +s.value, 0) },
        (d.names || []).map((n, j) => h("option", { value: d.min + j, "data-n": n }, n)));
      if (it === dst) dstSelects.push(s);
      ctl[0].set(it.id, { el: s, show: (v) => { s.value = String(v); }, text: () => {} });
      return s;
    };
    m.append(h("span", { class: "slot" }, String(k + 1)), sel(src), sel(dst), control(0, amt.id, "Amount"));
  }
  return m;
}
// the values of the items given, and their texts, from the firmware
// the processing chosen (EFFECTS > PROCESSING): with amb-1's, what it has none of is dimmed (the chorus, the delay's and
// reverb's TYPE, the master's compressor and limiter); the knobs it reads stay
function procSync() {
  const k = desc[2].findIndex((d) => d && d.label === "PROC"), amb = k >= 0 && S.x[k] === 1;
  const grp = (key) => document.querySelector(`[data-group="${key}"]`);
  grp("C")?.classList.toggle("dim", amb);
  for (const key of ["Y", "R"]) grp(key)?.querySelector(".choice")?.classList.toggle("dim", amb);
  grp("Q")?.querySelector(".stages")?.classList.toggle("dim", amb);
}
async function showValues(items) {
  const q = items.map(({ scope, id }) => ({ fn: "stu_format", args: [scope, id, cur(scope, id)], ret: "str" }));
  const r = await call(q);
  items.forEach(({ scope, id }, i) => {
    const c = ctl[scope].get(id);
    if (c) { c.show(cur(scope, id)); c.text(r[i]); }
  });
  procSync();
  drawEnv();
}

// ------------------------------------------------------------------ polling the firmware (MIDI CCs, the undo of
// the device, loads all change values without the page)
let polling = false;
async function poll(force = false) {
  if (!node || polling) return;
  polling = true;
  try {
    const [d] = await call([{ fn: "stu_dump", ret: ["i16", fw.P + fw.G + 4 + fw.X] }]);
    const x = d.slice(fw.P + fw.G + 4);
    const p = d.slice(0, fw.P), g = d.slice(fw.P, fw.P + fw.G);
    const [engine, preset, user, sel] = d.slice(fw.P + fw.G, fw.P + fw.G + 4);
    const engineMoved = engine !== S.engine || sel !== S.sel;
    const changed = [];
    for (let i = 0; i < fw.P; i++) if (force || p[i] !== S.p[i]) if (!inflight.has("0." + i)) { S.p[i] = p[i]; changed.push({ scope: 0, id: i }); }
    for (let i = 0; i < fw.G; i++) if (force || g[i] !== S.g[i]) if (!inflight.has("1." + i)) { S.g[i] = g[i]; changed.push({ scope: 1, id: i }); }
    for (let i = 0; i < fw.X; i++) if (force || x[i] !== S.x[i]) if (!inflight.has("2." + i)) { S.x[i] = x[i]; changed.push({ scope: 2, id: i }); }
    const selMoved = sel !== S.sel;
    Object.assign(S, { engine, preset, user, sel });
    if (selMoved && ctl[0].size) await buildGroups();   // (the groups name the track: "track 2")
    if (engineMoved || force) {
      renderEngines();
      renderPresets();
      await refreshTracks();
      await refreshEngineLabels();
      const box = $("engine-group")?.querySelector(".ctrls");
      if (box) engineGroup(box, fw.layout.find((x) => x.key === "E").items);
    }
    if (selMoved) await nameFromDevice();
    const shown = changed.filter(({ scope, id }) => ctl[scope].has(id));
    if (engineMoved || force) await showValues(fw.layout.flatMap((x) => x.items));
    else if (shown.length) await showValues(shown);
    renderPresetMark();
  } finally { polling = false; }
}
async function nameFromDevice() {        // the track's sound as the firmware knows it: a user slot or a preset
  const e = fw.engines[S.engine];
  if (S.user) setSound(await str("stu_up_name", S.user - 1), `${e.name} · my sound ${S.user}`);
  else if (e && e.presets[S.preset]) setSound(e.presets[S.preset].name, `${e.name} · preset ${S.preset + 1}`);
}

// ------------------------------------------------------------------ tracks, engines, presets, user slots
async function refreshTracks() {
  const q = [...Array(fw.ntrk).keys()];
  const r = await call(q.map((t) => ({ fn: "stu_track_engine", args: [t] })));
  const on = await call(q.map((t) => ({ fn: "stu_layer_on", args: [t] })));
  S.tracks = r;
  const nav = $("tracks");
  nav.textContent = "";
  r.forEach((e, t) => nav.append(h("button", { class: "trk", type: "button", "aria-pressed": String(t === S.sel),
    onclick: () => selectTrack(t), title: on[t] ? "In the patch" : "Not in the patch" },
    h("small", {}, h("span", { class: "dot" + (on[t] ? " on" : "") }), `LAYER ${t + 1}`), h("b", {}, fw.engines[e]?.name || "-"))));
  const pm = $("patch-mode");                 // the patch's play mode (STACK / ROUND ROBIN), beside the layers
  if (pm && fw.SX_LMODE >= 0 && desc[2][fw.SX_LMODE]) {
    pm.textContent = "";
    pm.append(control(2, fw.SX_LMODE, "Play"));
    ctl[2].get(fw.SX_LMODE)?.show(S.x[fw.SX_LMODE]);
  }
}
async function selectTrack(t) {
  if (t === S.sel) return;
  await one("stu_select", t);
  hist.gesture = 0;
  await poll(true);
  await nameFromDevice();
}
function renderEngines() {
  const box = $("engines");
  box.textContent = "";
  for (const e of fw.engines)
    box.append(h("button", { class: "eng", type: "button", "aria-pressed": String(e.index === S.engine),
      onclick: () => pickEngine(e.index) }, h("b", {}, e.name), h("small", {}, ENGINE_WORDS[e.name] || "")));
}
async function pickEngine(e) {
  if (e === S.engine) return;
  remember(0);
  await one("stu_set_engine", e);
  await poll(true);
  const en = fw.engines[e];
  setSound(en.presets[0]?.name || en.name, `${en.name} · preset 1`);
}
let catFilter = +(pref("bluehare-studio-cat") || 0);
function renderPresets() {
  const e = fw.engines[S.engine];
  const cats = $("cats");
  cats.textContent = "";
  const present = new Set(e.presets.map((p) => p.cat));
  if (!present.has(catFilter)) catFilter = 0;
  cats.append(h("button", { class: "chip", type: "button", "aria-pressed": String(!catFilter), onclick: () => { catFilter = 0; pref("bluehare-studio-cat", 0); renderPresets(); } }, "All"));
  for (const c of [...present].sort((a, b) => a - b))
    if (c) cats.append(h("button", { class: "chip", type: "button", "aria-pressed": String(c === catFilter),
      onclick: () => { catFilter = c; pref("bluehare-studio-cat", c); renderPresets(); } }, fw.cats[c] || "?"));
  const list = $("presets");
  list.textContent = "";
  e.presets.forEach((p, k) => {
    if (catFilter && p.cat !== catFilter) return;
    list.append(h("button", { class: "row", type: "button", "data-k": k, onclick: () => pickPreset(k) },
      h("span", { class: "n" }, String(k + 1).padStart(2, "0")), h("span", { class: "name" }, p.name),
      p.cat ? h("span", { class: "tag" }, fw.cats[p.cat]) : null));
  });
  renderPresetMark();
}
function renderPresetMark() {
  for (const r of $("presets").children) r.setAttribute("aria-current", String(!S.user && +r.dataset.k === S.preset));
  for (const r of $("users").children) r.setAttribute("aria-current", String(S.user && +r.dataset.k === S.user - 1));
}
async function pickPreset(k) {
  remember(0);
  await one("stu_load_preset", k);
  await poll(true);
  const e = fw.engines[S.engine];
  setSound(e.presets[k].name, `${e.name} · preset ${k + 1}`);
}
let users = [];
async function refreshUsers() {
  const q = [];
  for (let k = 0; k < fw.slots; k++)
    q.push({ fn: "stu_up_used", args: [k] }, { fn: "stu_up_name", args: [k], ret: "str" }, { fn: "stu_up_engine", args: [k] }, { fn: "stu_up_cat", args: [k] });
  const r = await call(q);
  users = [];
  for (let k = 0; k < fw.slots; k++) users.push({ used: r[4 * k], name: r[4 * k + 1], engine: r[4 * k + 2], cat: r[4 * k + 3] });
  const list = $("users");
  list.textContent = "";
  const slotSel = $("slot"), keep = slotSel.value;
  slotSel.textContent = "";
  users.forEach((u, k) => {
    const n = h("span", { class: "n" }, "U" + String(k + 1).padStart(2, "0"));
    if (u.used) {
      list.append(h("button", { class: "row", type: "button", "data-k": k, onclick: () => pickUser(k) }, n,
        h("span", { class: "name" }, u.name), h("span", { class: "tag" }, fw.engines[u.engine]?.name || "—")));
    }
    slotSel.append(h("option", { value: k }, `U${String(k + 1).padStart(2, "0")} ${u.used ? u.name : "· empty"}`));
  });
  if (!list.children.length) list.append(h("p", { class: "note", style: "margin:4px 8px" }, "Nothing saved yet: shape a sound, name it and press Save to slot."));
  const firstFree = users.findIndex((u) => !u.used);
  slotSel.value = keep !== "" ? keep : String(firstFree >= 0 ? firstFree : 0);
  renderPresetMark();
}
async function pickUser(k) {
  remember(0);
  const rc = await one("stu_up_load", k);
  if (rc) return say("save-msg", "That slot's sound cannot be loaded here (its engine is not in this build).", "bad");
  await poll(true);
  setSound(users[k].name, `${fw.engines[S.engine]?.name} · my sound ${k + 1}`);
}

// ------------------------------------------------------------------ saving, files, the FM-1
const say = (id, t, kind = "") => { const e = $(id); e.textContent = t; e.className = "note" + (kind ? " " + kind : ""); };
const cleanName = (s) => s.toUpperCase().replace(/[^\x20-\x7E]/g, "").slice(0, 12).trim();
function fillCats() {
  const c = $("cat");
  c.textContent = "";
  fw.cats.forEach((n, k) => c.append(h("option", { value: k }, k ? n : "From the sound")));
}
async function storeSlot() {
  const name = cleanName($("name").value), k = +$("slot").value, cat = +$("cat").value;
  if (!name) return say("save-msg", "Give the sound a name first.", "bad");
  if (users[k]?.used && !confirm(`U${String(k + 1).padStart(2, "0")} holds ${users[k].name}. Replace it?`)) return;
  const [rc] = await call([{ fn: "stu_up_store", args: [k, cat], str: name }]);
  if (rc) return say("save-msg", rc === 1 ? "The name can use letters, digits and punctuation, up to 12." : "Could not write the slot.", "bad");
  await refreshUsers();
  setSound(name, `${fw.engines[S.engine].name} · my sound ${k + 1}`);
  say("save-msg", `Saved to U${String(k + 1).padStart(2, "0")}.`, "ok");
}
function download(name, data) {
  const a = h("a", { href: URL.createObjectURL(new Blob([data], { type: "application/json" })), download: name });
  document.body.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}
const fileBase = (n) => (n || "sound").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "sound";
// a sound's file: the engine (by name and by its stored id) and every parameter by id (the same numbering as user
// presets and the editor protocol, web/EDITOR_PROTOCOL.md), with the device's labels to read it by
function soundFile() {
  const e = fw.engines[S.engine], name = cleanName($("name").value) || sound.name || e.name;
  const params = {}, labels = {};
  for (let i = 0; i < fw.P; i++) {
    if (kept.has(i)) continue;
    params[i] = S.p[i];
    if (desc[0][i]) labels[i] = desc[0][i].label;
  }
  return { format: "blue-hare-sound", version: 1, name, category: fw.cats[+$("cat").value] || "-", engine: e.name,
           engineId: e.id, pcount: fw.P, params, labels };
}
async function loadSoundFile(j) {
  if (j.format !== "blue-hare-sound") throw new Error("not a Blue Hare sound file");
  let e = fw.engines.find((x) => x.id === j.engineId) || fw.engines.find((x) => x.name === j.engine);
  if (!e) throw new Error(`its engine (${j.engine}) is not in this build`);
  remember(0);
  const q = [{ fn: "stu_stage_begin" }];
  for (const [k, v] of Object.entries(j.params || {})) if (+k < fw.P) q.push({ fn: "stu_stage_set", args: [+k, v | 0] });
  q.push({ fn: "stu_stage_load", args: [e.index] });
  await call(q);
  await poll(true);
  setSound(cleanName(j.name || "IMPORTED"), `${e.name} · from a file`);
  const c = fw.cats.indexOf(j.category);
  if (c > 0) $("cat").value = String(c);
}
// the effects' file: the shared buses (song ids) and the other effects (scope 2: the selected track's inserts, the
// performance macros, the speaker EQ; not MASTER, the level), keyed "song.<id>" / "fx.<id>" with the device's labels
const fxItems = () => fw.layout.filter((g) => GROUPS[g.key]?.fx).flatMap((g) => g.items)
  .filter((it) => it.scope && !(it.scope === 2 && desc[2][it.id]?.label === "MASTER"));
// a patch: its layers, each embedded whole (engine, every parameter, the layer's settings), the play mode, the effects
const LAYER_LABELS = ["LAYER", "KEY LO", "KEY HI", "VEL LO", "VEL HI", "TRANS", "LFILT", "LCUT", "LRES", "LOWCUT", "PANMODE"];
const sxId = (label) => desc[2].findIndex((d) => d && d.label === label);
async function patchFile() {
  const sel = S.sel, layers = [];
  for (let t = 0; t < fw.ntrk; t++) {
    await one("stu_select", t);
    const [d] = await call([{ fn: "stu_dump", ret: ["i16", fw.P + fw.G + 4 + fw.X] }]);
    const e = fw.engines[d[fw.P + fw.G]], params = {}, layer = {};
    for (let i = 0; i < fw.P; i++) if (!kept.has(i)) params[i] = d[i];
    for (const l of LAYER_LABELS) layer[l] = d[fw.P + fw.G + 4 + sxId(l)];
    const pan = desc[0].findIndex((x) => x && x.label === "PAN");
    layers.push({ engine: e.name, engineId: e.id, params, layer, level: d[0], pan: pan >= 0 ? d[pan] : 0 });
  }
  await one("stu_select", sel);
  const fx = fxFile();
  return { format: "blue-hare-patch", version: 1, name: cleanName($("name").value) || sound.name || "PATCH",
           mode: S.x[sxId("MODE")], layers, fx: fx.params };
}
async function loadPatchFile(j) {
  if (j.format !== "blue-hare-patch") throw new Error("not a Blue Hare patch file");
  remember(0);
  const sel = S.sel;
  for (let t = 0; t < Math.min(fw.ntrk, (j.layers || []).length); t++) {
    const L = j.layers[t], e = fw.engines.find((x) => x.id === L.engineId) || fw.engines.find((x) => x.name === L.engine);
    if (!e) continue;
    const q = [{ fn: "stu_select", args: [t] }, { fn: "stu_stage_begin" }];
    for (const [k, v] of Object.entries(L.params || {})) if (+k < fw.P) q.push({ fn: "stu_stage_set", args: [+k, v | 0] });
    q.push({ fn: "stu_stage_load", args: [e.index] });
    for (const [l, v] of Object.entries(L.layer || {})) if (sxId(l) >= 0) q.push({ fn: "stu_set", args: [2, sxId(l), v | 0, 0] });
    const pan = desc[0].findIndex((x) => x && x.label === "PAN");
    if (L.level !== undefined) q.push({ fn: "stu_set", args: [0, 0, L.level | 0, 0] });
    if (pan >= 0 && L.pan !== undefined) q.push({ fn: "stu_set", args: [0, pan, L.pan | 0, 0] });
    await call(q);
  }
  if (sxId("MODE") >= 0) await one("stu_set", 2, sxId("MODE"), j.mode | 0, 0);
  await one("stu_select", sel);
  if (j.fx) await loadFxFile({ format: "blue-hare-fx", params: j.fx });
  await poll(true);
  await refreshTracks();
  setSound(cleanName(j.name || "PATCH"), "a patch");
}
function fxFile() {
  const params = {}, labels = {};
  for (const it of fxItems()) {
    const k = (it.scope === 1 ? "song." : "fx.") + it.id;
    params[k] = cur(it.scope, it.id);
    labels[k] = desc[it.scope][it.id]?.label;
  }
  return { format: "blue-hare-fx", version: 2, params, labels };
}
async function loadFxFile(j) {
  if (j.format !== "blue-hare-fx") throw new Error("not a Blue Hare FX file");
  remember(0);
  const known = new Map(fxItems().map((it) => [(it.scope === 1 ? "song." : "fx.") + it.id, it]));
  const q = [];
  for (const [k, v] of Object.entries(j.params || {})) {
    const it = known.get(/^\d+$/.test(k) ? "song." + k : k);   // (version 1: song ids alone)
    if (it) q.push({ fn: "stu_set", args: [it.scope, it.id, v | 0, 0] });
  }
  await call(q);
  await poll(true);
}
function pickFile(input, then) {
  input.onchange = async () => {
    const f = input.files[0];
    input.value = "";
    if (!f) return;
    try { await then(JSON.parse(await f.text())); say("save-msg", `Loaded ${f.name}.`, "ok"); }
    catch (err) { say("save-msg", `Could not load ${f.name}: ${err.message}.`, "bad"); }
  };
  input.click();
}

// the FM-1 over Web MIDI: the editor protocol (sysex.js), INFO for its parameter count, UP_LIST, UP_PUT
const dev = { access: null, input: null, output: null, wait: null, pcount: 0, slots: [] };
function devRequest(cmd, args, ms = 1500) {
  return new Promise((res, rej) => {
    const t = setTimeout(() => { dev.wait = null; rej(new Error("the FM-1 did not answer")); }, ms);
    dev.wait = { cmd, res: (a) => { clearTimeout(t); dev.wait = null; res(a); } };
    dev.output.send(frame(cmd, args));
  });
}
function devReceive(e) {
  const d = e.data, a = dev.wait && reply(d, dev.wait.cmd);
  if (a) dev.wait.res(a);
  else if (d[0] >= 0x80 && d[0] < 0xF0) midiIn(d);
}
async function connectDevice() {
  say("dev-msg", "");
  try {
    if (!navigator.requestMIDIAccess) throw new Error("this browser has no Web MIDI (Chrome and Edge have it)");
    dev.access = await navigator.requestMIDIAccess({ sysex: true });
    midiBind();
    const isF = (p) => /blue ?hare|fm-?1/i.test(p.name || "") && p.state !== "disconnected";
    dev.input = [...dev.access.inputs.values()].find(isF);
    dev.output = [...dev.access.outputs.values()].find(isF);
    if (!dev.input || !dev.output) throw new Error("no FM-1 found: plug it in by USB, with Blue Hare on it");
    dev.input.onmidimessage = devReceive;
    const info = parseInfo(await devRequest(ED.INFO, [])), ver = info.version;
    dev.pcount = info.pcount;
    if (dev.pcount !== fw.P)
      throw new Error(`the FM-1 runs ${ver}, whose sounds have ${dev.pcount} parameters (this studio: ${fw.P}); update one of them`);
    const slots = [];
    for (let s = 0; s < 32; s += 16) slots.push(...parseUpList(await devRequest(ED.UP_LIST, [s, 16])));
    dev.slots = slots;
    const sel = $("dev-slot");
    sel.textContent = "";
    slots.forEach((s, k) => sel.append(h("option", { value: k }, `U${String(k + 1).padStart(2, "0")} ${s.used ? s.name : "· empty"}`)));
    const free = slots.findIndex((s) => !s.used);
    sel.value = String(free >= 0 ? free : 0);
    $("dev-slot-field").hidden = false;
    $("send").hidden = false;
    $("connect").textContent = "Reconnect";
    say("dev-msg", `Connected: ${ver}.`, "ok");
  } catch (err) {
    say("dev-msg", err.message + ".", "bad");
  }
}
async function sendDevice() {
  const k = +$("dev-slot").value, name = cleanName($("name").value) || sound.name;
  if (!name) return say("dev-msg", "Give the sound a name first.", "bad");
  if (dev.slots[k]?.used && !confirm(`The FM-1's U${String(k + 1).padStart(2, "0")} holds ${dev.slots[k].name}. Replace it?`)) return;
  const e = fw.engines[S.engine];
  const args = upPutArgs(k, e.id, name, S.p.slice(0, fw.P), +$("cat").value);
  try {
    const a = await devRequest(ED.UP_PUT, args, 3000);
    if (a[1]) throw new Error(a[1] === 1 ? "the FM-1 refused the sound" : "the FM-1 could not write its flash (stop playback and try again)");
    dev.slots[k] = { used: 1, engine: e.id, name };
    $("dev-slot").options[k].textContent = `U${String(k + 1).padStart(2, "0")} ${name}`;
    say("dev-msg", `Sent to the FM-1's U${String(k + 1).padStart(2, "0")}: SAVE > USER on the device loads it.`, "ok");
  } catch (err) {
    say("dev-msg", err.message + ".", "bad");
  }
}

// ------------------------------------------------------------------ playing: the keys, the computer keys, MIDI in
let octave = +(pref("bluehare-studio-oct") || 4), hold = false;
const held = new Map();                  // note -> how it is held ("ptr" pointers, key letters) / latched
const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const LETTERS = "awsedftgyhujkolp;";     // C .. E of the next octave
const kbdEls = new Map();
function buildKeys() {
  const k = $("kbd");
  k.textContent = "";
  kbdEls.clear();
  const n0 = 12 * octave, whites = [];
  for (let n = n0; n <= n0 + 24; n++) if (![1, 3, 6, 8, 10].includes(n % 12)) whites.push(n);
  const W = 100 / whites.length;
  whites.forEach((n) => {
    const i = n - n0, el = h("div", { class: "w", "data-n": n }, i < LETTERS.length ? LETTERS[i].toUpperCase() : (n % 12 ? "" : NOTE_NAMES[0] + (n / 12 - 1)));
    k.append(el);
    kbdEls.set(n, el);
  });
  whites.forEach((n, wi) => {
    if ([0, 2, 5, 7, 9].includes(n % 12) && n + 1 <= n0 + 24) {
      const i = n + 1 - n0;
      const el = h("div", { class: "b", "data-n": n + 1, style: `left:calc(${(wi + 1) * W}% - ${W * .3}%);width:${W * .6}%` },
        i < LETTERS.length ? LETTERS[i].toUpperCase() : "");
      k.append(el);
      kbdEls.set(n + 1, el);
    }
  });
  $("oct").textContent = "C" + (octave - 1);
  for (const n of held.keys()) kbdEls.get(n)?.classList.add("down");
}
function noteOn(n, how) {
  if (!node) return;
  if (hold && held.has(n) && how !== "midi") { noteOff(n, true); return; }
  if (held.has(n)) return;
  held.set(n, how);
  node.port.postMessage({ type: "call", id: 0, calls: [{ fn: "stu_note", args: [n, +$("vel").value] }] });
  kbdEls.get(n)?.classList.add("down");
}
function noteOff(n, force = false) {
  if (!held.has(n) || (hold && !force)) return;
  held.delete(n);
  node.port.postMessage({ type: "call", id: 0, calls: [{ fn: "stu_note", args: [n, 0] }] });
  kbdEls.get(n)?.classList.remove("down");
}
function allOff() {
  for (const n of [...held.keys()]) noteOff(n, true);
  if (node) node.port.postMessage({ type: "call", id: 0, calls: [{ fn: "stu_all_off" }] });
}
function bindKeys() {
  const k = $("kbd");
  const at = (e) => { const t = document.elementFromPoint(e.clientX, e.clientY); return t && t.dataset.n ? +t.dataset.n : null; };
  const ptr = new Map();
  k.addEventListener("pointerdown", (e) => {
    k.setPointerCapture(e.pointerId);
    const n = at(e);
    if (n !== null) { ptr.set(e.pointerId, n); noteOn(n, "ptr"); }
  });
  k.addEventListener("pointermove", (e) => {
    if (!ptr.has(e.pointerId) || hold) return;
    const n = at(e), o = ptr.get(e.pointerId);
    if (n !== null && n !== o) { noteOff(o); ptr.set(e.pointerId, n); noteOn(n, "ptr"); }
  });
  const up = (e) => { if (ptr.has(e.pointerId)) { noteOff(ptr.get(e.pointerId)); ptr.delete(e.pointerId); } };
  k.addEventListener("pointerup", up);
  k.addEventListener("pointercancel", up);
  const typing = (e) => /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName) || e.target.isContentEditable;
  const down = new Map();
  addEventListener("keydown", (e) => {
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "z" && !typing(e)) { e.preventDefault(); e.shiftKey ? redo() : undo(); return; }
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "y" && !typing(e)) { e.preventDefault(); redo(); return; }
    if (e.metaKey || e.ctrlKey || e.altKey || typing(e) || e.repeat) return;
    const key = e.key.toLowerCase();
    if (key === "z") { setOctave(octave - 1); return; }
    if (key === "x") { setOctave(octave + 1); return; }
    if (key === "escape") { allOff(); return; }
    const i = LETTERS.indexOf(key);
    if (i < 0) return;
    const n = 12 * octave + i;
    down.set(key, n);
    noteOn(n, "key");
  });
  addEventListener("keyup", (e) => {
    const key = e.key.toLowerCase();
    if (down.has(key)) { noteOff(down.get(key)); down.delete(key); }
  });
  addEventListener("blur", () => { for (const n of down.values()) noteOff(n); down.clear(); });
}
function setOctave(o) {
  octave = clamp(o, 1, 8);
  pref("bluehare-studio-oct", octave);
  buildKeys();
}
// MIDI in: a controller plays the selected track (its channel, MIDI IN CH1-4: channel k + 1 plays track k + 1), its CCs
// reach the firmware's MIDI control as on the device
let midiAccess = null, midiBlink = 0;
function midiIn(d) {
  if (!node || d.length < 2) return;
  const st = (d[0] & 0xF0) | (S.sel & 15);
  node.port.postMessage({ type: "midi", data: [st, d[1], d[2] || 0] });
  if ((d[0] & 0xF0) === 0x90 && d[2]) kbdEls.get(d[1])?.classList.add("down");
  else if ((d[0] & 0xF0) === 0x80 || ((d[0] & 0xF0) === 0x90 && !d[2])) { if (!held.has(d[1])) kbdEls.get(d[1])?.classList.remove("down"); }
  $("midi-dot").classList.add("on");
  clearTimeout(midiBlink);
  midiBlink = setTimeout(() => $("midi-dot").classList.remove("on"), 90);
}
function midiBind() {
  const access = dev.access || midiAccess;
  if (!access) return;
  const names = [];
  for (const input of access.inputs.values()) {
    if (input.state === "disconnected") continue;
    names.push(input.name || "an input");
    if (input !== dev.input) input.onmidimessage = (e) => midiIn(e.data);
  }
  $("midi-text").textContent = names.length ? "MIDI: " + names.join(", ") : "MIDI: no input";
  access.onstatechange = () => midiBind();
}
async function midiOn() {
  if (!window.isSecureContext || !navigator.requestMIDIAccess) { $("midi-text").textContent = "MIDI: not in this browser"; return; }
  try { midiAccess = await navigator.requestMIDIAccess(); midiBind(); }
  catch { $("midi-text").textContent = "MIDI: not allowed"; }
}

// ------------------------------------------------------------------ the output: scope, meter, recording, screen
let peakHold = 0, clipT = 0;
function drawScope() {
  requestAnimationFrame(drawScope);
  if (!analyser || document.hidden) return;
  const c = $("scope"), x = c.getContext("2d"), W = c.width, H = c.height;
  const buf = new Float32Array(analyser.fftSize);
  analyser.getFloatTimeDomainData(buf);
  let start = 0;                            // a rising zero crossing: a steady picture
  for (let i = 1; i < buf.length / 2; i++) if (buf[i - 1] < 0 && buf[i] >= 0) { start = i; break; }
  const st = getComputedStyle(document.documentElement);
  x.clearRect(0, 0, W, H);
  x.strokeStyle = st.getPropertyValue("--edge");
  x.beginPath(); x.moveTo(0, H / 2); x.lineTo(W, H / 2); x.stroke();
  x.strokeStyle = st.getPropertyValue("--accent");
  x.lineWidth = 2;
  x.beginPath();
  const n = Math.min(1024, buf.length - start);
  let pk = 0;
  for (let i = 0; i < buf.length; i++) pk = Math.max(pk, Math.abs(buf[i]));
  for (let i = 0; i < n; i++) {
    const y = H / 2 - buf[start + i] * H / 2 * .95;
    i ? x.lineTo(i * W / n, y) : x.moveTo(0, y);
  }
  x.stroke();
  peakHold = Math.max(pk, peakHold * .96);
  const db = 20 * Math.log10(peakHold || 1e-9);
  $("peak").textContent = (db < -90 ? "-∞" : db.toFixed(1)) + " dB";
  $("bar").style.width = clamp((db + 60) / 60 * 100, 0, 100) + "%";
  if (pk >= .999) clipT = performance.now();
  $("meter").classList.toggle("clip", performance.now() - clipT < 1500);
}
let recorder = null;
async function toggleRecord() {
  if (!ac) return;
  const b = $("rec");
  if (recorder) { recorder.stop(); return; }
  const dest = ac.createMediaStreamDestination();
  node.connect(dest);
  const chunks = [];
  recorder = new MediaRecorder(dest.stream);
  recorder.ondataavailable = (e) => chunks.push(e.data);
  recorder.onstop = async () => {
    node.disconnect(dest);
    recorder = null;
    b.classList.remove("on");
    b.textContent = "● Record";
    try {
      const audio = await ac.decodeAudioData(await new Blob(chunks).arrayBuffer());
      const a = h("a", { href: URL.createObjectURL(new Blob([wav(audio)], { type: "audio/wav" })),
                         download: `${fileBase(sound.name)}-${new Date().toISOString().slice(0, 19).replace(/[:T]/g, "")}.wav` });
      document.body.append(a);
      a.click();
      setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
    } catch (err) { console.warn("recording:", err); }
  };
  recorder.start();
  b.classList.add("on");
  b.textContent = "■ Stop";
}
function wav(buf) {                      // 16-bit stereo PCM
  const n = buf.length, ch = Math.min(2, buf.numberOfChannels), out = new DataView(new ArrayBuffer(44 + n * 4));
  const w = (o, s) => { for (let i = 0; i < s.length; i++) out.setUint8(o + i, s.charCodeAt(i)); };
  w(0, "RIFF"); out.setUint32(4, 36 + n * 4, true); w(8, "WAVEfmt "); out.setUint32(16, 16, true);
  out.setUint16(20, 1, true); out.setUint16(22, 2, true); out.setUint32(24, buf.sampleRate, true);
  out.setUint32(28, buf.sampleRate * 4, true); out.setUint16(32, 4, true); out.setUint16(34, 16, true);
  w(36, "data"); out.setUint32(40, n * 4, true);
  const L = buf.getChannelData(0), R = buf.getChannelData(ch - 1);
  for (let i = 0; i < n; i++) {
    out.setInt16(44 + i * 4, clamp(Math.round(L[i] * 32767), -32768, 32767), true);
    out.setInt16(46 + i * 4, clamp(Math.round(R[i] * 32767), -32768, 32767), true);
  }
  return out.buffer;
}
const screenCtx = $("screen").getContext("2d"), screenImg = screenCtx.createImageData(240, 240);
function drawScreen(fb) {
  const d = screenImg.data;
  for (let i = 0; i < 240 * 240; i++) {
    const v = fb[i], p = ((v >> 8) | (v << 8)) & 0xFFFF;
    d[i * 4] = ((p >> 11) & 31) * 255 / 31;
    d[i * 4 + 1] = ((p >> 5) & 63) * 255 / 63;
    d[i * 4 + 2] = (p & 31) * 255 / 31;
    d[i * 4 + 3] = 255;
  }
  screenCtx.putImageData(screenImg, 0, 0);
}

// ------------------------------------------------------------------ the flash, kept in this browser
const STORE = "bluehare-studio-flash";
function loadFlash() {
  try {
    const raw = JSON.parse(localStorage.getItem(STORE) || "{}"), out = {};
    for (const [off, b64] of Object.entries(raw)) out[off] = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    return out;
  } catch { return {}; }
}
function saveFlash(sectors) {
  try {
    const raw = {};
    for (const [off, u8] of Object.entries(sectors)) {
      let s = "";
      for (let i = 0; i < u8.length; i += 4096) s += String.fromCharCode.apply(null, u8.subarray(i, i + 4096));
      raw[off] = btoa(s);
    }
    localStorage.setItem(STORE, JSON.stringify(raw));
    return true;
  } catch { return false; }
}

// ------------------------------------------------------------------ switching on
async function switchOn() {
  const go = $("go");
  go.disabled = true;
  go.textContent = "Starting…";
  try {
    ac = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
    const [wasm] = await Promise.all([
      fetch("bluehare.wasm").then((r) => { if (!r.ok) throw new Error(`bluehare.wasm: ${r.status}`); return r.arrayBuffer(); }),
      ac.audioWorklet.addModule("worklet.js")]);
    node = new AudioWorkletNode(ac, "bluehare", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
    analyser = ac.createAnalyser();
    analyser.fftSize = 2048;
    node.connect(ac.destination);
    node.connect(analyser);
    await new Promise((res, rej) => {
      node.port.onmessage = (e) => {
        const m = e.data;
        if (m.type === "ready") res();
        else if (m.type === "error") rej(new Error(m.message));
        else if (m.type === "ret") {
          const p = pending.get(m.id);
          if (!p) return;
          pending.delete(m.id);
          m.error ? p.rej(new Error(m.error)) : p.res(m.values);
        } else if (m.type === "frame") {
          if (m.fb) drawScreen(m.fb);
          if (m.sectors) saveFlash(m.sectors);
          if (m.load !== undefined) $("load").textContent = `CPU ${(m.load * 100).toFixed(0)}%`;
        }
      };
      node.port.postMessage({ type: "load", wasm, sectors: loadFlash(), master: 700 }, [wasm]);
    });
    await ac.resume();
    await loadConstants();
    (await call([...Array(fw.P).keys()].map((i) => ({ fn: "stu_kept", args: [i] })))).forEach((k, i) => { if (k) kept.add(i); });
    fillCats();
    await poll(true);                     // the values first: the groups show them
    await buildGroups();
    await poll(true);
    await refreshUsers();
    await nameFromDevice();
    buildKeys();
    bindKeys();
    $("start").hidden = true;
    midiOn();
    setInterval(() => poll(), 300);
    setInterval(() => meterPoll(), 120);
    setInterval(() => genPoll(), 150);
    node.port.postMessage({ type: "visible", on: !document.hidden });
    if (ac.sampleRate !== 44100) console.warn(`the browser plays at ${ac.sampleRate} Hz: the pitch is off`);
  } catch (err) {
    console.error(err);
    go.disabled = false;
    go.textContent = "Switch on";
    $("start-msg").textContent = "Could not start: " + (err && err.message || err) + ". A current Chrome, Edge, Firefox or Safari should work.";
  }
}

// ------------------------------------------------------------------ the page's buttons
$("go").addEventListener("click", switchOn);
for (const [b, pane] of [["lt-presets", "pane-presets"], ["lt-users", "pane-users"]])
  $(b).addEventListener("click", () => {
    for (const [b2, p2] of [["lt-presets", "pane-presets"], ["lt-users", "pane-users"]]) {
      $(b2).setAttribute("aria-selected", String(b2 === b));
      $(p2).hidden = p2 !== pane;
    }
  });
$("undo").addEventListener("click", undo);
$("redo").addEventListener("click", redo);
$("rec").addEventListener("click", toggleRecord);
$("store").addEventListener("click", storeSlot);
$("export").addEventListener("click", () => { const j = soundFile(); download(`${fileBase(j.name)}.bluehare-sound.json`, JSON.stringify(j, null, 1)); });
$("import").addEventListener("click", () => pickFile($("file"), loadSoundFile));
$("patch-export").addEventListener("click", async () => { const j = await patchFile(); download(`${fileBase(j.name)}.bluehare-patch.json`, JSON.stringify(j, null, 1)); });
$("patch-import").addEventListener("click", () => pickFile($("patch-file"), loadPatchFile));
$("connect").addEventListener("click", connectDevice);
$("send").addEventListener("click", sendDevice);
$("oct-down").addEventListener("click", () => setOctave(octave - 1));
$("oct-up").addEventListener("click", () => setOctave(octave + 1));
$("vel").addEventListener("input", () => { $("vel-v").textContent = $("vel").value; });
$("panic").addEventListener("click", allOff);
$("hold").addEventListener("click", () => {
  hold = !hold;
  $("hold").setAttribute("aria-pressed", String(hold));
  $("hold").classList.toggle("on", hold);
  if (!hold) allOff();
});
$("name").addEventListener("input", () => { const p = $("name").selectionStart; $("name").value = $("name").value.toUpperCase(); $("name").setSelectionRange(p, p); });
$("theme").addEventListener("click", () => {
  const light = document.documentElement.dataset.theme !== "light";
  document.documentElement.dataset.theme = light ? "light" : "dark";
  pref("bluehare-studio-theme", light ? "light" : "dark");
  drawEnv();
});
document.documentElement.dataset.theme = pref("bluehare-studio-theme") || "dark";
document.addEventListener("visibilitychange", () => node && node.port.postMessage({ type: "visible", on: !document.hidden }));
requestAnimationFrame(drawScope);
