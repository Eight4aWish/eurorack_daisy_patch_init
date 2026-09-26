#!/usr/bin/env python3
"""
quantisation_study.py — how few bits can A2's weights survive?

The single most transferable question before any fixed-point work on the
Tiliqua: at what precision does A2 stop sounding like A2? Answer it on the Mac,
in an afternoon, before writing a line of HDL.

How it works, and why this way:

    The float reference is the REAL engine, not a reimplementation.
    nam_a2_runtime.h is portable — standard library only — so tools/a2_host.cpp
    compiles it natively and this script drives it. A numpy rewrite of A2 would
    risk being subtly wrong, and the study would then be measuring the rewrite's
    bugs instead of A2's tolerance.

    Quantising the weights and handing them back to the same engine isolates one
    variable. Everything else — layer order, activations, history handling — is
    bit-identical between reference and test.

WHAT THIS DOES NOT MEASURE, and it matters:

    Only WEIGHT quantisation. A real fixed-point engine also quantises
    activations and accumulators, which this leaves in float. So the numbers
    here are a LOWER BOUND on the damage — an optimistic answer.

    Read it as a veto rather than a permit. If A2 already falls apart at 12-bit
    weights, fixed point at 12 bits is dead and no amount of careful accumulator
    design saves it. If it survives 8-bit weights comfortably, that is necessary
    but not sufficient, and activations are the next thing to study.

Two scaling modes, because the granularity is a real design decision:

    global   one scale across all 1871 weights. Simplest in hardware, worst
             case for accuracy when magnitudes vary between layers.
    chunked  a separate scale per contiguous run of N weights, approximating
             the per-tensor or per-channel scaling a real engine would use.
             Costs a small table of scales; usually buys several bits.

Usage:
    python3 tools/quantisation_study.py                 # sweep 16..6 bits
    python3 tools/quantisation_study.py --bits 12 10 8
    python3 tools/quantisation_study.py --input di.wav  # your own audio
    python3 tools/quantisation_study.py --keep-wavs     # write A/B files

Outputs WAVs into quant_study/ so the numbers can be checked by ear, which is
the only judge that counts.
"""

import argparse
import array
import math
import pathlib
import struct
import subprocess
import sys
import wave

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
HEADER = PROJECT / "nam" / "model_data_nam_a2.h"
HARNESS = HERE / "a2_host"
SAMPLE_RATE = 48000

sys.path.insert(0, str(HERE))
from export_captures import parse_header  # noqa: E402  (same directory)


# ---------------------------------------------------------------------------
# Test signal
# ---------------------------------------------------------------------------

def make_pluck(seconds=2.0, freq=110.0, sr=SAMPLE_RATE):
    """Karplus-Strong pluck. Far more revealing of distortion artefacts than a
    sine sweep: it has a dense harmonic stack that decays, so quantisation noise
    shows up in the tail where nothing is masking it."""
    n = int(seconds * sr)
    period = max(2, int(sr / freq))
    # Deterministic noise burst, so runs are comparable.
    state = 12345
    buf = []
    for _ in range(period):
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        buf.append((state / 0x3FFFFFFF) - 1.0)

    out = array.array("f", [0.0]) * 0
    out = array.array("f", bytes(4 * n))
    idx = 0
    for i in range(n):
        v = buf[idx]
        out[i] = v
        nxt = buf[(idx + 1) % period]
        buf[idx] = 0.996 * 0.5 * (v + nxt)
        idx = (idx + 1) % period
    return out


def make_sweep(seconds=2.0, f0=30.0, f1=6000.0, sr=SAMPLE_RATE):
    n = int(seconds * sr)
    out = array.array("f", bytes(4 * n))
    k = math.log(f1 / f0)
    for i in range(n):
        t = i / sr
        phase = 2.0 * math.pi * f0 * seconds * (math.exp(k * t / seconds) - 1.0) / k
        out[i] = math.sin(phase)
    return out


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit(f"{path}: expected 16-bit PCM")
        frames = w.readframes(w.getnframes())
        ch = w.getnchannels()
    pcm = array.array("h")
    pcm.frombytes(frames)
    out = array.array("f", bytes(4 * (len(pcm) // ch)))
    for i in range(len(out)):
        out[i] = pcm[i * ch] / 32768.0
    return out


def write_wav(path, samples):
    pcm = array.array("h", bytes(2 * len(samples)))
    for i, v in enumerate(samples):
        s = int(max(-1.0, min(1.0, v)) * 32767.0)
        pcm[i] = s
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm.tobytes())


def scale_peak(samples, peak):
    m = max((abs(v) for v in samples), default=0.0)
    if m <= 0.0:
        return samples
    g = peak / m
    for i in range(len(samples)):
        samples[i] *= g
    return samples


# ---------------------------------------------------------------------------
# Quantisation
# ---------------------------------------------------------------------------

def quantise(weights, bits, chunk=None):
    """Symmetric uniform quantisation. chunk=None gives one global scale;
    otherwise a separate scale per contiguous run of `chunk` weights."""
    qmax = (1 << (bits - 1)) - 1
    out = array.array("f", weights)
    n = len(weights)
    step = n if chunk is None else chunk

    for start in range(0, n, step):
        end = min(start + step, n)
        peak = max((abs(w) for w in weights[start:end]), default=0.0)
        if peak <= 0.0:
            continue
        scale = peak / qmax
        for i in range(start, end):
            q = int(round(weights[i] / scale))
            q = max(-qmax, min(qmax, q))
            out[i] = q * scale
    return out


# ---------------------------------------------------------------------------
# Running the engine
# ---------------------------------------------------------------------------

def run_engine(weights, audio, workdir, tag):
    wpath = workdir / f"w_{tag}.f32"
    ipath = workdir / "in.f32"
    opath = workdir / f"out_{tag}.f32"

    wpath.write_bytes(array.array("f", weights).tobytes())
    if not ipath.exists():
        ipath.write_bytes(array.array("f", audio).tobytes())

    r = subprocess.run([str(HARNESS), str(wpath), str(ipath), str(opath)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"a2_host failed ({tag}): {r.stderr.strip()}")

    out = array.array("f")
    out.frombytes(opath.read_bytes())
    return out


def metrics(ref, test):
    """Error-to-signal ratio, the measure this literature uses, plus a plain
    peak error. ESR is scale-free, so it does not flatter a quiet output."""
    n = min(len(ref), len(test))
    num = 0.0
    den = 0.0
    peak_err = 0.0
    for i in range(n):
        d = ref[i] - test[i]
        num += d * d
        den += ref[i] * ref[i]
        if abs(d) > peak_err:
            peak_err = abs(d)
    if den <= 0.0:
        return float("inf"), peak_err
    esr = num / den
    return esr, peak_err


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", default="kWeightsJcm800",
                    help="weight symbol in model_data_nam_a2.h")
    ap.add_argument("--bits", type=int, nargs="+",
                    default=[16, 14, 12, 10, 9, 8, 7, 6])
    ap.add_argument("--chunk", type=int, default=64,
                    help="weights per scale in chunked mode (0 = global only)")
    ap.add_argument("--signal", choices=["pluck", "sweep"], default="pluck")
    ap.add_argument("--input", help="16-bit mono WAV to use instead")
    ap.add_argument("--peak", type=float, default=0.5,
                    help="normalise input to this peak; A2 is nonlinear, so "
                         "level changes the answer")
    ap.add_argument("--keep-wavs", action="store_true")
    ap.add_argument("--out", default=str(PROJECT / "quant_study"))
    args = ap.parse_args()

    if not HARNESS.exists():
        raise SystemExit(
            f"harness not built. Run:\n"
            f"  c++ -std=c++17 -O2 -I{PROJECT} -o {HARNESS} {HERE / 'a2_host.cpp'}")

    arrays, _ = parse_header(HEADER)
    weights = arrays.get(args.capture)
    if not weights:
        raise SystemExit(f"no weights named {args.capture}. "
                         f"Have: {', '.join(sorted(arrays))}")

    workdir = pathlib.Path(args.out)
    workdir.mkdir(parents=True, exist_ok=True)

    if args.input:
        audio = read_wav(args.input)
    elif args.signal == "sweep":
        audio = make_sweep()
    else:
        audio = make_pluck()
    scale_peak(audio, args.peak)

    print(f"capture   {args.capture}  ({len(weights)} weights)")
    print(f"signal    {args.input or args.signal}, {len(audio)} samples, "
          f"peak {args.peak}")
    print()

    ref = run_engine(weights, audio, workdir, "float")
    if args.keep_wavs:
        write_wav(workdir / "out_float32.wav", ref)

    modes = [("global", None)]
    if args.chunk > 0:
        modes.append((f"chunk{args.chunk}", args.chunk))

    for mode_name, chunk in modes:
        print(f"  {mode_name} scaling")
        print(f"    {'bits':>4}  {'ESR':>12}  {'ESR dB':>8}  {'peak err':>9}")
        for bits in args.bits:
            q = quantise(weights, bits, chunk)
            tag = f"{mode_name}_{bits}"
            out = run_engine(q, audio, workdir, tag)
            esr, perr = metrics(ref, out)
            db = 10.0 * math.log10(esr) if esr > 0 else -999.0
            print(f"    {bits:>4}  {esr:>12.3e}  {db:>8.1f}  {perr:>9.5f}")
            if args.keep_wavs:
                write_wav(workdir / f"out_{tag}bit.wav", out)
        print()

    print(f"files in {workdir}")
    print()
    print("Reading this: ESR is error power over signal power, so lower is")
    print("better and -40 dB or so is where differences stop being obvious on")
    print("most material. Trust your ears over the number — and remember this")
    print("is weights only, so a real fixed-point engine will do worse.")


if __name__ == "__main__":
    main()
