#!/usr/bin/env python3
"""
notamp_ideas.py — listening demos for two new kinds of not-amp (David, 2026-10-22: the
current twelve are not that compelling).

1. Filters past their limits. WASP_LP_16's network (the Wasp's cutoff + Res GRU-16,
   lp_cr_gru16_s3) driven where it was never trained: Res above 10 and below 0, the
   cutoff past both ends, its output fed back into its input, and Res following the
   audio. Mirth clamps all of this today.
2. Random weights, done fairly. The seed slot (retired 2026-09-29) drew random weights
   into the wrong places: its generator assumed a layout the file does not have. Here an
   A2-Lite network is rebuilt from a starter capture's own config by NAM itself
   (init_from_nam), its weights re-drawn by PyTorch's default initialisation with the
   biases at zero, and exported in the order the engine reads; the round trip is checked
   against the capture's own weights first. Rendered through the real engine (a2_host).

Every clip: the not-amps' test phrase twice (8 s, peak 0.5), a 20 Hz DC blocker as on
Mirth, then normalised to -1 dBFS peak, so compare character, not level.

    .venv-nam/bin/python tools/notamp_ideas.py <out dir>      # WAVs; mp3 with ffmpeg
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
import train_gru as T  # noqa: E402
from test_signal import make_synth, scale_peak  # noqa: E402

SR = 48000
WASP_LP_16 = pathlib.Path.home() / "wasp_lp/gru_runs/lp_cr_gru16_s3"
STARTER = HERE.parent / "captures/nam_test/KLON 2.nam"


def phrase():
    p = make_synth()
    scale_peak(p, 0.5)
    return np.tile(np.asarray(p, np.float64), 2)


def finish(y):
    y = np.nan_to_num(np.asarray(y, np.float64))
    out, x1, y1, r = np.empty_like(y), 0.0, 0.0, 0.99738
    for i, x in enumerate(y):  # Mirth's one-pole DC blocker
        y1 = x - x1 + r * y1
        x1 = x
        out[i] = y1
    peak = np.abs(out).max()
    return out * (10 ** (-1 / 20) / peak) if peak > 0 else out


# ---------------------------------------------------------------- 1. filters past limits
def gru_parts():
    model, inputs, out, scale = C.load_model(WASP_LP_16)
    assert inputs == ["audio", "cv", "res"], inputs
    cell = torch.nn.GRUCell(3, model.rnn.hidden_size).double()
    with torch.no_grad():
        for n in ("weight_ih", "weight_hh", "bias_ih", "bias_hh"):
            getattr(cell, n).copy_(getattr(model.rnn, f"{n}_l0").double())
    dense = model.dense.double()
    return cell, dense, scale


def run_gru(x, cut, res, feedback=0.0):
    """The network sample by sample, so its output can feed its input."""
    cell, dense, scale = gru_parts()
    h = torch.zeros(1, cell.hidden_size, dtype=torch.float64)
    y, prev = np.zeros(len(x)), 0.0
    with torch.no_grad():
        for i in range(len(x)):
            a = x[i] + feedback * prev
            f = torch.tensor([[a, cut[i] * T.SCALE["cv"], res[i] * T.SCALE["res"]]], dtype=torch.float64)
            h = cell(f, h)
            prev = float(dense(h)) / scale
            if not np.isfinite(prev) or abs(prev) > 50:  # a runaway: restart from rest
                h.zero_()
                prev = 0.0
            y[i] = prev
    return y


def filters(out_dir):
    x = phrase()
    n = len(x)
    sweep = lambda a, b: np.linspace(a, b, n)  # noqa: E731
    const = lambda v: np.full(n, float(v))  # noqa: E731
    clips = {
        "f1_reference_res6_cut2to8": (sweep(2, 8), const(6), 0.0),
        "f2_res12_past_the_top": (sweep(2, 8), const(12), 0.0),
        "f3_res16": (sweep(2, 8), const(16), 0.0),
        "f4_res_minus4_below_the_bottom": (sweep(2, 8), const(-4), 0.0),
        "f5_cutoff_8_to_14_past_the_top": (sweep(8, 14), const(6), 0.0),
        "f6_cutoff_2_to_minus4_past_the_bottom": (sweep(2, -4), const(6), 0.0),
        "f7_feedback_0.6_res8": (sweep(2, 8), const(8), 0.6),
        "f8_feedback_0.95_res8": (sweep(2, 8), const(8), 0.95),
        "f9_res_follows_the_audio": (sweep(2, 8), 6 + 40 * np.abs(x), 0.0),
    }
    for name, (cut, res, fb) in clips.items():
        y = run_gru(x, cut, res, fb)
        sf.write(out_dir / f"{name}.wav", finish(y), SR, subtype="PCM_24")
        print(f"  {name}: raw peak {np.abs(y).max():.2f}", flush=True)


# ---------------------------------------------------------------- 2. random weights
def lite_model():
    d = json.loads(STARTER.read_text())
    return min(d["config"]["submodels"], key=lambda s: abs(s.get("max_value", 1.0) - 0.5))["model"]


def a2_render(weights, x, tmp):
    w, i, o = tmp / "w.f32", tmp / "in.f32", tmp / "out.f32"
    np.asarray(weights, "<f4").tofile(w)
    np.asarray(x, "<f4").tofile(i)
    subprocess.run([str(HERE / "a2_host"), str(w), str(i), str(o)], check=True, capture_output=True)
    return np.fromfile(o, "<f4").astype(np.float64)[: len(x)]


def random_weights(out_dir):
    from nam.models import init_from_nam
    lite = lite_model()
    net = init_from_nam(lite)
    back = np.asarray(net._export_weights(), np.float64)
    assert len(back) == 1871 and np.allclose(back, lite["weights"], atol=1e-7), "round trip failed"
    print("  round trip: NAM's export order matches the capture's weights exactly")
    x, tmp = phrase(), pathlib.Path(tempfile.mkdtemp())
    sf.write(out_dir / "r0_the_klon_itself.wav", finish(a2_render(back, x, tmp)), SR, subtype="PCM_24")
    for seed in (1, 2, 3, 4):
        for k, mult in (("", 1.0), ("_x2", 2.0)):
            if mult != 1.0 and seed > 2:
                continue
            torch.manual_seed(seed)
            net = init_from_nam(lite)
            with torch.no_grad():
                for m in net.modules():
                    if hasattr(m, "reset_parameters") and m is not net:
                        m.reset_parameters()
                for name, p in net.named_parameters():
                    if name.endswith("bias"):
                        p.zero_()
                    else:
                        p.mul_(mult)
            w = np.asarray(net._export_weights(), np.float64)
            y = a2_render(w, x, tmp)
            sf.write(out_dir / f"r{seed}_random_seed{seed}{k}.wav", finish(y), SR, subtype="PCM_24")
            print(f"  random seed {seed}{k}: raw peak {np.abs(y).max():.3f}, rms {np.sqrt(np.mean(y**2)):.4f}", flush=True)


if __name__ == "__main__":
    out_dir = pathlib.Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    sf.write(out_dir / "00_the_phrase_dry.wav", finish(phrase()), SR, subtype="PCM_24")
    print("2. random weights")
    random_weights(out_dir)
    print("1. filters past their limits")
    filters(out_dir)
