# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Modified by Tristano Suriani for Blue Hare (2026): see AUTHORS.md
"""WAV reading and pitch detection for the host test pitch.py (the user sample slots and their tools were removed
in 1.5).

The float arithmetic here is part of the tests' expectations: keep the order of operations (and Python's sum() where
it is used) when touching these functions.
"""
import struct
from operator import sub


def wav_chunks(d):
    """RIFF/WAVE chunks -> {id: (offset of the body, declared length)}; the first of each id wins"""
    if len(d) < 12 or d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    out, i = {}, 12
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack_from("<I", d, i + 4)[0]
        out.setdefault(cid, (i + 8, n))
        i += 8 + n + (n & 1)
    return out


def wav_format(d, ch):
    """-> (tag, channels, rate, bits); WAVE_FORMAT_EXTENSIBLE resolved to its subformat"""
    if b"fmt " not in ch:
        raise ValueError("WAV without a fmt chunk")
    o = ch[b"fmt "][0]
    tag, nch, sr = struct.unpack_from("<HHI", d, o)
    bits = struct.unpack_from("<H", d, o + 14)[0]
    if tag == 0xFFFE:
        tag = struct.unpack_from("<H", d, o + 24)[0]
    return tag, nch, sr, bits


def _channel_values(tag, bps, raw):
    """interleaved sample values as floats in [-1, 1)"""
    n = len(raw) // bps
    if tag == 3:
        return list(struct.unpack("<%d%s" % (n, "f" if bps == 4 else "d"), raw))
    if bps == 2:
        return [v / 32768 for v in struct.unpack("<%dh" % n, raw)]
    if bps == 3:                                     # 24 bit: pad to 32 (zero low byte), exact power-of-two scale
        w = bytearray(4 * n)
        w[1::4], w[2::4], w[3::4] = raw[0::3], raw[1::3], raw[2::3]
        return [v / 2147483648 for v in struct.unpack("<%di" % n, w)]
    if bps == 4:
        return [v / 2147483648 for v in struct.unpack("<%di" % n, raw)]
    return [(b - 128) / 128 for b in raw]


def read_any_wav(path):
    """PCM 8/16/24/32 or float, any channel count, mixed to mono -> (rate, [float]).
    A truncated data chunk is fine."""
    d = path.read_bytes()
    ch = wav_chunks(d)
    tag, nch, sr, bits = wav_format(d, ch)
    bps = bits // 8
    if tag not in (1, 3) or not bps or not nch:
        raise ValueError(f"{path}: unsupported WAV format (tag {tag}, {bits} bit)")
    o, n = ch.get(b"data", (0, 0))
    frame = bps * nch
    nfr = min(n, len(d) - o) // frame
    v = _channel_values(tag, bps, d[o:o + nfr * frame])
    if nch == 1:
        return sr, v
    if nch == 2:                                     # (0.0 + l + r) / 2, as the per-channel sum below
        return sr, [(a + b) / 2 for a, b in zip(v[0::2], v[1::2])]
    out = []
    for f in range(0, len(v), nch):
        acc = 0.0
        for c in range(nch):
            acc += v[f + c]
        out.append(acc / nch)
    return sr, out


def _diff_py(w, W, hi):
    """d[tau] = sum((w[i] - w[i + tau]) ** 2 for i in range(W)), tau = 1..hi+1"""
    head, two = w[:W], [2] * W
    return [0.0] + [sum(map(pow, map(sub, head, w[tau:tau + W]), two)) for tau in range(1, hi + 2)]


def _diff_np(w, W, hi):
    """_diff_py with numpy, all lags at once. Python >= 3.12 sums floats with Neumaier
    compensation (Objects/bltinmodule.c builtin_sum); this runs the same steps in the same
    order per lag, so the results are the same doubles."""
    a = np.asarray(w, dtype=np.float64)
    taus = np.arange(1, hi + 2)
    f = (a[0] - a[taus]) ** 2                      # sum() starts at int 0: 0 + x == x for x >= 0
    c = np.zeros_like(f)
    for i in range(1, W):
        x = (a[i] - a[i + taus]) ** 2
        t = f + x
        c += np.where(np.abs(f) >= np.abs(x), (f - t) + x, (x - t) + f)
        f = t
    f = np.where(c != 0, f + c, f)
    return [0.0] + f.tolist()


try:
    import numpy as np
    if sys.version_info < (3, 12):               # plain left-to-right sum() there
        raise ImportError
    _diff = _diff_np
except ImportError:
    _diff = _diff_py


def yin(w, sr, fmax=2000, fmin=30, threshold=0.15, interpolate=True):
    """YIN (cumulative-mean-normalised difference) over the window w: the first dip under
    the threshold between the periods of fmax and fmin, else the deepest -> Hz"""
    W = len(w) // 2
    lo, hi = max(2, int(sr / fmax)), min(W - 1, int(sr / fmin))
    d = _diff(w, W, hi)
    cm, run = [1.0] * (hi + 2), 0.0
    for tau in range(1, hi + 2):
        run += d[tau]
        cm[tau] = d[tau] * tau / run if run else 1.0
    tau = next((t for t in range(lo, hi) if cm[t] < threshold and cm[t] <= cm[t + 1]), None)
    if tau is None:
        tau = min(range(lo, hi), key=lambda t: cm[t])
    if interpolate and 1 <= tau < hi:
        y0, y1, y2 = cm[tau - 1], cm[tau], cm[tau + 1]
        den = y0 - 2 * y1 + y2
        tau = tau + (0.5 * (y0 - y2) / den if den else 0)
    return sr / tau
