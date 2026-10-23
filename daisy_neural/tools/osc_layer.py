#!/usr/bin/env python3
"""
osc_layer.py — SWORDS_OSC's oscillator layer (nam/osc_layer.h; SWORDS.md, "SWORDS_OSC"): its constants,
each from a measurement, and the three checks the design set, run with SWORDS_16's network
(sw4_all_gru16_s3) plus the layer through the same C++ as the firmware (tools/osc_host):

  1. the glide takes, read as Swords' were (swords_glide.analyse);
  2. step 0's Res ramps with no input: onset, stop and level against Swords';
  3. the 4.5 and 5.5 V test takes: per cutoff hold, the oscillation's pitch and level, and
     the band levels (phase-blind), against Swords'.

    .venv-nam/bin/python tools/osc_layer.py check     # writes captures/swords/osc_params.f32
"""

import json
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
import torch

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import control_response as C  # noqa: E402
import swords_capture as S  # noqa: E402
import swords_glide as G  # noqa: E402

SR = 48000
CAP = HERE.parent / "captures/swords"

# The constants, in nam/osc_layer.h's Params order, each from a measurement.
PARAMS = {
    # step 0 (cal_result.json): Res CV where it starts, and stops, at V/OCT 1, 4, 7 V
    "on_v": [1.0, 4.0, 7.0], "on_r": [5.425, 4.555, 4.215], "off_r": [3.445, 3.885, 4.035],
    # the free pitch at Res CV 5.5 V: sw-glide (164.9 Hz at 4 V, 640 at 6, 2,468 at 8) and
    # sw-a's riff takes a day earlier (61.7 at 2.5 ... 12,541 at 10.5), one curve through
    # all nine, within 44 cents of each (about the day-to-day drift)
    "hz_at": 169.12, "v_at": 4.0, "oct_per_v": 0.9775, "oct_curve": -0.00295,
    # step 0: +26 cents from the onset to 8 V at V/OCT 4, +10 at 7: about 5 cents per volt
    "cents_per_res": 5.0, "r_ref": 5.5,
    # step 0: 7.0 V peak 0.3 V past the onset, 7.44 V at 8 V
    "amp_on": 6.956, "amp_slope": 0.147, "amp_max": 7.45,
    # sw-a's riff takes: the oscillation 7.5 dB under its no-input level with the riff at 5 V
    # peak, the two sharing Swords' clip: 1 / (1 + 0.276 * 5) = -7.5 dB
    "duck_k": 0.276,
    # step 0: H2 about -51 dB; H3 -34.5 dB and H5 -42.4 just past the onset, -29.6 and -38.2 at 8 V
    "h2_db": -51.0, "h3_lo_db": -34.5, "h3_hi_db": -29.6, "h5_lo_db": -42.4, "h5_hi_db": -38.2,
    "h_lo_dr": 0.3, "h_hi_dr": 3.3,
    # sw-glide: lock range about 1.2%, 8.7%, 31% of the pitch at 0.2, 1, 3 V peak (symmetric
    # in Hz: -624/+476 cents at 3 V): 0.087 * A^1.2; the part of the input it hears, within
    # 0.4 of the pitch
    "lock_c": 0.075, "lock_p": 1.2, "lock_bw": 0.6,
    # sw-a's riff takes: every hold oscillates at 4.5 V, none at 4.0 V below the top cutoff
    "on_with_input": 4.45,
    "attack_s": 0.05, "release_s": 0.03,
    # the rig the network was trained through: ES-10 10.96 V out, 11.97 V in per unit
    "u_volts": 10.96, "y_volts": 11.9712,
}
ORDER = ["on_v", "on_r", "off_r", "hz_at", "v_at", "oct_per_v", "oct_curve", "cents_per_res", "r_ref", "amp_on",
         "amp_slope", "amp_max", "duck_k", "h2_db", "h3_lo_db", "h3_hi_db", "h5_lo_db", "h5_hi_db", "h_lo_dr", "h_hi_dr", "lock_c",
         "lock_p", "lock_bw", "on_with_input", "attack_s", "release_s", "u_volts", "y_volts"]


def floats():
    out = []
    for k in ORDER:
        v = PARAMS[k]
        out += list(v) if isinstance(v, list) else [v]
    assert len(out) == 34, len(out)
    return np.asarray(out, "<f4")


TMP = pathlib.Path(tempfile.mkdtemp())


def layer(u, cut, res):
    """The layer's output (network units) through tools/osc_host, the firmware's code."""
    floats().tofile(TMP / "p.f32")
    n = len(u)
    for name, a in (("u", u), ("c", cut), ("r", res)):
        np.broadcast_to(np.asarray(a, np.float64), (n,)).astype("<f4").tofile(TMP / f"{name}.f32")
    subprocess.run([str(HERE / "osc_host"), *(str(TMP / f"{k}.f32") for k in ("p", "u", "c", "r", "y"))], check=True)
    return np.fromfile(TMP / "y.f32", "<f4").astype(np.float64)


NET = None


def network(x, cut, res):
    global NET
    if NET is None:
        NET = C.load_model(CAP / "gru_runs/sw4_all_gru16_s3")
    model, inputs, out, scale = NET
    n = len(x)
    f = np.stack([x, np.broadcast_to(cut, (n,)) * 0.1, np.broadcast_to(res, (n,)) * 0.1], 1).astype(np.float32)
    with torch.no_grad():
        y, _ = model(torch.from_numpy(f)[None])
    return y[0].numpy().astype(np.float64) / scale


def flt6(x, cut, res):
    """SWORDS_OSC on input x (network units), V/OCT cut and Res CV res (volts): the network at Res
    CV up to 4 V, the layer at the Res CV itself."""
    res_net = np.minimum(np.broadcast_to(np.asarray(res, np.float64), (len(x),)), 4.0)
    return network(x, cut, res_net) + layer(x, cut, res)


def check_glide():
    print("1. the glide takes: SWORDS_OSC against Swords (free pitch, lock range up / down, pull)")
    d = CAP / "sw-glide"
    s = json.loads((d / "session.json").read_text())
    for t in s["takes"]:
        play, _ = sf.read(d / "play" / f"{t['name']}.wav", dtype="float64", always_2d=True)
        x = play[t["payload_at"]:t["payload_at"] + t["payload_len"], S.AUDIO]  # network units = ES-10 units
        y = flt6(x, t["cut_v"], t["res_v"]) * PARAMS["y_volts"]
        print("   model  " + G.analyse(y, t))
    print("   (Swords: python tools/swords_capture.py glide captures/swords/sw-glide)")


def check_ramps():
    print("\n2. step 0's Res ramps, no input: where it starts and stops, and its level")
    cal = json.loads((CAP / "cal_result.json").read_text())
    for o in cal["osc"]:
        v = o["cut_v"]
        if v < 0:
            continue
        n = int(16 * SR)
        res = np.concatenate([np.linspace(0, 8, n), np.linspace(8, 0, n)])
        y = layer(np.zeros(2 * n), v, res) * PARAMS["y_volts"]
        w = SR // 50
        rms = np.sqrt(np.mean(y[:len(y) // w * w].reshape(-1, w) ** 2, axis=1))
        rv = res[:len(res) // w * w].reshape(-1, w).mean(axis=1)
        loud = rms > 0.1
        half = len(rms) // 2
        up, down = np.where(loud[:half])[0], np.where(loud[half:])[0]
        on = rv[up[0]] if len(up) else float("nan")
        off = rv[half + down[-1]] if len(down) else float("nan")
        lvl = 20 * np.log10(rms.max() + 1e-12)  # rms over 20 ms, as cmd_cal reads Swords
        print(f"   V/OCT {v:+.0f} V: starts {on:.2f} V (Swords {o['starts_at_res_v'] or float('nan'):.2f}), stops "
              f"{off:.2f} V (Swords {o['stops_at_res_v']:.2f}), loudest {lvl:+.1f} dBV (Swords {o['peak_v_db']:+.1f})")


def check_tests():
    print("\n3. the oscillating test takes: per hold, the oscillation's pitch (Hz) and level (dBV), "
          "and the band levels' mean difference (dB), model against Swords")
    s = json.loads((CAP / "sw-a/session.json").read_text())
    holds = C.swords_holds(s["test_holds_v"])
    for r in (4.5, 5.5):
        z = np.load(CAP / f"sw-a/gru/test_r{r:+.2f}_1.npz")
        x, cv, real = z["x"].astype(np.float64), z["cv_v"].astype(np.float64), z["y_f1"].astype(np.float64)
        model = flt6(x, cv, r)
        lr, lm = C.band_levels_at(real, holds), C.band_levels_at(model, holds)
        print(f"   Res CV {r} V")
        for v, (a, b) in holds.items():
            f0 = G.osc_hz(v)
            seg_r, seg_m = real[int(a * SR):int(b * SR)], model[int(a * SR):int(b * SR)]
            fr, dr = G.peak_hz(seg_r * PARAMS["y_volts"], f0 / 1.6, f0 * 1.6)
            fm, dm = G.peak_hz(seg_m * PARAMS["y_volts"], f0 / 1.6, f0 * 1.6)
            print(f"     V/OCT {v:5.2f}: pitch {fm:7.1f} / {fr:7.1f}   level {dm:+6.1f} / {dr:+6.1f}   "
                  f"bands {np.mean(np.abs(lm[v] - lr[v])):4.1f} dB")


def main():
    if sys.argv[1:] != ["check"]:
        raise SystemExit(__doc__)
    floats().tofile(CAP / "osc_params.f32")
    check_glide()
    check_ramps()
    check_tests()


if __name__ == "__main__":
    main()
