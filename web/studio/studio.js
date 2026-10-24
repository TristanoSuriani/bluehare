// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tristano Suriani, Blue Hare: see AUTHORS.md
// The sound design studio's page. The FM-1 firmware runs in an AudioWorklet (worklet.js, bluehare.wasm: the browser
// emulator's build with the studio's exports, web/studio/studio_web.c); everything here asks it through "call":
// the engines and presets, every parameter's description, value and text as the device shows it, the user preset
// slots (kept in this browser as the emulator keeps its flash). Sounds go to a file, to a slot here, or to a
// connected FM-1 (the editor protocol's UP_PUT, web/EDITOR_PROTOCOL.md).
import { ED, frame, reply, parseInfo, parseUpList, upPutArgs } from "./sysex.js";
import { initShell, build, currentLayout, drawLights, tap, B } from "./shell.js";
import { createStrobe } from "./strobe.js";

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
const fw = { P: 0, G: 0, X: 0, PE0: 0, ntrk: 4, engines: [], cats: [], slots: 80, ENG_NO: 255, layout: [] };
const S = { p: [], g: [], x: [], m: [], engine: 0, preset: 0, user: 0, sel: 0, tracks: [] };
const desc = { 0: [], 1: [], 2: [], 3: [] };    // desc[scope][id] (scope 0 the track, 1 the song, 2 the other effects; 3 the engine's macros) = {fmt, min, max, def, names: [..] | null, label}
const ctl = { 0: new Map(), 1: new Map(), 2: new Map(), 3: new Map() };   // controls by id: {set(v), refresh()}

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
  // (2026-10-23) each track its own engines: fw.engOn[t][e]
  const nt = await one("stu_ntracks"), oq = [];
  for (let t = 0; t < nt; t++) for (let e = 0; e < ne; e++) oq.push({ fn: "stu_engine_on", args: [t, e] });
  const on = await call(oq);
  fw.engOn = [...Array(nt).keys()].map((t) => on.slice(t * ne, t * ne + ne).map((v) => !!v));
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
// (2026-10-18) the four are patches, not layers: the PATCH tab holds the patch's insert; the stacking (Z: in the patch,
// key / velocity ranges; J: the crossfade) stays in the firmware, off the screen
// (2026-10-18) the tabs are the FM-1's panel: each holds the device's pages of its families (stu_pg_*: titles, the
// columns, the ones MENU > EXPERT shows), then a few groups of the studio's own (layout keys: the patch insert, the
// matrix, the processing, the performance holds, the master). The families (params.c FAM_*): 1 ENV, 2 LFO, 3 FX,
// 4 SCL, 5 EDIT, 8 ARP, 10 the TRK pages (CLOCK)
const FAM = { ENV: 1, LFO: 2, FX: 3, SCL: 4, EDIT: 5, GLO: 6, ARP: 8, TRK: 10 };
const GR = { ADSR: 1, MOD: 9, TRK: 8, GEN: 13 };
// (2026-10-19) named as the device's buttons; the title line says more
const TABS = [["sound", "EDIT", [FAM.EDIT], "F", "Edit · the sound"], ["env", "ENV", [FAM.ENV], "", "Envelopes"],
              ["lfo", "LFO", [FAM.LFO], "M", "LFO · modulation"], ["keys", "SCL", [FAM.SCL], "", "Scale · keys"],
              ["fx", "FX", [FAM.FX], "XPQ", "Effects"], ["gen", "ARP", [FAM.ARP], "", "Arpeggiator · visuals"],
              ["glo", "GLO", [FAM.GLO, FAM.TRK], "", "Global · MIDI IN · clock"]];   // (2026-10-23: TECHNO's tab the ARP's)
let expert = pref("bluehare-studio-expert") === "1";
fw.pages = [];
async function loadPages() {             // the device's pages as they are now (the engine's titles, its plain page's columns)
  const n = await one("stu_pg_count");
  const q = [];
  for (let i = 0; i < n; i++)
    q.push({ fn: "stu_pg_fam", args: [i] }, { fn: "stu_pg_graph", args: [i] }, { fn: "stu_pg_visible", args: [i] },
           { fn: "stu_pg_title", args: [i], ret: "str" }, ...[0, 1, 2, 3].map((k) => ({ fn: "stu_pg_item", args: [i, k] })));
  const r = await call(q);
  fw.pages = [];
  for (let i = 0; i < n; i++) {
    const [fam, graph, vis, title, ...it] = r.slice(i * 8, i * 8 + 8);
    const items = it.filter((v) => v >= 0).map((v) => ({ scope: v >> 8, id: v & 255 }));
    fw.pages.push({ i, fam, graph, vis: !!vis, title, items });
  }
}
const GROUPS = {
  E: { title: "Engine", wide: true },
  A: { title: "Amp envelope", names: ["Attack", "Decay", "Sustain", "Release", "Level", "Octave"] },
  N: { title: "Envelope moves", names: ["Filter", "Pitch", "Shape"] },
  F: { title: "Patch insert", names: ["Drive", "Filter", "Cutoff", "Resonance", "Low cut", "Pan mode"], track: true, wide: true,
       note: "The patch's own processing before the effects: DRIVE, a 12 dB FILTER (low-, band- or high-pass), LOW CUT; PAN MODE spreads its voices (ALT left and right, RND at random)." },
  S: { title: "Spread, chorus", names: ["Spread", "Chorus"],
       note: "Saved with the sound." },
  V: { title: "Voice", names: ["Mode", "Glide", "Glide by", "Priority", "Allocation", "Detune", "Pan"] },
  L: { title: "LFO", names: ["Rate", "Wave", "Phase", "Fade in", "Sync", "Trigger", "Polarity"] },
  D: { title: "LFO moves", names: ["Pitch", "Filter", "Shape", "Amp"] },
  M: { title: "Mod matrix", wide: true },
  X: { title: "Sound processing", names: ["Processing"], fx: true, wide: true,
       note: "BLUE HARE: its chorus, delay and reverb buses (amb-1's plate and echo among their types: PLATE, AMB), the EQ, mono lows, compressor and limiter. AMB-1 (Charles Vestal's): a 5.6 s tape echo with two wandering heads and a 160 ms Echoplex, fed into a full-rate Dattorro plate, mixed wet, one soft clip at the end; no chorus, no compressor. The same knobs drive it: TIME, FEEDBACK, COLOR, MIX the echo; SIZE, DAMPING the plate; GEN's MOTION the heads." },
  Y: { title: "Delay", names: ["Mix", "Feedback", "Time"], track: true,
       note: "The patch's own delay: MIX sends it in (0: out, the default; a preset leaves it as it is), FEEDBACK the repeats (past 100 they grow), TIME a division of the tempo, 0.74 s at most. The repeats darken a little: the line runs at half the rate." },
  R: { title: "Reverb", names: ["Type", "Size", "Mix"], fx: true,
       note: "One reverb for the whole instrument, every patch into it at MIX. DRY: none (a tail dies away) · SPRING: a dispersive spring · GATED: a hall cut off, the 80s · PLATE: amb-1's Dattorro plate, in float." },
  U: { title: "EQ", names: ["Preset", "Low", "Low mid", "High mid", "High"], fx: true,
       note: "The patch's four bands on the mix, before the compressor: shelves at 100 Hz and 8 kHz, peaks at 400 Hz and 2.5 kHz, ±12 dB." },
  C: { title: "Chorus", names: ["Preset", "Rate", "Depth"], fx: true,
       note: "A preset sets the rate and depth: SUBTLE WIDE DEEP FAST WOBBLE (each patch sends to it with its Chorus amount, under Spread)." },
  P: { title: "Performance", names: ["Filter", "Crush", "Throw", "Depth"], fx: true,
       note: "Hold a button, or latch it, as a white key with FX held on the FM-1. FILTER: LPF left, HPF right; CRUSH: fewer bits; THROW: the dry mix into the delay and reverb; DEPTH: the hold effects' level (OCT UP / DN: the shimmer)." },
  Q: { title: "Master", names: ["Speaker EQ", "Master"], fx: true },
  G: { title: "Arpeggiator", names: ["Mode", "Rate", "Octaves", "Gate", "Swing", "Probability", "Hold", "Order", "Tempo"], track: true, wide: true,
       note: "Each patch its own arp: with ARP playing, the keys held on a patch whose MODE is not OFF play one after another at RATE, over OCT octaves, GATE long; HOLD latches the chord, ORDER NOTE sorts it low to high, PLAY keeps the order pressed. On the FM-1: ARP starts and stops it, its visuals on the screen while it plays." },
  H: { title: "MIDI IN", names: ["Input", "Channel"], track: true,
       note: "What this patch listens to: USB, TRS, both or nothing, on its own channel (TRK: its number), a channel 1..16, or ALL (the channels no other patch has). GLO > MIDI IN on the FM-1." },
  K: { title: "Keys", names: ["Root", "Scale", "Snap", "Transpose", "Chord", "Voicing"], track: true },
};
const ENGINE_WORDS = {
  SID: "The C64's chip voice: pulse, saw, noise, ring, sync",
  VOSIM: "Two formants of pulse trains: voices, reeds, brass",
  VA: "The virtual analogue: two oscillators, a sub, noise, oversampled",
  OBIE: "The OB-Xa's flavour of VA: sync, 12 / 24 dB, slop",
  DUBSIREN: "The dub siren and its echo (patch 4)",
  KIT: "A drum kit on the General MIDI notes",
  LINNKIT: "The 80s drum machine, after the LinnDrum",
  WAR: "An industrial kit: FM drums, driven",
  SPEECH: "Words, as the speech chips spoke them",
  ONYX: "Phrases in four Kokoro voices, as LPC",
  VOCODER: "The voices' words and vowels shaping a synth",
  SANDSTRM: "The trance lead on VA's core: five saws, the gate",
  HOOVER: "The rave lead on VA's core: PWM saws, the bend",
  RHYTHM: "The 808 and the 909, synthesized",
  VAKIT: "Synth drums on VA's core: zaps, toms, blips",
  RISERS: "Build-ups and landings, timed to the song",
  NOISE: "Noise as an instrument: wind, surf, whistles",
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
  $("sound-num").textContent = String(S.sel + 1);
  if (!edited && name) $("name").value = name.slice(0, 12);
}
const markEdited = () => { if (!sound.edited) { sound.edited = true; $("sound-edited").hidden = false; } };

// ------------------------------------------------------------------ a row (2026-10-19: the device's knob row)
// the label, the value, a thin bar (a choice of a few: its names in a line); the one being turned lit in the active
// colour, as the device lights a turned knob's row. Drag it sideways (or up), the wheel, the arrows; a double click: the
// default; a click on a longer choice: its list
let menuEl = null;
function closeMenu() { if (menuEl) { menuEl.remove(); menuEl = null; } }
function openMenu(el, d, scope, id) {
  closeMenu();
  const m = h("div", { class: "menu", role: "listbox", "aria-label": d.label });
  d.names.forEach((n, k) => m.append(h("button", { type: "button", role: "option", "aria-current": String(d.min + k === cur(scope, id)),
    onclick: () => { change(scope, id, d.min + k, 0); ctl[scope].get(id)?.flash(); closeMenu(); el.focus(); } }, n)));
  document.body.append(m);
  const r = el.getBoundingClientRect(), mh = Math.min(m.scrollHeight, 360);
  m.style.left = clamp(r.right - m.offsetWidth, 8, innerWidth - m.offsetWidth - 8) + "px";
  m.style.top = (r.bottom + mh + 8 < innerHeight ? r.bottom + 4 : Math.max(8, r.top - mh - 4)) + "px";
  menuEl = m;
  (m.querySelector('[aria-current="true"]') || m.firstChild)?.focus();
  m.querySelector('[aria-current="true"]')?.scrollIntoView({ block: "nearest" });
  m.addEventListener("keydown", (e) => {
    const bs = [...m.children], i = bs.indexOf(document.activeElement);
    if (e.key === "Escape") { closeMenu(); el.focus(); }
    else if (e.key === "ArrowDown" || e.key === "ArrowUp") bs[clamp(i + (e.key === "ArrowDown" ? 1 : -1), 0, bs.length - 1)].focus();
    else return;
    e.preventDefault();
  });
}
addEventListener("pointerdown", (e) => { if (menuEl && !menuEl.contains(e.target)) closeMenu(); }, true);

function control(scope, id, name) {
  const d = desc[scope][id];
  if (!d) return h("span");
  const label = name || d.label;
  const fixed = d.max <= d.min;
  const named = d.fmt === F.ENUM && d.names;
  const few = named && d.names.length <= 6 && d.names.join("").length <= 24;
  const val = h("span", { class: "val" }, "");
  const el = h("div", { class: "prow" + (fixed ? " off" : "") + (named && !few ? " pick" : ""), role: "slider", tabindex: fixed ? "-1" : "0",
                        "aria-label": label, "aria-valuemin": d.min, "aria-valuemax": d.max,
                        title: `${d.label} · drag, wheel or arrows${named && !few ? " · click: the list" : ""} · double-click: default` },
    h("span", { class: "lbl" }, label), val);
  const pct = (v) => 100 * (v - d.min) / Math.max(1, d.max - d.min);
  let show;
  let hotT = 0;
  const flash = (ms = 900) => { el.classList.add("hot"); clearTimeout(hotT); hotT = setTimeout(() => el.classList.remove("hot"), ms); };
  if (few) {
    val.hidden = true;
    const opts = h("div", { class: "opts" });
    const bs = d.names.map((n, k) => {
      const b = h("button", { type: "button", tabindex: "-1", "aria-pressed": "false",
        onclick: (e) => { e.stopPropagation(); change(scope, id, d.min + k, "seg" + scope + "." + id + "." + k); flash(); } }, n);
      opts.append(b);
      return b;
    });
    el.append(opts);
    show = (v) => { bs.forEach((b, k) => b.setAttribute("aria-pressed", String(d.min + k === v))); el.setAttribute("aria-valuenow", v); };
  } else {
    const bar = h("span", { class: "bar" }), fill = h("b");
    const bip = d.min < 0 && d.max > 0;
    bar.append(fill);
    if (!named && d.def > d.min && d.def < d.max) bar.append(h("i", { style: `left:${pct(d.def)}%` }));   // (where the default is)
    if (!(named && d.max - d.min < 2)) el.append(bar);                                                   // (the device: no bar for a two-way choice)
    show = (v) => {
      const p = pct(v), z = bip ? pct(0) : 0;
      fill.style.left = Math.min(p, z) + "%";
      fill.style.width = Math.abs(p - z) + "%";
      el.setAttribute("aria-valuenow", v);
    };
  }
  if (!fixed) {
    let drag = null;
    el.addEventListener("pointerdown", (e) => {
      if (e.button || e.target.closest(".opts button")) return;
      el.setPointerCapture(e.pointerId);
      drag = { x: e.clientX, y: e.clientY, v: cur(scope, id), key: Symbol(), moved: false };
      el.focus({ preventScroll: true });
      e.preventDefault();
    });
    el.addEventListener("pointermove", (e) => {
      if (!drag) return;
      const dd = (e.clientX - drag.x) - (e.clientY - drag.y);
      if (!drag.moved && Math.abs(dd) < 4) return;
      drag.moved = true;
      flash(100000);
      let v;
      if (named) v = drag.v + Math.trunc(dd / 22);
      else {
        const span = d.max - d.min, px = (e.shiftKey ? 3 : 1) * Math.max(240, el.clientWidth);
        v = Math.round(drag.v + dd * Math.max(span, 8) / px);
      }
      change(scope, id, clamp(v, d.min, d.max), drag.key);
    });
    const end = () => {
      if (!drag) return;
      if (!drag.moved && named && !few) openMenu(el, d, scope, id);
      if (drag.moved) flash();
      drag = null;
    };
    el.addEventListener("pointerup", end);
    el.addEventListener("pointercancel", () => { drag = null; flash(); });
    el.addEventListener("dblclick", (e) => { if (!e.target.closest(".opts button")) { change(scope, id, d.def, 0); flash(); } });
    el.addEventListener("wheel", (e) => {
      e.preventDefault();
      const step = named || e.shiftKey ? 1 : Math.max(1, Math.round((d.max - d.min) / 64));
      const dir = (Math.abs(e.deltaY) >= Math.abs(e.deltaX) ? -e.deltaY : e.deltaX) > 0 ? 1 : -1;
      change(scope, id, clamp(cur(scope, id) + dir * step, d.min, d.max), "wheel" + scope + "." + id);
      flash();
    }, { passive: false });
    el.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && named && !few) { e.preventDefault(); openMenu(el, d, scope, id); return; }
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
      flash();
    });
  }
  const c = { el, show, flash, text: (t) => { val.textContent = t; el.setAttribute("aria-valuetext", t); } };
  const prev = ctl[scope].get(id);                     // (one parameter on two pages, as on the device: both rows follow)
  ctl[scope].set(id, !prev ? c : { el, show: (v) => { prev.show(v); show(v); }, flash: (ms) => { prev.flash?.(ms); flash(ms); },
                                     text: (t) => { prev.text(t); c.text(t); } });
  return el;
}
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const vals = (scope) => (scope === 3 ? S.m : scope === 2 ? S.x : scope ? S.g : S.p);
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
  const acc = st.getPropertyValue("--accent").trim();
  x.fillStyle = st.getPropertyValue("--line").trim();
  x.fillRect(0, y(0), W, 2);
  x.beginPath();
  x.moveTo(8, y(0)); x.lineTo(xa, y(1)); x.lineTo(xd, y(s)); x.lineTo(xs, y(s)); x.lineTo(xr, y(0));
  const grad = x.createLinearGradient(0, 0, 0, H);
  grad.addColorStop(0, acc + "40");
  grad.addColorStop(1, acc + "00");
  x.fillStyle = grad;
  x.fill();
  x.strokeStyle = acc;
  x.lineWidth = 3;
  x.lineJoin = "round";
  x.shadowColor = acc;
  x.shadowBlur = 10;
  x.stroke();
  x.shadowBlur = 0;
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
    if (!$(id) || !$(id + "-v")) continue;   // (the groups rebuilt meanwhile: a page or mode change)
    $(id).style.width = clamp(-db / 12 * 100, 0, 100) + "%";
    $(id + "-v").textContent = (db > -0.05 ? "0.0" : db.toFixed(1)) + " dB";
  }
  for (const b of perfEls) b.setAttribute("aria-pressed", String(!!((on >>> +b.dataset.e) & 1)));
}

// (2026-10-19) the screen's tabs: HOME and SAVE (the studio's own views) round the families; SOUNDS (the engines and
// presets) a tab of its own on a narrow screen. The shell's buttons open them as the device's open its pages; a tab
// clicked presses the device's button (its screen, on HOME, follows), not ARP (it starts the arpeggiator) nor SAVE
const VIEWS = [["home", "HOME", "Home", B.HOME], ["sound", "EDIT", "", B.EDIT], ["env", "ENV", "", B.ENV], ["lfo", "LFO", "", B.LFO],
               ["keys", "SCL", "", B.SCL], ["fx", "FX", "", B.FX], ["gen", "ARP", "", -1], ["glo", "GLO", "", B.GLO],
               ["save", "SAVE", "Save · files · USB", -1],
               ["lib", "SOUNDS", "Engines · presets", -1]];
const BUTTON_TAB = { [B.HOME]: "home", [B.EDIT]: "sound", [B.ENV]: "env", [B.LFO]: "lfo", [B.SCL]: "keys", [B.FX]: "fx", [B.ARP]: "gen",
                     [B.GLO]: "glo", [B.SAVE]: "save" };
let tabNow = pref("bluehare-studio-tab") || "home";
function showTab(id, press = false) {
  tabNow = VIEWS.some((t) => t[0] === id) ? id : "home";
  pref("bluehare-studio-tab", tabNow);
  $("ui").dataset.tab = tabNow;
  for (const b of $("tabs").children) b.setAttribute("aria-selected", String(b.dataset.tab === tabNow));
  const page = TABS.some((t) => t[0] === tabNow);
  $("groups").hidden = !page;
  $("view-home").hidden = tabNow !== "home";
  $("view-save").hidden = tabNow !== "save";
  for (const p of $("groups").children) p.hidden = p.dataset.tab !== tabNow;
  $("page-title").textContent = page ? TABS.find((t) => t[0] === tabNow)[4] : VIEWS.find((t) => t[0] === tabNow)[2];
  const bit = VIEWS.find((t) => t[0] === tabNow)[3];
  if (press && node && bit >= 0) tap(bit);
  if ($("fx-actions")) $("fx-actions").hidden = tabNow !== "fx";
  drawEnv();
}
async function buildGroups() {
  const root = $("groups"), tabs = $("tabs");
  root.textContent = "";
  tabs.textContent = "";
  for (const [id, title] of VIEWS)
    tabs.append(h("button", { class: "tab" + (id === "lib" ? " libtab" : ""), role: "tab", type: "button", "data-tab": id,
                              onclick: () => showTab(id, true) }, title,
      id === "gen" ? h("span", { class: "live", id: "gen-live", hidden: true }) : null));
  for (const k of [0, 1, 2, 3]) ctl[k].clear();
  await one("stu_set_expert", expert ? 1 : 0);
  await loadPages();
  const extra = (keys) => fw.layout.filter((g) => keys.includes(g.key));
  await loadDescs([...fw.pages.filter((pg) => pg.vis).flatMap((pg) => pg.items), ...fw.layout.flatMap((g) => g.items)]);
  for (const [id, title, fams, keys] of TABS) {
    const panel = h("div", { class: "groups", role: "tabpanel", "data-tab": id });
    let genDone = false;
    for (const pg of fw.pages) {
      if (!pg.vis || !fams.includes(pg.fam) || !pg.items.length || pg.graph === GR.MOD) continue;
      if (pg.fam === FAM.TRK && pg.graph === GR.TRK) continue;   // (the MIXER: the patches' strip shows them)
      const g = { key: "pg" + pg.i, items: pg.items, page: pg, gen: pg.graph === GR.GEN && !genDone };
      if (g.gen) genDone = true;
      panel.append(groupBox(g));
    }
    for (const g of extra(keys)) panel.append(groupBox(g));
    root.append(panel);
  }
  $("acts").textContent = "";
  $("acts").append(fxActions());
  showTab(tabNow);
  $("expert").setAttribute("aria-pressed", String(expert));
  $("normal").setAttribute("aria-pressed", String(!expert));
}
const pageItems = () => [...fw.pages.filter((pg) => pg.vis).flatMap((pg) => pg.items), ...fw.layout.flatMap((x) => x.items)];
async function setExpert(on) {
  expert = !!on;
  pref("bluehare-studio-expert", expert ? "1" : "0");
  await buildGroups();
  await poll(true);
}
function groupBox(g) {
  const G = g.page ? { title: g.page.title, track: g.items.some((it) => it.scope === 0 || it.scope === 3) }
                   : GROUPS[g.key] || { title: g.key };
  const box = h("section", { class: "group" + (G.wide ? " wide" : ""), "data-group": g.key });
  const info = G.note && g.key !== "G" && !G.show ? h("button", { class: "info", type: "button", title: G.note, "aria-label": "About " + G.title,
    onclick: () => { const n = box.querySelector(".gnote"); if (n) n.hidden = !n.hidden; } }, "ⓘ") : null;
  box.append(h("h3", {}, G.title, G.track ? h("span", { class: "sub" }, `patch ${S.sel + 1}`) : null,
    G.fx ? h("span", { class: "sub" }, "every patch") : null, info));
  const ctrls = h("div", { class: "ctrls" });
  if (g.key === "E") {
    box.id = "engine-group";
    engineGroup(ctrls, g.items);
  } else if (g.key === "M") {
    ctrls.append(matrix(g.items));
  } else {
    if (g.gen) ctrls.append(genBar());
    if (g.key === "P") ctrls.append(perfButtons());
    g.items.forEach((it, i) => ctrls.append(control(it.scope, it.id, g.page ? undefined : G.names?.[i])));
    if (g.page && g.page.graph === GR.ADSR) {           // (the device's strip under the rows: the envelope)
      envCanvas = h("canvas", { id: "env-graph", width: 720, height: 168 });
      ctrls.append(envCanvas);
    }
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
// the ARP: play / stop, the selected patch's MODE, the bar, a light for each track as its arp plays
const ARP_ROLES = ["BASS", "DRUMS", "VOICE"];
let genOn = false, genPlayed = [0, 0, 0, 0];
function genBar() {
  const play = h("button", { class: "btn primary play", type: "button", id: "gen-play", onclick: async () => {
    await one("stu_gen", genOn ? 0 : 1);
    await genPoll();
  } }, "▶ Play");
  const vis = h("button", { class: "btn", type: "button", title: "The visuals on the whole screen (Esc leaves)", onclick: () => openVisuals() }, "◼ Visuals");
  return h("div", { class: "genbar" }, play, vis, h("span", { class: "chord", id: "gen-chord" }, "—"),
    h("span", { class: "where", id: "gen-where" }, "stopped"),
    h("span", { class: "roles" }, ARP_ROLES.map((n, i) => h("span", { class: "role", id: "gen-r" + i }, `${i + 1} ${n}`))));
}
async function genPoll() {
  if (!node || !$("gen-play")) return;
  const [st, chord] = await call([{ fn: "stu_gen_state", ret: ["i32", 12] }, { fn: "stu_gen_chord", ret: "str" }]);
  genOn = !!st[0];
  $("gen-play").textContent = genOn ? "■ Stop" : "▶ Play";
  $("gen-play").classList.toggle("on", genOn);
  $("gen-chord").textContent = genOn ? chord || "—" : "—";
  $("gen-where").textContent = genOn ? `bar ${st[1] + 1} · step ${st[2] + 1}` : "stopped";
  $("gen-live").hidden = !genOn;
  for (let r = 0; r < ARP_ROLES.length; r++) {
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
    engItems = [...Array(8).keys()].map((k) => ({ scope: 0, id: fw.PE0 + k }));
    const before = engItems.map((it) => JSON.stringify(desc[0][it.id]));
    await loadDescs(engItems);
    const after = engItems.map((it) => JSON.stringify(desc[0][it.id]));
    relabelMatrix();
    if (before.join() !== after.join()) {    // (a mode moved a label: the pages again, as the device shows them)
      await buildGroups();
      await showValues(pageItems());
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
  grp("C")?.classList.toggle("dimmed", amb);
  for (const key of ["Y", "R"]) grp(key)?.querySelector(".choice")?.classList.toggle("dimmed", amb);
  grp("Q")?.querySelector(".stages")?.classList.toggle("dimmed", amb);
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
    const soundMoved = engineMoved || preset !== S.preset || user !== S.user;   // (a piece, the device: another sound)
    if (!engineMoved && !force && changed.some((c) => c.scope === 1)) await refreshTracks();   // (a piece: the other patches' sounds)
    Object.assign(S, { engine, preset, user, sel });
    if (selMoved && ctl[0].size && !engineMoved) await buildGroups();   // (the groups name the patch: "patch 2")
    if (engineMoved || force || selMoved) {
      renderEngines();
      renderPresets();
      await refreshTracks();
      await buildGroups();                  // (the engine's pages: its titles, its plain page, its macros)
      await refreshEngineLabels();
    }
    if (selMoved || (soundMoved && !force)) await nameFromDevice();
    const shown = changed.filter(({ scope, id }) => ctl[scope].has(id));
    if (ctl[3].size) {                      // (the macros: read back from the parameters they move)
      const ids = [...ctl[3].keys()], mv = await call(ids.map((id) => ({ fn: "stu_get", args: [3, id] })));
      ids.forEach((id, j) => { if (force || engineMoved || mv[j] !== S.m[id]) { S.m[id] = mv[j]; if (!inflight.has("3." + id)) shown.push({ scope: 3, id }); } });
    }
    if (engineMoved || force) await showValues(pageItems());
    else if (shown.length) {
      await showValues(shown);
      for (const { scope, id } of shown) if (!inflight.has(scope + "." + id)) ctl[scope].get(id)?.flash?.();
    }
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
  S.tracks = r;
  const nav = $("tracks");
  nav.textContent = "";
  r.forEach((e, t) => nav.append(h("button", { class: "trk", type: "button", "aria-pressed": String(t === S.sel),
    onclick: () => selectTrack(t), title: `Patch ${t + 1}` },
    h("small", {}, String(t + 1)), h("b", {}, fw.engines[e]?.name || "-"))));
  $("sound-num").textContent = String(S.sel + 1);
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
  for (const e of fw.engines.filter((x) => !fw.engOn || fw.engOn[S.sel]?.[x.index]))   // (the patch's own engines)
    box.append(h("button", { class: "eng", type: "button", "aria-pressed": String(e.index === S.engine),
      title: ENGINE_WORDS[e.name] || e.name, onclick: () => pickEngine(e.index) }, e.name));
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
const LAYER_LABELS = ["LFILT", "LCUT", "LRES", "LOWCUT", "PANMODE"];   // (2026-10-18: a patch's insert; the layers deleted)
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
           layers, fx: fx.params };
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

// ------------------------------------------------------------------ playing: the shell's keys (shell.js), MIDI in
function allOff() {
  if (node) node.port.postMessage({ type: "call", id: 0, calls: [{ fn: "stu_all_off" }] });
}
// MIDI in: a controller plays the selected track (its channel, MIDI IN CH1-4: channel k + 1 plays track k + 1), its CCs
// reach the firmware's MIDI control as on the device
let midiAccess = null, midiBlink = 0;
function midiIn(d) {
  if (!node || d.length < 2) return;
  const st = (d[0] & 0xF0) | (S.sel & 15);
  node.port.postMessage({ type: "midi", data: [st, d[1], d[2] || 0] });
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
  x.fillStyle = st.getPropertyValue("--line");
  x.fillRect(0, H / 2 - 1, W, 2);
  x.strokeStyle = st.getPropertyValue("--accent");
  x.shadowColor = x.strokeStyle;
  x.shadowBlur = 8;
  x.lineWidth = 3;
  x.beginPath();
  const n = Math.min(1024, buf.length - start);
  let pk = 0;
  for (let i = 0; i < buf.length; i++) pk = Math.max(pk, Math.abs(buf[i]));
  for (let i = 0; i < n; i++) {
    const y = H / 2 - buf[start + i] * H / 2 * .95;
    i ? x.lineTo(i * W / n, y) : x.moveTo(0, y);
  }
  x.stroke();
  x.shadowBlur = 0;
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
          if (m.leds) drawLights(m.leds, m.anim);
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
    $("start").hidden = true;
    midiOn();
    setInterval(() => poll(), 150);                 // (the shell's knobs turn the device's: their rows light soon)
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
$("normal").onclick = () => setExpert(false);   // (2026-10-18: the device's MENU > EXPERT)
$("expert").onclick = () => setExpert(true);
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
$("name").addEventListener("input", () => { const p = $("name").selectionStart; $("name").value = $("name").value.toUpperCase(); $("name").setSelectionRange(p, p); });
document.addEventListener("visibilitychange", () => node && node.port.postMessage({ type: "visible", on: !document.hidden }));
requestAnimationFrame(drawScope);

// ------------------------------------------------------------------ the instrument (2026-10-19: the emulator's, its screen this page)
// WIDE: one laptop screen, the screen 1292 x 646 of 1760 x 920, the four left knobs in a column, KNOB 1 .. 4 and the
// twelve buttons (four rows of three) right, OCT- / OCT+ and the keys in front; TALL (a phone held upright): the
// screen across the top, the knobs, the buttons, the keys below
const STUDIO_LAYOUTS = {
  studio: {
    w: 1760, h: 920, screen: [214, 22, 1292, 646], inset: 6, r3d: true,
    knobs: [[107, 96, 34], [107, 246, 34], [107, 396, 34], [107, 546, 34], [1580, 92, 30], [1690, 92, 30], [1580, 222, 30], [1690, 222, 30]],
    trays: [[1524, 300, 226, 288]],
    oct: [[246, 728, 150, 82], [246, 822, 150, 82]],
    grid: [1534, 312, 62, 56, 72, 68, 3], keybed: [420, 716, 1130, 196], logo: [36, 700],
  },
  studiotall: {
    w: 600, h: 1300, screen: [10, 10, 580, 700], inset: 4,
    knobs: [[90, 790, 26], [230, 790, 26], [370, 790, 26], [510, 790, 26], [90, 890, 26], [230, 890, 26], [370, 890, 26], [510, 890, 26]],
    trays: [[14, 940, 572, 134]],
    oct: [[164, 1082, 128, 40], [308, 1082, 128, 40]],
    grid: [24, 950, 86, 52, 94, 62], keybed: [14, 1130, 572, 160],
  },
};
const ui = $("ui");
initShell({ device: $("device"), svg: $("panel"), slot: ui, layouts: STUDIO_LAYOUTS, before: document.querySelector(".case-front"),
  send: (m) => { if (node) node.port.postMessage(m); }, on: () => !!node,
  onButton: (bit) => { if (BUTTON_TAB[bit] !== undefined) showTab(BUTTON_TAB[bit]); },
  keydown: (e) => {                                  // (the page's own first: undo, every note off; a row's arrows)
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "z") { e.preventDefault(); e.shiftKey ? redo() : undo(); return true; }
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "y") { e.preventDefault(); redo(); return true; }
    if (!$("vis").hidden && e.key === "Escape") { closeVisuals(); return true; }
    if (!$("vis").hidden && e.code === "Space") { e.preventDefault(); if (!e.repeat) one("stu_gen", visState.on ? 0 : 1); return true; }
    if (e.key === "Escape") { closeMenu(); allOff(); return true; }
    return !!e.target.closest?.(".prow, .menu");
  } });
const tall = matchMedia("(orientation: portrait) and (max-width: 700px)");
const shape = () => { document.body.classList.toggle("tall", tall.matches); build(tall.matches ? "studiotall" : "studio"); };
shape();
tall.addEventListener("change", shape);
document.fonts?.load("16px DotGothic16").then(() => build(currentLayout()), () => {});
// the screen's type: a share of its width (a phone's narrow screen: larger, the sounds a tab)
new ResizeObserver(() => {
  const w = ui.clientWidth, narrow = w < 640;
  ui.classList.toggle("narrow", narrow);
  ui.style.fontSize = Math.max(9.5, Math.min(16, w / (narrow ? 33 : 78))) + "px";
  if (!narrow && tabNow === "lib") showTab("home");
}).observe(ui);

// ------------------------------------------------------------------ VISUALS (2026-10-19): STROBE, full screen
// the firmware's visuals (ui_strobe.c) drawn again at the screen's resolution (strobe.js), following the same signals:
// the player's part, piece, tempo, 1/16 and the roles' notes, read ~30 times a second. Esc (or leaving full screen)
// closes it; Space starts and stops the ARP (2026-10-23, TECHNO's before); the shell's keys still play along
const visState = { on: 0, step: 0, part: 0, piece: 0, bpm: 120, notes: [0, 0, 0, 0], roles: ["", "", "", ""], name: "" };
let strobe = null, visRaf = 0, visPoll = 0, visBusy = false, visLast = 0, visIdle = 0;
async function visRead() {
  if (visBusy || !node) return;
  visBusy = true;
  try {
    const [g] = await call([{ fn: "stu_gen_state", ret: ["i32", 12] }]);
    Object.assign(visState, { on: g[0], notes: g.slice(4, 8), part: g[8], piece: g[9], bpm: g[10], step: g[11] });
    visState.name = "ARP";
  } finally { visBusy = false; }
}
function visLoop(t) {
  visRaf = requestAnimationFrame(visLoop);
  const dt = visLast ? Math.min(100, t - visLast) : 16;
  visLast = t;
  strobe.frame(visState, dt);
}
function visWake() {
  $("vis").classList.remove("idle");
  clearTimeout(visIdle);
  visIdle = setTimeout(() => $("vis").classList.add("idle"), 2500);
}
async function openVisuals() {
  const v = $("vis");
  if (!node || !v.hidden) return;
  v.hidden = false;
  strobe = strobe || createStrobe($("vis-cv"));
  strobe.resize();
  const r = await call([0, 1, 2, 3].map((k) => ({ fn: "stu_gen_role", args: [k], ret: "str" })));
  visState.roles = r;
  visState.name = "";
  await visRead();
  visPoll = setInterval(visRead, 30);
  visLast = 0;
  visRaf = requestAnimationFrame(visLoop);
  visWake();
  try { await v.requestFullscreen?.(); } catch {}
  strobe.resize();
}
function closeVisuals() {
  const v = $("vis");
  if (v.hidden) return;
  v.hidden = true;
  cancelAnimationFrame(visRaf);
  clearInterval(visPoll);
  clearTimeout(visIdle);
  if (document.fullscreenElement) document.exitFullscreen?.().catch(() => {});
}
$("vis").addEventListener("pointermove", visWake);
$("vis").addEventListener("dblclick", closeVisuals);
document.addEventListener("fullscreenchange", () => { if (!document.fullscreenElement && !$("vis").hidden) closeVisuals(); });
addEventListener("resize", () => { if (strobe && !$("vis").hidden) strobe.resize(); });
