#!/usr/bin/env python3
"""gran_check.py: nam/gran_layer.h (through tools/gran_host) against wasp_layer.Stream.

    tools/gran_check.py                       a synthetic layer (random banks), anywhere
    tools/gran_check.py --grnb FILE.grnb      a real version-2 file's layer

Builds tools/gran_host if it is missing or older than its sources, then runs both on the
same input and controls, and reports, for each test, the difference between them as a
fraction of the output's energy (ESR) and its largest sample. The tests:

  identity   Res 6, below the phase-in: the output must be the input, kDelay late
  res 9      a constant Res 9 with the cutoff stepping 2 -> 10 V
  res 10     the same at Res 10
  ramp       Res rising 7.5 -> 10 with the cutoff sweeping: the phase-in and both banks

The input is a riff-like test signal (two detuned saws through a soft clip), so the check
needs no captures. Passing means ESR under 1e-8 everywhere: float32 against float64.
"""
import argparse
import pathlib
import subprocess
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import grnb  # noqa: E402
import wasp_layer as WL  # noqa: E402

HOST = HERE / "gran_host"
SOURCES = [HERE / "gran_host.cpp", ROOT / "nam" / "gran_layer.h", ROOT / "nam" / "gru_runtime.h"]


def build_host():
    if HOST.exists() and all(HOST.stat().st_mtime > s.stat().st_mtime for s in SOURCES):
        return
    subprocess.run(["c++", "-std=c++17", "-O2", "-I", str(ROOT), str(SOURCES[0]), "-o", str(HOST)], check=True)


def synthetic_layer(seed=7):
    """Two banks shaped like the Wasp's (107 bands, 3 takes, grains of 11), random gains
    within +-10.5 dB, the CV sweeping 0 -> 10 -> 0 V."""
    rng = np.random.default_rng(seed)
    banks = []
    for res, slope in ((9.0, 1.0), (10.0, 0.96)):
        F, K, B = 2917, 3, 107
        t = np.arange(F) / (F - 1)
        cv = (10 * (1 - np.abs(2 * t - 1))).astype(np.float32)
        g = np.clip(rng.normal(0, 4, (K, F, B)), -10.5, 10.5)
        banks.append({"res": res, "slope": slope, "lc0": float(np.log2(40 * 2 ** (0.5 / 12))), "dlc": 1 / 12,
                      "cv": cv, "gains": g, "sources": F - 11 + 1, "L": 11, "limit": 10.5})
    return WL.pack(banks)


def test_signal(n, seed=3):
    rng = np.random.default_rng(seed)
    t = np.arange(n) / WL.SR
    f = 110 * 2 ** (np.floor(t * 4) % 5 / 12)
    saw = lambda fr: 2 * ((np.cumsum(fr) / WL.SR) % 1.0) - 1
    x = 0.3 * np.tanh(1.5 * (saw(f) + saw(f * 1.006))) + 0.001 * rng.standard_normal(n)
    return x.astype(np.float32)


def run_host(path, x, cut, res, seed, work):
    inp, out = work / "gran_in.bin", work / "gran_out.bin"
    with open(inp, "wb") as f:
        f.write(np.uint32(len(x)).tobytes() + x.astype("<f4").tobytes() + cut.astype("<f4").tobytes()
                + res.astype("<f4").tobytes())
    subprocess.run([str(HOST), str(path), str(inp), str(out), str(seed)], check=True, capture_output=True)
    y = np.fromfile(out, "<f4")
    return y[:len(x)].astype(np.float64), y[len(x):].astype(np.float64)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--grnb", help="a version-2 .grnb whose layer to check")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--work", default="/tmp")
    args = ap.parse_args()
    work = pathlib.Path(args.work)
    build_host()
    if args.grnb:
        path = pathlib.Path(args.grnb)
        layer_bytes = grnb.unpack(path.read_bytes(), with_layer=True)[-1]
    else:
        layer_bytes = synthetic_layer()
        weights = [0.0] * grnb.weight_count(16, 3)
        path = work / "gran_synthetic.grnb"
        path.write_bytes(grnb.pack("SYNTH", 1.0, 16, ["audio", "cv", "res"], [1.0, 0.1, 0.1], weights, layer_bytes))
    layer = WL.unpack(layer_bytes)
    n = int(args.seconds * WL.SR) // WL.BLOCK * WL.BLOCK
    nb = n // WL.BLOCK
    x = test_signal(n)
    tb = np.arange(nb) * WL.BLOCK / WL.SR
    steps = np.clip(2 + 2 * np.floor(tb / 4), 2, 10).astype(np.float32)
    sweep = (5 + 5 * np.sin(2 * np.pi * tb / 7)).astype(np.float32)
    tests = {"identity": (steps, np.full(nb, 6.0, np.float32)),
             "res 9": (steps, np.full(nb, 9.0, np.float32)),
             "res 10": (steps, np.full(nb, 10.0, np.float32)),
             "ramp": (sweep, np.linspace(7.5, 10.0, nb).astype(np.float32))}
    ok = True
    seed = 0x2545F491
    for name, (cut, res) in tests.items():
        y_c, dry_c = run_host(path, x, cut, res, seed, work)
        y_p = WL.Stream(layer, seed).run(x, cut, res)
        settle = WL.NFFT + WL.DELAY  # the first frames have fewer overlaps
        e = y_c[settle:] - y_p[settle:]
        esr = float(np.sum(e ** 2) / np.sum(y_p[settle:] ** 2))
        line = f"  {name:9s} C++ vs Python: ESR {esr:.1e}, largest difference {np.max(np.abs(e)):.1e}"
        if name == "identity":
            ref = WL.dry(x.astype(np.float64))
            e2 = y_c[settle:] - ref[settle:]
            line += f"; against the input delayed: largest difference {np.max(np.abs(e2)):.1e}"
            ok &= np.max(np.abs(e2)) < 1e-5
        dry_err = np.max(np.abs(dry_c - WL.dry(x.astype(np.float64))))
        line += f"; dry {dry_err:.1e}"
        lift = 10 * np.log10(np.sum(y_p[settle:] ** 2) / np.sum(WL.dry(x.astype(np.float64))[settle:] ** 2))
        line += f"; level against dry {lift:+.2f} dB"
        print(line)
        ok &= esr < 1e-8 and dry_err == 0.0
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
