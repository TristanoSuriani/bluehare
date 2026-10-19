// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The page follows X0X's browser page, web/emu/index.html of charlesvestal/fm1-x0x (GPL-3.0), a Felucca fork.
// Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md
// (2026-10-19) The instrument, shared by the emulator (web/emu/index.html) and the studio (web/studio: the same shell,
// keys and key mapping round a larger screen): its drawing (shell.css), its knobs, buttons and keys, the computer
// keyboard's mapping, the LEDs. The page gives initShell its elements and a send (the worklet's messages):
//   initShell({ device, svg, slot, send, on, before, layouts, onButton, keydown, onNumKnobs }); build("wide")
const NS = "http://www.w3.org/2000/svg";
// panel.c's labels (bit = label) and encoder roles
const B = { FX: 0, SCL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11, "OCT-": 12, "OCT+": 13 };
const EN = { SELECT: 0, ALGORITHM: 1, PRESETS: 2, "KNOB 1": 3, "KNOB 2": 4, "KNOB 3": 5, "KNOB 4": 6 };
const PLAY_GREEN = 14;
// the 27 keys F3..G5: key k = k semitones above F3
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];      // the white key each black one follows

// ------------------------------------------------------------------ the panel
// Two drawings of the same controls: WIDE after the FM-1's own layout, TALL for a phone held upright (the
// screen larger, the keys across the whole width). Rebuilt when the orientation changes; the state stays.
const ROW1 = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO"], ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
const KNOBS = ["MASTER", "SELECT", "PRESETS", "ALGORITHM", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"];
// (2026-10-18) WIDE: a synthesizer seen from the front, slightly above (the user's: "a rendered 3d synthesiser ... a
// piano keyboard on it", after a keyboard synth's layout): the panel at the back (the four left knobs, the screen in the
// middle, a third of the width, KNOB 1 .. 4 and the twelve buttons right), the keyboard in front (OCT- / OCT+ left of it,
// where a synth's wheels are). Drawn flat, tilted back in 3D by the page (.device.r3d: perspective, a front face)
const LAYOUTS = {
  wide: {
    w: 1000, h: 700, screen: [330, 26, 340], r3d: true, cheek: 0,
    knobs: [[95, 92, 30], [222, 92, 30], [95, 232, 30], [222, 232, 30], [737, 84, 24], [807, 84, 24], [877, 84, 24], [947, 84, 24]],
    trays: [[692, 158, 296, 172]],
    oct: [[48, 470, 120, 76], [48, 572, 120, 76]],
    grid: [702, 170, 40, 62, 48, 86], keybed: [200, 418, 780, 262], logo: [48, 400],
  },
  tall: {
    w: 600, h: 1070, screen: [130, 104, 340],
    knobs: [[66, 64, 24], [186, 64, 24], [414, 64, 24], [534, 64, 24], [90, 512, 26], [230, 512, 26], [370, 512, 26], [510, 512, 26]],
    trays: [[14, 572, 572, 146], [150, 728, 300, 62]],
    oct: [[164, 737, 128, 44], [308, 737, 128, 44]],
    grid: [24, 582, 86, 60, 94, 70], keybed: [14, 800, 572, 256],
  },
};
let svg = null, device = null, canvas = null, opts = {};
function el(tag, attrs, parent = svg) {
  const e = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  parent.appendChild(e);
  return e;
}
const SANS = "-apple-system, 'Helvetica Neue', Arial, sans-serif", DOT = "'DotGothic16', " + SANS;
// (2026-10-18) the lettering in fourteen segments, as a VFD panel's (the firmware's CLOCK the seven-segment figures):
// every segment of a character drawn faint (its ghost), the lit ones over it in the ink, glowing. A character is W x 2W
// (H the cap height), slanted a little; a segment a stroke. The unit cell: x 0 .. 1, y 0 .. 2
const SEG14 = {
  a: [.12, 0, .88, 0], b: [1, .12, 1, .88], c: [1, 1.12, 1, 1.88], d: [.12, 2, .88, 2], e: [0, 1.12, 0, 1.88],
  f: [0, .12, 0, .88], g: [.12, 1, .44, 1], G: [.56, 1, .88, 1], h: [.14, .16, .4, .84], i: [.5, .12, .5, .88],
  j: [.86, .16, .6, .84], k: [.4, 1.16, .14, 1.84], l: [.5, 1.12, .5, 1.88], m: [.6, 1.16, .86, 1.84],
};
const CHAR14 = {
  0: "abcdefjk", 1: "bcj", 2: "abdegG", 3: "abcdG", 4: "bcfgG", 5: "acdfgG", 6: "acdefgG", 7: "abc", 8: "abcdefgG",
  9: "abcdfgG", A: "abcefgG", B: "abcdGil", C: "adef", D: "abcdil", E: "adefg", F: "aefg", G: "acdefG", H: "bcefgG",
  I: "adil", J: "bcde", K: "efgjm", L: "def", M: "bcefhj", N: "bcefhm", O: "abcdef", P: "abefgG", Q: "abcdefm",
  R: "abefgGm", S: "acdfgG", T: "ail", U: "bcdef", V: "efjk", W: "bcefkm", X: "hjkm", Y: "hjl", Z: "adjk",
  "-": "gG", "+": "gGil", "/": "jk", " ": "",
};
function segText(parent, x, y, s, size, ink, anchor = "middle") {
  const H = size * 0.74, W = H * 0.52, adv = W * 1.42, sw = Math.max(1.1, H * 0.12), slant = H * 0.1;
  const g = el("g", { class: "seg14" }, parent), str = String(s).toUpperCase();
  const tw = str.length * adv - (adv - W);
  let x0 = anchor === "middle" ? x - tw / 2 : anchor === "end" ? x - tw : x;
  const top = y - H, u = H / 2;
  const line = (cx, k, on) => {
    const [x1, y1, x2, y2] = SEG14[k];
    el("line", { x1: cx + x1 * W + slant * (1 - y1 / 2), y1: top + y1 * u, x2: cx + x2 * W + slant * (1 - y2 / 2), y2: top + y2 * u,
                 stroke: on ? ink : "var(--label)", "stroke-width": sw, "stroke-linecap": "round", class: on ? "seg-on" : "seg-off" }, g);
  };
  for (const ch of str) {
    const on = CHAR14[ch] ?? "";
    if (ch !== " ")
      for (const k of Object.keys(SEG14)) line(x0, k, on.includes(k));
    x0 += adv;
  }
  return g;
}
function label(parent, x, y, s, size, font = SANS, ink = "var(--label)", anchor = "middle") {
  if (document.body.className.startsWith("dist-"))
    return segText(parent, x, y, s, size, ink, anchor);
  const t = el("text", { x, y, "text-anchor": anchor, "font-family": font,
                         "font-size": size, "font-weight": 600, fill: ink, "letter-spacing": ".04em" }, parent);
  t.textContent = s;
  return t;
}
const KEYNAMES = ["F", "F♯", "G", "G♯", "A", "A♯", "B", "C", "C♯", "D", "D♯", "E"];
const keyName = (k) => KEYNAMES[k % 12] + (3 + Math.floor((k + 5) / 12));

let btnMask = 0, keyMask = 0, latched = 0, masterValue = 700, lastLeds = [0, 0, 0, 0, 0], layout = null;
const angle = Object.fromEntries(KNOBS.map((n) => [n, 0]));
angle.MASTER = -135 + 270 * masterValue / 1023;
const knobs = [], buttons = [], keys = [];
const btnBy = new Map(), keyBy = new Map();                   // pointer / key code -> label bit or key
const send = (m) => opts.send?.(m);

const hard = true;   /* (2026-10-18: square and 3D; SOFT, the rounded shapes, gone) */
function build(name) {
  const L = LAYOUTS[name];
  const R = { shell: 10, tray: 3, bezel: 4, btn: 2, key: 3, slit: 1 };
  document.body.classList.add("hard");
  device.classList.toggle("r3d", !!L.r3d);
  layout = name;
  svg.innerHTML = "";
  svg.setAttribute("viewBox", `0 0 ${L.w} ${L.h}`);
  device.style.aspectRatio = `${L.w} / ${L.h}`;
  for (const d of device.querySelectorAll(".hit")) d.remove();
  knobs.length = buttons.length = keys.length = 0;
  const defs = el("defs", {});                                   // (2026-10-18) the shell's neon edge: the palette's two
  const lg = el("linearGradient", { id: "edge", x1: 0, y1: 0, x2: 1, y2: 1 }, defs);   // accents, corner to corner
  el("stop", { offset: "0", "stop-color": "var(--a2, var(--edge))" }, lg);
  el("stop", { offset: "1", "stop-color": "var(--a1, var(--edge))" }, lg);
  const grad = (id, stops, attrs = { x1: 0, y1: 0, x2: 0, y2: 1 }, tag = "linearGradient") => {
    const g = el(tag, { id, ...attrs }, defs);
    for (const [o, c, a] of stops) el("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g);
  };
  grad("sheen", [[0, "#fff", .22], [.45, "#fff", .04], [.55, "#000", 0], [1, "#000", .35]]);   // a keycap's light and shade
  grad("inset", [[0, "#000", .55], [.18, "#000", 0], [.9, "#fff", 0], [1, "#fff", .07]]);     // a recess: shade at the top
  grad("shellsheen", [[0, "#fff", .07], [.25, "#fff", 0], [1, "#000", .3]]);
  grad("ivory", [[0, "#000", .12], [.06, "#fff", 0], [.85, "#000", 0], [1, "#000", .22]]);   // a white key's shade
  grad("dome", [[0, "#fff", .32], [.55, "#fff", .03], [1, "#000", .45]], { cx: .38, cy: .3, r: .75 }, "radialGradient");
  el("rect", { x: 4, y: 4, width: L.w - 8, height: L.h - 8, rx: R.shell, fill: "var(--body)", class: "shell" });
  if (hard) el("rect", { x: 4, y: 4, width: L.w - 8, height: L.h - 8, rx: R.shell, fill: "url(#shellsheen)", "pointer-events": "none" });
  const tray = (x, y, w, h) => {
    el("rect", { x, y, width: w, height: h, rx: R.tray, fill: "var(--tray)", class: "tray" });
    if (hard) el("rect", { x, y, width: w, height: h, rx: R.tray, fill: "url(#inset)" });
  };
  for (const [x, y, w, h] of L.trays) tray(x, y, w, h);
  const [sx, sy, ss, sh = ss] = L.screen;                     // (the studio's: a wider screen, [x, y, w, h])
  if (hard) tray(sx - 8, sy - 8, ss + 16, sh + 16);
  if (L.logo) {                                                  // the wordmark, between the panel and the keys
    label(svg, L.logo[0], L.logo[1] + 2, "BLUE HARE", 22, DOT, "var(--label)", "start").classList.add("logo");
    el("line", { x1: L.logo[0], y1: L.logo[1] + 8, x2: L.w - 20, y2: L.logo[1] + 8, stroke: "var(--a2, var(--edge))", "stroke-width": 1, opacity: .5, class: "rule" });
  }
  el("rect", { x: sx, y: sy, width: ss, height: sh, rx: R.bezel, fill: "#0b0b0c", class: "bezel" });
  const inset = L.inset ?? ss / 32;                              // the 240 x 240 panel inside its bezel
  Object.assign(canvas.style, { left: `${(sx + inset) / L.w * 100}%`, top: `${(sy + inset) / L.h * 100}%`,
                                width: `${(ss - 2 * inset) / L.w * 100}%`, height: `${(sh - 2 * inset) / L.h * 100}%` });
  KNOBS.forEach((n, i) => {
    const [x, y, r] = L.knobs[i], g = el("g", {});
    el("circle", { cx: x, cy: y, r: r + 5, fill: "var(--tray)", class: "knob-well" }, g);
    if (hard) el("circle", { cx: x, cy: y + 3, r: r + 1, fill: "#000", opacity: .55 }, g);   // (its skirt: it stands up)
    el("circle", { cx: x, cy: y, r, fill: "var(--part)", stroke: "var(--edge)", "stroke-width": 2, class: "knob-face" }, g);
    if (hard) el("circle", { cx: x, cy: y, r, fill: "url(#dome)", class: "knob-dome" }, g);
    const ind = n === "MASTER" ? el("line", { x1: x, y1: y - r + 5, x2: x, y2: y - r + 14, stroke: "var(--label)", "stroke-width": 3, "stroke-linecap": "round", class: "knob-ind" }, g)
              : el("circle", { cx: x, cy: y - r + 7, r: 2.6, fill: "var(--label)", class: "knob-ind" }, g);   // (an encoder: a dot that turns)
    label(g, x, y - r - 12, n, 16, DOT);
    el("circle", { cx: x, cy: y, r: r + 12, fill: "transparent" }, g);
    knobs.push({ g, ind, x, y, name: n, role: n === "MASTER" ? "master" : EN[n] });
  });
  const button = ([x, y, w, h], n, sub) => {
    const g = el("g", {});
    if (hard) el("rect", { x, y: y + 4, width: w, height: h, rx: R.btn, fill: "#000", opacity: .6, class: "skirt" }, g);
    el("rect", { x, y, width: w, height: h, rx: R.btn, fill: "var(--part)", stroke: "var(--edge)", "stroke-width": 1.5, class: "face btn" }, g);
    if (hard) el("rect", { x, y, width: w, height: h, rx: R.btn, fill: "url(#sheen)", class: "sheen" }, g);
    const ink = n === "REC" ? "#ff453a" : undefined;
    const F = document.body.className.startsWith("dist-") ? DOT : SANS;   // (2026-10-18: the dot face, as the knobs')
    if (sub) { label(g, x + w / 2, y + h / 2 - 2, n, 11.5, F, ink); label(g, x + w / 2, y + h / 2 + 11, sub, 10.5, F); }
    else label(g, x + w / 2, y + h / 2 + 5, n, 14, F, ink);
    buttons.push({ g, bit: B[n], name: sub ? `${n} / ${sub}` : n });
  };
  button(L.oct[0], "OCT-");
  button(L.oct[1], "OCT+");
  const [gx, gy, gw, gh, dx, dy, cols = 6] = L.grid;             // (cols 3: the studio's, four rows of three)
  [...ROW1, ...ROW2].forEach((n, i) => button([gx + (i % cols) * dx, gy + Math.floor(i / cols) * dy, gw, gh], n, n === "PLAY" ? "STOP" : ""));
  const [x0, y0, w, h] = L.keybed;
  tray(x0, y0, w, h);
  // the keys: a piano keyboard, 16 white and 11 black (F3 .. G5); each key's LED a strip at its back edge
  const ww = (w - 16) / 16, bw = ww * 0.6, bh = (h - 16) * 0.6, top = y0 + 8, wh = h - 16;
  const key = (x, kw, kh, k, black) => {
    const g = el("g", {});
    el("rect", { x, y: top + 4, width: kw, height: kh, rx: R.key, fill: "#000", opacity: black ? .7 : .45, class: "skirt" }, g);
    el("rect", { x, y: top, width: kw, height: kh, rx: R.key, class: "face key " + (black ? "black" : "white") }, g);
    el("rect", { x, y: top, width: kw, height: kh, rx: R.key, fill: black ? "url(#sheen)" : "url(#ivory)", class: "sheen" }, g);
    el("rect", { x: x + kw * 0.2, y: top + 6, width: kw * 0.6, height: 4, rx: 1, class: "slit" }, g);   // its LED
    keys[k] = { g, name: keyName(k) };
  };
  WHITE.forEach((k, i) => key(x0 + 8 + i * ww + 1, ww - 2, wh, k, false));
  BLACK_AFTER.forEach((a, j) => key(x0 + 8 + (a + 1) * ww - bw / 2, bw, bh, BLACK[j], true));
  // HTML targets over the drawing (pointer-events: none): touch-action and pointer capture work on them everywhere
  const hit = (c, cls, role) => {
    const b = c.g.getBBox(), d = document.createElement("div");
    d.className = "hit" + (cls ? " " + cls : "");
    Object.assign(d.style, { left: `${b.x / L.w * 100}%`, top: `${b.y / L.h * 100}%`,
                             width: `${b.width / L.w * 100}%`, height: `${b.height / L.h * 100}%` });
    d.setAttribute("role", role);
    d.setAttribute("aria-label", c.name);
    d.title = c.name;
    d.tabIndex = 0;
    d.addEventListener("touchstart", (e) => e.preventDefault(), { passive: false });
    d.addEventListener("contextmenu", (e) => e.preventDefault());
    device.insertBefore(d, opts.before || null);
    c.hit = d;
  };
  knobs.forEach((k) => { hit(k, "knob", "slider"); bindKnob(k); drawKnob(k); });
  buttons.forEach((b) => { hit(b, "", "button"); bindButton(b); });
  for (const i of [...WHITE, ...BLACK]) { hit(keys[i], "", "button"); bindKey(keys[i], i); }   // (the black keys' targets over
                                                                                              //  the white ones)
  syncButtons(true);
  syncKeys(true);
  drawLights(lastLeds);
}

// ------------------------------------------------------------------ input
function syncButtons(redraw) {
  let m = latched;
  for (const b of btnBy.values()) m |= 1 << b;
  if (m !== btnMask) { btnMask = m; send({ type: "buttons", mask: m }); }
  else if (!redraw) return;
  for (const b of buttons) {
    const on = !!(m & (1 << b.bit));
    b.g.classList.toggle("held", on);
    b.g.classList.toggle("latched", !!(latched & (1 << b.bit)));
    b.hit.setAttribute("aria-pressed", on);
  }
}
function syncKeys(redraw) {
  let m = 0;
  for (const k of keyBy.values()) m |= 1 << k;
  if (m !== keyMask) { keyMask = m; send({ type: "keys", mask: m }); }
  else if (!redraw) return;
  keys.forEach((k, i) => k.g.classList.toggle("held", !!(m & (1 << i))));
}
function bindButton(b) {
  b.hit.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    if (e.shiftKey || e.button === 2) { latched ^= 1 << b.bit; syncButtons(true); return; }
    if (latched & (1 << b.bit)) { latched &= ~(1 << b.bit); syncButtons(true); return; }
    b.hit.setPointerCapture(e.pointerId);
    btnBy.set("p" + e.pointerId, b.bit);
    syncButtons();
    opts.onButton?.(b.bit);
  });
  b.hit.addEventListener("keydown", (e) => {
    if ((e.key === "Enter" || e.key === " ") && !e.repeat) { btnBy.set("f" + b.bit, b.bit); syncButtons(); opts.onButton?.(b.bit); e.preventDefault(); e.stopPropagation(); }
  });
  b.hit.addEventListener("keyup", () => { if (btnBy.delete("f" + b.bit)) syncButtons(); });
}
function bindKey(k, i) {
  k.hit.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    k.hit.setPointerCapture(e.pointerId);
    keyBy.set("p" + e.pointerId, i);
    syncKeys();
  });
}
function release(e) {
  const id = "p" + e.pointerId;
  if (btnBy.delete(id)) syncButtons();
  if (keyBy.delete(id)) syncKeys();
}

function drawKnob(k) { if (k.ind) k.ind.setAttribute("transform", `rotate(${angle[k.name]} ${k.x} ${k.y})`); }
function turn(k, n) {
  if (!n) return;
  if (k.role === "master") {
    masterValue = Math.max(0, Math.min(1023, masterValue + n * 16));
    angle.MASTER = -135 + 270 * masterValue / 1023;
    send({ type: "master", value: masterValue });
  } else {
    angle[k.name] = (angle[k.name] + n * 18) % 360;            // 20 detents a turn
    send({ type: "enc", role: k.role, n });
  }
  drawKnob(k);
}
function bindKnob(k) {
  let drag = null, wheel = 0;
  k.hit.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    k.hit.setPointerCapture(e.pointerId);
    drag = { y: e.clientY, acc: 0 };
  });
  k.hit.addEventListener("pointermove", (e) => {
    if (!drag) return;
    drag.acc += drag.y - e.clientY;
    drag.y = e.clientY;
    const step = k.role === "master" ? 3 : 8, n = Math.trunc(drag.acc / step);
    if (n) { drag.acc -= n * step; turn(k, n); }
  });
  const end = () => { drag = null; };
  k.hit.addEventListener("pointerup", end);
  k.hit.addEventListener("pointercancel", end);
  k.hit.addEventListener("wheel", (e) => {
    e.preventDefault();
    const dy = e.deltaMode === 1 ? e.deltaY * 33 : e.deltaY;
    if (Math.abs(dy) >= 50) { turn(k, dy < 0 ? 1 : -1); wheel = 0; return; }   // a wheel notch: one detent
    wheel -= dy;                                                                   // a trackpad: by distance
    const n = Math.trunc(wheel / 25);
    if (n) { wheel -= n * 25; turn(k, n); }
  }, { passive: false });
  k.hit.addEventListener("keydown", (e) => {
    const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key];
    if (d) { turn(k, d); e.preventDefault(); e.stopPropagation(); }
  });
}
// the computer keyboard, every key one control: F3..A#4 on the Z (white) and A (black) rows, B4..G5 on Q..O
// in semitones, the twelve buttons on the number row in the panel's order (held while the key is), [ ] OCT
const KEYMAP = {};
["KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM", "Comma", "Period", "Slash"].forEach((c, i) => KEYMAP[c] = { key: WHITE[i] });
[["KeyS", 1], ["KeyD", 3], ["KeyF", 5], ["KeyH", 8], ["KeyJ", 10], ["KeyL", 13], ["Semicolon", 15], ["Quote", 17]].forEach(([c, k]) => KEYMAP[c] = { key: k });
["KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO"].forEach((c, i) => KEYMAP[c] = { key: 18 + i });
["Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9", "Digit0", "Minus", "Equal"]
  .forEach((c, i) => KEYMAP[c] = { btn: B[[...ROW1, ...ROW2][i]] });
Object.assign(KEYMAP, { BracketLeft: { btn: B["OCT-"] }, BracketRight: { btn: B["OCT+"] } });
// (Blue Hare) the arrows: up and down KNOB 1 (SELECT with Shift; up a step back, down a step on: a list's way),
// left and right ALGORITHM. \ turns Q E, A D, Z C from notes into KNOB 2, 3, 4 (each pair: down, up) and back
const ARROWS = { ArrowUp: [EN["KNOB 1"], -1], ArrowDown: [EN["KNOB 1"], 1], ArrowRight: [EN.ALGORITHM, 1], ArrowLeft: [EN.ALGORITHM, -1] };
const ARROWS_SHIFT = { ArrowUp: [EN.SELECT, -1], ArrowDown: [EN.SELECT, 1] };
const NUMKNOBS = { KeyQ: [EN["KNOB 2"], -1], KeyE: [EN["KNOB 2"], 1], KeyA: [EN["KNOB 3"], -1], KeyD: [EN["KNOB 3"], 1],
                   KeyZ: [EN["KNOB 4"], -1], KeyC: [EN["KNOB 4"], 1] };
export const pref = (k, v) => { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch { return null; } };
let numKnobs = pref("bluehare-letterknobs") === "1";
export const lettersAreKnobs = () => numKnobs;
export function numToggle() {
  numKnobs = !numKnobs;
  pref("bluehare-letterknobs", numKnobs ? "1" : "0");
  for (const c of Object.keys(NUMKNOBS)) keyBy.delete("k" + c);   // (a note held on one of them: let go)
  syncKeys();
  opts.onNumKnobs?.(numKnobs);
}
// the page's own keys first (opts.keydown: true when it took the key); typing in a field: none of these
const typing = (e) => /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName) || e.target.isContentEditable;
function keydown(e) {
  if (typing(e) || opts.keydown?.(e)) return;
  if (e.code === "Backslash" && !e.repeat && !e.metaKey && !e.ctrlKey && !e.altKey) { numToggle(); e.preventDefault(); return; }
  if (!opts.on?.() || e.metaKey || e.ctrlKey || e.altKey || e.target.closest?.(".hit.knob")) return;
  const arrow = (e.shiftKey && ARROWS_SHIFT[e.code]) || ARROWS[e.code];
  if (arrow) { const [r, n] = arrow; turn(knobs.find((k) => k.role === r), n); e.preventDefault(); return; }
  if (numKnobs && NUMKNOBS[e.code]) { const [r, n] = NUMKNOBS[e.code]; turn(knobs.find((k) => k.role === r), n); e.preventDefault(); return; }
  const m = KEYMAP[e.code];
  if (!m) return;
  e.preventDefault();
  if (e.repeat) return;
  if (m.key !== undefined) { keyBy.set("k" + e.code, m.key); syncKeys(); }
  else { btnBy.set("k" + e.code, m.btn); syncButtons(); opts.onButton?.(m.btn); }
}
function keyup(e) {
  const id = "k" + e.code;
  if (keyBy.delete(id)) syncKeys();
  if (btnBy.delete(id)) syncButtons();
}
function blur() {
  for (const id of [...btnBy.keys()]) if (id[0] !== "p") btnBy.delete(id);
  for (const id of [...keyBy.keys()]) if (id[0] !== "p") keyBy.delete(id);
  syncButtons();
  syncKeys();
}

const lin = (h) => [1, 3, 5].map((i) => (parseInt(h.slice(i, i + 2), 16) / 255) ** 2.2);
const ANIM = { btn: [lin("#3a3934"), lin("#3a3934"), lin("#fff4d6")], slit: [lin("#3a3a3c"), lin("#77777b"), lin("#ececee")] };
function animFill(kind, q) {
  const [dark, glow, lit] = ANIM[kind], s = Math.min(q & 127, 64) / 64, base = q & 128 ? glow : dark;
  const c = base.map((v, i) => Math.round(255 * (v + (lit[i] - v) * s) ** (1 / 2.2)));
  return `rgb(${c.join(",")})`;
}
function drawAnim(anim) {
  for (const b of buttons) b.g.querySelector(".face").style.fill = anim ? animFill("btn", anim[b.bit]) : "";
  keys.forEach((k, i) => { k.g.querySelector(".slit").style.fill = anim ? animFill("slit", anim[14 + i]) : ""; });
}
export function drawLights(leds, anim = null) {
  lastLeds = leds;
  const [litB, litK, dimB, dimK, dimLo = 0, brB = 0, brK = 0, midK = 0] = leds;
  drawAnim(anim);
  document.documentElement.classList.toggle("dimlo", !!dimLo);   // (the breath's lower peak)
  for (const b of buttons) {
    const bit = 1 << b.bit, green = b.bit === B.PLAY && (litB & (1 << PLAY_GREEN));
    b.g.classList.toggle("lit", !!(litB & bit) && !green);
    b.g.classList.toggle("green", !!green);
    b.g.classList.toggle("dim", !!(dimB & bit));
    b.g.classList.toggle("breath", !!(brB & bit));
  }
  keys.forEach((k, i) => {
    k.g.classList.toggle("lit", !!(litK & (1 << i)));
    k.g.classList.toggle("dim", !!(dimK & (1 << i)));
    k.g.classList.toggle("breath", !!(brK & (1 << i)));
    k.g.classList.toggle("mid", !!(midK & (1 << i)));
  });
}


// the page's elements and hooks; the keyboard and the pointers' releases bound once
export function initShell(o) {
  opts = o;
  ({ device, svg } = o);
  canvas = o.slot;
  Object.assign(LAYOUTS, o.layouts || {});
  for (const t of ["pointerup", "pointercancel", "lostpointercapture"]) device.addEventListener(t, release);
  addEventListener("keydown", keydown);
  addEventListener("keyup", keyup);
  addEventListener("blur", blur);
}
// a button pressed and let go from the page (not the shell): the firmware's, not the page's onButton
export function tap(bit, ms = 70) {
  btnBy.set("t" + bit, bit);
  syncButtons();
  setTimeout(() => { btnBy.delete("t" + bit); syncButtons(); }, ms);
}
export const currentLayout = () => layout;
export const master = () => masterValue;
export { B, EN, WHITE, BLACK, KNOBS, LAYOUTS, segText, build, turn, knobs, syncButtons, syncKeys, keyName };
