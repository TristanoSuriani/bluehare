// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Modified by Tristano Suriani for Fel-Tracker (2026): see AUTHORS.md
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html: the protocol section (between PROTO-BEGIN/END) against its mock device (v1 commands,
//   the user preset bank / librarian, library files, live pushes, older-firmware fallback, the v3 tracks
//   and the mixer, the v5 drum grid in steps and user presets), its tab layout and ja/en strings
// - fm1pkg.js: productOf and logicalImage on build/felucca.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import vm from "node:vm";
import { logicalImage, productOf } from "./fm1pkg.js";
import { Updater, pack7, unpack7 } from "./fm1ota.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const py = (code, ...args) => execFileSync("python3", ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = new URL(".", import.meta.url).pathname;
/* what lives outside web/, each overridable (a web-only checkout passes them, or the checks skip):
   FELUCCA_DESC  the firmware's parameter / protocol table (tests/descdump.c -> build/host/desc.json)
   FELUCCA_ROOT  the Felucca tree with a ./build.sh build (the package check) */
const DESC = process.env.FELUCCA_DESC || join(HERE, "../build/host/desc.json");
const FW_ROOT = process.env.FELUCCA_ROOT || join(HERE, "..");

/* ------------------------------------------------------------ editor protocol --- */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `
;({ frame, unframe, parse, req, Link, makeMockDevice, CMD,
   UP, bank, capturePatch, auditionPatch, startWatch, libraryFile, readLibraryFile, paramKeys, patternFromSteps, stepsFromPattern, upName,
   mixer, parseNotes, parseHits, hitsText, gridFromSteps, LANE_NOTE, LANE_OF, readDevicePreferences, devicePresetRows, engineOrder, ENGINE_ORDER, aliasOf, fmtValue, enumShown, F,
   MENU: typeof MENU === "undefined" ? null : MENU, MENU_TABS: typeof MENU_TABS === "undefined" ? null : MENU_TABS,
   readDeviceMenu: typeof readDeviceMenu === "undefined" ? null : readDeviceMenu,
   parseMotionAll: typeof parseMotionAll === "undefined" ? null : parseMotionAll,
   fm6Bank: typeof fm6Bank === "undefined" ? null : fm6Bank, crc32: typeof crc32 === "undefined" ? null : crc32 })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console });

async function editorMock() {
  const m = E.makeMockDevice();
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 300 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (r, o) => link.request(r, o);
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  {   /* the primary distribution's engines by id (IDM), "-" for the rest: retired (0 .. 13, 27) and left out */
    const IDM = { 14: "WAVE", 16: "WEST", 18: "SPEECH", 21: "VOSIM", 22: "PARTICLE", 23: "CHAOS", 24: "SID", 25: "DUBSIREN",
      28: "KIT", 29: "BREAKS", 32: "DUBCHORD", 33: "WASP", 34: "MODAL", 35: "VECTOR", 36: "ECHOES", 37: "GLITCH" };
    ok(info.nengines === 61 && info.engines.every((n, e) => n === (IDM[e] || "-")) && info.pcount === 104 && info.pe0 === 96,
      "editor: INFO");
  }
  let descs = 0;
  for (let i = 0; i < info.pcount; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label) descs++;
  ok(descs === info.pcount, "editor: DESC for every parameter");
  {
    /* the SLICER (core.h P_SLCR..P_SLDEPTH = 45..48, just before P_E0; its descriptors: mockTables) is the
       track's: a factory preset keeps it, as ui.c apply_preset_to does (param_kept) */
    const sd = [];
    for (let i = 45; i < 49; i++) sd.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(sd.map((d) => d.label).join() === "SLCR,PAT,RATE,DEPTH" && sd[0].names.join() === "OFF,GATE,STUT",
      "editor: SLICER parameters 45..48 over DESC");
    await rq(E.req.set(0, 45, 2));
    await rq(E.req.set(0, 46, 7));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(24, 1));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[45] === 2 && on.p[46] === 7 && off.p[45] === 2 && off.p[46] === 7 && off.preset === 1, "editor: a factory preset keeps the SLICER (the track's)");
    await rq(E.req.set(0, 45, 0));
  }
  {
    /* the modulation matrix (core.h P_M1SRC..P_M4AMT = 49..60, just before P_E0 61): SRC / DST / AMT x 4, the
       DST names (20: more than 16) over DESC, every slot OFF after a factory preset (ui.c apply_preset_to) */
    const md = [];
    for (let i = 49; i < 61; i++) md.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(md.map((d) => d.label).join() === "SRC1,DST1,AMT1,SRC2,DST2,AMT2,SRC3,DST3,AMT3,SRC4,DST4,AMT4"
      && md[0].names.join() === "OFF,LFO,ENV,VEL,KEY,RAND,MODW,AT,EXPR,S&H,SLEW" && md[1].names.length === 21
      && md[1].names[11] === "VIB" && md[1].names[19] === "E8" && md[1].names[20] === "DEPTH" && md[2].min === -64 && md[2].max === 63 && md.every((d) => d.def === 0),
      "editor: matrix parameters 49..60 over DESC (21 DST names: DEPTH appended; S&H SLEW)");
    await rq(E.req.set(0, 52, 1));
    await rq(E.req.set(0, 53, 12));
    await rq(E.req.set(0, 54, -30));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(24, 2));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[52] === 1 && on.p[53] === 12 && on.p[54] === -30 && off.p.slice(49, 61).every((v) => v === 0),
      "editor: a factory preset turns every matrix slot off");
  }
  {
    /* the chord keys (core.h P_CHRD, P_VOIC = 81, 82, just before P_E0 83): the track's, a factory preset keeps them */
    const c0 = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 81)));
    const c1 = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 82)));
    ok(c0.label === "CHRD" && c0.names.join() === "OFF,DIA3,DIA7,MAJ,MIN,DOM7,MAJ7,MIN7,SUS4,POW" && c0.def === 0 &&
       c1.label === "VOIC" && c1.names.join() === "CLOSE,OPEN,INV1,INV2,+OCT" && c1.def === 0,
      "editor: the chord keys 81, 82 over DESC");
    await rq(E.req.set(0, 81, 2));
    await rq(E.req.set(0, 82, 4));
    await rq(E.req.preset(24, 3));
    const kept = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(kept.p[81] === 2 && kept.p[82] === 4 && kept.preset === 3, "editor: a factory preset keeps CHRD and VOIC (the track's)");
    await rq(E.req.set(0, 81, 0));
    /* the DRUM lane levels (core.h P_LN0..P_LN7 = 83..90, just before P_E0 91): percents, 100 % by default; the sound's,
       a factory preset sets them back */
    const lanes = [];
    for (let i = 83; i <= 90; i++) lanes.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(lanes.map((d) => d.label).join() === "KICK,SNARE,CLAP,HATCL,HATOP,TOM,RIM,BELL" &&
       lanes.every((d) => d.fmt === E.F.PCT && d.min === 0 && d.max === 127 && d.def === 127),
      "editor: the DRUM lane levels 83..90 over DESC (percent, 100 % default)");
    await rq(E.req.set(0, 84, 40));
    const lv = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(24, 3));
    const lv2 = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(lv.p[84] === 40 && lv2.p[84] === 127, "editor: a lane level is the sound's: a factory preset sets it back to 100 %");
    await rq(E.req.set(0, 82, 0));
  }
  ok(E.CMD.SONG === 33 && E.CMD.UI_STATE === 34 && E.CMD.FAV_SET === 38, "editor: preferences preserve the existing SONG command");
  const baseInfo = [116,101,115,116,0, 0, 69, 27, 64, 61, 4];
  const untagged = E.parse[E.CMD.INFO]([...baseInfo, 15]);
  ok(!untagged.chainRows && !untagged.uiCaps, "editor: an untagged PR capability cannot masquerade as SONG or preferences");
  const oldSong = E.parse[E.CMD.INFO]([...baseInfo, 16]);
  ok(oldSong.chainRows === 16 && !oldSong.uiCaps, "editor: existing SONG firmware remains compatible without preference probes");
  const tagged = E.parse[E.CMD.INFO]([...baseInfo, 16, 0x55, 1, 11]);
  ok(tagged.chainRows === 16 && tagged.uiCaps === 11, "editor: tagged preference extension advertises its supported controls");
  const pending = E.parse[E.CMD.FAV_SET]([4, 0, 0, 64, 1]);
  ok(pending.rc === 4 && pending.on && pending.engine === 0, "editor: queued favorite response is applied while waiting for STOP");
  const names = [];
  for (let e = 0; e < info.nengines; e++) names.push(E.parse[E.CMD.NAMES](await rq(E.req.names(e))).names);
  let prefs = await E.readDevicePreferences(rq, info, names);
  ok(info.uiCaps === 9 && prefs.palettes.length === 10 && prefs.palettes[0] === "GREY" && prefs.palettes.includes("HI-CON") && prefs.palettes[8] === "NIGHT" && prefs.palettes[9] === "MONO",
     "editor: preference capabilities (palette, favorites) and palette names");
  const pal = E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 2)));
  ok(pal.rc === 0 && pal.palette === 2, "editor: display preference 0 (palette) round trip");
  ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 10))).rc === 1, "editor: out-of-range palette refused");
  ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(1, 1))).rc === 2, "editor: the retired font weight answers not supported");
  ok(E.parse[E.CMD.FAV_SET](await rq(E.req.favSet(info.nengines, 31, true))).rc === 1, "editor: empty user slot cannot be favorited");
  await rq(E.req.favSet(24, 0, true));                  /* (SID: id 24) */
  await rq(E.req.uiSet(3, 1));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(E.devicePresetRows(info, names, prefs).length === 1 && prefs.favorites[24][0], "editor: favorites filter follows device state");
  m.state.favorites[24][0] = false; m.state.favorites[33][0] = true;
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(!prefs.favorites[24][0] && prefs.favorites[33][0], "editor: panel-side favorite changes refresh");
  await rq(E.req.upStore(31, "FAVORITE"));
  await rq(E.req.favSet(info.nengines, 31, true));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(E.devicePresetRows(info, names, prefs).some((r) => r.user && r.preset === 31), "editor: saved user slot appears as favorite");
  await rq(E.req.upStore(31, "RENAMED"));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(prefs.favorites[info.nengines][31] && prefs.slots.slots[31].name === "RENAMED", "editor: overwrite retains star and refreshes name");
  await rq(E.req.upErase(31));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(!prefs.favorites[info.nengines][31] && !E.devicePresetRows(info, names, prefs).some((r) => r.user), "editor: erased slot disappears and loses star");
  {   /* the lists in the device's order (engines.c ENGINE_ORDER): SID first, WASP second, GLITCH last, "-" never; the ids stay */
    const shown = E.engineOrder(info.engines).map((i) => info.engines[i]), o = E.engineOrder(info.engines);
    ok(shown.join() === "SID,WASP,WAVE,BREAKS,WEST,VECTOR,DUBSIREN,MODAL,DUBCHORD,VOSIM,PARTICLE,CHAOS,ECHOES,KIT,SPEECH,GLITCH" &&
       o[0] === 24 && o[1] === 33 && o[2] === 14 && o[3] === 29 && o[15] === 37,
       "editor: engines listed SID first, GLITCH last (ids kept)");
    ok(E.engineOrder(["SID", "X", "-", "KIT", "WASP"]).join() === "0,4,3,1", "editor: an unknown engine follows the known ones");
    m.state.favorites[28][0] = m.state.favorites[33][0] = true;
    await rq(E.req.uiSet(3, 0));
    prefs = await E.readDevicePreferences(rq, info, names, prefs);
    const rows = E.devicePresetRows(info, names, prefs).filter((r) => !r.user), eng = [...new Set(rows.map((r) => r.engine))];
    ok(eng[0] === 24 && eng[1] === 33 && eng[eng.length - 1] === 37, "editor: device presets in the device's engine order");
  }
  const none = await E.readDevicePreferences(() => { throw new Error("unexpected request"); }, { uiCaps: 0 }, []);
  ok(none === null, "editor: old firmware receives no unsupported preference requests");
  if (!E.readDeviceMenu) console.log(`${"editor: MENU settings (this editor has none)".padEnd(64)} skip`);
  else {   /* the MENU settings (1.0.4): INFO 4E 01 count, MENU_DESC (72), MENU_SET (73) */
    ok(info.menuCount === 24, "editor: INFO advertises the MENU settings (4E 01 24; 1.1: CLICK, CLICK LEVEL, COUNT-IN; 1.2: RESTORE LAST, SCALE LEDS; 1.1.5: SCREEN OFF; 1.2: SCOPE, STEP PREVIEW, CHORD ENTRY, MIDI IN, TUNE; 1.3: HOME)");
    const items = await E.readDeviceMenu(rq, info);
    ok(items.length === 24 && items.map((d) => d.id).join() === "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23" &&
       items.map((d) => d.name).join() ===
         "COLOR,STYLE,LARGE,ANIM,LEDS,HOLD,KNOB ACCEL,FX LATCH,BPM LOCK,SPEAKER EQ,USB LEVEL,USB SERIAL,CLICK,CLICK LEVEL,COUNT-IN,RESTORE LAST,SCALE LEDS,SCREEN OFF,SCOPE,STEP PREVIEW,CHORD ENTRY,MIDI IN,TUNE,HOME" &&
       items[12].names.join() === "OFF,REC,ON" && items[12].value === 0 && items[13].names.join() === "LOW,MID,HIGH" &&
       items[13].value === 1 && items[14].names.join() === "OFF,1 BAR,2 BARS" && items[14].value === 0 &&
       items[15].names.join() === "ON,OFF" && items[15].value === 0 && items[16].names.join() === "OFF,ON" && items[16].value === 0 &&
       items[17].names.join() === "NEVER,5 MIN,15 MIN,30 MIN,60 MIN" && items[17].value === 0 &&
       items[18].names.join() === "OUT,MIX" && items[18].value === 0 && items[19].names.join() === "OFF,ON" &&
       items[19].value === 0 && items[20].names.join() === "HOLD,ADD" && items[20].value === 0 &&
       items.slice(0, 22).every((d) => d.kind === 0 && d.min === 0 && d.names.length === d.max - d.min + 1) &&
       items[21].names.join() === "CH1-4,SEL,CH5-8,CH9-12,CH13-16" && items[21].value === 0 &&
       items[22].kind === 1 && items[22].min === -50 && items[22].max === 50 && items[22].unit === "ct" && items[22].value === 0 &&
       items[23].kind === 0 && items[23].names.join() === "SCOPE,TRACKS" && items[23].value === 0 &&
       eq(items[0].names, prefs.palettes) && items[0].value === m.state.palette &&
       items[4].names.join() === "OFF,DIM LO,DIM HI,INV" && items[4].value === 2 && items[5].value === 1,
       "editor: MENU_DESC lists every setting, its value names and value");
    ok(items.map((d) => d.tab).join() === "0,0,0,0,0,1,1,1,1,2,2,4,2,2,2,4,1,0,0,1,1,3,2,0" &&
       items.map((d) => d.tabName).join() ===
         "DISPLAY,DISPLAY,DISPLAY,DISPLAY,DISPLAY,CONTROL,CONTROL,CONTROL,CONTROL,AUDIO,AUDIO,SYSTEM,AUDIO,AUDIO,AUDIO,SYSTEM,CONTROL,DISPLAY,DISPLAY,CONTROL,CONTROL,MIDI,AUDIO,DISPLAY",
       "editor: MENU_DESC (1.0.5) gives each setting's tab on the device, after its names");
    ok(E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(24))).id === 127, "editor: MENU_DESC past the list answers id 127");
    let r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(4, 3)));
    ok(r.rc === 0 && r.id === 4 && r.value === 3 && E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(4))).value === 3,
       "editor: MENU_SET round trip (LEDS INV)");
    const pal = m.state.palette;
    r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(0, 50)));
    ok(r.rc === 0 && r.value === 9 && E.parse[E.CMD.UI_STATE](await rq(E.req.uiState())).palette === 9,
       "editor: MENU_SET clamps (COLOR 50 -> 9), the same setting as UI_SET 0");
    r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(14, 1)));
    ok(r.rc === 0 && r.id === 14 && r.value === 1 && E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(14))).value === 1,
       "editor: MENU_SET round trip (1.1: COUNT-IN 1 BAR)");
    await rq(E.req.menuSet(14, 0));
    r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(15, 1)));
    ok(r.rc === 0 && r.id === 15 && r.value === 1, "editor: MENU_SET round trip (1.2: RESTORE LAST OFF)");
    await rq(E.req.menuSet(15, 0));
    r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(16, 1)));
    ok(r.rc === 0 && r.id === 16 && r.value === 1, "editor: MENU_SET round trip (1.2: SCALE LEDS ON)");
    await rq(E.req.menuSet(16, 0));
    r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(17, 9)));
    ok(r.rc === 0 && r.id === 17 && r.value === 4 && E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(17))).value === 4,
       "editor: MENU_SET round trip (1.1.5: SCREEN OFF 9 -> 60 MIN, clamped)");
    await rq(E.req.menuSet(17, 3));
    for (const [id, name] of [[18, "1.2: SCOPE MIX"], [19, "1.2: STEP PREVIEW ON"], [20, "1.2: CHORD ENTRY ADD"], [23, "1.3: HOME TRACKS"]]) {
      r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(id, 5)));
      ok(r.rc === 0 && r.id === id && r.value === 1 && E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(id))).value === 1,
         `editor: MENU_SET round trip (${name}, clamped)`);
      await rq(E.req.menuSet(id, 0));
    }
    {   /* 1.2: MIDI IN (21) and TUNE (22) are the project's ROUT (global 14) and TUNE (global 3): the same values as SET */
      r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(21, 9)));
      const g14 = E.parse[E.CMD.GET](await rq(E.req.get(1, 14))).value;
      await rq(E.req.set(1, 14, 2));
      const d21 = E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(21))).value;
      const t = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(22, -99)));
      const g3 = E.parse[E.CMD.GET](await rq(E.req.get(1, 3))).value;
      await rq(E.req.set(1, 3, 17));
      const d22 = E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(22))).value;
      ok(r.rc === 0 && r.value === 4 && g14 === 4 && d21 === 2 && t.rc === 0 && t.value === -50 && g3 === -50 && d22 === 17,
         "editor: MENU_SET MIDI IN / TUNE (1.2) set globals 14 / 3 (clamped, rc 0), MENU_DESC reads what SET set");
      await rq(E.req.set(1, 14, 0)); await rq(E.req.set(1, 3, 0));
    }
    ok(E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(24, 1))).rc === 1, "editor: MENU_SET of an unknown id: rc 1");
    await rq(E.req.menuSet(0, pal)); await rq(E.req.menuSet(4, 2));
    const d = E.parse[E.CMD.MENU_DESC]([3, 20, 1, 5, 64, 0, 64, 100, 64, 88, 0, 109, 115, 0]);
    ok(d.kind === 1 && d.value === 5 && d.max === 100 && d.name === "X" && d.unit === "ms" && d.names === null && d.tab === -1,
       "editor: MENU_DESC kind 1 (a number): its unit");
    const d2 = E.parse[E.CMD.MENU_DESC]([3, 20, 1, 5, 64, 0, 64, 100, 64, 88, 0, 109, 115, 0, 2, 65, 0]);
    ok(d2.unit === "ms" && d2.tab === 2 && d2.tabName === "A", "editor: MENU_DESC kind 1 with its tab after the unit");
    const d3 = E.parse[E.CMD.MENU_DESC]([4, 4, 0, 2, 64, 0, 64, 1, 64, 76, 0, 65, 0, 66, 0]);
    ok(d3.names.join() === "A,B" && d3.tab === -1 && d3.tabName === "", "editor: MENU_DESC from 1.0.4 firmware (no tab): tab -1");
    ok((await E.readDeviceMenu(() => { throw new Error("unexpected request"); }, { menuCount: 0 })).length === 0,
       "editor: firmware without MENU settings is not asked");
  }
  await rq(E.req.uiSet(3, 0));
  const scale = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 26)));
  const scaleNames = ["CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM", "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"];
  ok(scale.label === "SCL" && scale.max === 15 && eq(scale.names, scaleNames), "editor: all 16 scale names exposed");
  const scaleSet = E.parse[E.CMD.SET](await rq(E.req.set(0, scale.id, 15)));
  ok(scaleSet.value === 15, "editor: new scale selection is not clamped to the old range");
  const dump = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
  ok(dump.p.length === info.pcount && dump.g.length === info.gcount, "editor: DUMP");
  const set = E.parse[E.CMD.SET](await rq(E.req.set(0, 3, 500)));
  ok(set.value === 127, "editor: SET clamps to the range");
  {
    /* MENU > MIDI > MIDI IN (G_ROUTE 14; up to 1.1.5 GLO > SYSTEM ROUT): CH1-4 (0, the default) / SEL (1) / 1.2: CH5-8 CH9-12 CH13-16 (2..4);
     * the id and G_COUNT unchanged; 5 clamps to the last */
    const rd = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 14)));
    const r1 = E.parse[E.CMD.SET](await rq(E.req.set(1, 14, 5)));
    const r0 = E.parse[E.CMD.SET](await rq(E.req.set(1, 14, 0)));
    ok(rd.label === "ROUT" && rd.def === 0 && eq(rd.names, ["CH1-4", "SEL", "CH5-8", "CH9-12", "CH13-16"]) && r1.value === 4 && r0.value === 0 && info.gcount === 27,
      "editor: MIDI IN routing (ROUT CH1-4 / SEL / CH5-8 / CH9-12 / CH13-16, global id 14)");
  }
  {
    /* FX > REVERB TYPE (G_RTYPE 24, the old drum channel's id): ROOM (0) / SPRING (1) / HALL (2, 1.2: the default);
     * G_COUNT unchanged; 10 clamps to the last */
    const rd = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 24)));
    const r1 = E.parse[E.CMD.SET](await rq(E.req.set(1, 24, 10)));
    const r0 = E.parse[E.CMD.SET](await rq(E.req.set(1, 24, 0)));
    const d26 = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 26)));
    ok(rd.label === "TYPE" && rd.def === 2 && eq(rd.names, ["ROOM", "SPRING", "HALL", "GATED", "CONCERT"]) && r1.value === 4 && r0.value === 0 &&
       d26.label === "-" && d26.max === 0 && info.gcount === 27,
      "editor: REVERB TYPE (ROOM / SPRING / HALL / GATED / CONCERT, global id 24; 26 still inert)");
  }
  {
    /* FX > DLY 2 TYPE (G_DTYPE 25, the old drum level's id, Fel-Tracker 1.5): CLEAN (0, the default) / TAPE / WORN / PING / DUAL;
     * 10 clamps to the last */
    const dd = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 25)));
    const r1 = E.parse[E.CMD.SET](await rq(E.req.set(1, 25, 10)));
    const r0 = E.parse[E.CMD.SET](await rq(E.req.set(1, 25, 0)));
    ok(dd.label === "TYPE" && dd.def === 0 && eq(dd.names, ["CLEAN", "TAPE", "WORN", "PING", "DUAL"]) && r1.value === 4 &&
       r0.value === 0 && info.gcount === 27, "editor: DELAY TYPE (CLEAN / TAPE / WORN / PING / DUAL, global id 25)");
  }
  const st = E.parse[E.CMD.STEP_SET](await rq(E.req.stepSet(5, { n: 2, notes: [60, 64], time: 0, flags: 1, vel: 100 })));
  ok(st.n === 2 && st.notes[1] === 64 && st.vel === 100, "editor: STEP_SET");
  const pj = E.parse[E.CMD.PROJECT](await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 }));
  ok(pj.used === 1, "editor: PROJECT save");
  /* a device that never answers */
  const dead = new E.Link(() => {}, { timeout: 30 });
  const err = await dead.request(E.req.info(), { retries: 1 }).then(() => null, (e) => e.message);
  ok(/^timeout/.test(err || ""), "editor: no reply -> timeout after the retries");
  link.close();
  m.stop();
}

async function editorPresetAliases() {
  const C = E.CMD;
  const { m, rq, done } = attachMock();
  const info = E.parse[C.INFO](await rq(E.req.info()));
  if (E.parseMotionAll) {   /* 1.2 (FUN10): 128 motion records; MOTION op 8 lists all, the other ops as before (max 64) */
    let rc = 0;
    for (let i = 0; i < 70; i++)
      rc |= E.parse[C.MOTION](await rq(E.req.motion(1, 3, { step: i % 64, param: i < 64 ? 36 : 35, value: i }))).rc;
    const legacy = E.parse[C.MOTION](await rq(E.req.motion(1)));
    const all = E.parseMotionAll(await rq(E.req.motion(1, 8)));
    const lfo = E.parse[C.MOTION](await rq(E.req.motion(1, 3, { step: 0, param: 91, value: 1 })));
    ok(!rc && legacy.count === 64 && legacy.max === 64 && all.count === 70 && all.max === 128 && all.events[69].value === 69 &&
       all.events[69].param === 35 && !all.events[0].lock && lfo.rc === 1,
       "editor: MOTION 1.2: 70 records on a track; the query as before (64 listed, max 64), op 8 all 70 (max 128); LFO 2 not recordable");
    await rq(E.req.motion(1, 2));
  }
  done();
}

/* the mock's tables == the firmware's (build/host/desc.json from tests/descdump.c, written by run_tests.sh):
   parameter descriptors, engines (titles, EDIT, presets), power-on sounds. The step sequencer's tables (NSTEP, the
   factory patterns, a preset's suggested pattern, the DRUM grid's lane notes) are the editor's own since 1.5 (the
   firmware has none): kept for firmware before 1.5, not compared */
function mockTables() {
  const dj = DESC;
  if (!existsSync(dj)) { console.log("editor: mock tables == firmware (no build/host/desc.json)        skip"); return; }
  const fw = JSON.parse(readFileSync(dj, "utf8"));
  const m = E.makeMockDevice({ auto: false }), T = m.tables;
  m.stop();
  const norm = (d) => ({ label: d.label, fmt: d.fmt, min: d.min, max: d.max, def: d.def, names: d.names || null, unit: d.unit || "" });
  const diffs = [];
  const cmp = (what, a, b) => { if (JSON.stringify(a) !== JSON.stringify(b)) diffs.push(`${what}: mock ${JSON.stringify(a)} != firmware ${JSON.stringify(b)}`); };
  for (const k of ["P_COUNT", "G_COUNT", "P_E0", "G_ENGSEL", "P_SLCR", "NTRK"]) cmp(k, T[k], fw[k]);
  cmp("TP length", T.TP.length, fw.TP.length);
  fw.TP.forEach((d, i) => cmp(`TP[${i}]`, T.TP[i] && norm(T.TP[i]), d));
  cmp("GP length", T.GP.length, fw.GP.length);
  fw.GP.forEach((d, i) => cmp(`GP[${i}]`, T.GP[i] && norm(T.GP[i]), d));
  cmp("engines", T.ENG.map((e) => e.name), fw.ENG.map((e) => e.name));
  cmp("engine order (ENGINE_ORDER)", E.ENGINE_ORDER.filter((n) => fw.ENG.some((e) => e.name === n)), fw.ORDER);
  fw.ENG.forEach((fe, i) => {
    const me = T.ENG[i];
    if (!me) return;
    cmp(`${fe.name} titles`, me.titles, fe.titles);
    fe.edit.forEach((d, k) => cmp(`${fe.name} edit[${k}]`, norm(me.edit[k]), d));
    cmp(`${fe.name} presets`, me.presets.map((p) => p.name), fe.presets.map((p) => p.name));
    fe.presets.forEach((p, k) => {
      const mp = me.presets[k];
      if (mp) cmp(`${fe.name} ${p.name}`, { e: mp.e, env: mp.env, mono: mp.mono }, { e: p.e, env: p.env, mono: p.mono });
    });
  });
  const m2 = E.makeMockDevice({ auto: false });   /* the mock's power-on sounds: its parts 1..4, track 4's beat */
  cmp("power-on sounds", m2.state.tracks.map((t) => [t.engine, t.preset]), fw.TRK_DEF.map((x) => x.slice(0, 2)));
  m2.stop();
  cmp("firmware: G_ENGSEL names == engine names", fw.GP[fw.G_ENGSEL].names, fw.ENG.map((e) => e.name));
  cmp("DRUM grid: the lane of GM 35..81", E.LANE_OF, fw.LANE_OF);
  const mj = join(DESC, "../menu.json");
  if (E.MENU && existsSync(mj)) {                   /* the MENU settings: tests/editor_test.c's MENU_DESC replies */
    const fm = JSON.parse(readFileSync(mj, "utf8"));
    cmp("MENU settings", E.MENU.map((x) => ({ id: x.id, kind: x.kind || 0, min: x.kind === 1 ? x.min : 0,
      max: x.kind === 1 ? x.max : (x.names || m2.state.palettes).length - 1, value: x.def, name: x.name,
      names: x.names || m2.state.palettes, unit: x.unit || "", tab: x.tab, tabName: E.MENU_TABS[x.tab] })), fm);
  }
  diffs.slice(0, 20).forEach((d) => console.log("  " + d));
  ok(!diffs.length, `editor: mock tables == firmware (${diffs.length} differences)`);
  /* #31: percent values as the firmware formats them (param_format), SWG 0..100 shows its value */
  const pd = (fw.PCT || []).filter(([sc, i, v, , txt]) => E.fmtValue((sc ? T.GP : T.TP)[i], v)[0] !== txt);   /* [scope, index, value, max, text] */
  pd.slice(0, 5).forEach((x) => console.log("  PCT " + JSON.stringify(x)));
  ok(fw.PCT && fw.PCT.length && !pd.length, `editor: percent values == firmware (${fw.PCT ? fw.PCT.length : 0} values)`);
  /* #48: every list's order in the editor == the order the device's knobs step it (descdump SHOWN: param_turn);
     the note divisions longest first, with the triplets by their length */
  const shown = (d) => E.enumShown(d).filter((v) => E.aliasOf(d.names, v - d.min) === v - d.min).map((v) => d.names[v - d.min])
    .filter((n) => n !== "-");                       /* ("-": a retired engine number, inert: never offered) */
  const sd = (fw.SHOWN || []).filter(([sc, i, names]) => JSON.stringify(shown((sc ? T.GP : T.TP)[i])) !==
    JSON.stringify(names.filter((n) => n !== "-")));
  sd.slice(0, 5).forEach((x) => console.log("  SHOWN " + JSON.stringify(x) + " editor " + JSON.stringify(shown((x[0] ? T.GP : T.TP)[x[1]]))));
  ok(fw.SHOWN && fw.SHOWN.length > 20 && !sd.length, `editor: enum lists in the device's knob order (${fw.SHOWN ? fw.SHOWN.length : 0} lists)`);
  const div = T.TP.find((d) => d.label === "DIV"), slr = T.TP.filter((d) => d.label === "RATE" && d.fmt === E.F.ENUM);
  ok(shown(div).join() === "4BAR,2BAR,1/1,1/2,1/4,1/8,8T,1/16,16T,1/32" &&
     slr.some((d) => shown(d).join() === "1/8,8T,1/16,16T,1/32,32T") && div.names[2] === "1/16",
     "editor: #48 DIV / RATE / TIME longest first (values unchanged: 2 is still 1/16)");
  const swg = [T.TP, T.GP].flatMap((tb) => tb.filter((d) => d.label === "SWG"));
  ok(swg.length === 3 && swg.every((d) => E.fmtValue(d, 100)[0] === "100" && E.fmtValue(d, 50)[0] === "50") &&
     E.fmtValue({ fmt: swg[0].fmt, min: 0, max: 127 }, 127)[0] === "100" && E.fmtValue({ fmt: swg[0].fmt, min: 0, max: 127 }, 64)[0] === "50",
     "editor: SWG 100 shows 100 % (not 79), 0..127 percents unchanged");
}

/* ------------------------------------------------------------- retired engines --- */
/* DIGITAL, SAMPLE, GRAIN and SLICE (1, 4, 8, 13) were removed in 1.5: the numbers are "-" with no presets and nothing
   to edit, G_ENGSEL / PRESET of one plays ANALOG, UP_PUT of one is refused, a library file's patch of one is skipped */
async function editorRetired() {
  const C = E.CMD;
  const { m, rq, done } = attachMock();
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const gone = [1, 4, 8, 13];
  let ok1 = true;
  for (const e of gone) {
    const n = E.parse[C.NAMES](await rq(E.req.names(e)));
    ok1 &&= info.engines[e] === "-" && !n.names.length && n.titles.join() === "-,-";
  }
  ok(ok1, "retired: engines 1 4 8 13 are \"-\", no presets");
  await rq(E.req.set(1, 20, 4));                  /* G_ENGSEL 4 (SAMPLE) */
  let d = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const a1 = d.engine;
  await rq(E.req.preset(13, 2));                  /* PRESET 13 2 (SLICE) */
  d = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(a1 === 24 && d.engine === 24 && d.preset === 0, "retired: G_ENGSEL / PRESET of one plays the first engine's first preset");
  const rc = await E.bank.put(rq, 9, { name: "OLD", engine: 8, p: d.p.slice(), pattern: null });
  ok(rc === 1 && !(await E.bank.get(rq, info, 9)).used, "retired: UP_PUT of a GRAIN sound refused");
  const ctx = { keys: null, engines: info.engines, pe0: info.pe0 };
  const lib = E.readLibraryFile({ format: "felucca-library", version: 1, engines: ["SID", "DIGITAL"], patches: [
    { name: "OLD PAD", engine: 1, engineName: "DIGITAL", params: d.p.slice(), pattern: null, tags: [] },
    { name: "PERC", engine: 4, engineName: "SAMPLE", params: d.p.slice(), pattern: null, tags: [] },
    { name: "A", engine: 0, engineName: "SID", params: d.p.slice(), pattern: null, tags: [] }] }, ctx);
  const one = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 13, engineName: "SLICE", p: d.p.slice() }, ctx);
  ok(lib.patches.length === 1 && lib.skipped === 2 && lib.patches[0].engine === 24 && !one.patches.length && one.skipped === 1,
    "retired: a library file's DIGITAL / SAMPLE / SLICE patches are skipped, the rest imported");
  done();
}

/* ------------------------------------- editor protocol v2: librarian + live --- */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (x) => JSON.stringify(x);
/* a mock + Link pair; counts frames sent per cmd and timeouts reported */
function attachMock(opt, linkOpt = {}) {
  const m = E.makeMockDevice({ auto: false, ...opt });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const sent = {}, ev = { timeouts: 0, unknown: [], pushes: [] };
  const link = new E.Link((d) => { sent[d[4]] = (sent[d[4]] || 0) + 1; out.send(d); }, {
    timeout: 300, onTimeout: () => ev.timeouts++, onUnknown: (f) => ev.unknown.push(f),
    onPush: (f) => ev.pushes.push({ ...f, pending: link.cur ? link.cur.cmd : 0 }), ...linkOpt });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  return { m, link, rq, sent, ev, done: () => { link.close(); m.stop(); } };
}
const emptyStep = { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 };

async function editorLibrarian() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  await rq(E.req.track(1));                        /* (Fel-Tracker: SID BASS with ACID is track 2 since the rhythm section came first) */
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  ok(keys[6] === "PIT" && keys[13] === "PIT#2" && keys[info.pe0] === "E0" && new Set(keys).size === keys.length, "librarian: parameter keys unique (label#n, E0..E7)");

  const b = await E.bank.list(rq);
  ok(b.total === 32 && b.slots.length === 32 && b.slots[1].used && b.slots[1].name === "VOX PAD" && !b.slots[3].used
    && b.slots[31].slot === 31, "librarian: UP_LIST, 32 slots in 2 frames");

  const cap = (await E.capturePatch(rq, info, "acid test")).patch;
  ok(cap.engine === 24 && cap.p.length === info.pcount && cap.pattern && cap.pattern[0][0] === 45 && cap.pattern[0][1] === 1,
    "librarian: capture = DUMP + first 16 steps");
  let rc = await E.bank.put(rq, 10, cap);
  const g = await E.bank.get(rq, info, 10);
  ok(rc === 0 && g.used && g.name === "acid test" && g.engine === 24 && eq(g.p, cap.p) && js(g.pattern) === js(cap.pattern),
    "librarian: UP_PUT -> UP_GET round trip");
  const bad = E.parse[C.UP_PUT](await rq(E.req.upPut(60, cap), { timeout: 2500, retries: 0 }));
  ok(bad.rc === 1, "librarian: UP_PUT to a slot past the bank -> rc 1");
  ok(E.upName("") === "PATCH" && E.upName("abcdefghijklmnop") === "abcdefghijkl" && E.upName("Bäss") === "Bss", "librarian: device names (ASCII, 1..12)");

  await rq(E.req.set(0, 1, 33));
  rc = await E.bank.store(rq, 11, "STORED");
  const g2 = await E.bank.get(rq, info, 11);
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && g2.name === "STORED" && g2.engine === d1.engine && eq(g2.p, d1.p) && g2.p[1] === 33, "librarian: UP_STORE keeps the current sound");

  /* another engine, an empty sequencer, then UP_LOAD brings the sound back, not the stored pattern */
  await rq(E.req.preset(33, 0));
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  await rq(E.req.set(0, 17, 3));                 /* the track's own: ARP MODE */
  rc = await E.bank.load(rq, 10);
  const d2 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  ok(rc === 0 && d2.engine === 24 && eq(d2.p.filter((_, i) => i !== 17), cap.p.filter((_, i) => i !== 17)) && d2.p[17] === 3
    && st.every((x) => !x.n) && cap.pattern, "librarian: UP_LOAD applies the sound only (steps and the track's ARP stay)");
  await rq(E.req.set(0, 17, 0));
  for (let i = 0; i < 16; i++) await rq(E.req.stepSet(i, E.stepsFromPattern(cap.pattern)[i]));   /* (for the captures below) */

  rc = await E.bank.erase(rq, 11);
  const b2 = await E.bank.list(rq);
  const rcEmpty = await E.bank.load(rq, 11);
  ok(rc === 0 && !b2.slots[11].used && b2.slots[10].used && rcEmpty === 1, "librarian: UP_ERASE, UP_LOAD of an empty slot -> rc 1");

  /* audition: a WASP patch that holds a pattern; the sound only, the sequence (with notes) stays */
  const bass = await E.bank.get(rq, info, 2);   /* WASP RESO BASS */
  await rq(E.req.stepSet(20, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await rq(E.req.set(0, 29, 24));               /* the track's own: LEN 24, ARP MODE 2 */
  await rq(E.req.set(0, 17, 2));
  await rq(E.req.set(0, 81, 1));                /* CHRD DIA3: the track's too */
  const seqBefore = [];
  for (let i = 0; i < 24; i++) seqBefore.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const flashBefore = js(m.state.bank);
  await E.auditionPatch(rq, info, bass, { gEng: 20 });
  const d3 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st3 = [];
  for (let i = 0; i < 24; i++) st3.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const own = new Set([0, 39, 40, ...Array.from({ length: 16 }, (_, i) => 17 + i), 45, 46, 47, 48, 81, 82]);
  ok(d3.engine === bass.engine && eq(d3.p.filter((_, i) => !own.has(i)), bass.p.filter((_, i) => !own.has(i)))
    && d3.p[29] === 24 && d3.p[81] === 1 && d3.p[17] === 2 && js(st3) === js(seqBefore) && st3[20].notes[0] === 50 && js(m.state.bank) === flashBefore,
    "librarian: audition = G_ENGSEL + SETs of the sound; steps, LEN and ARP stay; no flash write");
  /* the next audition: the same rule */
  const bell = await E.bank.get(rq, info, 1);
  await E.auditionPatch(rq, info, bell, { gEng: 20 });
  const s20 = E.parse[C.STEP_GET](await rq(E.req.stepGet(20)));
  const d4 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(d4.engine === bell.engine && bell.engine === 21 && s20.notes[0] === 50 && d4.p[29] === 24 && eq(d4.p.filter((_, i) => !own.has(i)), bell.p.filter((_, i) => !own.has(i))),
    "librarian: a second audition keeps the sequence too");
  await rq(E.req.set(0, 29, 16));
  await rq(E.req.set(0, 17, 0));
  await rq(E.req.set(0, 81, 0));

  /* ties as the firmware keeps them: no note; a rest has no flags */
  const tied = [[60, 1], [61, 4], [0, 4], [0, 3], [64, 2]];
  const norm = E.patternFromSteps(E.stepsFromPattern(tied));
  rc = await E.bank.put(rq, 12, { ...cap, name: "TIES", pattern: tied });
  const g3 = await E.bank.get(rq, info, 12);
  ok(rc === 0 && js(norm.slice(0, 5)) === js([[60, 1], [0, 4], [0, 4], [0, 0], [64, 2]]) && js(g3.pattern) === js(norm),
    "librarian: pattern ties / rests normalised like the firmware");
  rc = await E.bank.store(rq, 13, "");
  ok(rc === 0 && (await E.bank.get(rq, info, 13)).name === `${info.engines[d4.engine]} 14`, "librarian: UP_STORE with no name -> automatic name");

  /* library files */
  const ctx = { keys, engines: info.engines, firmware: info.version, pe0: info.pe0 };
  const pts = [cap, { ...bass, engineName: info.engines[bass.engine], tags: ["bass", "device"] }];
  const file = JSON.parse(JSON.stringify(E.libraryFile("library", pts, ctx)));
  ok(file.format === "felucca-library" && file.version === 1 && file.pCount === 104 && file.paramLabels.length === 104 && file.paramLabels[81] === "CHRD" && file.paramLabels[82] === "VOIC" &&
    file.paramLabels[83] === "KICK" && file.paramLabels[90] === "BELL" && file.paramLabels.slice(91, 96).join() === "SYNC,TRIG,POL,QNTZ,SPRD" &&
    file.engines.length === 61,
    "library file: versioned, with P_COUNT, labels and engines");
  const back = E.readLibraryFile(file, ctx);
  ok(back.patches.length === 2 && !back.skipped && eq(back.patches[0].p, cap.p) && eq(back.patches[1].p, bass.p)
    && js(back.patches[1].pattern) === js(bass.pattern) && back.patches[1].tags.join() === "bass,device" && back.patches[1].engineName === "WASP",
    "library file: write -> read round trip");
  /* a future firmware: one more parameter at id 5, engines in another order and one of them gone */
  const keys2 = [...keys.slice(0, 5), "NEW", ...keys.slice(5)];
  const eng2 = ["WASP", "SID", "WAVE"];
  const fut = E.readLibraryFile(file, { keys: keys2, engines: eng2 });
  const p0 = fut.patches[0].p;
  ok(fut.patches.length === 2 && p0.length === 105 && p0[5] === null && p0[6] === cap.p[5] && p0[104] === cap.p[103]
    && fut.patches[0].engine === 1 && fut.patches[1].engine === 0, "library file: other ids / engine order mapped by label and name");
  const lost = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engineName: "WAVETABLE" }] }, ctx);
  ok(lost.patches.length === 0 && lost.skipped === 1, "library file: a patch for an unknown engine is skipped");
  const bankFile = E.libraryFile("bank", [{ ...g, engineName: "SID", slot: 10 }], ctx);
  ok(bankFile.kind === "bank" && bankFile.patches[0].slot === 10 && E.readLibraryFile(bankFile, ctx).patches[0].slot === 10, "library file: bank export keeps slot numbers");
  {   /* files from the 69-parameter firmware (P_E0 61): the engine's 8 land on E0..E7 (96..103), FM op ENV, the
         chord keys and the lane levels stay unset */
    const p69 = [...cap.p.slice(0, 61), ...cap.p.slice(96, 104)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p69 }, ctx).patches[0].p;
    const keys69 = [...keys.slice(0, 61), ...keys.slice(96)];
    const lp = E.readLibraryFile({ ...file, pCount: 69, paramLabels: keys69, patches: [{ ...file.patches[0], params: p69 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p69 }] }, ctx).patches[0].p;
    const good = (q) => q.length === 104 && eq(q.slice(0, 61), cap.p.slice(0, 61)) && eq(q.slice(96), cap.p.slice(96)) && q.slice(61, 96).every((v) => v === null);
    ok(good(sp) && good(lp) && good(nk), "library file: 69-parameter files (patch, labelled, unlabelled) map the engine's 8 to 96..103");
  }
  {   /* files from the 89-parameter firmware (P_E0 81, before the chord keys): the engine's 8 land on 96..103, the FM
         op ENV in place, CHRD VOIC unset (left as the track has them), the lane levels unset */
    const p89 = [...cap.p.slice(0, 81), ...cap.p.slice(96, 104)];
    const keys89 = [...keys.slice(0, 81), ...keys.slice(96)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p89 }, ctx).patches[0].p;
    const lp = E.readLibraryFile({ ...file, pCount: 89, pE0: 81, paramLabels: keys89, patches: [{ ...file.patches[0], params: p89 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p89 }] }, ctx).patches[0].p;
    const good = (q) => q.length === 104 && eq(q.slice(0, 81), cap.p.slice(0, 81)) && q.slice(81, 96).every((v) => v === null) && eq(q.slice(96), cap.p.slice(96));
    ok(keys89[81] === "E0" && good(sp) && good(lp) && good(nk),
      "library file: 89-parameter files (patch, labelled, unlabelled) map the engine's 8 to 96..103, the chord keys unset");
  }
  {   /* files from 1.0.x (91 parameters, P_E0 83, before the DRUM lane levels): the engine's 8 land on 96..103, the
         chord keys in place, the lane levels unset (a sound load sets them to 100 %) */
    const p91 = [...cap.p.slice(0, 83), ...cap.p.slice(96, 104)];
    const keys91 = [...keys.slice(0, 83), ...keys.slice(96)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p91 }, ctx).patches[0].p;
    const lp = E.readLibraryFile({ ...file, pCount: 91, pE0: 83, paramLabels: keys91, patches: [{ ...file.patches[0], params: p91 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p91 }] }, ctx).patches[0].p;
    const good = (q) => q.length === 104 && eq(q.slice(0, 83), cap.p.slice(0, 83)) && q.slice(83, 96).every((v) => v === null) && eq(q.slice(96), cap.p.slice(96));
    ok(keys91[83] === "E0" && good(sp) && good(lp) && good(nk),
      "library file: 91-parameter files (1.0.x: patch, labelled, unlabelled) map the engine's 8 to 96..103, the lane levels unset");
  }
  const old = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 24, preset: 0, engineName: "SID", presetName: "SID BASS", p: d2.p, steps: E.stepsFromPattern(cap.pattern) }, ctx);
  ok(old.patches.length === 1 && eq(old.patches[0].p, d2.p) && js(old.patches[0].pattern) === js(cap.pattern), "library file: reads the old \"Save to file\" format");
  let threw = false;
  try { E.readLibraryFile({ format: "something" }, ctx); } catch (e) { threw = true; }
  ok(threw, "library file: unknown format -> error");
  done();
}

/* v5: the DRUM grid. Steps carry lane hits and accents (3 bytes after vel); a user preset of a DRUM track holds a
   16-step grid (UP_GET / UP_PUT kind 1); the steps page writes them as lane names */
async function editorGrid() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const h = E.parseHits("kick >snare HATCL cym");
  ok(h && h.hit === (1 | 2 | 8 | 128) && h.acc === 2 && E.hitsText(h.hit, h.acc) === "KICK >SNARE HATCL BELL" &&
     E.parseHits("KICK FOO") === null && E.parseHits(" ").hit === 0, "grid: hits as text (lanes, > accent, CYM / CONGA aliases)");
  let s = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [41, 0, 0, 0], time: 0, flags: 0, vel: 100, hit: 0x81, acc: 0x80 })));
  ok(s.hit === 0x81 && s.acc === 0x80 && s.notes[0] === 41, "grid: STEP_SET / STEP_GET carry the hits (lane 8 too)");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [42, 0, 0, 0], time: 0, flags: 0, vel: 100 })));
  ok(s.hit === 0x81 && s.notes[0] === 42, "grid: a STEP_SET without hits (an older editor) keeps them");
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 4, { n: 0, notes: [0, 0, 0, 0], time: 0, flags: 0, vel: 0, hit: 0x12, acc: 0x02 })));
  ok(w.hit === 0x12 && w.acc === 0x02 && E.parse[C.TRACK_STEP]([2, 4, 0, 0, 0, 0, 0, 0, 0, 0]).hit === 0,
    "grid: TRACK_STEP carries the hits (an older firmware's reply: none)");
  await rq(E.req.preset(28, 0));                   /* the selected track a DRUM kit, its 16 steps a grid */
  for (let i = 0; i < 16; i++) {
    await rq(E.req.stepSet(i, { n: i === 2 ? 1 : 0, notes: [i === 2 ? 49 : 0, 0, 0, 0], time: i % 4 === 0 || i === 2 ? 0 : 2, flags: 0,
      vel: 0, hit: i % 4 === 0 ? 1 | (i === 0 ? 128 : 0) : 0, acc: i === 0 ? 128 : 0 }));
  }
  const cap = await E.capturePatch(rq, info, "MY BEAT");
  ok(cap.patch.grid && !cap.patch.pattern && cap.patch.grid[0][0] === 0x81 && cap.patch.grid[0][1] === 0x80 &&
     cap.patch.grid[2][0] === 0x80 && cap.patch.grid[1][0] === 0 && cap.patch.grid[4][0] === 1,
     "grid: capture of a DRUM track = its grid (a crash 49 on BELL)");
  const rc = await E.bank.put(rq, 14, cap.patch);
  const g = await E.bank.get(rq, info, 14);
  ok(rc === 0 && g.grid && !g.pattern && JSON.stringify(g.grid) === JSON.stringify(cap.patch.grid), "grid: UP_PUT kind 1 -> UP_GET round trip");
  const put = E.req.upPut(14, cap.patch), saved = JSON.stringify(m.state.bank[14]);
  const base = put[1].length - 17;
  for (const bytes of [put[1].slice(0, -1), [...put[1], 0], [...put[1].slice(0, base), 2, ...Array(16).fill(0)]]) {
    const rejected = E.parse[C.UP_PUT](await rq([C.UP_PUT, bytes]));
    ok(rejected.rc === 1 && JSON.stringify(m.state.bank[14]) === saved,
       "grid: malformed preset extension rejected without changing saved data");
  }
  const ctx = { keys: null, engines: info.engines };
  const back = E.readLibraryFile(JSON.parse(JSON.stringify(E.libraryFile("bank", [{ ...g, slot: 14 }], ctx))), ctx);
  ok(JSON.stringify(back.patches[0].grid) === JSON.stringify(g.grid) && !back.patches[0].pattern, "grid: a library file keeps the grid");
  await rq(E.req.upStore(15, "STORED"));
  const g2 = await E.bank.get(rq, info, 15);
  ok(g2.grid && g2.grid[0][0] === 0x81 && g2.grid[0][1] === 0x80, "grid: UP_STORE of a DRUM track stores its grid");
  const p3 = await E.bank.get(rq, info, 2);
  ok(p3.pattern && p3.grid === null, "grid: a note pattern stays a pattern (kind 0)");
  done();
}

/* RATCH: a step's ratchet (1..4) after its chance, INFO 52 01 04; older firmware (no tag): none, replies read x1 */
async function editorRatchet() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  if (info.ratchet === undefined) {               /* (an editor without it) */
    console.log(`${"ratchet (this editor has none)".padEnd(64)} skip`);
    done();
    return;
  }
  let s = E.parse[C.STEP_SET](await rq(E.req.stepSet(6, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 1, vel: 100, chance: 70, ratchet: 3 })));
  ok(info.ratchet === 4 && s.ratchet === 3 && s.chance === 70 && s.flags === 1, "ratchet: INFO 52 01 04, STEP_SET x3 after the chance");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(6, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 100, chance: 70 })));
  ok(s.ratchet === 3 && s.notes[0] === 62, "ratchet: a STEP_SET without it (an older editor) keeps the step's");
  ok(E.req.stepSet(6, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 0, vel: 100, ratchet: 2 })[1].length === 9,
    "ratchet: never sent without the chance before it");
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(1, 2, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90, chance: 100, ratchet: 4 })));
  ok(w.ratchet === 4 && E.parse[C.TRACK_STEP](await rq(E.req.trackStep(1, 2))).ratchet === 4, "ratchet: TRACK_STEP x4 on another track");
  const fw = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 27, 0x53, 1, 3, 0x52, 1, 4]);
  const f105 = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 0, 0x53, 1, 3,
    0x50, 1, 3, 0x4E, 1, 12, 0x52, 1, 4]);         /* 1.0.5: FM6 v2, MENU settings, then RATCH */
  ok(f105.ratchet === 4 && f105.syncCaps === 3, "ratchet: INFO 52 01 04 after 50 01 03 and 4E 01 count (a Felucca firmware's)");
  const old = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 27, 0x53, 1, 3]);
  ok(fw.ratchet === 4 && fw.syncCaps === 3 && old.ratchet === 0 && E.parse[C.STEP_GET]([1, 1, 60, 0, 0, 0, 0, 0, 96, 0, 0, 0, 100]).ratchet === 1,
    "ratchet: the firmware's INFO trailer; 1.0's has none and its steps read x1");
  const steps = E.stepsFromPattern([[60, 1 | 2 << 3], [0, 4], [62, 3 << 3], ...Array(13).fill([0, 0])]);
  ok(steps[0].ratchet === 3 && steps[0].flags === 1 && steps[2].ratchet === 4 && steps[1].time === 1 &&
     JSON.stringify(E.patternFromSteps(steps).slice(0, 3)) === JSON.stringify([[60, 17], [0, 4], [62, 24]]),
     "ratchet: a user preset's pattern keeps it in its flags (8 | 16)");
  done();
  const o = attachMock({ noSync: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  s = E.parse[C.STEP_GET](await o.rq(E.req.stepGet(0)));
  ok(oi.ratchet === 0 && s.ratchet === 1 && s.chance === 100, "ratchet: firmware without it: no tag, steps x1");
  o.done();
}

async function editorLive() {
  const C = E.CMD;
  const { m, link, rq, sent, ev, done } = attachMock({ watchMs: 250 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(await E.startWatch(rq), "live: WATCH on");

  /* a push between a request and its reply */
  const pend = rq(E.req.dump());
  const kn = m.sim.knob(9, 4);
  const dump = E.parse[C.DUMP](await pend, info);
  const ch = ev.pushes.find((f) => f.cmd === C.CHANGED);
  const cv = ch && E.parse[C.CHANGED](ch.a);
  ok(dump.p.length === 104 && ch && ch.pending === C.DUMP && cv.scope === 0 && cv.id === 9 && cv.value === kn.value && !ev.unknown.length,
    "live: CHANGED while DUMP waits -> push handler, reply still matched");
  const rl = m.sim.reload();
  m.sim.step(3);
  const pend2 = rq(E.req.stepGet(7));
  const s7 = E.parse[C.STEP_GET](await pend2);
  await sleep(10);
  const r = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(s7.index === 7 && r && E.parse[C.RELOAD](r.a).preset === rl.preset && sc && E.parse[C.STEP_CHANGED](sc.a).index === 3,
    "live: RELOAD and STEP_CHANGED routed");
  const nr = ev.pushes.filter((f) => f.cmd === C.RELOAD).length;
  await rq(E.req.preset(1, 2));
  await rq(E.req.stepSet(9, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await sleep(10);
  ok(info.syncCaps === 3 && ev.pushes.filter((f) => f.cmd === C.RELOAD).length === nr && !ev.pushes.some((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9),
    "live: INFO 53 01 03, no RELOAD echo of an editor PRESET, nothing after its STEP_SET");
  {
    const o = attachMock({ watchMs: 250, noSync: true });
    const oi = E.parse[C.INFO](await o.rq(E.req.info()));
    await E.startWatch(o.rq);
    await o.rq(E.req.preset(24, 2));
    await sleep(10);
    ok(oi.syncCaps === 0 && o.ev.pushes.filter((f) => f.cmd === C.RELOAD).length === 1,
      "live: firmware before the sync tag echoes the editor's PRESET as RELOAD (the editor skips it)");
    o.done();
    const fw = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 27, 0x53, 1, 3]);
    ok(fw.backupCaps === 3 && fw.syncCaps === 3,
      "live: the INFO trailer of Felucca 1.0.2: backup, FM6's tag (stepped over), then the sync tag");
    const f3 = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 0, 0x53, 1, 3, 0x50, 1, 3]);
    ok(f3.syncCaps === 3 && f3.backupCaps === 3, "live: the INFO trailer of Felucca 1.0.3: FM6 v2 after the sync tag (stepped over)");
  }

  /* PING keeps the watch on; without requests it ends */
  for (let i = 0; i < 4; i++) { await sleep(120); await rq(E.req.ping()); }
  let n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0 + 1, "live: PING keeps WATCH on");
  await sleep(320);
  n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0, "live: WATCH ends by itself without requests");

  /* a slider drag: 40 values at once -> one SET in flight + one coalesced, the last value wins */
  const before = sent[C.SET] || 0;
  const all = [];
  for (let v = 0; v < 40; v++) all.push(rq(E.req.set(0, 9, v * 3), { key: "0:9" }));
  await Promise.all(all);
  ok((sent[C.SET] || 0) - before === 2 && m.state.p[9] === 117 && link.idle, "live: drag SETs coalesce (2 frames for 40 values, latest kept)");
  ok(ev.timeouts === 0, "live: no timeouts");
  done();

  /* older firmware: no reply to WATCH -> false without a "no reply" message; no bank */
  const o = attachMock({ legacy: true });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const w = await o.rq(E.req.watch(1), { timeout: 60, retries: 0, quiet: true }).then(() => true, () => false);
  const sw = await E.startWatch(o.rq);
  const bl = await E.bank.list((rr, oo) => o.rq(rr, { ...oo, timeout: 60, quiet: true })).then(() => "listed", (e) => e.message);
  ok(!w && !sw && /^timeout/.test(bl) && o.ev.timeouts === 0, "live: older firmware -> WATCH unanswered (fall back to polling), no bank");
  o.done();
}

async function preferenceReplies() {
  const C = E.CMD, unknown = [];
  const link = new E.Link(() => {}, { onUnknown: f => unknown.push(f) });
  for (const [request, stale, reply] of [
    [E.req.uiSet(0, 4), [0, 1, 1], [0, 0, 4]],
    [E.req.uiSet(0, 4), [0, 0, 3], [0, 0, 4]],
    [E.req.favGet(0, 2, 1), [0, 0, 3, 64, 1, 1], [0, 0, 2, 64, 1, 1]],
    [E.req.favSet(0, 2, true), [4, 0, 2, 64, 0], [4, 0, 2, 64, 1]],
  ]) {
    const pending = link.request(request);
    link.receive(E.frame(request[0], stale));
    ok(link.cur !== null, 'preferences: a reply for another setting or favorite cannot finish the request');
    link.receive(E.frame(request[0], reply));
    ok(eq(await pending, reply), 'preferences: the matching reply completes the request');
  }
  ok(unknown.length === 4 && link.idle, 'preferences: stale replies are routed away without leaving requests busy');
  link.close();
}

/* ------------------------------------------------------ editor protocol v3: tracks --- */
async function editorTracks() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(info.ntrk === 4 && tr.sel === 0 && tr.ntrk === 4 && tr.tracks[0].engine === 28 && tr.tracks[0].preset === 0
    && tr.tracks[1].engine === 24 && tr.tracks[2].engine === 21, "tracks: INFO NTRK, TRACK lists 4 parts (track 1: KIT FM KIT)");
  /* the v1 commands follow the selected track */
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const t1 = E.parse[C.TRACK](await rq(E.req.track(2)));
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(0, 1, 77));
  const td0 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(0)), info);
  const td1 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(t1.sel === 2 && d1.engine === 21 && d0.engine === 28 && td1.p[1] === 77 && td0.p[1] === d0.p[1] && td0.p[1] !== 77,
    "tracks: TRACK selects; DUMP / SET act on it, TRACK_DUMP reads any track");
  /* steps of a track that is not selected */
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, 5, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 1, vel: 99 })));
  const g2 = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, 5)));
  const s1 = E.parse[C.STEP_GET](await rq(E.req.stepGet(5)));
  ok(w.track === 3 && g2.n === 2 && g2.notes[1] === 67 && g2.vel === 99 && s1.n === 0, "tracks: TRACK_STEP set / get on another track");
  /* level / mute, track 4 as any other */
  const mx = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0, 90, 1)));
  const mxd = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(3, 64, 0)));
  const mx2 = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0)));
  const tr2 = E.parse[C.TRACK](await rq(E.req.track()));
  ok(mx.level === 90 && mx.mute === 1 && mx2.level === 90 && mxd.level === 64 && m.state.tracks[3].p[0] === 64 && tr2.tracks[3].level === 64
    && tr2.tracks[0].level === 90 && tr2.tracks[0].mute === 1, "tracks: TRACK_MIX level / mute (track 4: its own LEVEL)");
  /* the kit's track has a sound: stored and loaded as any other */
  await rq(E.req.track(0));
  const dd = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const us = E.parse[C.UP_STORE](await rq(E.req.upStore(20, "X"), { timeout: 2500, retries: 0 }));
  const ul = E.parse[C.UP_LOAD](await rq(E.req.upLoad(1), { timeout: 2500, retries: 0 }));
  const dl = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(dd.engine === 28 && us.rc === 0 && ul.rc === 0 && dl.engine === 21, "tracks: track 1 selected -> DUMP engine KIT, UP_STORE / UP_LOAD rc 0");
  /* pushes carry the selected track */
  await rq(E.req.track(0));
  ok(await E.startWatch(rq), "tracks: WATCH on");
  m.sim.track(2);
  m.sim.step(4);
  await sleep(10);
  const rl = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(rl && E.parse[C.RELOAD](rl.a).track === 2 && sc && E.parse[C.STEP_CHANGED](sc.a).track === 2 && E.parse[C.STEP_CHANGED](sc.a).index === 4,
    "tracks: RELOAD / STEP_CHANGED carry the selected track");
  /* projects keep all four tracks */
  await rq(E.req.project(1, 3), { timeout: 4000, retries: 0 });
  await rq(E.req.trackStep(3, 5, { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 }));
  await rq(E.req.track(0));
  await rq(E.req.project(0, 3), { timeout: 4000, retries: 0 });
  const back = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, 5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track())).sel;
  ok(back.n === 2 && back.notes[0] === 60 && sel === 2, "tracks: PROJECT save / load keeps every track and the selection");
  /* older firmware: no NTRK in INFO, no RELOAD track byte */
  const o = attachMock({ legacy: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  ok(oi.ntrk === 0 && E.parse[C.RELOAD]([0, 4]).track === 0 && E.parse[C.STEP_CHANGED]([7]).track === 0, "tracks: older firmware parses (no tracks)");
  o.done();
  done();
}

/* ------------------------------------------------ editor v3: the mixer (Tracks tab) --- */
async function editorMixer() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const PAN = 39, MUTE = 40;
  const m0 = await E.mixer.read(rq, info, { pan: PAN });
  ok(m0.ntrk === 4 && m0.tracks.length === 4 && m0.tracks[2].pan === -24 && m0.tracks[3].pan === 24 && m0.tracks[0].engine === 28
    && m0.tracks.every((x) => Number.isInteger(x.level) && (x.mute === 0 || x.mute === 1)), "mixer: read = TRACK + pan of every track (TRACK_DUMP)");
  /* level / mute of tracks that are not selected (clamped) */
  const a = await E.mixer.setMix(rq, 2, 70, 1);
  const b = await E.mixer.setMix(rq, 3, 200, 0);
  const m1 = await E.mixer.read(rq, info, { pan: PAN });
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(a.level === 70 && a.mute === 1 && b.level === 127 && m.state.tracks[3].p[0] === 127 && m1.tracks[3].level === 127
    && m1.tracks[2].level === 70 && m1.tracks[2].mute === 1
    && td2.p[0] === 70 && td2.p[MUTE] === 1 && m1.sel === 0, "mixer: TRACK_MIX level / mute round trip (clamped)");
  /* pan of another track: selected for the SET, the selection put back, no RELOAD pushed */
  ok(await E.startWatch(rq), "mixer: WATCH on");
  const pushes = ev.pushes.length;
  /* (the v3 path: firmware 0.8 has no TRACK_PARAM) */
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40);
  const p0 = await E.mixer.setPan(rq, 0, 0, PAN, 99);
  await sleep(10);
  const m2 = await E.mixer.read(rq, info, { pan: PAN });
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(p2 === -40 && p0 === 63 && m2.sel === 0 && m2.tracks[2].pan === -40 && m2.tracks[0].pan === 63 && d0.p[PAN] === 63 && m2.tracks[3].pan === 24
    && ev.pushes.length === pushes, "mixer: pan of any track via SET (other track selected for a moment, then back; no push)");
  /* the device's TRACKS page: level of the selected track pushes CHANGED; REC arm shows in TRACK */
  m.sim.level(33);
  m.sim.arm(1);
  await sleep(10);
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  const m3 = await E.mixer.read(rq, info, { pan: PAN });
  ok(ch && ch.scope === 0 && ch.id === 0 && ch.value === 33 && m3.tracks[0].level === 33 && m3.tracks[1].armed === 1 && m3.tracks[0].armed === 0,
    "mixer: device-side level (CHANGED push) and REC arm read back");
  await rq(E.req.track(3));
  m.sim.level(90);
  await sleep(10);
  const chd = E.parse[C.CHANGED](ev.pushes.filter((f) => f.cmd === C.CHANGED).pop().a);
  ok(chd.scope === 0 && chd.id === 0 && chd.value === 90, "mixer: track 4's level on the device pushes its LEVEL (P_LEVEL)");
  /* track 1's demo beat: the kit's lane hits and accents */
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.TRACK_STEP](await rq(E.req.trackStep(0, i))));
  ok(st.every((s) => s.n === 0) && st[0].hit === 1 && st[0].acc === 1 && st[1].hit === 8
    && st[4].hit === 2 && st[4].acc === 2 && st[14].hit === 16,
    "mixer: the mock's track 1 holds the BEAT grid (KICK CHH ... SNARE ... OHH)");
  ok(JSON.stringify(E.parseNotes("C4 E4 G4")) === "[60,64,67]" && JSON.stringify(E.parseNotes("36 38")) === "[36,38]" && E.parseNotes("KICK") === null,
    "mixer: notes parse as numbers or note names (no GM drum names)");
  done();
}

/* ------------------------------------- editor v4: TRACK_PARAM and TRACK_CHANGED --- */
async function editorTrackParam() {
  const C = E.CMD, PAN = 39, MUTE = 40;
  const { m, rq, sent, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const w1 = E.parse[C.WATCH](await rq(E.req.watch(1)));
  m.sim.param(2, PAN, 11);
  await sleep(10);
  ok(w1.on === 1 && !ev.pushes.some((f) => f.cmd === C.TRACK_CHANGED), "v4: WATCH 1 answers 1 as before (no TRACK_CHANGED pushes)");
  ok(E.parse[C.WATCH](await rq(E.req.watch(3))).on === 3, "v4: WATCH 3 -> 3 (TRACK_PARAM / TRACK_CHANGED known)");
  ok(await E.startWatch(rq) === 3, "the editor's WATCH 3 -> 3 (TRACK_CHANGED; no TRACKER_CHANGED: the tracker is gone)");
  const tracks0 = sent[C.TRACK] || 0, pushes = ev.pushes.length;
  const g = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(3, PAN)));   /* (track 4, the lead: +24 at power-on) */
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40, true);
  const p1 = await E.mixer.setPan(rq, 1, 0, PAN, 99, true);
  const alg = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(2, info.pe0, 150)));   /* track 3 is VOSIM FORMANT PAD: F1 0..127 */
  const lv = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(3, 0, -5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track()));
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  await sleep(10);
  ok(g.track === 3 && g.id === PAN && g.value === 24 && p2 === -40 && p1 === 63 && alg.value === 127 && lv.value === 0 && td2.p[PAN] === -40
    && sel.sel === 0 && (sent[C.TRACK] || 0) === tracks0 + 1 && ev.pushes.length === pushes,
    "v4: TRACK_PARAM get / set on other tracks (clamped as SET, selection kept, no push)");
  const bad = await rq(E.req.trackParam(4, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(bad === "none", "v4: TRACK_PARAM of track 5: no reply");
  /* device-side changes: CHANGED for the selected track, TRACK_CHANGED for the others */
  m.sim.param(2, PAN, 30);
  m.sim.param(3, MUTE, 1);
  m.sim.param(0, PAN, -7);
  await sleep(10);
  const tc = ev.pushes.filter((f) => f.cmd === C.TRACK_CHANGED).map((f) => E.parse[C.TRACK_CHANGED](f.a));
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  ok(tc.length === 2 && tc[0].track === 2 && tc[0].id === PAN && tc[0].value === 30 && tc[1].track === 3 && tc[1].id === MUTE && tc[1].value === 1
    && ch && ch.scope === 0 && ch.id === PAN && ch.value === -7 && !ev.unknown.length, "v4: TRACK_CHANGED pushes for the other tracks, CHANGED for the selected one");
  done();
  /* firmware 0.8 (v3): WATCH 3 answers 1, TRACK_PARAM unanswered: the editor keeps the select / restore path */
  const o = attachMock({ v3: true, watchMs: 1000 });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const on = await E.startWatch(o.rq);
  const tp = await o.rq(E.req.trackParam(1, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const pv = await E.mixer.setPan(o.rq, 2, 0, PAN, 5, false);
  ok(on === 1 && tp === "none" && pv === 5 && o.ev.timeouts === 0 && !o.ev.unknown.length, "v4: v3 firmware -> WATCH 1, no TRACK_PARAM (pan by select / restore)");
  o.done();
}

/* ------------------------------------------------- editor tabs and strings --- */
async function editorSong() {
  const m = E.makeMockDevice({ auto: false });
  const link = new E.Link((d) => [...m.access.outputs.values()][0].send(d), { timeout: 100 });
  [...m.access.inputs.values()][0].onmidimessage = (e) => link.receive(e.data);
  const rq = (r) => link.request(r);
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.chainRows === 16, "SONG: INFO capability appended");
  const ask = async (op = 0, rows = []) => E.parse[C.SONG](await rq(E.req.song(op, rows)));
  const rows = [{ slot: 0, repeat: 4 }, { slot: 1, repeat: 2 }, { slot: 0, repeat: 1 }];
  let r = await ask(1, rows);
  ok(r.rc === 0 && JSON.stringify(r.rows) === JSON.stringify(rows), "SONG: rows and repeats round trip");
  r = await ask(2);
  ok(r.rc === 4 && !r.playing, "SONG: missing source rejected before PLAY");
  await rq(E.req.project(1, 1));
  r = await ask(2);
  ok(!r.rc && r.playing && r.row === 0 && r.remaining === 4, "SONG: PLAY starts from row 1");
  r = await ask(1, [{ slot: 0, repeat: 1 }]);
  ok(r.rc === 2 && r.count === 3, "SONG: editing during PLAY refused, rows kept");
  await ask(3);
  r = E.parse[C.SONG](await rq([C.SONG, [1, 1, 0, 0]]));
  ok(r.rc === 1 && r.count === 3, "SONG: zero repeats rejected without changing rows");
  r = E.parse[C.SONG](await rq([C.SONG, [1, 2, 0, 1]]));
  ok(r.rc === 1 && r.count === 3, "SONG: short row list rejected");
  await rq(E.req.project(1, 2));
  await ask(1, []);
  await rq(E.req.project(0, 2));
  r = await ask();
  ok(JSON.stringify(r.rows) === JSON.stringify(rows) && !r.playing, "SONG: project save/load recalls the chain");
  await ask(1, []);
  r = await ask(2);
  ok(r.rc === 1 && !r.playing, "SONG: empty chain does not PLAY");
  link.close(); m.stop();
  const old = E.makeMockDevice({ auto: false, v5: true });
  const l = new E.Link((d) => [...old.access.outputs.values()][0].send(d));
  [...old.access.inputs.values()][0].onmidimessage = (e) => l.receive(e.data);
  ok(E.parse[C.INFO](await l.request(E.req.info())).chainRows === 0, "SONG: older firmware has no capability");
  l.close(); old.stop();
}

/* Run the page's actual session helpers and connect-load path with delayed replies. */
async function editorSessions() {
  const state = html.slice(html.indexOf("let busy = 0;"), html.indexOf("let dragging", html.indexOf("let busy = 0;")));
  const requests = html.slice(html.indexOf("function assertSession(d)"), html.indexOf("/* the whole connect sequence */"));
  const loading = html.slice(html.indexOf("async function load(d)"), html.indexOf("async function getDescOrNull"));
  const library = html.slice(html.indexOf("async function libOp(fn)"), html.indexOf("const checkRc", html.indexOf("async function libOp(fn)")));
  let shown = 0, adopted, adoptStarted;
  const nodes = new Map();
  const noop = () => {};
  const S = vm.runInNewContext(`let dev = null, lastDump = 0, libBusy = false; ${state} ${requests} ${loading} ${library}
    ;({ beginBusy, resetBusy, sessionRequest, load, libOp,
       setDevice: d => { dev = d; }, count: () => busy })`, {
    Date, Error,
    $: id => { if (!nodes.has(id)) nodes.set(id, {}); return nodes.get(id); },
    CMD: { INFO: 1, PROJECT: 2, DUMP: 4 },
    parse: { 1: r => r, 2: () => ({ used: false }), 4: r => r },
    req: { info: () => "info", project: () => "project", dump: () => "dump" },
    paramKeys: noop, libAdopt: () => { adoptStarted?.(); return adopted || Promise.resolve(); },
    buildUI: () => { shown++; }, renderPanels: noop, loadSteps: noop,
    startWatch: async () => false, renderLive: noop, renderLib: noop,
    renderBank: noop, renderSysinfo: noop, sayK: noop, report: noop, syncPreferences: noop,
    seqTodo: () => false,
  });
  const oldRelease = S.beginBusy();
  S.resetBusy();
  const newRelease = S.beginBusy();
  oldRelease();
  ok(S.count() === 1, "editor: old completion cannot release new connection's busy count");
  newRelease(); newRelease(); oldRelease();
  ok(S.count() === 0, "editor: operation release is idempotent and never goes negative");

  let resolve;
  const d = { link: { request: () => new Promise(r => { resolve = r; }) } };
  S.setDevice(d);
  const stale = S.sessionRequest(d, "dump").then(() => null, e => e);
  S.setDevice({}); resolve({});
  ok((await stale)?.message === "closed", "editor: late reply from an unplugged device is rejected");
  let sent = 0;
  d.link.request = async () => { sent++; };
  const wrong = await S.sessionRequest(d, "write").then(() => null, e => e);
  ok(wrong?.message === "closed" && sent === 0, "editor: captured request cannot write through a replacement connection");

  const old = { link: { request: () => new Promise(r => { resolve = r; }) } };
  S.setDevice(old);
  const loadingOld = S.load(old).then(() => null, e => e);
  S.resetBusy(); S.setDevice({});
  const currentRelease = S.beginBusy();
  resolve({});
  ok((await loadingOld)?.message === "closed" && !shown && S.count() === 1,
     "editor: interrupted initial load cannot paint or unlock a new session");
  currentRelease();

  adopted = new Promise(r => { resolve = r; });
  const reachedAdoption = new Promise(r => { adoptStarted = r; });
  const next = { pdesc: [], gdesc: [], names: [], slotUsed: [], link: { request: async kind =>
    kind === "info" ? { pcount: 0, gcount: 0, nengines: 0 } : {} } };
  S.setDevice(next);
  const loadingNext = S.load(next).then(() => null, e => e);
  await reachedAdoption;
  S.resetBusy(); S.setDevice({}); resolve();
  ok((await loadingNext)?.message === "closed" && !shown && S.count() === 0,
     "editor: unplug during library adoption aborts the old load");
  adopted = null;
  S.setDevice(next);
  await S.load(next);
  ok(shown === 1 && S.count() === 0, "editor: a current connection still completes its initial load");

  const savedDevice = { link: { request: async () => { sent++; } } };
  sent = 0;
  S.setDevice(savedDevice);
  const storageWait = new Promise(r => { resolve = r; });
  const copying = S.libOp(async (device, request) => { await storageWait; await request("save"); });
  S.resetBusy(); S.setDevice({}); resolve();
  await copying;
  ok(!sent && S.count() === 0, "editor: library storage delay cannot send a save to a new device");
}

function editorTabs() {
  const tabs = [...html.matchAll(/<button role="tab" data-tab="(\w+)"/g)].map((x) => x[1]);
  const panels = [...html.matchAll(/<section class="panel" id="p-(\w+)" data-tab="(\w+)"/g)].map((x) => [x[1], x[2]]);
  const TABS = JSON.parse((/const TABS = (\[[^\]]*\]);/.exec(html) || [])[1] || "[]");
  ok(tabs.length === 6 && js(tabs) === js(TABS) && js(panels.map((x) => x[1])) === js(TABS) && panels.every(([a, b]) => a === b),
    `editor: ${tabs.length} tabs, one panel each (${tabs.join(" ")})`);
  ok(/localStorage\.setItem\(TAB_KEY/.test(html) && /try \{ localStorage/.test(html) && /history\.replaceState\([^)]*"#" \+ name\)/.test(html)
    && /addEventListener\("hashchange"/.test(html), "editor: last tab in localStorage (try/catch) and in the URL hash");
  /* every string key in both languages */
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const ja = new Set(Object.keys(TEXT.ja)), en = new Set(Object.keys(TEXT.en));
  const used = new Set([...html.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"|hint = "(\w+)"/g)].map((x) => x[1] || x[2] || x[3] || x[4]));
  for (const k of ["needDevice", "bankConnect", "bankNone", "selectedTrack", "selectTrack", "notesHelp", "live", "polling"]) used.add(k);
  const miss = [...used].filter((k) => !ja.has(k) || !en.has(k));
  const odd = [...ja].filter((k) => !en.has(k)).concat([...en].filter((k) => !ja.has(k)));
  ok(!miss.length && !odd.length, `editor: every string in ja and en (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", one language only " + odd : ""})`);
  /* the page script parses (the browser's view of it) */
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "editor: page script compiles" + (err ? ` (${err})` : ""));
  ok(!/#[0-9a-f]{3,6}\b/i.test(html.slice(html.indexOf("[hidden]") - 6000, html.indexOf("[hidden]")).replace(/:root[^}]*\}/g, "")),
    "editor: no colours beyond the black / white tokens in the new styles");
}

/* ------------------------------------------------- editor icons (Fukiai) --- */
function editorIcons() {
  const blk = html.slice(html.indexOf("const GLYPH = {"), html.indexOf("};", html.indexOf("const GLYPH = {")));
  const names = new Set([...blk.matchAll(/(\w+): 0x[0-9A-F]{4}/g)].map((m) => m[1]));
  const used = new Set([...html.matchAll(/data-ic="(\w+)"|ic: "(\w+)"|: "((?:waveform|function|symbol|control|port|ui|note)_\w+)"/g)].map((m) => m[1] || m[2] || m[3]));
  const missing = [...used].filter((n) => !names.has(n));
  ok(names.size > 0 && !missing.length, `editor: every icon name is in GLYPH (${used.size} used${missing.length ? ", missing " + missing : ""})`);
  const ttf = existsSync(join(HERE, "fukiai.ttf")) && readFileSync(join(HERE, "fukiai.ttf"));
  ok(ttf && ttf.readUInt32BE(0) === 0x00010000 && existsSync(join(HERE, "FUKIAI-LICENSE.txt")) && html.includes('href="FUKIAI-LICENSE.txt"'),
    "editor: fukiai.ttf and FUKIAI-LICENSE.txt next to editor.html");
  ok(/html:not\(\.fk\) \.ic \{ display: none; \}/.test(html) && html.includes('classList.add("fk")'), "editor: icons hidden until the font has loaded");
}


/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  const pkg = join(FW_ROOT, "build/felucca.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_9\d\d$/.test(productOf(raw)), "fm1pkg.js productOf");
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot("FM-1_015", "FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot("ota-FM-1_900", "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot("FM-1_900", "Felucca"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_900", (k) => steps.push(k));
  ok(got === "FM-1_900" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Felucca (${dev.served} reads)`);

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_900", "Felucca Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");
}

await editorMock();
await editorPresetAliases();
mockTables();
await editorLibrarian();
await editorGrid();
await editorRatchet();
await editorLive();
await preferenceReplies();
await editorTracks();
await editorMixer();
await editorTrackParam();
await editorSong();
await editorSessions();
await editorRetired();
editorTabs();
editorIcons();
await packages();
await updater();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
