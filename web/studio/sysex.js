// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tristano Suriani, Blue Hare: see AUTHORS.md
// The studio's side of the editor protocol (web/EDITOR_PROTOCOL.md): frames F0 7D 46 4C cmd args.. F7, 14-bit values
// as two 7-bit bytes, LSB first, offset by 8192. Only what the studio sends and reads: INFO, UP_LIST, UP_PUT.
export const ED = { HDR: [0x7D, 0x46, 0x4C], INFO: 1, UP_LIST: 16, UP_PUT: 18 };
export const v14 = (v) => { const u = Math.max(-8192, Math.min(8191, v | 0)) + 8192; return [u & 127, u >> 7]; };
export const frame = (cmd, args) => [0xF0, ...ED.HDR, cmd, ...args, 0xF7];
// a reply's arguments (after cmd, before F7), or null when it is not an editor frame of cmd
export function reply(d, cmd) {
  if (d.length < 6 || d[0] !== 0xF0 || d[1] !== ED.HDR[0] || d[2] !== ED.HDR[1] || d[3] !== ED.HDR[2] || d[4] !== cmd) return null;
  return Array.from(d.slice(5, d[d.length - 1] === 0xF7 ? d.length - 1 : d.length));
}
export const cstr = (a, i) => { let s = ""; while (i < a.length && a[i]) s += String.fromCharCode(a[i++]); return [s, i + 1]; };
// INFO -> {version, nengines, pcount}
export function parseInfo(a) {
  const [version, i] = cstr(a, 0);
  return { version, nengines: a[i], pcount: a[i + 1] };
}
// UP_LIST (start, count, total, per slot: used, engine, name) -> [{used, engine, name}]
export function parseUpList(a) {
  const out = [];
  let j = 3;
  for (let n = 0; n < a[1]; n++) {
    const used = a[j], engine = a[j + 1];
    const [name, k] = cstr(a, j + 2);
    j = k;
    out.push({ used, engine, name });
  }
  return out;
}
// UP_PUT's arguments: slot, engine id, name, the P_COUNT values, an empty 16-step pattern, kind 0 and its 16 bytes,
// the category (1.2's form: what UP_GET answers with)
export const upPutArgs = (slot, engineId, name, values, cat) =>
  [slot & 127, engineId & 127, ...[...name].map((c) => c.charCodeAt(0) & 127), 0, ...values.flatMap(v14),
   ...Array(32).fill(0), 0, ...Array(16).fill(0), cat & 127];
