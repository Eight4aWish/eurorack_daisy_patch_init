"""wasp_layer.py: the Wasp's granular layer as Mirth plays it (David's design, 2026-10-20).

Three things, each the counterpart of firmware code in nam/gran_layer.h:

  build_bank(r)   one Res mark's bank, computed exactly as tools/wasp_noise_layer.py does
                  (learned from the take's sweep, 24-56 s; David's setting s 0.5)
  pack / unpack   the layer section of a version-2 .grnb file (tools/grnb.py)
  Stream          the firmware's algorithm, sample by sample and stage by stage, in
                  float64 with the same random numbers, so tools/gran_check.py can hold
                  the C++ to it

The layer analyses nothing. Every 512 samples it takes the last 2,048 samples of the
network's output, scales each FFT bin by a gain in dB drawn from the bank, and adds the
result back with a Hann window (overlap 4, so with every gain 0 dB the output is the
input, delayed). Gains come from grains: two streams, each a new grain every L frames,
offset by L // 2, triangular crossfades. A grain is K takes' worth of recorded lift and
dip against the average, L consecutive frames from a moment whose CV was within the CV
window of the CV now, re-mapped so it sits at the same distance from the cutoff
(shifted by slope * (CV then - CV now) octaves). The phase-in (phase() in
wasp_noise_layer.py): the gains scale from 0 at Res 8 to 1 at Res 9; between two
banks' marks each grain takes the higher bank with probability given by where Res sits
between them.

Latency: DELAY = 2,048 + 11 * 48 samples (53.7 ms), for the dry side as well. The
firmware spreads one frame's work over ten audio blocks (a few FFT passes each; three
blocks overran the Patch SM's budget, 2026-10-20) and DELAY leaves room for that.
"""
import pathlib
import struct
import zlib

import numpy as np
from scipy.ndimage import gaussian_filter1d
from scipy.signal import stft, welch

SR = 48000
NFFT, HOP = 2048, 512
BLOCK = 48
SLICES = 10    # the firmware spreads a frame's work over this many audio blocks
DELAY = NFFT + (SLICES + 1) * BLOCK
FIT = (24.0, 56.0)
CV_WINDOW = 1.0
DB_STEP = 0.1  # one stored unit, dB
CHOSEN = 0.5   # David's s
PHASE = (8.0, 9.0)
MAGIC = b"GRLY"
VERSION = 1
HEADER = 48
BANK_DESC = 16
W = pathlib.Path.home() / "GitHub/eurorack_daisy_patch_init/daisy_neural/captures/wasp"


def step(s):
    """The granular EQ's settings at s: grain frames, limit (dB), smoothing sigma (bands)."""
    return int(round(8 * 2 ** s)), 12 - 3 * s, 0.816 * s


def _align(a, b):
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    A_, B_ = np.fft.rfft(a), np.fft.rfft(b)
    xc = np.fft.irfft(A_ * np.conj(B_), n); k = int(np.argmax(np.r_[xc[-8:], xc[:9]])) - 8
    y0, y1, y2 = (xc[(k + d) % n] for d in (-1, 0, 1)); frac = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    return np.fft.irfft(B_ * np.exp(2j * np.pi * np.fft.rfftfreq(n) * frac), n)


def build_bank(r, s=CHOSEN, captures=W):
    """One Res mark's bank, as wasp_noise_layer.py computes it. Returns a dict: res, slope
    (octaves per volt, the CV -> cutoff fit's), lc0 and dlc (the bands' log2 centres),
    cv (frames), gains (takes x frames x bands, dB, smoothed and limited), sources (how
    many frames a grain may start at), L, limit."""
    names = [f"lp-res{r}", f"lp-res{r}-rep1", f"lp-res{r}-rep2", f"lp-res{r}-rep3"]
    takes = [np.load(captures / x / "gru" / "test.npz")["y_lphp"].astype(np.float64) for x in names]
    al = [takes[0]] + [_align(takes[0], t) for t in takes[1:]]
    n = min(map(len, al)); al = [t[:n] for t in al]
    mean = np.mean(al, axis=0); resid = [t - mean for t in al]
    cv = np.load(captures / names[0] / "gru" / "test.npz")["cv_v"][:n].astype(np.float64)

    def psd(y, seg):
        return welch(y[int(seg[0] * SR):int(seg[1] * SR)], SR, nperseg=8192)

    vs, hz = [], []
    for t0 in np.arange(FIT[0], FIT[1] - 1.0 + 1e-9, 1.0):
        seg = (t0, t0 + 1.0)
        v = float(np.mean(cv[int(seg[0] * SR):int(seg[1] * SR)]))
        if v < 3:
            continue
        f, _ = psd(resid[0], seg)
        prs = np.convolve(np.mean([psd(t, seg)[1] for t in resid], axis=0), np.ones(5) / 5, mode="same")
        band = (f > 40) & (f < 16000)
        vs.append(v); hz.append(float(f[band][np.argmax(prs[band])]))
    slope, _ = np.polyfit(vs, np.log2(hz), 1)

    spec = lambda y: stft(y, SR, nperseg=NFFT, noverlap=NFFT - HOP)
    fq, tq, Zm = spec(mean)
    edges = 40 * 2 ** (np.arange(0, 9 * 12 + 1) / 12); edges = edges[edges < 20000]
    centres = np.sqrt(edges[:-1] * edges[1:])
    which = np.digitize(fq, edges) - 1

    def band_power(Z):
        P = np.abs(Z) ** 2
        out = np.full((len(centres), Z.shape[1]), 1e-18)
        for i in range(len(centres)):
            sel = which == i
            if sel.any():
                out[i] = P[sel].mean(axis=0) + 1e-18
        return out

    Pm = band_power(Zm)
    raw = np.stack([10 * np.log10(band_power(spec(t)[2]) / Pm) for t in al[1:]])
    L, limit, sigma = step(s)
    bank = gaussian_filter1d(raw, sigma, axis=1, mode="nearest") if sigma > 0 else raw
    bank = np.clip(bank, -limit, limit)
    cv_frames = np.interp(tq * SR, np.arange(n), cv)
    pool = np.where((tq > FIT[0] + 0.5) & (tq < FIT[1] - 0.5))[0]
    first, last = int(pool[0]), int(pool[-1])
    frames = slice(first, min(last + L, len(tq)))  # a grain may start at the last pool frame
    return {"res": float(r), "slope": float(slope), "lc0": float(np.log2(centres[0])), "dlc": 1.0 / 12,
            "cv": cv_frames[frames].astype(np.float32),
            "gains": np.ascontiguousarray(bank[:, :, frames].transpose(0, 2, 1)),
            "sources": last - first + 1, "L": L, "limit": limit}


def pack(banks):
    """The layer section: header, one descriptor per bank, then each bank's CV (f32 per
    frame) and gains (int8, DB_STEP each, take-major then frame then band), each bank
    starting on a 4-byte boundary."""
    banks = sorted(banks, key=lambda b: b["res"])
    K, F, B = banks[0]["gains"].shape
    L = banks[0]["L"]
    head = (MAGIC + struct.pack("<HHHHHHHH", VERSION, len(banks), B, K, L, NFFT, HOP, 0)
            + struct.pack("<ffffff", banks[0]["lc0"], banks[0]["dlc"], DB_STEP, CV_WINDOW, *PHASE)
            + b"\0" * 4)
    assert len(head) == HEADER
    off = HEADER + BANK_DESC * len(banks)
    descs, blobs = b"", b""
    for bk in banks:
        k, f, b = bk["gains"].shape
        assert (k, b, bk["L"]) == (K, B, L) and bk["sources"] == f - L + 1
        q = np.clip(np.round(bk["gains"] / DB_STEP), -127, 127).astype(np.int8)
        blob = bk["cv"].astype("<f4").tobytes() + q.tobytes()
        blob += b"\0" * (-len(blob) % 4)
        descs += struct.pack("<ffII", bk["res"], bk["slope"], f, off + len(blobs))
        blobs += blob
    return head + descs + blobs


def unpack(data):
    """The banks back from a layer section, gains in dB (as stored, so quantised)."""
    if data[:4] != MAGIC:
        raise ValueError("not a layer section")
    version, n, B, K, L, nfft, hop, _ = struct.unpack_from("<HHHHHHHH", data, 4)
    lc0, dlc, db_step, cv_window, p0, p1 = struct.unpack_from("<ffffff", data, 20)
    if version != VERSION or nfft != NFFT or hop != HOP:
        raise ValueError(f"layer version {version}, fft {nfft}/{hop}")
    banks = []
    for i in range(n):
        res, slope, F, off = struct.unpack_from("<ffII", data, HEADER + BANK_DESC * i)
        cv = np.frombuffer(data, "<f4", F, off).astype(np.float64)
        g = np.frombuffer(data, np.int8, K * F * B, off + 4 * F).reshape(K, F, B).astype(np.float64) * db_step
        banks.append({"res": res, "slope": slope, "lc0": lc0, "dlc": dlc, "cv": cv, "gains": g,
                      "sources": F - L + 1, "L": L})
    return {"banks": banks, "L": L, "cv_window": cv_window, "phase": (p0, p1)}


class XorShift32:
    """The firmware's random numbers: Marsaglia's xorshift32."""

    def __init__(self, seed):
        self.x = seed & 0xFFFFFFFF or 1

    def next(self):
        x = self.x
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.x = x
        return x

    def uniform(self):
        return (self.next() >> 8) * (1.0 / (1 << 24))


def phase_in(layer, res):
    """(depth, the bank below, the bank above, the chance of the bank above)."""
    p0, p1 = layer["phase"]
    depth = min(max((res - p0) / (p1 - p0), 0.0), 1.0)
    marks = [b["res"] for b in layer["banks"]]
    if res <= marks[0]:
        return depth, 0, 0, 0.0
    for i in range(len(marks) - 1):
        if res < marks[i + 1]:
            return depth, i, i + 1, (res - marks[i]) / (marks[i + 1] - marks[i])
    return depth, len(marks) - 1, len(marks) - 1, 0.0


class Stream:
    """The firmware's layer in float64. A grain's draws, in order: one uniform for the
    bank, one integer for the take, then up to 32 integers for the frame it starts at
    (the first whose CV is within the window, else the last tried). Stream 0 draws before
    stream 1, and grains are drawn whatever the depth, so the sequence never depends on
    Res."""

    TRIES = 32

    def __init__(self, layer, seed=0x2545F491):
        self.layer, self.rng = layer, XorShift32(seed)
        self.L = layer["L"]
        n = np.arange(NFFT)
        self.win = 0.5 - 0.5 * np.cos(2 * np.pi * n / NFFT)  # periodic Hann
        self.lfq = np.log2(np.maximum(np.fft.rfftfreq(NFFT, 1.0 / SR), 1.0))
        self.frame = 0
        self.grain = [None, None]  # per stream: (bank, take, start frame)

    def _new_grain(self, v_now, res):
        _, lo, hi, p = phase_in(self.layer, res)
        b = hi if self.rng.uniform() < p else lo
        bank = self.layer["banks"][b]
        k = self.rng.next() % bank["gains"].shape[0]
        src = 0
        for _ in range(self.TRIES):
            src = self.rng.next() % bank["sources"]
            if abs(bank["cv"][src] - v_now) < self.layer["cv_window"]:
                break
        return b, k, src

    def gains_db(self, v_now, res):
        """This frame's gain per bin, dB, advancing both streams by one frame."""
        depth, _, _, _ = phase_in(self.layer, res)
        L, g = self.L, np.zeros(NFFT // 2 + 1)
        for s, offset in enumerate((0, L // 2)):
            i = (self.frame + offset) % L
            if i == 0 or self.grain[s] is None:
                self.grain[s] = self._new_grain(v_now, res)
            b, k, src = self.grain[s]
            if depth == 0.0:
                continue
            bank = self.layer["banks"][b]
            w = 1 - abs((i + 0.5) / L * 2 - 1)
            shift = bank["slope"] * (min(max(bank["cv"][src + i], 0.0), 10.0) - min(max(v_now, 0.0), 10.0))
            pos = (self.lfq + shift - bank["lc0"]) / bank["dlc"]
            row = bank["gains"][k, src + i]
            g += depth * w * np.interp(pos, np.arange(len(row)), row)
        self.frame += 1
        return g

    def run(self, x, cut, res):
        """A whole signal, as the firmware plays it in blocks of 48: cut and res give each
        block's cutoff (volts) and Res. A frame completes on every HOP-th sample and takes
        the cutoff and Res of the block it completes in. Returns the output, DELAY late."""
        x = np.asarray(x, np.float64)
        n = len(x)
        z = np.zeros(n + NFFT)
        pad = np.r_[np.zeros(NFFT), x]  # pad[i + NFFT] = x[i]
        for end in range(HOP - 1, n, HOP):  # the frame is x[end - NFFT + 1 .. end]
            b = end // BLOCK
            seg = pad[end + 1:end + 1 + NFFT]
            X = np.fft.rfft(seg * self.win)
            g = self.gains_db(float(cut[b]), float(res[b]))
            y = np.fft.irfft(X * 10 ** (g / 20), NFFT) * self.win / 1.5
            lo = end + 1 - NFFT
            a = max(lo, 0)
            z[a:end + 1] += y[a - lo:]
        return np.r_[np.zeros(DELAY), z[:n]][:n]


def dry(x):
    """The dry side, as the firmware delays it."""
    return np.r_[np.zeros(DELAY), x][:len(x)]
