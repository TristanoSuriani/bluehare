// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Blue Hare in an AudioWorklet: bluehare.wasm is the whole FM-1 firmware against a simulated FM-1
// (web/emu/bluehare_web.c). Each render quantum asks it for 128 frames, which runs the device's clock forward;
// between quanta the page's input goes in and, about 30 times a second (4 when the page is hidden), the screen,
// the lights and any changed flash go out. After X0X's web/emu/worklet.js (charlesvestal/fm1-x0x, GPL-3.0).

// The flash the page keeps: storage.c's areas (projects, user presets and their FM6 patches, the user sample
// slots 0x97000..0xDFFFF; the autosave 0xE5000..0xE6FFF (1.2) and FM6's voice bank 0xE7000 (1.4.1); the settings and
// the second FM6 copy 0xFC000..0xFFFFF), only the sectors that hold something (an erased sector is all 0xFF). {offset: bytes}
export const STORE_AREAS = [[0x97000, 0xE0000], [0xE5000, 0xE8000], [0xFC000, 0x100000]];
export function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = s.slice();
    }
  return out;
}
export function restoreSectors(nor, sectors) {
  for (const [k, v] of Object.entries(sectors || {})) {
    const off = +k, b = v instanceof Uint8Array ? v : new Uint8Array(v);
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && b.length === 4096)
      nor.set(b, off);
  }
}

export const HOLD_MS = 24;

if (typeof registerProcessor === "function") {
  const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();
  class BlueHare extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentWrites = -1;
      this.sentLeds = "";
      this.lastFrame = 0;
      this.period = 1 / 30;
      this.busy = 0;
      this.frames = 0;
      this.queue = [];                                // button / key changes, each held >= HOLD_MS of device time
      this.nextOk = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(m.wasm, {});
          const ex = instance.exports;
          ex._initialize();
          ex.web_nor_erase();
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), m.sectors);
          ex.web_master(m.master ?? 700);
          ex.web_boot();
          this.ex = ex;
          this.sentWrites = ex.web_flash_writes_count();
          this.port.postMessage({ type: "ready", rate: ex.web_sample_rate() });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String(err && err.message || err) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
      else if (m.type === "enc") ex.web_enc(m.role, m.n | 0);
      else if (m.type === "master") ex.web_master(m.value | 0);
      else if (m.type === "midi") ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
      else if (m.type === "call") this.call(m);
    }

    // (Blue Hare, the sound design studio) the page's calls of the wasm's exports, in order, between two render
    // quanta: each {fn, args, str, ret}; str is written to stu_buf() before the call (ASCII), ret reads the result:
    // "str" a C string at it, ["i16" | "i32" | "u8", n] n values at it, else the number. One reply with them all
    call(m) {
      const ex = this.ex, out = [];
      try {
        for (const c of m.calls) {
          const f = ex[c.fn];
          if (typeof f !== "function") throw new Error("no export " + c.fn);
          if (c.str !== undefined) {
            const b = new Uint8Array(ex.memory.buffer, ex.stu_buf(), 256), s = String(c.str).slice(0, 255);
            for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 0x7F;
            b[s.length] = 0;
          }
          const r = f(...(c.args || []));
          if (c.ret === "str") {
            const b = new Uint8Array(ex.memory.buffer, r >>> 0, 256);
            let s = "";
            for (let i = 0; i < 256 && b[i]; i++) s += String.fromCharCode(b[i]);
            out.push(s);
          } else if (Array.isArray(c.ret)) {
            const T = { i16: Int16Array, i32: Int32Array, u8: Uint8Array }[c.ret[0]];
            out.push(Array.from(new T(ex.memory.buffer, r >>> 0, c.ret[1])));
          } else {
            out.push(r);
          }
        }
        this.port.postMessage({ type: "ret", id: m.id, values: out });
      } catch (err) {
        this.port.postMessage({ type: "ret", id: m.id, error: String(err && err.message || err) });
      }
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer;
      const msg = { type: "frame" };
      const leds = [ex.web_lit_buttons(), ex.web_lit_keys(), ex.web_dim_buttons(), ex.web_dim_keys(), ex.web_dim_level(),
                    ex.web_breath_buttons(), ex.web_breath_keys(), ex.web_mid_keys ? ex.web_mid_keys() : 0];
      const an = ex.web_anim_levels ? ex.web_anim_levels() : 0;   // the power-on sweep: each LED's level
      const anim = an ? Array.from(new Uint8Array(mem, an, 41)) : null;
      const key = leds.join(",") + (anim ? ";" + anim.join(",") : "");
      if (key !== this.sentLeds) {
        this.sentLeds = key;
        msg.leds = leds;
        msg.anim = anim;
      }
      if (this.frames >= 44100) {                     // the share of real time spent running the device
        msg.load = this.busy / (this.frames / 44.1);
        this.busy = this.frames = 0;
      }
      const transfer = [];
      const draws = ex.web_screen_draws();
      if (draws !== this.sentDraws) {
        this.sentDraws = draws;
        msg.fb = new Uint16Array(mem, ex.web_screen(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      const writes = ex.web_flash_writes_count();
      if (writes !== this.sentWrites) {               // a save is done within one main-loop pass: whole here
        this.sentWrites = writes;
        msg.sectors = storeSectors(new Uint8Array(mem, ex.web_nor(), ex.web_nor_size()));
      }
      if (msg.leds || msg.fb || msg.sectors || msg.load !== undefined) this.port.postMessage(msg, transfer);
    }

    // A click's down and up can arrive within one render quantum, but the FM-1's contacts never close for less than
    // its debounce, and the UI reads them once a frame: each change of the buttons or the keys stays HOLD_MS of the
    // device's time before the next one goes in (a tap: down for a frame and more, then up).
    input() {
      const ex = this.ex, now = ex.web_now_ms();
      while (this.queue.length && now - this.nextOk >= 0) {
        const m = this.queue.shift();
        if (m.type === "buttons") ex.web_buttons(m.mask >>> 0);
        else ex.web_keys(m.mask >>> 0);
        this.nextOk = now + HOLD_MS;
      }
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      if (this.queue.length) this.input();
      const out = outputs[0], n = out[0].length;
      const t0 = clock();
      ex.web_render(n);
      this.busy += clock() - t0;
      this.frames += n;
      out[0].set(new Float32Array(ex.memory.buffer, ex.web_out_l(), n));
      if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.web_out_r(), n));
      if (currentTime - this.lastFrame >= this.period) {
        this.lastFrame = currentTime;
        this.publish();
      }
      return true;
    }
  }
  registerProcessor("bluehare", BlueHare);
}
