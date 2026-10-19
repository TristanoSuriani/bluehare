// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tristano Suriani, Blue Hare: see AUTHORS.md
// (2026-10-19) STROBE at the screen's resolution: the firmware's TECHNO visuals (firmware/os/ui_strobe.c) drawn again
// for the studio's VISUALS (the user's "a visual mode for techno, the screen going fullscreen, much higher resolution
// visuals but comparable to the small screen version"). The same five scenes (GRID TUNNEL STEPS WARP WAVES), the same
// six neon pairs, picked by the music's part and the piece as the firmware picks them; the same listening (a drum hit
// jolts the floor and thickens the ring, a bass note raises the bars and swells the waves, anything else shifts slices
// sideways). The picture is 240 units high and as wide as the screen; its lines drawn smooth, a little glow round them.
// SAFE TO WATCH as the device's: nothing flashes, no area lights at once.
//   const s = createStrobe(canvas); s.frame(state, dtMs)   state: {on, step, part, piece, bpm, notes[4], roles[4], name}
const SCHEMES = [["#00e6ff", "#ff28c8"], ["#78ff28", "#ffe600"], ["#ff9600", "#ff2828"],
                 ["#78aaff", "#ebf5ff"], ["#aa46ff", "#ff5ab4"], ["#ff3c1e", "#ffc83c"]];
const NBAR = 12, NSTAR = 160, TICK = 30;              // (the firmware's state steps ~33 times a second)

export function createStrobe(cv) {
  const x = cv.getContext("2d");
  const st = { ready: false, scene: 0, scheme: 0, part: -1, rng: 0x5770B, ph: 0, t: 0, acc: 0, jolt: 0, energy: 0, glitch: 0,
               thick: 0, swell: 0, bar: new Array(NBAR).fill(0), btgt: new Array(NBAR).fill(0), stars: [],
               hits: [0, 0, 0, 0], seen: null, wrapped: false, slices: [] };
  const rand = (n) => { st.rng = (Math.imul(st.rng, 1664525) + 1013904223) >>> 0; return n ? (st.rng >>> 8) % n : 0; };
  let W = 240, H = 240, s = 1;
  function resize() {
    const r = cv.getBoundingClientRect(), dpr = Math.min(2, devicePixelRatio || 1);
    cv.width = Math.max(1, Math.round(r.width * dpr));
    cv.height = Math.max(1, Math.round(r.height * dpr));
    s = cv.height / 240;
    H = 240;
    W = cv.width / s;
  }
  const star = () => {
    const a = rand(1024) / 1024 * 2 * Math.PI;
    return { dx: Math.sin(a), dy: Math.cos(a), d: 1 + rand(20) };
  };
  function part(g) {
    if (st.ready && g.part === st.part) return;
    st.part = g.part;
    const h = (Math.imul(g.part >>> 0, 2654435761) ^ Math.imul(g.piece >>> 0, 40503)) >>> 0;
    st.scene = (g.part + (h >>> 7)) % 5;
    st.scheme = (h >>> 13) % 6;
  }
  function listen(g) {
    if (!st.seen) st.seen = g.notes.slice();
    const step = g.step;
    if (step === 0 && st.wrapped) { st.hits = [0, 0, 0, 0]; st.wrapped = false; }   // (a new bar: STEPS starts again)
    if (step === 15) st.wrapped = true;
    for (let r = 0; r < 4; r++) {
      const n = g.notes[r] - st.seen[r];
      st.seen[r] = g.notes[r];
      if (n <= 0 || n > 64) continue;
      st.hits[r] |= 1 << step;
      const nm = g.roles[r] || "";
      if (nm === "DRUMS" || nm === "KICK" || nm === "HATS") {
        st.jolt = 5; st.thick = 3; st.energy = Math.min(256, st.energy + 50);
      } else if (nm === "BASS") {
        for (let k = 0; k < NBAR; k++) st.btgt[k] = Math.max(st.btgt[k], 14 + rand(28));
        st.swell = 256; st.energy = Math.min(256, st.energy + 30);
      } else {
        st.glitch = 4; st.energy = Math.min(256, st.energy + 20);
      }
    }
  }
  function tick(g) {                                  // (st_step: what decays, the slices, the bars)
    st.t++;
    st.slices = [];
    if (st.glitch) {
      for (let k = 0; k < 6; k++) st.slices.push({ y: rand(232), h: 2 + rand(6), o: rand(41) - 20 });
      st.glitch--;
    }
    for (let i = 0; i < NBAR; i++) {
      if (st.bar[i] < st.btgt[i]) st.bar[i] = Math.min(st.btgt[i], st.bar[i] + 3);
      else { st.bar[i] = st.bar[i] - Math.floor(st.bar[i] / 6) - (st.bar[i] > 0 ? 1 : 0); st.btgt[i] = 0; }
    }
    if (st.jolt) st.jolt--;
    if (st.thick) st.thick--;
    st.swell -= Math.floor(st.swell / 10) + (st.swell > 0 ? 1 : 0);
    st.energy -= Math.floor(st.energy / 16) + (st.energy > 0 ? 1 : 0);
    if (st.scene === 3) {                             // WARP's stars move a tick at a time
      const sp = 1 + st.energy / 48;
      for (const p of st.stars) p.d += sp + p.d / 20;
    }
  }
  const mix = (a, b, t) => {                          // (ux_mix: the dim colour, 40/255 of the neon)
    const p = (c) => [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16));
    const A = p(a), B = p(b);
    return `rgb(${A.map((v, i) => Math.round(v + (B[i] - v) * t)).join(",")})`;
  };
  function stroke(c, w = 1.1, glow = 5) {
    x.strokeStyle = c;
    x.lineWidth = w;
    x.shadowColor = c;
    x.shadowBlur = glow * s;
    x.stroke();
  }
  function grid(sn, sd, sh) {
    const hz = H * 2 / 5, cx = W / 2;
    for (let k = 0; k < 8; k++) {                     // the floor's lines, rushing at you, one a beat
      const z = ((k + (st.ph % 1)) * 256);
      const yy = hz + z * z / 2048 * (H - hz) / 2048 + st.jolt;
      x.beginPath(); x.moveTo(0, yy); x.lineTo(W, yy); stroke(k > 5 ? sn : sd);
    }
    const n = Math.ceil(W / 40);
    for (let j = -n; j <= n; j++) {
      x.beginPath(); x.moveTo(cx + j * 8, hz); x.lineTo(cx + j * 40, H); stroke(j % 3 ? sd : sn, .9, 3);
    }
    x.beginPath(); x.moveTo(0, hz); x.lineTo(W, hz); stroke(sh, 1.4, 8);
    const r = 10 + st.energy * 22 / 256;             // the ring at the horizon, breathing
    x.beginPath(); x.arc(cx, hz - 4 - r, r, 0, 2 * Math.PI); stroke(sh, 1.2 + st.thick, 8);
    const bw = W / NBAR;                              // the bass's bars
    for (let i = 0; i < NBAR; i++) {
      const hb = st.bar[i];
      if (hb <= 0) continue;
      x.shadowBlur = 6 * s;
      x.shadowColor = sn;
      x.fillStyle = sn;
      x.fillRect(i * bw + 2, H - hb, bw - 4, hb);
      x.fillStyle = sh;
      x.fillRect(i * bw + 2, H - hb, bw - 4, Math.min(3, hb));
    }
  }
  function tunnel(sn, sd, sh) {
    const cx = W / 2, cy = H / 2;
    for (let k = 0; k < 9; k++) {
      const z = (k + (st.ph % 1)) * 256;
      const hw = 4 + z * z / 14000 * W / 240, hh = hw * H / W + st.jolt;
      x.beginPath(); x.rect(cx - hw, cy - hh, 2 * hw, 2 * hh); stroke(k >= 6 ? sh : k >= 3 ? sn : sd);
    }
    x.beginPath(); x.rect(cx - 3 - st.thick, cy - 2 - st.thick, 6 + 2 * st.thick, 4 + 2 * st.thick); stroke(sh, 1.4, 8);
  }
  function steps(sn, sd, sh, g) {
    const cw = W / 16, top = 40, rh = (H - 2 * top) / 4;
    for (let i = 0; i < 16; i++) {
      const x0 = i * cw + 2, x1 = x0 + cw - 5;
      for (let r = 0; r < 4; r++) {
        const y0 = top + r * rh + 1, y1 = y0 + rh - 5;
        if ((st.hits[r] >> i) & 1) {
          x.shadowColor = r ? sn : sh; x.shadowBlur = 8 * s; x.fillStyle = r ? sn : sh;
          x.fillRect(x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2);
        } else { x.beginPath(); x.rect(x0, y0, x1 - x0, y1 - y0); stroke(sd, .8, 2); }
      }
      if (i === g.step) { x.beginPath(); x.rect(x0 - 2, top - 2, x1 - x0 + 4, 4 * rh); stroke(sh, 1.2, 8); }
    }
  }
  function warp(sn, sh) {
    const cx = W / 2, cy = H / 2, sp = 1 + st.energy / 48, k = Math.max(W, H) / 240;
    while (st.stars.length < NSTAR) st.stars.push(star());
    for (let i = 0; i < NSTAR; i++) {
      const p = st.stars[i], d = p.d * k;
      const x0 = cx + p.dx * d * 4, y0 = cy + p.dy * d * 4, x2 = cx + p.dx * (d + (sp + 2) * k) * 4, y2 = cy + p.dy * (d + (sp + 2) * k) * 4;
      if (x0 < 0 || x0 >= W || y0 < 0 || y0 >= H) { st.stars[i] = star(); continue; }
      x.beginPath(); x.moveTo(x0, y0); x.lineTo(x2, y2); stroke(p.d > 60 ? sh : sn, 1, 4);
    }
  }
  function waves(sn, sd, sh) {
    for (let k = 0; k < 4; k++) {
      const y0 = 30 + k * (H - 40) / 4, amp = 4 + (k === 3 ? st.swell * 14 / 256 : st.energy * 10 / 256);
      const f = (k === 3 ? 3 : 6 + k * 5) * 2000000 / 4294967296 * 2 * Math.PI, v = (40000000 + k * 9000000) / 4294967296 * 2 * Math.PI;
      x.beginPath();
      for (let px = 0; px <= W; px += 1) {
        const y = y0 + Math.sin(px * 240 / 240 * f + st.t * v) * amp;
        px ? x.lineTo(px, y) : x.moveTo(px, y);
      }
      stroke(k === 3 ? sh : k === 0 ? sd : sn, k === 3 ? 1.4 : 1.1, 6);
    }
  }
  function idle(g) {                                  // (stopped: the piece, as the device shows it)
    x.shadowBlur = 0;
    x.textAlign = "center";
    x.fillStyle = "#faf096";
    x.shadowColor = "rgba(250, 240, 150, .35)";
    x.shadowBlur = 12 * s;
    x.font = `400 34px BlueHareLED, ui-monospace, monospace`;
    x.fillText(g.name || "TECHNO", W / 2, H / 2);
    x.shadowBlur = 0;
    x.fillStyle = "#9a9466";
    x.font = `400 9px Pixelify, ui-monospace, monospace`;
    x.fillText("SPACE  OR  ARP  TO  PLAY", W / 2, H / 2 + 26);
  }
  function frame(g, dt) {
    if (cv.width !== Math.round(cv.getBoundingClientRect().width * Math.min(2, devicePixelRatio || 1))) resize();
    x.setTransform(1, 0, 0, 1, 0, 0);
    x.shadowBlur = 0;
    x.fillStyle = "#000";
    x.fillRect(0, 0, cv.width, cv.height);
    x.setTransform(s, 0, 0, s, 0, 0);
    if (!g.on) { st.ready = false; st.seen = null; idle(g); return; }
    if (!st.ready) { Object.assign(st, { ready: true, part: -1, stars: [], hits: [0, 0, 0, 0] }); }
    part(g);
    listen(g);
    st.ph += (g.bpm > 20 ? g.bpm : 120) / 60 * dt / 1000;   // (a line a beat)
    for (st.acc += dt; st.acc >= TICK; st.acc -= TICK) tick(g);
    st.acc = Math.min(st.acc, TICK * 4);
    const [neon, hot] = SCHEMES[st.scheme], dim = mix("#000000", neon, 40 / 255 * 1.6);
    x.lineCap = "round";
    x.lineJoin = "round";
    if (st.scene === 1) tunnel(neon, dim, hot);
    else if (st.scene === 2) steps(neon, dim, hot, g);
    else if (st.scene === 3) warp(neon, hot);
    else if (st.scene === 4) waves(neon, dim, hot);
    else grid(neon, dim, hot);
    x.setTransform(1, 0, 0, 1, 0, 0);
    x.shadowBlur = 0;
    for (const sl of st.slices) {                     // the shift: slices of the picture moved sideways
      const y = Math.round(sl.y * s), h = Math.max(1, Math.round(sl.h * s)), o = Math.round(sl.o * s);
      x.drawImage(cv, 0, y, cv.width, h, o, y, cv.width, h);
    }
  }
  return { frame, resize };
}
