#!/usr/bin/env python3
"""
wasp_capture.py — record the Doepfer A-124 (Wasp) for three networks: NAM A2 snapshots
(one capture per cutoff), a GRU that takes the cutoff CV as an input, and a GRU that takes
the cutoff and the resonance. The bench sheet is ../WASP.md; read that first.

Only the cutoff is under CV on the A-124, so the resonance is stepped between sessions:
one session per Res knob position, each sweeping the cutoff. Pure low-pass and pure
high-pass are the two end stops of the Mix knob on the one LP/HP output, so each is its
own series of sessions (--mix). One session, the comparison session, also carries the A2
snapshot takes (--snapshots).

Everything goes through the ES-10, played and recorded in one stream, so every take is
sample-locked:

    play file        ES-10       patched to
    ch 1 audio       OUT 1  -->  Wasp AUDIO IN
    ch 2 cutoff CV   OUT 2  -->  Wasp CV 1 (no attenuator; roughly 1 V/oct)
    ch 3 loopback    OUT 3  -->  ES-10 IN 3, one cable straight back

    recording        ES-10       from
    ch 1             IN 1   <--  Wasp LP/HP OUT
    ch 2             IN 2   <--  Wasp BP OUT
    ch 3             IN 3   <--  the loopback

The loopback carries a copy of the audio. A noise burst at the start of every take
measures the round-trip latency; the end of the take is measured again, and if the two
disagree, samples were dropped and the take is recorded again.

Steps, in order. Files land in captures/wasp/, which git ignores.

    .venv/bin/python tools/wasp_capture.py devices
    .venv/bin/python tools/wasp_capture.py make rigcheck
    .venv/bin/python tools/wasp_capture.py record captures/wasp/rigcheck.wav
    .venv/bin/python tools/wasp_capture.py check captures/wasp/rigcheck.rec.wav --meter-volts 5.0 --save
    .venv/bin/python tools/wasp_capture.py make ladder --from -4 --to 6
    .venv/bin/python tools/wasp_capture.py record captures/wasp/ladder.wav
    .venv/bin/python tools/wasp_capture.py make session lp-res6 --mix lp --res 6 --cv-range=-3,5 --snapshots=-2,0,1.5,3,4.5
    .venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res6
    .venv/bin/python tools/wasp_capture.py split captures/wasp/lp-res6
    .venv/bin/python tools/wasp_capture.py make session lp-res0 --mix lp --res 0 --cv-range=-3,5   # and so on,
    .venv/bin/python tools/wasp_capture.py make session hp-res0 --mix hp --res 0 --cv-range=-3,5   # then the HP series

`record` needs sounddevice (.venv/bin/pip install sounddevice); nothing else does.
"""

import argparse
import datetime
import hashlib
import json
import math
import pathlib
import sys

import numpy as np
import soundfile as sf
from scipy import signal

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_signal import make_synth  # noqa: E402

SR = 48000
ROOT = HERE.parent / "captures" / "wasp"
RIG = ROOT / "rig.json"
INPUT_WAV = ROOT / "input.wav"

# NAM's standard training file, v3.0.0. Its trainer recognises the file by this MD5
# (nam/train/core.py, _detect_input_version) and needs the recording to be the same
# length. It opens and closes with the same 9 s of validation audio, which is how the
# trainer checks that the device did not drift during the take.
NAM_V3_MD5 = "36cd1af62985c2fac3e654333e36431e"
NAM_V3_LEN = 9_120_000
NAM_T_VALIDATE = 432_000

FS_MAX = 0.98  # never ask the ES-10 for more than this fraction of full scale

# Every session take opens the same way: the CV moves to its first value, the Wasp
# settles in silence (the rest level is measured there), a noise burst marks time zero
# on the audio and the loopback, then a gap, then the payload, then a second of silence.
RAMP = SR // 4
SETTLE = SR
BURST = 2048
GAP = SR // 2
POST = SR
BURST_AT = RAMP + SETTLE
PAYLOAD_AT = BURST_AT + BURST + GAP
REST_WINDOW = (RAMP + SETTLE // 2, BURST_AT)  # silent, CV settled
MAX_LATENCY = SR // 4  # the search range; a real chain is a few ms

# The rig check, in seconds.
RC_QUIET = (0.5, 2.5)
RC_BURST = 3.0
RC_POS = (4.0, 19.0)  # +0.5 of full scale on OUT 2 and OUT 3: read the meter here
RC_NEG = (19.0, 34.0)  # -0.5
RC_SAW = (36.0, 46.0)  # a 110 Hz saw through the Wasp
RC_LEN = 48.0

LP, BP, LOOP = 0, 1, 2  # recording channels
AUDIO, CV = 0, 1  # play channels (2 is the loopback, a copy of AUDIO)

# The Mix knob's two end stops: exactly repeatable, unlike any point between them.
MIX_ENDS = ("lp", "hp")
MIX_ENDS_TEXT = {"lp": "fully anticlockwise, the LP end stop: pure low-pass",
                 "hp": "fully clockwise, the HP end stop: pure high-pass"}


# --------------------------------------------------------------------------- rig

def rel(path):
    """A path as the bench sheet writes it: relative to daisy_neural when it can be."""
    try:
        return path.relative_to(HERE.parent)
    except ValueError:
        return path


def load_rig():
    if RIG.exists():
        return json.loads(RIG.read_text())
    return {
        "_help": "device: part of the interface's name, as `devices` prints it. "
                 "play_channels / rec_channels: the interface's channel numbers (from 1) "
                 "that reach ES-10 OUT 1-3 and come back from ES-10 IN 1-3, as routed in "
                 "Focusrite Control. out_vfs / in_vfs: volts per full scale, from `check`.",
        "device": None, "play_channels": None, "rec_channels": None,
        "out_vfs": None, "in_vfs": None, "latency_samples": None,
    }


def save_rig(rig):
    ROOT.mkdir(parents=True, exist_ok=True)
    RIG.write_text(json.dumps(rig, indent=2) + "\n")


def need_out_vfs(args):
    vfs = args.out_vfs or load_rig().get("out_vfs")
    if not vfs:
        raise SystemExit("no out_vfs yet: run the rig check first (`check ... --meter-volts V "
                         "--save`), or pass --out-vfs")
    return float(vfs)


def to_fs(volts, out_vfs, what):
    fs = np.asarray(volts, dtype=np.float64) / out_vfs
    if np.max(np.abs(fs)) > FS_MAX:
        raise SystemExit(f"{what}: {np.max(np.abs(volts)):.2f} V is beyond the ES-10's "
                         f"{FS_MAX * out_vfs:.2f} V at {out_vfs:.2f} V per full scale")
    return fs


# --------------------------------------------------------------------- signals

def burst():
    """The timing mark: a short, fixed noise burst. Noise rather than a click because it
    correlates with itself at exactly one lag, so the latency comes out unambiguous."""
    rng = np.random.default_rng(424242)
    b = rng.standard_normal(BURST) * np.hanning(BURST)
    return 0.25 * b / np.abs(b).max()


def wavetable(shape, harmonics, size=4096):
    """One cycle built from its harmonics, so it is band-limited by construction and any
    aliasing heard later belongs to the Wasp or the network, not the source."""
    n = np.arange(1, harmonics + 1)
    if shape == "saw":
        amp = 1.0 / n
    elif shape == "square":
        amp = np.where(n % 2 == 1, 1.0 / n, 0.0)
    elif shape == "triangle":
        amp = np.where(n % 2 == 1, (-1.0) ** ((n - 1) // 2) / n ** 2, 0.0)
    else:
        amp = (n == 1).astype(float)
    t = np.sin(2 * np.pi * np.outer(np.arange(size), n) / size) @ amp
    return t / np.abs(t).max()


def oscillate(table, freq):
    """Read a wavetable at a per-sample frequency (so glides are free)."""
    size = len(table)
    x = (np.cumsum(freq / SR) % 1.0) * size
    i0 = x.astype(np.int64) % size
    frac = x - np.floor(x)
    return table[i0] * (1 - frac) + table[(i0 + 1) % size] * frac


def envelope(n, attack, release):
    a = min(max(1, int(attack * SR)), n // 2)
    r = min(max(1, int(release * SR)), n - a)
    env = np.ones(n)
    env[:a] = np.arange(a) / a
    env[n - r:] = np.linspace(1, 0, r)
    return env


def harmonics_for(fmax):
    return max(1, min(int((SR / 2) / fmax) - 1, 400))


def phrase(rng, root, bars, notes_per_bar=16, bar_seconds=4.0, peak=1.0):
    """A random minor-pentatonic line on two detuned saws: what a filter in a rack is fed.
    Every bar is bar_seconds long, so CV holds can be lined up with bars."""
    scale = np.array([0, 3, 5, 7, 10, 12, 15])
    note_n = int(bar_seconds * SR / notes_per_bar)
    out = np.zeros(bars * notes_per_bar * note_n)
    top = root * 2 ** (scale.max() / 12)
    table = wavetable("saw", harmonics_for(top * 1.01))
    for k in range(bars * notes_per_bar):
        f = root * 2 ** (rng.choice(scale) / 12)
        vel = rng.uniform(0.5, 1.0)
        gate = int(note_n * rng.uniform(0.4, 0.9))
        f_arr = np.full(gate, f)
        v = 0.5 * (oscillate(table, f_arr * 2 ** (-7 / 1200)) + oscillate(table, f_arr * 2 ** (7 / 1200)))
        out[k * note_n:k * note_n + gate] = v * envelope(gate, 0.004, 0.06) * vel
    return out * (peak / np.abs(out).max())


def sweep_audio(seconds, rng, peaks_fs):
    """Training material for the cutoff knob: oscillator notes and glides at several
    levels (a filter's character changes with how hard it is driven, far more than an
    amp's), noise bursts, slow sine sweeps, phrases, and rests to hear the tails."""
    n_total = int(seconds * SR)
    parts, n = [], 0
    while n < n_total:
        kind = rng.choice(["tone", "noise", "sweep", "phrase", "rest"], p=[0.6, 0.1, 0.1, 0.1, 0.1])
        peak = rng.choice(peaks_fs)
        if kind == "tone":
            dur = int(rng.uniform(0.25, 2.0) * SR)
            f0 = math.exp(rng.uniform(math.log(40), math.log(1200)))
            f1 = f0 * 2 ** rng.uniform(-2, 2) if rng.random() < 0.25 else f0
            freq = f0 * (f1 / f0) ** (np.arange(dur) / dur)
            table = wavetable(rng.choice(["saw", "square", "triangle", "sine"]), harmonics_for(max(f0, f1)))
            attack = math.exp(rng.uniform(math.log(0.002), math.log(0.08)))
            release = math.exp(rng.uniform(math.log(0.005), math.log(0.3)))
            seg = oscillate(table, freq) * envelope(dur, attack, release) * peak
        elif kind == "noise":
            dur = int(rng.uniform(0.1, 1.0) * SR)
            seg = rng.standard_normal(dur) * envelope(dur, 0.005, 0.02)
            seg *= peak / np.abs(seg).max()
        elif kind == "sweep":
            dur = int(rng.uniform(1.0, 3.0) * SR)
            f0, f1 = (30.0, 12000.0) if rng.random() < 0.5 else (12000.0, 30.0)
            freq = f0 * (f1 / f0) ** (np.arange(dur) / dur)
            seg = np.sin(2 * np.pi * np.cumsum(freq) / SR) * envelope(dur, 0.01, 0.01) * peak
        elif kind == "phrase":
            seg = phrase(rng, rng.choice([55.0, 82.41, 110.0, 146.83]), bars=1, peak=peak)
        else:
            seg = np.zeros(int(rng.uniform(0.05, 0.6) * SR))
        parts.append(seg)
        n += len(seg)
    out = np.concatenate(parts)[:n_total]
    out[-SR // 100:] *= np.linspace(1, 0, SR // 100)
    return out


def sweep_cv(n, rng, lo, hi, start):
    """The cutoff CV for the GRU's training: holds, ramps and a smoothed wander, in
    segments of 4-15 s, always continuous apart from 5 ms steps into a hold."""
    cv = np.empty(n)
    pos, cur = 0, start
    while pos < n:
        seg = min(int(rng.uniform(4, 15) * SR), n - pos)
        kind = rng.choice(["hold", "ramp", "wander"], p=[0.3, 0.35, 0.35])
        if kind == "hold":
            target = rng.uniform(lo, hi)
            s = np.full(seg, target)
            k = min(SR // 200, seg)
            s[:k] = cur + (target - cur) * np.arange(1, k + 1) / k
        elif kind == "ramp":
            target = rng.uniform(lo, hi)
            s = cur + (target - cur) * np.arange(1, seg + 1) / seg
        else:
            step = int(rng.uniform(0.1, 0.6) * SR)
            knots = [cur]
            for _ in range(seg // step + 2):
                v = knots[-1] + rng.normal(0, 0.15 * (hi - lo))
                v = lo + abs(v - lo) if v < lo else hi - abs(v - hi) if v > hi else v
                knots.append(v)
            knots = np.clip(knots, lo, hi)
            i = np.arange(seg)
            k, f = i // step, (i % step) / step
            w = 0.5 - 0.5 * np.cos(np.pi * f)
            s = knots[k] * (1 - w) + knots[k + 1] * w
        cv[pos:pos + seg] = s
        cur = s[-1]
        pos += seg
    return np.clip(cv, lo, hi)


def held(values, seconds, ramp_from):
    """Hold each value for `seconds`, stepping between them over 5 ms."""
    n = int(seconds * SR)
    out, cur = [], ramp_from
    for v in values:
        s = np.full(n, v)
        k = SR // 200
        s[:k] = cur + (v - cur) * np.arange(1, k + 1) / k
        out.append(s)
        cur = v
    return np.concatenate(out)


def take(audio_fs, cv_fs):
    """Wrap a payload in the standard opening and close. Returns the 3-channel play file."""
    n = len(audio_fs)
    out = np.zeros((PAYLOAD_AT + n + POST, 3), np.float32)
    start = cv_fs[0]
    out[:RAMP, CV] = start * (0.5 - 0.5 * np.cos(np.pi * np.arange(RAMP) / RAMP))
    out[RAMP:PAYLOAD_AT, CV] = start
    out[PAYLOAD_AT:PAYLOAD_AT + n, CV] = cv_fs
    out[PAYLOAD_AT + n:, CV] = cv_fs[-1]
    out[BURST_AT:BURST_AT + BURST, AUDIO] = burst()
    out[PAYLOAD_AT:PAYLOAD_AT + n, AUDIO] = audio_fs
    out[:, 2] = out[:, AUDIO]
    return out


def read_nam_input():
    if not INPUT_WAV.exists():
        raise SystemExit(f"no {rel(INPUT_WAV)}: download NAM's standard "
                         "input file (see WASP.md) and put it there")
    md5 = hashlib.md5(INPUT_WAV.read_bytes()).hexdigest()
    if md5 != NAM_V3_MD5:
        raise SystemExit(f"{INPUT_WAV.name} is not NAM's v3.0.0 input (MD5 {md5}); the trainer "
                         "would not recognise it")
    x, fs = sf.read(INPUT_WAV, dtype="float32")
    assert fs == SR and len(x) == NAM_V3_LEN
    return x, md5


# ----------------------------------------------------------------------- make

def make_rigcheck(args):
    n = int(RC_LEN * SR)
    out = np.zeros((n, 3), np.float32)
    s = lambda t: int(t * SR)  # noqa: E731
    b = burst()
    out[s(RC_BURST):s(RC_BURST) + BURST, AUDIO] = b
    out[s(RC_POS[0]):s(RC_POS[1]), CV] = 0.5
    out[s(RC_NEG[0]):s(RC_NEG[1]), CV] = -0.5
    saw = oscillate(wavetable("saw", harmonics_for(110)), np.full(s(RC_SAW[1]) - s(RC_SAW[0]), 110.0))
    out[s(RC_SAW[0]):s(RC_SAW[1]), AUDIO] = 0.3 * saw * envelope(len(saw), 0.01, 0.01)
    out[:, 2] = out[:, AUDIO] + out[:, CV]  # the loopback sees the burst, the DC holds and the saw
    ROOT.mkdir(parents=True, exist_ok=True)
    path = ROOT / "rigcheck.wav"
    sf.write(path, out, SR, subtype="FLOAT")
    if not RIG.exists():
        save_rig(load_rig())
        print(f"wrote {rel(RIG)}: fill in device and the channel numbers")
    print(f"wrote {rel(path)} ({RC_LEN:.0f} s)\n"
          f"  {RC_POS[0]:.0f}-{RC_POS[1]:.0f} s: OUT 2 holds +0.5 of full scale. Read the meter "
          f"on OUT 2 here; that number is --meter-volts.\n"
          f"  {RC_NEG[0]:.0f}-{RC_NEG[1]:.0f} s: -0.5 of full scale.\n"
          f"  {RC_SAW[0]:.0f}-{RC_SAW[1]:.0f} s: a 110 Hz saw through the Wasp.")


def make_ladder(args):
    out_vfs = need_out_vfs(args)
    steps = np.arange(args.cv_from, args.cv_to + 1e-9, args.step)
    cv_fs = to_fs(steps, out_vfs, "ladder CV")
    peak_fs = float(to_fs(args.peak, out_vfs, "ladder level"))
    riff = np.asarray(make_synth(), dtype=np.float64)
    riff *= peak_fs / np.abs(riff).max()
    gap = SR // 2
    seg = gap + len(riff)
    out = np.zeros((seg * len(steps) + SR, 3), np.float32)
    cues = []
    prev = 0.0
    for i, (v, c) in enumerate(zip(steps, cv_fs)):
        a = i * seg
        k = SR // 4
        out[a:a + k, CV] = prev + (c - prev) * (0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k))
        out[a + k:a + seg, CV] = c
        out[a + gap:a + seg, AUDIO] = riff
        cues.append({"t": round(a / SR, 2), "cv_v": round(float(v), 3)})
        prev = c
    out[seg * len(steps):, CV] = prev
    out[:, 2] = out[:, AUDIO]
    ROOT.mkdir(parents=True, exist_ok=True)
    path = ROOT / "ladder.wav"
    sf.write(path, out, SR, subtype="FLOAT")
    (ROOT / "ladder.json").write_text(json.dumps({"out_vfs": out_vfs, "peak_v": args.peak, "cues": cues}, indent=2))
    print(f"wrote {rel(path)} ({len(out) / SR:.0f} s): the test riff at "
          f"{args.peak:g} V peak, once per CV step")
    for c in cues:
        print(f"  {c['t']:6.1f} s   CV 1 = {c['cv_v']:+.2f} V")


def make_session(args):
    out_vfs = need_out_vfs(args)
    lo, hi = sorted(float(v) for v in args.cv_range.split(","))
    snaps = sorted(float(v) for v in args.snapshots.split(",")) if args.snapshots else []
    to_fs([lo, hi] + snaps, out_vfs, "CV")
    for v in snaps:
        if not lo <= v <= hi:
            print(f"note: snapshot {v:+.2f} V is outside the sweep range {lo:+.2f}..{hi:+.2f} V; "
                  "the GRU will not have trained there")
    x, md5 = read_nam_input() if snaps else (None, None)

    peaks_v = [v for v in (0.5, 1.0, 2.0, 3.5, 5.0, 7.0, 9.0) if v <= FS_MAX * out_vfs]
    peaks_fs = [v / out_vfs for v in peaks_v]
    test_peak_fs = float(to_fs(args.test_peak, out_vfs, "test level"))

    d = ROOT / args.name
    if (d / "session.json").exists() and not args.overwrite:
        raise SystemExit(f"{rel(d)} already has a session; --overwrite to replace it")
    (d / "play").mkdir(parents=True, exist_ok=True)
    takes = []

    def add(name, role, audio_fs, cv_v, nam=False):
        cv_fs = np.asarray(cv_v) / out_vfs
        if np.isscalar(cv_v) or np.ndim(cv_v) == 0:
            cv_fs = np.full(len(audio_fs), float(cv_v) / out_vfs)
        sf.write(d / "play" / f"{name}.wav", take(audio_fs, cv_fs), SR, subtype="FLOAT")
        takes.append({"name": name, "role": role, "nam": nam, "payload_at": PAYLOAD_AT,
                      "payload_len": len(audio_fs), "burst_at": BURST_AT, "rest_window": REST_WINDOW,
                      "cv_v": float(cv_v) if np.ndim(cv_v) == 0 else None,
                      "seconds": round((PAYLOAD_AT + len(audio_fs) + POST) / SR, 1)})
        print(f"  {name:22s} {takes[-1]['seconds']:6.1f} s  {role}")

    print(f"{rel(d)}/play:")
    # 1. A2 snapshots: NAM's own input file, bit for bit, at each cutoff CV.
    for i, v in enumerate(snaps, 1):
        add(f"snap{i}_{v:+.2f}V", "snapshot", x, v, nam=True)
    # 2. The knob: material for the GRU while the CV moves. Fresh material for every Res
    #    position, so the series as a whole covers more ground.
    n_sweeps = max(1, math.ceil(args.sweep_minutes / 3.0))
    for i in range(1, n_sweeps + 1):
        sub = np.random.default_rng([args.seed, MIX_ENDS.index(args.mix), round(args.res * 10), i])
        secs = args.sweep_minutes * 60 / n_sweeps
        audio = sweep_audio(secs, sub, peaks_fs)
        cv = sweep_cv(len(audio), sub, lo, hi, start=sub.uniform(lo, hi))
        add(f"sweep{i}", "sweep", audio, cv)
    # 3. Held out: never trained on. The same riff and the same CV path in every session
    #    with the same range, so one Res position can be compared with another: six holds
    #    across the range, then a slow ramp up and back.
    def held_out(name, cv):
        bars = len(cv) // (4 * SR)
        audio = phrase(np.random.default_rng(args.seed + 7919), 73.42, bars=bars, peak=test_peak_fs)
        add(name, "test", audio, cv[:len(audio)])

    points = list(np.linspace(lo, hi, 6))
    held_out("test", np.concatenate([held(points, 4.0, ramp_from=points[0]),
                                     np.linspace(lo, hi, 16 * SR), np.linspace(hi, lo, 16 * SR)]))
    # In the comparison session only: CV between the snapshots, where A2 can only jump,
    # and on them, where A2 is at home.
    if snaps:
        mids = [(a + b) / 2 for a, b in zip(snaps, snaps[1:])]
        held_out("test_snaps", held(mids + snaps, 4.0, ramp_from=(mids or snaps)[0]))

    session = {
        "name": args.name, "made": datetime.datetime.now().isoformat(timespec="seconds"),
        "sr": SR, "out_vfs": out_vfs, "in_vfs": load_rig().get("in_vfs"),
        "mix": args.mix, "res": args.res, "knobs": args.knobs, "snapshots_v": snaps, "cv_range_v": [lo, hi],
        "sweep_peaks_v": peaks_v, "test_peak_v": args.test_peak, "seed": args.seed,
        "nam_input": {"file": INPUT_WAV.name, "md5": md5} if snaps else None,
        "channels": {"play": ["audio -> Wasp AUDIO IN", "CV -> Wasp CV 1", "loopback -> ES-10 IN 3"],
                     "rec": [f"Wasp LP/HP OUT, Mix at {args.mix.upper()}", "Wasp BP OUT", "loopback"]},
        "takes": takes,
    }
    (d / "session.json").write_text(json.dumps(session, indent=2) + "\n")
    notes = d / "notes.md"
    if not notes.exists():
        notes.write_text(NOTES.format(name=args.name, res=args.res, knobs=args.knobs or "(set --knobs)",
                                      mix=MIX_ENDS_TEXT[args.mix],
                                      snaps=", ".join(f"{v:+.2f}" for v in snaps) or "none in this session",
                                      lo=lo, hi=hi,
                                      out_vfs=out_vfs, in_vfs=session["in_vfs"]))
    total = sum(t["seconds"] for t in takes)
    print(f"{len(takes)} takes, {total / 60:.1f} min to record. Notes: {rel(notes)}")


NOTES = """# Wasp session: {name}

- Mix: {mix}
- Res: {res:g} on the dial (photograph it)
- Lev and Frq, the same for both series: {knobs}
- Patching: ES-10 OUT 1 -> AUDIO IN, OUT 2 -> CV 1, OUT 3 -> IN 3; LP/HP OUT -> IN 1, BP OUT -> IN 2
- CV 2: unpatched, knob at 0
- Snapshots at CV 1 (V): {snaps}
- Sweep range (V): {lo:+.2f} to {hi:+.2f}
- ES-10: {out_vfs} V per full scale out, {in_vfs} in
- Powered on at: (time, so the warm-up is known)
- Recorded on: (date)

## What I like about this setting

## Where it turns nasty

## Which snapshot sounds best, and why
"""


# --------------------------------------------------------------------- record

def record(args):
    try:
        import sounddevice as sd
    except ImportError:
        raise SystemExit("record needs sounddevice: .venv/bin/pip install sounddevice")
    rig = load_rig()
    device = args.device or rig.get("device")
    play_ch = [int(c) for c in args.play_channels.split(",")] if args.play_channels else rig.get("play_channels")
    rec_ch = [int(c) for c in args.rec_channels.split(",")] if args.rec_channels else rig.get("rec_channels")
    if not (device and play_ch and rec_ch):
        raise SystemExit(f"set device, play_channels and rec_channels in {rel(RIG)} "
                         "(or pass --device / --play-channels / --rec-channels); `devices` lists them")
    path = pathlib.Path(args.path)
    if path.is_dir():
        session = json.loads((path / "session.json").read_text())
        jobs = [(path / "play" / f"{t['name']}.wav", path / "rec" / f"{t['name']}.wav")
                for t in session["takes"] if not args.only or t["name"] in args.only.split(",")]
    else:
        jobs = [(path, path.with_suffix(".rec.wav"))]
    for play, rec in jobs:
        if rec.exists() and not args.overwrite:
            print(f"{rec.name}: already recorded, skipped (--overwrite to redo it)")
            continue
        data, fs = sf.read(play, dtype="float32", always_2d=True)
        if fs != SR or data.shape[1] != len(play_ch):
            raise SystemExit(f"{play.name}: {fs} Hz, {data.shape[1]} channels; expected {SR} Hz, "
                             f"{len(play_ch)} channels")
        print(f"{play.name}: {len(data) / SR:.0f} s ...", end="", flush=True)
        try:
            r = sd.playrec(data, samplerate=SR, device=device, dtype="float32", latency="high",
                           output_mapping=play_ch, input_mapping=rec_ch)
            status = sd.wait()
        except KeyboardInterrupt:
            sd.stop()
            raise SystemExit("\nstopped; nothing saved for this take")
        rec.parent.mkdir(parents=True, exist_ok=True)
        sf.write(rec, r, SR, subtype="FLOAT")
        peaks = 20 * np.log10(np.abs(r).max(axis=0) + 1e-12)
        print(" peaks " + "  ".join(f"{n} {p:6.1f}" for n, p in zip(("LP/HP", "BP", "loop"), peaks)) + " dBFS")
        if status:
            print(f"  WARNING: the audio stream reported {status}; record this take again")
        if peaks[LOOP] < -60:
            print("  WARNING: nothing came back on the loopback. Check the cable, rec_channels, "
                  "and that macOS lets this terminal use the microphone")
        if peaks[:2].max() > -0.1:
            print("  WARNING: a Wasp output reached full scale at the ES-10 input")


def devices(args):
    try:
        import sounddevice as sd
    except ImportError:
        raise SystemExit("needs sounddevice: .venv/bin/pip install sounddevice")
    for i, dv in enumerate(sd.query_devices()):
        print(f"{i:3d}  {dv['name']:40s} in {dv['max_input_channels']:2d}  out {dv['max_output_channels']:2d}  "
              f"{dv['default_samplerate']:.0f} Hz")


# --------------------------------------------------------------------- analyse

def latency(played, recorded, at, length):
    """Lag (samples) of `recorded` behind `played` for the window played[at:at+length]."""
    ref = played[at:at + length]
    seg = recorded[at:at + length + MAX_LATENCY]
    c = signal.correlate(seg, ref, mode="valid", method="fft")
    lag = int(np.argmax(np.abs(c)))
    strength = float(np.abs(c[lag]) / (np.median(np.abs(c)) + 1e-20))
    return lag, strength


def esr(pred, target):
    return float(np.mean((pred - target) ** 2) / (np.mean(target ** 2) + 1e-20))


def db(x):
    return 10 * math.log10(max(x, 1e-20))


def check(args):
    """Read the recorded rig check: latency, input offsets and noise, whether DC gets
    through, the loopback gain, and whether the Wasp is answering."""
    r, fs = sf.read(args.path, dtype="float64", always_2d=True)
    if fs != SR:
        raise SystemExit(f"{fs} Hz; expected {SR}")
    s = lambda t: int(t * SR)  # noqa: E731
    played = np.zeros(len(r))
    played[s(RC_BURST):s(RC_BURST) + BURST] = burst()
    lag, strength = latency(played, r[:, LOOP], s(RC_BURST), BURST)
    w = lambda a, b: r[s(a) + lag:s(b) + lag]  # noqa: E731
    quiet = w(*RC_QUIET)
    offset = quiet.mean(axis=0)
    noise = quiet.std(axis=0)
    print(f"latency: {lag} samples ({lag / SR * 1000:.2f} ms), burst found {strength:.0f}x above the floor"
          + ("" if strength > 20 else "  <- weak: is OUT 3 patched to IN 3?"))
    rig = load_rig()
    in_vfs = rig.get("in_vfs")
    for name, ch in (("IN 1 (LP/HP)", LP), ("IN 2 (BP)", BP), ("IN 3 (loop)", LOOP)):
        mv = f" = {offset[ch] * in_vfs * 1000:+.1f} mV" if in_vfs else ""
        print(f"{name:13s} offset {offset[ch]:+.5f} FS{mv}, noise {20 * math.log10(noise[ch] + 1e-12):6.1f} dBFS rms")
    loop = r[:, LOOP] - offset[LOOP]
    pos = loop[s(RC_POS[0] + 0.5) + lag:s(RC_POS[1] - 0.5) + lag].mean()
    neg = loop[s(RC_NEG[0] + 0.5) + lag:s(RC_NEG[1] - 0.5) + lag].mean()
    first = loop[s(RC_POS[0] + 0.5) + lag:s(RC_POS[0] + 1.5) + lag].mean()
    last = loop[s(RC_POS[1] - 1.5) + lag:s(RC_POS[1] - 0.5) + lag].mean()
    gain = pos - neg  # the holds are +0.5 and -0.5, one full scale apart
    held_ratio = last / first if abs(first) > 1e-6 else 0.0
    print(f"loopback: +0.5 FS out reads {pos:+.4f}, -0.5 reads {neg:+.4f}: gain {gain:.4f} "
          f"(in FS per out FS)")
    if held_ratio < 0.9:
        print(f"  DC is NOT getting through (the hold sags to {held_ratio:.0%}). Move the ES-10's "
              "jumper to DC; the CV channel needs it")
    else:
        print(f"  DC gets through: the hold keeps {held_ratio:.1%} of its level over 13 s")
    for name, ch in (("LP/HP", LP), ("BP", BP)):
        seg = w(RC_SAW[0] + 0.5, RC_SAW[1] - 0.5)[:, ch] - offset[ch]
        level = 20 * math.log10(np.sqrt(np.mean(seg ** 2)) + 1e-12)
        print(f"Wasp {name:5s} during the saw: {level:6.1f} dBFS rms"
              + ("  <- silent: check the patching" if level < -60 else ""))
    if args.meter_volts:
        out_vfs = args.meter_volts / 0.5
        in_vfs = out_vfs / gain
        print(f"ES-10: {out_vfs:.3f} V per full scale out, {in_vfs:.3f} V per full scale in")
        if args.save:
            rig.update(out_vfs=round(out_vfs, 4), in_vfs=round(in_vfs, 4), latency_samples=lag)
            save_rig(rig)
            print(f"saved to {rel(RIG)}")
    else:
        print("add --meter-volts (the meter on OUT 2 during the +0.5 hold) for volts per full scale")


def split(args):
    """Cut each recorded take into aligned training files:
    nam/    one 24-bit WAV per snapshot and output, the exact length of NAM's input file,
            ready for its trainer alongside input.wav
    gru/    one .npz per take: x (audio as played), cv_v (volts), res (the dial), y_lphp, y_bp
    listen/ the real Wasp on the sweep and held-out takes, and the held-out dry input
    """
    d = pathlib.Path(args.path)
    session = json.loads((d / "session.json").read_text())
    out_vfs = session["out_vfs"]
    for sub in ("nam", "gru", "listen"):
        (d / sub).mkdir(exist_ok=True)
    report, lines = [], []
    for t in session["takes"]:
        rec_path = d / "rec" / f"{t['name']}.wav"
        if not rec_path.exists():
            lines.append(f"{t['name']:22s} not recorded yet")
            continue
        play, _ = sf.read(d / "play" / f"{t['name']}.wav", dtype="float64", always_2d=True)
        rec, _ = sf.read(rec_path, dtype="float64", always_2d=True)
        lag, strength = latency(play[:, 2], rec[:, LOOP], t["burst_at"], BURST)
        a, b = t["rest_window"]
        offset = rec[a + lag:b + lag].mean(axis=0)
        y = rec - offset
        p, n = t["payload_at"], t["payload_len"]
        # The same lag must hold at the end, or the stream dropped samples mid-take.
        tail = 5 * SR
        if np.sqrt(np.mean(play[p + n - tail:p + n, 2] ** 2)) > 1e-4:
            end_lag, _ = latency(play[:, 2], rec[:, LOOP], p + n - tail, tail)
        else:
            end_lag = None
        x = play[p:p + n, 0]
        loop = y[p + lag:p + lag + n, LOOP]
        g = float(np.dot(loop, x) / np.dot(x, x))
        loop_esr = esr(g * x, loop)
        yl, yb = y[p + lag:p + lag + n, LP], y[p + lag:p + lag + n, BP]
        clipped = [nm for nm, ch in (("LP/HP", LP), ("BP", BP)) if np.abs(rec[:, ch]).max() >= 0.999]
        entry = {"take": t["name"], "role": t["role"], "latency": lag, "end_latency": end_lag,
                 "burst_strength": round(strength), "offset_fs": offset.round(6).tolist(),
                 "loop_gain": round(g, 5), "loop_esr_db": round(db(loop_esr), 1), "clipped": clipped}
        flags = []
        if end_lag is not None and end_lag != lag:
            flags.append(f"DROPOUT: lag {lag} at the start, {end_lag} at the end; record again")
        if strength < 20:
            flags.append("weak burst: is the loopback patched?")
        if db(loop_esr) > -30:
            flags.append(f"loopback only {db(loop_esr):.0f} dB clean: check the cable")
        if clipped:
            flags.append("full scale reached on " + ", ".join(clipped))
        if t["nam"]:
            for nm, sig in (("lphp", yl), ("bp", yb)):
                sf.write(d / "nam" / f"{t['name']}_{nm}.wav", np.clip(sig, -1, 1), SR, subtype="PCM_24")
                # NAM's own v3 check: the validation passage at the start and the end
                # should come out the same. Above 0.01 its trainer reports a failure.
                rep = esr(sig[:NAM_T_VALIDATE], sig[n - NAM_T_VALIDATE:n])
                entry[f"replicate_esr_{nm}"] = round(rep, 5)
                if rep > 0.01:
                    flags.append(f"{nm}: start and end differ (ESR {rep:.3f} > 0.01): the Wasp drifted?")
        cv_v = (play[p:p + n, CV] * out_vfs).astype(np.float32)
        np.savez(d / "gru" / f"{t['name']}.npz", x=x.astype(np.float32), cv_v=cv_v,
                 y_lphp=yl.astype(np.float32), y_bp=yb.astype(np.float32),
                 res=np.float32(session.get("res", np.nan)), mix=session.get("mix", ""),
                 role=t["role"], sr=SR, latency=lag)
        if t["role"] in ("sweep", "test"):
            for nm, sig in (("lphp", yl), ("bp", yb)):
                sf.write(d / "listen" / f"{t['name']}_wasp_{nm}.wav", np.clip(sig, -1, 1), SR, subtype="PCM_24")
            if t["role"] == "test":
                sf.write(d / "listen" / f"{t['name']}_dry.wav", x, SR, subtype="PCM_24")
        entry["flags"] = flags
        report.append(entry)
        rep_txt = "".join(f"  rep {nm} {entry[f'replicate_esr_{nm}']:.4f}" for nm in ("lphp", "bp")
                          if f"replicate_esr_{nm}" in entry)
        lines.append(f"{t['name']:22s} lag {lag:5d}  loop {g:.4f} ({db(loop_esr):5.1f} dB){rep_txt}"
                     + ("  OK" if not flags else "\n" + "\n".join(f"    ! {f}" for f in flags)))
    (d / "split.json").write_text(json.dumps(report, indent=2) + "\n")
    text = "\n".join(lines)
    (d / "report.txt").write_text(text + "\n")
    print(text)
    print(f"\nnam/ goes to NAM's trainer with {INPUT_WAV.name} as the input; gru/ is the GRU's data.")


# ------------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("devices", help="list audio devices").set_defaults(fn=devices)

    mk = sub.add_parser("make", help="write a play file").add_subparsers(dest="what", required=True)
    mk.add_parser("rigcheck", help="latency, offsets, DC and volts per full scale").set_defaults(fn=make_rigcheck)
    p = mk.add_parser("ladder", help="the test riff once per CV step, for choosing settings by ear")
    p.add_argument("--from", dest="cv_from", type=float, default=-4.0, help="first CV, volts")
    p.add_argument("--to", dest="cv_to", type=float, default=6.0, help="last CV, volts")
    p.add_argument("--step", type=float, default=1.0, help="volts per step")
    p.add_argument("--peak", type=float, default=5.0, help="riff level, volts peak")
    p.add_argument("--out-vfs", type=float, help="ES-10 volts per full scale (default: rig.json)")
    p.set_defaults(fn=make_ladder)
    p = mk.add_parser("session", help="all the takes for one Res position")
    p.add_argument("name", help="a folder name for this session, e.g. lp-res6")
    p.add_argument("--mix", choices=MIX_ENDS, required=True,
                   help="which end stop the Mix knob is at: lp (pure low-pass) or hp (pure high-pass)")
    p.add_argument("--res", type=float, required=True, help="the Res knob's position on its 0-10 dial")
    p.add_argument("--snapshots", help="the comparison session only: CV for each A2 snapshot, volts. "
                                       "Negative lists need the =: --snapshots=-2,0,1.5,3,4.5")
    p.add_argument("--cv-range", required=True, help="the GRU's sweep range, volts: --cv-range=-3,5")
    p.add_argument("--knobs", default="", help='Lev and Frq, as text: "Lev 7, Frq noon"')
    p.add_argument("--sweep-minutes", type=float, default=6.0, help="GRU sweep material (default 6)")
    p.add_argument("--test-peak", type=float, default=5.0, help="held-out riff level, volts peak")
    p.add_argument("--seed", type=int, default=124)
    p.add_argument("--out-vfs", type=float, help="ES-10 volts per full scale (default: rig.json)")
    p.add_argument("--overwrite", action="store_true")
    p.set_defaults(fn=make_session)

    p = sub.add_parser("record", help="play a file (or every take in a session) and record it")
    p.add_argument("path", help="a play .wav, or a session folder")
    p.add_argument("--only", help="session takes to (re)record, comma-separated")
    p.add_argument("--overwrite", action="store_true")
    p.add_argument("--device")
    p.add_argument("--play-channels", help="e.g. 9,10,11")
    p.add_argument("--rec-channels", help="e.g. 9,10,11")
    p.set_defaults(fn=record)

    p = sub.add_parser("check", help="read a recorded rig check")
    p.add_argument("path")
    p.add_argument("--meter-volts", type=float, help="the meter on OUT 2 during the +0.5 hold")
    p.add_argument("--save", action="store_true", help="write the results into rig.json")
    p.set_defaults(fn=check)

    p = sub.add_parser("split", help="cut a recorded session into NAM and GRU training files")
    p.add_argument("path", help="the session folder")
    p.set_defaults(fn=split)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
