#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tristano Suriani (Blue Hare)
# Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md
"""(2026-10-19) ONYX's voice, step 1 of 2: every phrase of docs/SpeechSynthesisVocabulary.md spoken by Kokoro TTS
(Kokoro-82M, Apache 2.0) in its am_onyx voice, one WAV a phrase (24 kHz), and an index of the banks (the vocabulary's
subsections). Not part of the build: run once, by hand, where Kokoro is installed (it needs PyTorch: a Python 3.12
environment, `pip install kokoro soundfile`, spaCy's en_core_web_sm); step 2, tools/gen_onyx_lpc.py, turns the WAVs into
the LPC frames the firmware keeps (firmware/baseline/onyx_lpc.h, committed: the build never runs Kokoro).
Kokoro's espeak fallback (words outside its lexicon) is left off (its bundled espeak-ng cannot find its data on
macOS): the words it does not know are respelled here (RESPELL).
  gen_onyx_render.py OUT_DIR [VOICE]   (VOICE: am_onyx, af_heart, bm_george, af_nicole: VOICES)"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VOCAB = ROOT / "docs" / "SpeechSynthesisVocabulary.md"
# the banks' names on the device (8 letters at most), by the vocabulary's subsection
BANKS = {"Data Systems": "DATA", "Signal Processing": "SIGNAL", "Monosyllabic Glitch": "GLITCH",
         "Directives": "COMMAND", "Machine Mechanics": "MACHINE", "High-Impact": "IMPACT", "Hypnotic": "HYPNOTIC",
         "DJ Culture": "DJ", "Euphoric": "EUPHORIA", "Sound-System": "SYSTEM", "Rhythmic": "RHYTHM",
         "Cosmic": "COSMOS", "Hardware Degradation": "RETRO"}
# what Kokoro's lexicon lacks, spelled as it should sound; the micro-phonetic tokens (the vocabulary's section 6)
RESPELL = {"pvt": "[pvt](/pˈʌvt/)", "chk": "[chk](/ʧˈʌk/)", "tsk": "[tsk](/tˈɪsk/)", "tok": "[tok](/tˈɑk/)",
           "tik": "[tik](/tˈɪk/)", "kik": "[kik](/kˈɪk/)", "bip": "[bip](/bˈɪp/)",
           "selecta": "[selecta](/səlˈɛktə/)", "nyquist": "[nyquist](/nˈIkwɪst/)", "bitshift": "[bitshift](/bˈɪtʃˌɪft/)",
           "hardline": "[hardline](/hˈɑɹdlˌIn/)", "sidechain": "[sidechain](/sˈIdʧˌAn/)",
           "bitcrushed": "[bitcrushed](/bˈɪtkɹˌʌʃt/)", "scanline": "[scanline](/skˈænlˌIn/)",
           "dubplate": "dub plate", "bassweight": "bass weight", "soundclash": "sound clash",
           "vhs": "V H S", "bpm": "B P M", "303": "three oh three", "44100": "forty four one hundred",
           "64-bit": "sixty four bit", "8-bit": "eight bit", "zero x four f": "zero x four F"}
TOKENS = [("TOKENS", "dat", "dat"), ("TOKENS", "glitch", "glitch"), ("TOKENS", "sub", "sub"),
          ("TOKENS", "0x4F", "zero x four F"), ("TOKENS", "ahh", "[ahh](/ˈɑɑɑɑɑɑ/)"), ("TOKENS", "eee", "[eee](/ˈiiiiii/)")]
# (2026-10-19) SING: vowels and phonemes held, to be sung (the key the pitch, HOLD sustains them; tools/gen_onyx_lpc.py
# keeps their steady middle): the vowels, a few syllables, the hums, the hisses
SING = [("SING", n, f"[{n.lower()}](/{ph}/)") for n, ph in [
    ("AH", "ˈɑɑɑɑɑɑɑɑɑ"), ("EH", "ˈɛɛɛɛɛɛɛɛ"), ("EE", "ˈiiiiiiiii"), ("OH", "ˈɔɔɔɔɔɔɔɔ"), ("OO", "ˈuuuuuuuuu"),
    ("UH", "ˈʌʌʌʌʌʌʌʌ"), ("AE", "ˈæææææææ"), ("WAH", "wˈɑɑɑɑɑɑɑ"), ("YAH", "jˈɑɑɑɑɑɑɑ"), ("LA", "lˈɑɑɑɑɑɑɑ"),
    ("DA", "dˈɑɑɑɑɑɑɑ"), ("NOO", "nˈuuuuuuu"), ("MM", "ˈəmmmmmmmm"), ("NN", "ˈənnnnnnnn"), ("ZZ", "zzzzzzzzz"),
    ("VV", "vvvvvvvvv"), ("SS", "sssssssss"), ("SH", "ʃʃʃʃʃʃʃʃʃ"), ("FF", "fffffffff")]]
# (2026-10-19) the voices: Onyx, and three more of Kokoro's (each the whole vocabulary); the name the device shows
VOICES = {"am_onyx": "ONYX", "af_heart": "HEART", "bm_george": "GEORGE", "af_nicole": "NICOLE"}


def phrases():
    """[(bank, name, text)] in the vocabulary's order"""
    out, bank = [], None
    for line in VOCAB.read_text().splitlines():
        if line.startswith("### "):
            bank = next((v for k, v in BANKS.items() if line[4:].startswith(k)), None)
        m = re.match(r'\* "(.+)"', line.strip())
        if m and bank:
            t = m.group(1)
            out.append((bank, t, t))
    out += TOKENS + SING
    return out


def speak(text):
    for k, v in RESPELL.items():
        text = re.sub(rf"(?<![\w-]){re.escape(k)}(?![\w-])", v, text)
    return text


def main(out, voice="am_onyx"):
    import misaki.espeak as me

    class NoFallback:                                    # (no espeak: RESPELL instead)
        def __init__(self, *a, **k):
            raise RuntimeError("no espeak")
    me.EspeakFallback = NoFallback
    import numpy as np
    import soundfile as sf
    from kokoro import KPipeline
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    pipe = KPipeline(lang_code=voice[0], repo_id="hexgrad/Kokoro-82M")   # (a: American English, b: British)
    index, total = [], 0.0
    for i, (bank, name, text) in enumerate(phrases()):
        audio = np.concatenate([a.numpy() for _, _, a in pipe(speak(text), voice=voice, speed=1.0)])
        f = f"{i:03d}.wav"
        sf.write(str(out / f), audio, 24000)
        total += len(audio) / 24000
        index.append({"bank": bank, "name": name, "text": text, "file": f, "secs": round(len(audio) / 24000, 3),
                      "voice": VOICES.get(voice, voice.upper())})
        print(f"{i:3d} {bank:9s} {len(audio) / 24000:5.2f} s  {name}")
    (out / "index.json").write_text(json.dumps(index, indent=1))
    print(f"onyx: {len(index)} phrases, {total:.1f} s -> {out}")


if __name__ == "__main__":
    main(sys.argv[1], *sys.argv[2:3])
