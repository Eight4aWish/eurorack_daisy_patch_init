#!/usr/bin/env python3
"""
swords_capture.py — record the Behringer Swords (Mutable Blades clone), filter 1, for a GRU
that takes the cutoff and the resonance, both under CV. The design is ../SWORDS.md; read it
first. The signals, timing marks and alignment are wasp_capture.py's, unchanged.

    play file        ES-10       patched to
    ch 1 audio       OUT 1  -->  Swords In A (13)
    ch 2 cutoff CV   OUT 2  -->  Swords V/OCT A (14)
    ch 3 loopback    OUT 3  -->  ES-10 IN 3, one cable straight back
    ch 4 Res CV      OUT 4  -->  Swords Resonance CV In A (9), its attenuverter (8) fully clockwise

    recording        ES-10       from
    ch 1             IN 1   <--  Swords Filter 1 Out (33)
    ch 2             IN 2   <--  Swords Main Out (34), if patched (not trained on)
    ch 3             IN 3   <--  the loopback

Steps. Files land in captures/swords/ on this machine's own disk (git ignores captures/).

    .venv/bin/python tools/swords_capture.py rig            # write rig.json (check it)
    .venv/bin/python tools/swords_capture.py make cal       # step 0: about 3 minutes
    .venv/bin/python tools/swords_capture.py record captures/swords/cal.wav
    .venv/bin/python tools/swords_capture.py cal captures/swords/cal.rec.wav
    .venv/bin/python tools/swords_capture.py make session sw-a --cv-range=LO,HI --res-range=0,8
    .venv/bin/python tools/swords_capture.py record captures/swords/sw-a
    .venv/bin/python tools/swords_capture.py split captures/swords/sw-a
    .venv/bin/python tools/swords_capture.py region captures/swords/sw-a    # step 1's verdict
"""

import argparse
import datetime
import json
import math
import pathlib
import shutil
import sys

import numpy as np
import soundfile as sf
from scipy import signal

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import wasp_capture as W  # noqa: E402
from test_signal import make_synth  # noqa: E402

SR = W.SR
ROOT = HERE.parent / "captures" / "swords"
RIG = ROOT / "rig.json"
AUDIO, CUT, LOOP_OUT, RES = 0, 1, 2, 3  # play channels
F1, MAIN, LOOP = 0, 1, 2                # recording channels
REC_NAMES = ("F1", "main", "loop")
THRESHOLD = 0.02                        # SWORDS.md: back-to-back take-to-take ESR per hold

# The rig as the brief records it (CLAUDE.md, the recording chain): the Scarlett 16i16's
# ADAT playback 7-14 reach ES-10 OUT 1-8, its inputs 11-18 come from ES-10 IN 1-8, and the
# ES-10 was measured at 10.96 V per full scale out and 11.9712 in (2026-10-18).
RIG_DEFAULT = {"device": "Scarlett 16i16", "play_channels": [7, 8, 9, 10], "rec_channels": [11, 12, 13],
               "out_vfs": 10.96, "in_vfs": 11.9712}


def rel(p):
    try:
        return p.relative_to(HERE.parent)
    except ValueError:
        return p


def load_rig():
    if not RIG.exists():
        raise SystemExit(f"no {rel(RIG)}: run `swords_capture.py rig` first")
    return json.loads(RIG.read_text())


def cmd_rig(args):
    ROOT.mkdir(parents=True, exist_ok=True)
    if RIG.exists() and not args.overwrite:
        raise SystemExit(f"{rel(RIG)} exists; --overwrite to replace it")
    RIG.write_text(json.dumps(RIG_DEFAULT, indent=2) + "\n")
    print(f"wrote {rel(RIG)}: {RIG_DEFAULT}\n  From the brief, not measured today. The cal recording checks the "
          "loopback (latency and gain); Res answering on OUT 4 checks the fourth channel.")


def fs_of(volts, vfs, what):
    return W.to_fs(volts, vfs, what)


def take4(audio_fs, cut_fs, res_fs):
    """wasp_capture.take() with a fourth channel: both CVs ramp to their first values,
    settle, the burst marks time zero, then the payload."""
    n = len(audio_fs)
    out = np.zeros((W.PAYLOAD_AT + n + W.POST, 4), np.float32)
    ramp = 0.5 - 0.5 * np.cos(np.pi * np.arange(W.RAMP) / W.RAMP)
    for ch, cv in ((CUT, cut_fs), (RES, res_fs)):
        cv = np.broadcast_to(np.asarray(cv, np.float64), (n,))
        out[:W.RAMP, ch] = cv[0] * ramp
        out[W.RAMP:W.PAYLOAD_AT, ch] = cv[0]
        out[W.PAYLOAD_AT:W.PAYLOAD_AT + n, ch] = cv
        out[W.PAYLOAD_AT + n:, ch] = cv[-1]
    out[W.BURST_AT:W.BURST_AT + W.BURST, AUDIO] = W.burst()
    out[W.PAYLOAD_AT:W.PAYLOAD_AT + n, AUDIO] = audio_fs
    out[:, LOOP_OUT] = out[:, AUDIO]
    return out


# --------------------------------------------------------------------- step 0

CAL_STEPS = list(range(-4, 9))      # V/OCT, volts, for the cutoff calibration
CAL_STEP_S = 3.0
CAL_OSC_CUTS = (-2.0, 1.0, 4.0, 7.0)  # V/OCT under the Res ramps
CAL_RES_TOP = 8.0                   # the guide's Res CV range, 0 to +8 V
CAL_RAMP_S = 16.0
CAL_NOISE_RMS_V = 1.0


def cal_plan(steps=None, transfer_only=False):
    """The calibration payload's parts, in seconds from the payload's start."""
    parts, t = [], 0.0
    for v in (steps if steps is not None else CAL_STEPS):
        parts.append({"kind": "transfer", "t0": t, "t1": t + CAL_STEP_S, "cut_v": float(v), "res_v": 0.0})
        t += CAL_STEP_S
    if transfer_only:
        return parts, t
    for v in CAL_OSC_CUTS:
        parts.append({"kind": "settle", "t0": t, "t1": t + 1.0, "cut_v": v, "res_v": 0.0})
        t += 1.0
        parts.append({"kind": "res_ramp", "t0": t, "t1": t + 2 * CAL_RAMP_S, "cut_v": v, "res_top": CAL_RES_TOP})
        t += 2 * CAL_RAMP_S
    parts.append({"kind": "riff", "t0": t, "t1": t + 9.0, "cut_v": 7.0, "res_v": 0.0, "peak_v": 5.0})
    t += 9.0
    return parts, t


def make_cal(args):
    rig = load_rig()
    vfs = float(rig["out_vfs"])
    steps = None
    if args.steps:
        a_, b_, d_ = (float(v) for v in args.steps.split(","))
        steps = [round(float(v), 3) for v in np.arange(a_, b_ + 1e-9, d_)]
    parts, total = cal_plan(steps, args.transfer_only)
    n = int(total * SR)
    audio, cut, res = np.zeros(n), np.zeros(n), np.zeros(n)
    rng = np.random.default_rng(2026)
    riff = np.asarray(make_synth(), np.float64)
    for p in parts:
        a, b = int(p["t0"] * SR), int(p["t1"] * SR)
        cut[a:b] = p["cut_v"]
        if p["kind"] == "transfer":
            audio[a:b] = np.clip(rng.standard_normal(b - a) * CAL_NOISE_RMS_V, -4 * CAL_NOISE_RMS_V, 4 * CAL_NOISE_RMS_V)
        elif p["kind"] == "res_ramp":
            m = (b - a) // 2
            res[a:a + m] = np.linspace(0.0, p["res_top"], m)
            res[a + m:b] = np.linspace(p["res_top"], 0.0, b - a - m)
        elif p["kind"] == "riff":
            r = riff * p["peak_v"] / np.abs(riff).max()
            audio[a:a + len(r)] = r[:b - a]
            audio[a + len(r):a + 2 * len(r)] = r[:max(0, b - a - len(r))]
    # 5 ms steps between CV values, as everywhere else.
    k = SR // 200
    for arr in (cut, res):
        jumps = np.where(np.abs(np.diff(arr)) > 0.05)[0] + 1
        for j in jumps:
            lo = arr[j - 1]
            arr[j:j + k] = lo + (arr[j:j + k] - lo) * np.arange(1, min(k, n - j) + 1) / k
    ROOT.mkdir(parents=True, exist_ok=True)
    out = take4(fs_of(audio, vfs, "cal audio"), fs_of(cut, vfs, "cal cutoff CV"), fs_of(res, vfs, "cal Res CV"))
    sf.write(ROOT / f"{args.name}.wav", out, SR, subtype="FLOAT")
    (ROOT / f"{args.name}.json").write_text(json.dumps({"out_vfs": vfs, "payload_at": W.PAYLOAD_AT, "parts": parts}, indent=2))
    if args.transfer_only:
        print(f"wrote {rel(ROOT / (args.name + '.wav'))} ({len(out) / SR:.0f} s): noise, V/OCT "
              + ", ".join(f"{p['cut_v']:+g}" for p in parts) + " V")
        return
    riff_at = W.PAYLOAD_AT / SR + parts[-1]["t0"]
    print(f"wrote {rel(ROOT / 'cal.wav')} ({len(out) / SR:.0f} s)\n"
          f"  {W.PAYLOAD_AT / SR:.1f} s on: noise at {CAL_NOISE_RMS_V:g} V rms, V/OCT stepping "
          f"{CAL_STEPS[0]:+d} to {CAL_STEPS[-1]:+d} V, Res CV 0 (the cutoff against V/OCT)\n"
          f"  then: silence, Res CV ramping 0 -> {CAL_RES_TOP:g} -> 0 V at V/OCT "
          + ", ".join(f"{v:+g}" for v in CAL_OSC_CUTS) + " V (where it self-oscillates)\n"
          f"  {riff_at:.0f} s on: the riff at 5 V peak, filter open: watch the drive LED")


def cmd_cal(args):
    """Read the calibration recording: the cutoff against V/OCT, where the filter starts and
    stops self-oscillating as Res CV rises and falls, and the riff's level."""
    rig = load_rig()
    stem = pathlib.Path(args.path).name.split(".")[0]
    meta = json.loads((ROOT / f"{stem}.json").read_text())
    play, _ = sf.read(ROOT / f"{stem}.wav", dtype="float64", always_2d=True)
    rec, fs = sf.read(args.path, dtype="float64", always_2d=True)
    assert fs == SR
    lag, strength = W.latency(play[:, LOOP_OUT], rec[:, LOOP], W.BURST_AT, W.BURST)
    a0, b0 = W.REST_WINDOW
    rec = rec - rec[a0 + lag:b0 + lag].mean(axis=0)
    x = play[:, AUDIO]
    print(f"latency {lag} samples, burst {strength:.0f}x the floor"
          + ("" if strength > 20 else "  <- weak: is OUT 3 patched to IN 3?"))
    pa = meta["payload_at"]
    out_vfs, in_vfs = rig["out_vfs"], rig["in_vfs"]
    s = lambda t: pa + int(t * SR)  # noqa: E731
    result = {"latency": lag, "transfer": [], "osc": [], "riff": {}}
    edges = 20 * 2 ** (np.arange(0, 10 * 12 + 1) / 12)
    edges = edges[edges < 20000]
    print("\nCutoff against V/OCT (noise in, Res CV 0): the -3 dB point of filter 1's response")
    for p in meta["parts"]:
        if p["kind"] != "transfer":
            continue
        a, b = s(p["t0"] + 0.5), s(p["t1"] - 0.1)
        xi, yo = x[a:b], rec[a + lag:b + lag, F1]
        f, pxy = signal.csd(xi, yo, SR, nperseg=8192)
        _, pxx = signal.welch(xi, SR, nperseg=8192)
        h = np.abs(pxy) / (pxx + 1e-20) * (in_vfs / out_vfs)  # volts out per volt in
        band = [20 * np.log10(np.median(h[(f >= lo) & (f < hi)]) + 1e-12)
                for lo, hi in zip(edges[:-1], edges[1:]) if np.any((f >= lo) & (f < hi))]
        centres = [math.sqrt(lo * hi) for lo, hi in zip(edges[:-1], edges[1:]) if np.any((f >= lo) & (f < hi))]
        band = np.array(band)
        k = int(np.argmax(band))
        below = np.where(band[k:] < band[k] - 3)[0]
        fc = centres[k + below[0]] if len(below) else None
        gain = band[k]
        result["transfer"].append({"cut_v": p["cut_v"], "fc_hz": fc, "peak_db": round(float(gain), 1),
                                   "peak_hz": round(centres[k], 1)})
        print(f"  V/OCT {p['cut_v']:+5.1f} V   -3 dB at {('%7.0f Hz' % fc) if fc else '  (none) '}"
              f"   passband {gain:+5.1f} dB at {centres[k]:6.0f} Hz")
    # Only steps whose passband shows (within 1 dB of the best): below that the cutoff is
    # under 20 Hz, the lowest band is already in the roll-off, and its "-3 dB" is the
    # method's floor, not the filter (2026-10-21: those points pulled the fit to 0.52).
    best = max(r["peak_db"] for r in result["transfer"])
    pts = [(r["cut_v"], r["fc_hz"]) for r in result["transfer"]
           if r["fc_hz"] and r["peak_db"] >= best - 1.0 and 30 < r["fc_hz"] < 15000]
    if len(pts) >= 3:
        v, hz = np.array(pts).T
        slope, icpt = np.polyfit(v, np.log2(hz), 1)
        result["fit"] = {"octaves_per_volt": round(float(slope), 3), "log2_hz_at_0v": round(float(icpt), 3)}
        print(f"  fit: log2(fc) = {icpt:.2f} + {slope:.3f} V   ({slope:.3f} octaves per volt;"
              f" 0 V -> {2 ** icpt:.0f} Hz)")
    print("\nSelf-oscillation (no input), Res CV ramping up then down at each V/OCT")
    for p in meta["parts"]:
        if p["kind"] != "res_ramp":
            continue
        a, b = s(p["t0"]), s(p["t1"])
        y = rec[a + lag:b + lag, F1]
        resv = play[a:b, RES] * out_vfs
        w = SR // 50
        nwin = len(y) // w
        rms = np.sqrt(np.mean(y[:nwin * w].reshape(nwin, w) ** 2, axis=1))
        rv = resv[:nwin * w].reshape(nwin, w).mean(axis=1)
        floor = np.median(rms[:SR // w]) + 1e-9
        loud = rms > 10 * floor
        half = nwin // 2
        up = np.where(loud[:half])[0]
        down = np.where(loud[half:])[0]
        on = float(rv[up[0]]) if len(up) else None
        off = float(rv[half + down[-1]]) if len(down) else None
        top = y[(half - SR // w) * w:(half + SR // w) * w]
        fosc = None
        if len(top) and np.sqrt(np.mean(top ** 2)) > 10 * floor:
            spec = np.abs(np.fft.rfft(top * np.hanning(len(top))))
            fosc = float(np.fft.rfftfreq(len(top), 1 / SR)[np.argmax(spec[1:]) + 1])
        level = 20 * np.log10(rms.max() * in_vfs + 1e-12)
        result["osc"].append({"cut_v": p["cut_v"], "starts_at_res_v": on, "stops_at_res_v": off,
                              "freq_at_top_hz": fosc, "peak_v_db": round(float(level), 1)})
        print(f"  V/OCT {p['cut_v']:+5.1f} V   starts at Res {('%4.2f V' % on) if on is not None else ' never '}"
              f"   stops at {('%4.2f V' % off) if off is not None else '  -   '}"
              f"   at the top {('%6.0f Hz' % fosc) if fosc else '   -   '}, loudest {level:+5.1f} dBV")
    p = meta["parts"][-1]
    if p["kind"] != "riff":
        (ROOT / f"{stem}_result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(f"\nsaved {rel(ROOT / (stem + '_result.json'))}")
        return
    a, b = s(p["t0"]), s(p["t1"])
    yo = rec[a + lag:b + lag, F1]
    pk = float(np.abs(yo).max())
    result["riff"] = {"out_peak_fs": round(pk, 4), "out_peak_v": round(pk * in_vfs, 2),
                      "in_peak_v": p["peak_v"]}
    print(f"\nThe riff, {p['peak_v']:g} V peak in, filter open: {pk * in_vfs:.2f} V peak out "
          f"({20 * math.log10(pk + 1e-12):+.1f} dBFS at the ES-10)"
          + ("  <- full scale reached: lower the level" if pk > 0.98 else ""))
    (ROOT / f"{stem}_result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"\nsaved {rel(ROOT / (stem + '_result.json'))}")


# ----------------------------------------------------------------- the session

def test_take_cv(lo, hi):
    """The Wasp's `test` CV path: six holds of 4 s across the range, then a 16 s ramp up and
    back, so the two filters' test takes line up."""
    points = list(np.linspace(lo, hi, 6))
    return np.concatenate([W.held(points, 4.0, ramp_from=points[0]),
                           np.linspace(lo, hi, 16 * SR), np.linspace(hi, lo, 16 * SR)]), points


def make_session(args):
    rig = load_rig()
    vfs = float(rig["out_vfs"])
    lo, hi = sorted(float(v) for v in args.cv_range.split(","))
    rlo, rhi = sorted(float(v) for v in args.res_range.split(","))
    test_res = ([float(v) for v in args.test_res.split(",")] if args.test_res
                else [round(float(v), 2) for v in np.linspace(rlo, rhi, args.test_points)])
    fs_of([lo, hi, rlo, rhi], vfs, "CV")
    peaks_v = [v for v in (0.5, 1.0, 2.0, 3.5, 5.0, 7.0) if v <= W.FS_MAX * vfs]
    peaks_fs = [v / vfs for v in peaks_v]
    d = ROOT / args.name
    if (d / "session.json").exists() and not args.overwrite:
        raise SystemExit(f"{rel(d)} already has a session; --overwrite to replace it")
    (d / "play").mkdir(parents=True, exist_ok=True)
    takes = []

    def add(name, role, audio_fs, cut_v, res_v, **extra):
        n = len(audio_fs)
        cut_v = np.broadcast_to(np.asarray(cut_v, np.float64), (n,))
        res_v = np.broadcast_to(np.asarray(res_v, np.float64), (n,))
        sf.write(d / "play" / f"{name}.wav", take4(audio_fs, cut_v / vfs, res_v / vfs), SR, subtype="FLOAT")
        takes.append({"name": name, "role": role, "payload_at": W.PAYLOAD_AT, "payload_len": n,
                      "burst_at": W.BURST_AT, "rest_window": W.REST_WINDOW,
                      "seconds": round((W.PAYLOAD_AT + n + W.POST) / SR, 1), **extra})
        print(f"  {name:22s} {takes[-1]['seconds']:6.1f} s  {role}")

    print(f"{rel(d)}/play:")
    # Step 1: the test take three times back to back at each Res value; the first of each
    # set is also the held-out test take at that Res.
    cv, points = test_take_cv(lo, hi)
    bars = len(cv) // (4 * SR)
    # The Wasp's own test riff (its sessions' default seed, 124, + 7919), whatever --seed
    # is, so the two filters' test takes play the same notes.
    test_audio = W.phrase(np.random.default_rng(124 + 7919), 73.42, bars=bars,
                          peak=float(fs_of(args.test_peak, vfs, "test level")))
    for r in test_res:
        for k in range(1, args.reps + 1):
            add(f"test_r{r:+.2f}_{k}", "test", test_audio, cv[:len(test_audio)], r, test_res=r, rep=k)
    # Step 2: the training material, in halves, each from its own seed. Within a half,
    # 3-minute takes; the cutoff and Res each an independent walk (wasp_capture.sweep_cv).
    n_takes = math.ceil(args.minutes / 3.0)
    for h in range(1, args.halves + 1):
        for i in range(1, n_takes + 1):
            sub = np.random.default_rng([args.seed, h, i])
            audio = W.sweep_audio(args.minutes * 60 / n_takes, sub, peaks_fs)
            rc = np.random.default_rng([args.seed, h, i, 1])
            cut = W.sweep_cv(len(audio), sub, lo, hi, start=sub.uniform(lo, hi))
            res = W.sweep_cv(len(audio), rc, rlo, rhi, start=rc.uniform(rlo, rhi))
            add(f"sweep{h}_{i}", "sweep", audio, cut, res, half=h)
    session = {
        "module": "swords", "filter": 1, "name": args.name,
        "made": datetime.datetime.now().isoformat(timespec="seconds"), "sr": SR,
        "out_vfs": vfs, "in_vfs": rig.get("in_vfs"), "cv_range_v": [lo, hi], "res_range_v": [rlo, rhi],
        "test_res_v": test_res, "test_holds_v": [float(v) for v in points], "reps": args.reps,
        "minutes_per_half": args.minutes, "halves": args.halves, "sweep_peaks_v": peaks_v,
        "test_peak_v": args.test_peak, "seed": args.seed, "panel": args.panel,
        "channels": {"play": ["audio -> Swords In A (13)", "cutoff CV -> V/OCT A (14)",
                              "loopback -> ES-10 IN 3", "Res CV -> Resonance CV In A (9)"],
                     "rec": ["Swords Filter 1 Out (33)", "Swords Main Out (34)", "loopback"]},
        "takes": takes,
    }
    (d / "session.json").write_text(json.dumps(session, indent=2) + "\n")
    total = sum(t["seconds"] for t in takes)
    print(f"{len(takes)} takes, {total / 60:.1f} min to record")


# --------------------------------------------------------------------- record

def record(args):
    try:
        import sounddevice as sd
    except ImportError:
        raise SystemExit("record needs sounddevice: .venv/bin/pip install sounddevice")
    rig = load_rig()
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
        need = data.nbytes * len(rig["rec_channels"]) // data.shape[1] * 3
        free = shutil.disk_usage(rec.parent if rec.parent.exists() else play.parent).free
        if free < need:
            raise SystemExit(f"{play.name}: only {free / 1e9:.1f} GB free, {need / 1e9:.1f} GB wanted")
        if fs != SR or data.shape[1] != len(rig["play_channels"]):
            raise SystemExit(f"{play.name}: {fs} Hz, {data.shape[1]} channels; expected {SR} Hz, "
                             f"{len(rig['play_channels'])}")
        print(f"{play.name}: {len(data) / SR:.0f} s ...", end="", flush=True)
        try:
            r = sd.playrec(data, samplerate=SR, device=rig["device"], dtype="float32", latency="high",
                           output_mapping=rig["play_channels"], input_mapping=rig["rec_channels"])
            status = sd.wait()
        except KeyboardInterrupt:
            sd.stop()
            raise SystemExit("\nstopped; nothing saved for this take")
        rec.parent.mkdir(parents=True, exist_ok=True)
        sf.write(rec, r, SR, subtype="FLOAT")
        peaks = 20 * np.log10(np.abs(r).max(axis=0) + 1e-12)
        print(" peaks " + "  ".join(f"{n} {p:6.1f}" for n, p in zip(REC_NAMES, peaks)) + " dBFS")
        if status:
            print(f"  WARNING: the audio stream reported {status}; record this take again")
        if peaks[LOOP] < -60:
            print("  WARNING: nothing came back on the loopback: check OUT 3 -> IN 3 and rec_channels")
        if peaks[F1] > -0.1:
            print("  WARNING: Filter 1 Out reached full scale at the ES-10 input")


# ---------------------------------------------------------------------- split

def split(args):
    """Each recorded take as gru/<take>.npz: x (audio as played), cv_v and res_v (volts, per
    sample), y_f1 and y_main (the outputs, aligned, cut CAUSAL_MARGIN late), plus res (the
    held Res for a test take, -1 for a sweep, whose Res moves) and role."""
    d = pathlib.Path(args.path)
    session = json.loads((d / "session.json").read_text())
    vfs = session["out_vfs"]
    (d / "gru").mkdir(exist_ok=True)
    (d / "listen").mkdir(exist_ok=True)
    report, lines = [], []
    for t in session["takes"]:
        rec_path = d / "rec" / f"{t['name']}.wav"
        if not rec_path.exists():
            lines.append(f"{t['name']:22s} not recorded yet")
            continue
        play, _ = sf.read(d / "play" / f"{t['name']}.wav", dtype="float64", always_2d=True)
        rec, _ = sf.read(rec_path, dtype="float64", always_2d=True)
        lag, strength = W.latency(play[:, LOOP_OUT], rec[:, LOOP], t["burst_at"], W.BURST)
        a, b = t["rest_window"]
        y = rec - rec[a + lag:b + lag].mean(axis=0)
        p, n = t["payload_at"], t["payload_len"]
        x = play[p:p + n, AUDIO]
        g, loop_esr, frac = W.band_gain(x, y[p + lag:p + lag + n, LOOP])
        cut = int(np.floor(lag + frac)) - W.CAUSAL_MARGIN
        tail = 5 * SR
        end_lag = (W.latency(play[:, LOOP_OUT], rec[:, LOOP], p + n - tail, tail)[0]
                   if np.sqrt(np.mean(play[p + n - tail:p + n, LOOP_OUT] ** 2)) > 1e-4 else None)
        flags = []
        if end_lag is not None and abs(end_lag - lag) > 2:
            flags.append(f"DROPOUT: lag {lag} at the start, {end_lag} at the end; record again")
        if strength < 20:
            flags.append("weak burst: is the loopback patched?")
        if W.db(loop_esr) > -30:
            flags.append(f"loopback only {W.db(loop_esr):.0f} dB clean: check the cable")
        if np.abs(rec[:, F1]).max() >= 0.999:
            flags.append("full scale reached on Filter 1 Out")
        y1, ym = y[p + cut:p + cut + n, F1], y[p + cut:p + cut + n, MAIN]
        np.savez(d / "gru" / f"{t['name']}.npz", x=x.astype(np.float32),
                 cv_v=(play[p:p + n, CUT] * vfs).astype(np.float32),
                 res_v=(play[p:p + n, RES] * vfs).astype(np.float32),
                 y_f1=y1.astype(np.float32), y_main=ym.astype(np.float32),
                 res=np.float32(t.get("test_res", -1.0)), role=t["role"], half=t.get("half", 0),
                 rep=t.get("rep", 0), sr=SR, latency=lag + frac, cut_at=cut)
        if t["role"] == "test" and t.get("rep") == 1:
            sf.write(d / "listen" / f"{t['name']}_swords.wav", np.clip(y1, -1, 1), SR, subtype="PCM_24")
        report.append({"take": t["name"], "role": t["role"], "latency": lag, "end_latency": end_lag,
                       "loop_esr_db": round(W.db(loop_esr), 1), "cut_at": cut, "flags": flags})
        lines.append(f"{t['name']:22s} lag {lag:5d}  loop {g:.4f} ({W.db(loop_esr):5.1f} dB)"
                     + ("  OK" if not flags else "\n" + "\n".join(f"    ! {f}" for f in flags)))
    (d / "split.json").write_text(json.dumps(report, indent=2) + "\n")
    print("\n".join(lines))


# --------------------------------------------------------------------- region

def align(a, b):
    """b shifted onto a by the whole-plus-fractional lag that best matches them (within
    +-8 samples): each take is cut at its own sub-sample delay."""
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    A, B = np.fft.rfft(a), np.fft.rfft(b)
    xc = np.fft.irfft(A * np.conj(B), n)
    k = int(np.argmax(np.r_[xc[-8:], xc[:9]])) - 8
    y0, y1, y2 = (xc[(k + dd) % n] for dd in (-1, 0, 1))
    frac = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-30)
    return np.fft.irfft(B * np.exp(2j * np.pi * np.fft.rfftfreq(n) * frac), n)


def region(args):
    """Step 1's verdict: for each Res value and each hold of the test take, the back-to-back
    take-to-take ESR (mean of rep 1 against 2 and 2 against 3), and whether it is within
    THRESHOLD: the judged region (SWORDS.md)."""
    d = pathlib.Path(args.path)
    s = json.loads((d / "session.json").read_text())
    holds = s["test_holds_v"]
    rows = []
    print(f"take-to-take ESR per hold (back to back); judged where <= {THRESHOLD}")
    print("Res CV  " + "".join(f"{h:+8.2f}V" for h in holds))
    for r in s["test_res_v"]:
        reps = []
        for k in range(1, s["reps"] + 1):
            f = d / "gru" / f"test_r{r:+.2f}_{k}.npz"
            if f.exists():
                reps.append(np.load(f)["y_f1"].astype(np.float64))
        if len(reps) < 2:
            print(f"{r:+6.2f}  (fewer than two takes split)")
            continue
        ref = reps[0]
        al = [ref] + [align(ref, t) for t in reps[1:]]
        n = min(map(len, al))
        cells = []
        for i in range(len(holds)):
            a, b = int((4 * i + 0.5) * SR), int((4 * i + 3.9) * SR)
            e = np.mean([W.esr(al[j][a:b], al[j + 1][a:b]) for j in range(len(al) - 1)]) if b <= n else float("nan")
            cells.append(float(e))
        rows.append({"res_v": r, "esr": cells, "judged": [bool(c <= THRESHOLD) for c in cells]})
        print(f"{r:+6.2f}  " + "".join(f"{c:9.4f}{'*' if c <= THRESHOLD else ' '}" for c in cells))
    out = {"threshold": THRESHOLD, "holds_v": holds, "rows": rows}
    (d / "region.json").write_text(json.dumps(out, indent=2) + "\n")
    print(f"* = judged.  saved {rel(d / 'region.json')}")


# ----------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("rig", help="write rig.json from the brief's values")
    p.add_argument("--overwrite", action="store_true")
    p.set_defaults(fn=cmd_rig)
    mk = sub.add_parser("make", help="write a play file").add_subparsers(dest="what", required=True)
    p = mk.add_parser("cal", help="step 0: cutoff against V/OCT, self-oscillation, level")
    p.add_argument("--name", default="cal", help="file name under captures/swords")
    p.add_argument("--steps", help="V/OCT steps as from,to,step, volts (default -4..8 in 1)")
    p.add_argument("--transfer-only", action="store_true", help="only the noise steps")
    p.set_defaults(fn=make_cal)
    p = mk.add_parser("session", help="steps 1 and 2: the repeat takes and the training material")
    p.add_argument("name")
    p.add_argument("--cv-range", required=True, help="V/OCT range, volts: --cv-range=-2,6")
    p.add_argument("--res-range", default="0,8", help="Res CV range, volts")
    p.add_argument("--test-res", help="Res CV values for the repeat takes (default: --test-points across the range)")
    p.add_argument("--test-points", type=int, default=8)
    p.add_argument("--reps", type=int, default=3)
    p.add_argument("--minutes", type=float, default=30.0, help="training material per half")
    p.add_argument("--halves", type=int, default=2)
    p.add_argument("--test-peak", type=float, default=5.0)
    p.add_argument("--seed", type=int, default=125)
    p.add_argument("--panel", default="Mode LP (CCW), Drive 11 o'clock, Response CCW, Res knob CCW, "
                                      "Res att CW, Freq att noon, Shift off, Routing CCW; Freq knob: see notes")
    p.add_argument("--overwrite", action="store_true")
    p.set_defaults(fn=make_session)
    p = sub.add_parser("record", help="play a file (or every take in a session) and record it")
    p.add_argument("path")
    p.add_argument("--only")
    p.add_argument("--overwrite", action="store_true")
    p.set_defaults(fn=record)
    p = sub.add_parser("cal", help="read the calibration recording")
    p.add_argument("path")
    p.set_defaults(fn=cmd_cal)
    p = sub.add_parser("split", help="cut a recorded session into GRU files")
    p.add_argument("path")
    p.set_defaults(fn=split)
    p = sub.add_parser("region", help="step 1: where Swords repeats itself, per hold and Res")
    p.add_argument("path")
    p.set_defaults(fn=region)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
