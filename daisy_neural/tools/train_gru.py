#!/usr/bin/env python3
"""
train_gru.py — train a GRU on the Wasp recordings: phase 4's own trainer, in place of
NAM's (which has no control inputs). Run it in the trainer's environment, .venv-nam,
which has PyTorch and TensorBoard.

The network is the NeuralSeed / Wright et al. shape: a recurrent layer (GRU, or LSTM with
--cell lstm; stacked with --layers) over the audio, plus any controls fed in as extra
inputs alongside it, then one dense layer to the output sample.

    --inputs audio            a fixed setting, like an A2 capture
    --inputs audio,cv         the cutoff as a knob (CV 1 volts)
    --inputs audio,cv,res     cutoff and resonance (the Res dial)

Training follows Wright et al.'s shape: half-second segments drawn from the training
material, the hidden state warmed up before any loss counts, truncated backpropagation
every 2048 samples, Adam, the learning rate cut when validation stops improving, and
early stopping. The loss is plain ESR, which is neither Wright et al.'s (they pre-emphasise
the signal before the ESR and add a DC term) nor NAM's (NAM trains on MSE of an output
normalised to -18 dBFS plus a multi-resolution STFT term, and only reports ESR); see
CLAUDE.md, "NAM's trainer, reviewed". Validation and test scores are pooled ESR: the
error energy over all scored samples divided by the signal's.

Sessions, for the playable networks. `--series lp` takes every session of that series in
captures/wasp, and the split is fixed by the recordings, not by chance:
  - training: the sweep takes, less the validation blocks (and, with --snapshots, the
    NAM-file takes as well);
  - validation, which picks the epoch: three blocks in every sweep take, at its beginning,
    middle and end, so a Wasp that drifts over a take is validated across it. Each block
    is 1 s of run-in, unscored while the state settles, then 8 s scored. No training
    segment touches a block or its run-in;
  - test, scored once with the chosen epoch: every `test` and `test_snaps` take of the
    series (the riff never trained on, with the cutoff held and ramped), and whole
    sessions at the --holdout-res marks, which are never trained or validated on.

    # the playable network: cutoff and Res, every mark but Res 5
    .venv-nam/bin/python tools/train_gru.py --name lp_cr_gru16_s1 --series lp \\
        --holdout-res 5 --inputs audio,cv,res --hidden 16 --seed 1 --threads 1
    # cutoff only, at Res 6, the NAM-file takes trained on as well
    .venv-nam/bin/python tools/train_gru.py --name lp6_c_gru16_s1 --series lp --res 6 \\
        --snapshots --inputs audio,cv --hidden 16 --seed 1 --threads 1

Takes are read through a memory-mapped copy, <take>.npy beside each .npz, so parallel runs
share one copy in memory instead of holding 1.4 GB each. `--prepare` builds them, prints
the split and exits: run it once before launching several runs, so they don't all build
at once. Each run writes its split to split.json, before training starts.

Files, for comparison 1 (the like-for-like test against the first A2 capture: same take,
NAM's own train/validation split, the held-out riff as the judge):

    .venv-nam/bin/python tools/train_gru.py --name lp6_+5V_gru24 --inputs audio \\
        --nam-split captures/wasp/lp-res6/gru/snap3_+5.00V.npz \\
        --judge "captures/wasp/lp-res6/gru/test_snaps.npz@24:28"

`--nam-split` trains on 10 s to the last 9 s of a NAM-file take and validates on its
closing 9 s, exactly as NAM's trainer does. `--train` and `--valid` take .npz files for
anything else. `--judge` files are scored at the end and never used for selection;
`file.npz@a:b` keeps seconds a to b of the take. Whole takes and spans start from rest,
and their first 0.1 s is not scored.

The revised recipe (2026-10-19, after NAM's trainer was reviewed; CLAUDE.md, "NAM's trainer,
reviewed"): --loss mse trains on MSE of a target normalised to --target-dbfs (NAM: -18)
plus --mrstft-weight times auraloss's multi-resolution STFT (NAM's own copy: FFT 512, 1024,
2048; spectral convergence + log magnitude), one optimiser step per batch (--step batch),
an exponential learning-rate decay over a fixed number of epochs (--schedule exp, no early
stopping), and the GRU's update gate started towards keeping its history (--z-bias). ESR
still picks the best epoch and is what is reported. --arch wavenet trains NAM's own WaveNet
in A2-Lite's layout with the inputs (audio, cutoff, Res) as its conditioning at every layer
and FiLM before each activation (--film), on the GPU if --device mps.

--arch hyper (2026-10-20, David's option (a)): Res sets the weights instead of entering as
an input. Every GRU and dense weight is w0 + P1(r) w1 + P2(r) w2, r the Res dial scaled to
-1..1 and P1, P2 the first Legendre polynomials (--hyper-degree); w1 and w2 start at zero,
so training starts from a plain GRU and learns what Res changes. --inputs is then
audio,cv, and each training batch is drawn from a single Res mark so one set of weights
serves it. On the Daisy the weights would be recomputed when Res moves, once a block.

Runs land in captures/wasp/gru_runs/<name>/, with TensorBoard logs:

    .venv-nam/bin/tensorboard --logdir captures/wasp/gru_runs
"""

import argparse
import json
import os
import pathlib
import time

import numpy as np
import soundfile as sf
import torch
from torch import nn
from torch.utils.tensorboard import SummaryWriter

HERE = pathlib.Path(__file__).resolve().parent
CAPTURES = HERE.parent / "captures" / "wasp"
SR = 48000
NAM_TRAIN_START = 480_000  # NAM's v3 split: train from 10 s ...
NAM_VALIDATE = 432_000     # ... to the last 9 s, which validate
SETTLE = SR // 10          # unscored at the start of a whole take, the state settling from rest
COLUMNS = ("x", "cv_v", "y_lphp", "y_bp")
TARGET = {"lphp": 2, "bp": 3}
SCALE = {"audio": 1.0, "cv": 0.1, "res": 0.1}  # inputs to about ±1: CV in tens of volts, Res in tens


# ------------------------------------------------------------------------- data

def cached(npz, z):
    """The take as one float32 array, columns COLUMNS, memory-mapped from <take>.npy.
    Built on first use, or when the .npz is newer; written whole and renamed into place,
    so a run never maps a half-written copy."""
    npy = npz.with_suffix(".npy")
    if not npy.exists() or npy.stat().st_mtime < npz.stat().st_mtime:
        a = np.stack([z[c] for c in COLUMNS], axis=1).astype(np.float32)
        tmp = npy.with_name(f"{npy.name}.{os.getpid()}.tmp")
        with open(tmp, "wb") as f:
            np.save(f, a)
        os.replace(tmp, npy)
    return np.load(npy, mmap_mode="r")


class Take:
    """One take from wasp_capture.py's split: the audio as played, CV 1 in volts, both
    outputs, and the session's Res mark."""

    def __init__(self, npz):
        self.path = pathlib.Path(npz)
        self.session = self.path.parent.parent.name
        self.name = self.path.stem
        z = np.load(self.path)
        self.res = float(z["res"])
        self.role = str(z["role"])
        self.data = cached(self.path, z)
        self.n = len(self.data)
        self.label = f"{self.session}/{self.name}"

    def features(self, a, b, inputs):
        d = self.data[a:b]
        cols = {"audio": lambda: d[:, 0], "cv": lambda: d[:, 1],
                "res": lambda: np.full(b - a, self.res, np.float32)}
        return np.stack([cols[k]() * SCALE[k] for k in inputs], axis=-1).astype(np.float32)

    def target(self, a, b, out):
        return np.array(self.data[a:b, TARGET[out]], dtype=np.float32)


def spec(s):
    """file.npz or file.npz@a:b (seconds) as (take, first sample, end sample)."""
    path, _, span = s.partition("@")
    t = Take(path)
    a, b = 0, t.n
    if span:
        a, b = (int(float(v) * SR) for v in span.split(":"))
    return t, a, min(b, t.n)


def carve(n, k, block):
    """k blocks of `block` samples spread over n, the first at the start and the last at
    the end, and the gaps between them."""
    starts = [round(i * (n - block) / (k - 1)) for i in range(k)] if k > 1 else [n - block]
    blocks, gaps, prev = [(a, a + block) for a in starts], [], 0
    for a, b in blocks:
        if a > prev:
            gaps.append((prev, a))
        prev = b
    if prev < n:
        gaps.append((prev, n))
    return blocks, gaps


def marks(text):
    return {float(v) for v in text.split(",")} if text else set()


def session_material(args, train, valid, test):
    """The split from a series' sessions (see the docstring). Items are (take, a, b) to
    train on, and (take, a, b, unscored, held_out) to score."""
    keep, held = marks(args.res), marks(args.holdout_res)
    block, run_in = int((args.run_in + args.val_seconds) * SR), int(args.run_in * SR)
    missing = []
    for f in sorted(pathlib.Path(args.captures).glob("*/session.json")):
        s = json.loads(f.read_text())
        res = s.get("res")
        if s.get("mix") != args.series or res is None or (keep and res not in keep | held):
            continue
        out = res in held
        for t in s["takes"]:
            role = t["role"]
            if role not in ("sweep", "test") and not (role == "snapshot" and args.snapshots):
                continue
            npz = f.parent / "gru" / f"{t['name']}.npz"
            if not npz.exists():
                # A session made but not yet recorded (the repeatability takes) is not
                # missing data; one recorded but not split is.
                if (f.parent / "rec" / f"{t['name']}.wav").exists():
                    missing.append(f"{f.parent.name}/{t['name']}")
                continue
            take = Take(npz)
            if out or role == "test":
                test.append((take, 0, take.n, SETTLE, out))
            elif role == "sweep":
                blocks, gaps = carve(take.n, args.val_blocks, block)
                valid += [(take, a, b, run_in, False) for a, b in blocks]
                train += [(take, a, b) for a, b in gaps]
            else:
                train.append((take, 0, take.n))
    if missing:
        raise SystemExit("no GRU file for " + ", ".join(missing)
                         + ": run wasp_capture.py split on those sessions first")
    if not train:
        raise SystemExit(f"no {args.series} sessions to train on in {args.captures}")
    leak = {t.label for t, *_ in train + valid if t.res in held}
    assert not leak, f"held-out takes in training or validation: {leak}"


def file_material(args, train, valid, test):
    for s in args.nam_split:
        t, a, b = spec(s)
        train.append((t, a + NAM_TRAIN_START, b - NAM_VALIDATE))
        valid.append((t, b - NAM_VALIDATE, b, SETTLE, False))
    for s in args.train:
        train.append(spec(s))
    valid += [(*spec(s), SETTLE, False) for s in args.valid]
    test += [(*spec(s), SETTLE, False) for s in args.judge]


def describe(train, valid, test):
    sec = lambda items: sum(b - a for _, a, b, *_ in items) / SR  # noqa: E731
    return {
        "train": [{"take": t.label, "res": t.res, "from_s": a / SR, "to_s": b / SR} for t, a, b in train],
        "valid": [{"take": t.label, "res": t.res, "from_s": a / SR, "to_s": b / SR, "unscored_s": u / SR}
                  for t, a, b, u, _ in valid],
        "test": [{"take": t.label, "res": t.res, "from_s": a / SR, "to_s": b / SR, "unscored_s": u / SR,
                  "held_out": h} for t, a, b, u, h in test],
        "seconds": {"train": round(sec(train), 1), "valid": round(sec(valid), 1), "test": round(sec(test), 1)},
    }


# ------------------------------------------------------------------------ model

class RNNModel(nn.Module):
    def __init__(self, n_inputs, hidden, cell="gru", layers=1, z_bias=0.0):
        super().__init__()
        rnn = {"gru": nn.GRU, "lstm": nn.LSTM}[cell]
        self.rnn = rnn(n_inputs, hidden, num_layers=layers, batch_first=True)
        self.dense = nn.Linear(hidden, 1)
        if z_bias and cell == "gru":
            # PyTorch's GRU gates are (r, z, n); h' = (1 - z) n + z h, so a positive z bias
            # starts every unit keeping its history: the GRU's version of NAM's LSTM
            # forget-gate bias ("up the remembering").
            with torch.no_grad():
                for layer in range(layers):
                    for b in (getattr(self.rnn, f"bias_ih_l{layer}"), getattr(self.rnn, f"bias_hh_l{layer}")):
                        b[hidden:2 * hidden] += z_bias

    def forward(self, x, h=None):
        z, h = self.rnn(x, h)
        return self.dense(z).squeeze(-1), h


class HyperRNN(nn.Module):
    """A GRU whose every weight, and the dense layer's, is a smooth function of the Res
    dial: w(r) = w0 + sum_d P_d(r) w_d, r = (Res - 5) / 5, P_1 = r, P_2 = (3r^2 - 1) / 2.
    set_res() chooses the dial position for the calls that follow."""

    def __init__(self, n_inputs, hidden, degree=2):
        super().__init__()
        self.base = RNNModel(n_inputs, hidden, "gru", 1)
        self.names = [n for n, _ in self.base.named_parameters()]
        self.degree = degree
        self.extra = nn.ParameterList(
            [nn.Parameter(torch.zeros_like(p)) for _ in range(degree) for _, p in self.base.named_parameters()])
        self.res = 6.0

    def set_res(self, res):
        self.res = float(res)

    def weights(self):
        r = (self.res - 5.0) / 5.0
        basis = [r, 1.5 * r * r - 0.5][: self.degree]
        k = len(self.names)
        out = {}
        for i, (name, p) in enumerate(self.base.named_parameters()):
            w = p
            for d, b in enumerate(basis):
                w = w + b * self.extra[d * k + i]
            out[name] = w
        return out

    def forward(self, x, h=None):
        w = self.weights()
        hidden = self.base.rnn.hidden_size
        if h is None:
            h = torch.zeros(1, x.shape[0], hidden, dtype=x.dtype, device=x.device)
        z, h = torch._VF.gru(x, h, [w["rnn.weight_ih_l0"], w["rnn.weight_hh_l0"], w["rnn.bias_ih_l0"],
                                    w["rnn.bias_hh_l0"]], True, 1, 0.0, self.training, False, True)
        return nn.functional.linear(z, w["dense.weight"], w["dense.bias"]).squeeze(-1), h


def a2_lite_config(n_inputs, film):
    """NAM's A2-Lite layout (its PackedWaveNet's 3-channel submodel), taking n_inputs
    channels as both its input and the condition every layer mixes in. film names the FiLM
    point to switch on in every layer (NAM 0.13's names, e.g. activation_pre), or ""."""
    import copy
    import nam.train
    base = json.loads((pathlib.Path(nam.train.__file__).parent / "_resources" / "config_model_packed.json").read_text())
    cfg = copy.deepcopy([s for s in base["net"]["config"]["submodels"] if s["name"] == "channels_3"][0]["config"])
    for lc in cfg["layers_configs"]:
        lc["input_size"] = n_inputs
        lc["condition_size"] = n_inputs
        if film:
            lc["film_params"] = {f"{film}_film": {"active": True}}
    return cfg


class WaveNetModel(nn.Module):
    """NAM's own WaveNet (nam.models.wavenet._wavenet), fed (B, L, inputs) like the RNN and
    returning (B, L - receptive_field + 1): no state, each output reads the receptive field
    of input before it."""

    def __init__(self, n_inputs, film="activation_pre"):
        super().__init__()
        from nam.models.wavenet._wavenet import WaveNet as Core
        self.config = a2_lite_config(n_inputs, film)
        import copy
        self.net = Core.init_from_config(copy.deepcopy(self.config))
        self.receptive_field = self.net.receptive_field

    def forward(self, x, h=None):
        return self.net(x.transpose(1, 2))[:, 0, :], None


def detach(h):
    """Cut the graph at a backprop boundary; an LSTM's state is a pair."""
    return tuple(t.detach() for t in h) if isinstance(h, tuple) else h.detach()


def esr(pred, target):
    return torch.mean((pred - target) ** 2) / (torch.mean(target ** 2) + 1e-10)


_MRSTFT = None


def mrstft(pred, target):
    """NAM's multi-resolution STFT loss: its vendored auraloss with the defaults it uses
    (FFT 512, 1024 and 2048; spectral convergence plus log magnitude), on (B, L)."""
    global _MRSTFT
    if _MRSTFT is None:
        # NAM's own vendored copy, loaded without importing nam itself: nam's package
        # start-up imports its GUI and so needs Tk, which the Mac mini's Python lacks. The
        # copy imports only torch, numpy, scipy and matplotlib.
        import importlib.util
        import sys
        deps = pathlib.Path(importlib.util.find_spec("nam").submodule_search_locations[0]) / "_dependencies"
        if str(deps) not in sys.path:
            sys.path.insert(0, str(deps))
        from auraloss.freq import MultiResolutionSTFTLoss
        _MRSTFT = MultiResolutionSTFTLoss()
    return _MRSTFT(pred[:, None, :], target[:, None, :])


def train_loss(args, pred, target):
    """The training loss: plain ESR (the first recipe), or MSE plus the spectral term."""
    if args.loss == "esr":
        return esr(pred, target)
    loss = torch.mean((pred - target) ** 2)
    if args.mrstft_weight:
        loss = loss + args.mrstft_weight * mrstft(pred, target)
    return loss


@torch.no_grad()
def evaluate(model, items, inputs, out, device, keep=False, chunk=SR, out_scale=1.0):
    """Each item (take, a, b, unscored, ...) run from rest with the state carried across
    chunks, as on a module. Items of the same length run as one batch. Returns each item's
    (error energy, signal energy) over its scored samples, and its output if keep. A model
    trained on a normalised target has its output divided by out_scale first, so scores
    and outputs are at the real Wasp's level."""
    model.eval()
    rf = getattr(model, "receptive_field", 1)
    sums, outputs, groups = [None] * len(items), [None] * len(items), {}
    hyper = hasattr(model, "set_res")
    for i, (t, a, b, u, *_) in enumerate(items):
        groups.setdefault((b - a, u, t.res if hyper else None), []).append(i)
    for (length, unscored, res), idx in groups.items():
        if hyper:
            model.set_res(res)
        err, sig, h, parts = np.zeros(len(idx)), np.zeros(len(idx)), None, []
        for c in range(0, length, chunk):
            d = min(chunk, length - c)
            y = np.stack([items[i][0].target(items[i][1] + c, items[i][1] + c + d, out) for i in idx])
            if rf > 1:
                # A WaveNet has no state: give each chunk the receptive field of input
                # before it, zeros before the item's start (rest).
                xs = []
                for i in idx:
                    t, a0 = items[i][0], items[i][1]
                    lo = a0 + c - (rf - 1)
                    pre = max(0, a0 - lo)
                    f = t.features(lo + pre, a0 + c + d, inputs)
                    xs.append(np.concatenate([np.zeros((pre, f.shape[1]), np.float32), f]) if pre else f)
                x = np.stack(xs)
            else:
                x = np.stack([items[i][0].features(items[i][1] + c, items[i][1] + c + d, inputs) for i in idx])
            pred, h = model(torch.from_numpy(x).to(device), h)
            pred = pred.cpu().numpy() / out_scale
            if keep:
                parts.append(pred)
            lo = max(0, unscored - c)
            if lo < d:
                err += ((pred[:, lo:] - y[:, lo:]).astype(np.float64) ** 2).sum(1)
                sig += (y[:, lo:].astype(np.float64) ** 2).sum(1)
        for j, i in enumerate(idx):
            sums[i] = (err[j], sig[j])
            if keep:
                outputs[i] = np.concatenate([p[j] for p in parts])
    return sums, outputs


def pooled(sums):
    e, s = map(sum, zip(*sums)) if sums else (0.0, 0.0)
    return float(e / (s + 1e-10))


def by_res(items, sums, only=None):
    """Pooled ESR per Res mark, over the items whose take is named `only` if given."""
    res = {}
    for item, s in zip(items, sums):
        if only is None or item[0].name == only:
            res.setdefault(item[0].res, []).append(s)
    return {f"{r:g}": pooled(v) for r, v in sorted(res.items())}


# ---------------------------------------------------------------------- training

def segments(train, inputs, out, seg, rng, n, context=0, same_res=False):
    """n random segments, seg samples long, from the training spans (weighted by length).
    context extra samples of input come before each (a WaveNet's receptive field), from
    inside the same span; the target covers the seg samples only. same_res draws the whole
    batch from one Res mark, chosen in proportion to its material; returns it as well."""
    room = np.array([b - a - seg - context for _, a, b in train], dtype=np.float64)
    res = None
    if same_res:
        res = train[rng.choice(len(train), p=room / room.sum())][0].res
        room = np.where([t.res == res for t, _, _ in train], room, 0.0)
    pick = rng.choice(len(train), size=n, p=room / room.sum())
    xs, ys = [], []
    for i in pick:
        t, a, b = train[i]
        s = int(rng.integers(a + context, b - seg))
        xs.append(t.features(s - context, s + seg, inputs))
        ys.append(t.target(s, s + seg, out))
    x, y = torch.from_numpy(np.stack(xs)), torch.from_numpy(np.stack(ys))
    return (x, y, res) if same_res else (x, y)


def target_scale(train, out, dbfs):
    """The factor that brings the training targets' RMS to dbfs (NAM normalises its output
    to -18 dBFS before training)."""
    e = sum(float(np.sum(t.target(a, b, out).astype(np.float64) ** 2)) for t, a, b in train)
    n = sum(b - a for _, a, b in train)
    return float(10 ** (dbfs / 20) / np.sqrt(e / n))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--name", help="run name, a folder under captures/wasp/gru_runs")
    ap.add_argument("--inputs", default="audio", help="audio[,cv][,res]")
    ap.add_argument("--output", default="lphp", choices=("lphp", "bp"))
    ap.add_argument("--captures", default=str(CAPTURES), help="where the sessions are")
    ap.add_argument("--series", choices=("lp", "hp"), help="train on that series' sessions")
    ap.add_argument("--res", default="", help="the Res marks to use, e.g. 6 (default: all)")
    ap.add_argument("--holdout-res", default="", help="Res marks never trained or validated on; tested whole")
    ap.add_argument("--snapshots", action="store_true", help="train on the NAM-file takes as well")
    ap.add_argument("--val-blocks", type=int, default=3, help="validation blocks per sweep take")
    ap.add_argument("--val-seconds", type=float, default=8.0, help="scored seconds per validation block")
    ap.add_argument("--run-in", type=float, default=1.0, help="unscored seconds before each block")
    ap.add_argument("--prepare", action="store_true", help="build the memory-mapped takes, print the split, exit")
    ap.add_argument("--nam-split", action="append", default=[], help="a NAM-file take, split as NAM does")
    ap.add_argument("--train", action="append", default=[], help="training .npz (spec)")
    ap.add_argument("--valid", action="append", default=[], help="validation .npz (spec), for selection")
    ap.add_argument("--judge", action="append", default=[], help="held-out .npz (spec), scored at the end only")
    ap.add_argument("--listen", action="store_true", help="write the chosen epoch's output on every test take")
    ap.add_argument("--arch", default="rnn", choices=("rnn", "wavenet", "hyper"),
                    help="rnn (GRU or LSTM, --cell), NAM's WaveNet in A2-Lite's layout, or a GRU "
                         "whose weights are set by Res (hyper)")
    ap.add_argument("--hyper-degree", type=int, default=2, help="hyper: polynomial degree in Res")
    ap.add_argument("--film", default="activation_pre",
                    help="wavenet: the FiLM point in every layer (NAM 0.13's names), or none")
    ap.add_argument("--loss", default="esr", choices=("esr", "mse"),
                    help="esr (the first recipe) or mse, with --mrstft-weight and --target-dbfs")
    ap.add_argument("--mrstft-weight", type=float, default=0.0, help="NAM's A2 uses 0.0005")
    ap.add_argument("--target-dbfs", type=float, default=None,
                    help="normalise the training target's RMS to this (NAM: -18); undone for scores")
    ap.add_argument("--step", default="chunk", choices=("chunk", "batch"),
                    help="optimiser step per backprop chunk (the first recipe) or per batch (NAM)")
    ap.add_argument("--schedule", default="plateau", choices=("plateau", "exp"))
    ap.add_argument("--gamma", type=float, default=0.999, help="exp schedule: lr factor per epoch")
    ap.add_argument("--z-bias", type=float, default=0.0, help="GRU update-gate bias at the start")
    ap.add_argument("--cell", default="gru", choices=("gru", "lstm"))
    ap.add_argument("--hidden", type=int, default=24)
    ap.add_argument("--layers", type=int, default=1)
    ap.add_argument("--threads", type=int, default=0, help="CPU threads (0: PyTorch's default)")
    ap.add_argument("--seg", type=float, default=0.5, help="segment seconds")
    ap.add_argument("--warmup", type=int, default=1000, help="samples of state warm-up before the loss counts")
    ap.add_argument("--tbptt", type=int, default=2048, help="samples per truncated backprop step")
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--steps-per-epoch", type=int, default=20, help="batches per epoch")
    ap.add_argument("--epochs", type=int, default=300)
    ap.add_argument("--lr", type=float, default=5e-3)
    ap.add_argument("--clip", type=float, default=0, help="largest gradient norm per step (0: no limit)")
    ap.add_argument("--patience", type=int, default=30,
                    help="epochs without improvement before stopping (0: run every epoch)")
    ap.add_argument("--device", default="cpu", help="cpu or mps")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    train, valid, test = [], [], []
    if args.series:
        session_material(args, train, valid, test)
    file_material(args, train, valid, test)
    seg = int(args.seg * SR)
    context = 0
    if args.arch == "wavenet":
        context = WaveNetModel(len(args.inputs.split(",")), "").receptive_field - 1
    train = [t for t in train if t[2] - t[1] > seg + context]
    split = describe(train, valid, test)
    print(f"{len(train)} training spans, {split['seconds']['train']:.0f} s; {len(valid)} validation "
          f"blocks, {split['seconds']['valid']:.0f} s; {len(test)} test takes, {split['seconds']['test']:.0f} s")
    if args.prepare:
        for kind in ("train", "valid", "test"):
            print(f"\n{kind}:")
            for e in split[kind]:
                print(f"  {e['take']:24s} Res {e['res']:4g}  {e['from_s']:6.1f}-{e['to_s']:6.1f} s"
                      + ("  held out" if e.get("held_out") else ""))
        return
    if not args.name:
        raise SystemExit("--name is required to train")
    if not train or not valid:
        raise SystemExit("need training material and a validation set")

    torch.manual_seed(args.seed)
    if args.threads:
        torch.set_num_threads(args.threads)
    rng = np.random.default_rng(args.seed)
    device = torch.device(args.device)
    inputs = args.inputs.split(",")
    run = pathlib.Path(args.captures) / "gru_runs" / args.name
    run.mkdir(parents=True, exist_ok=True)
    (run / "config.json").write_text(json.dumps(vars(args), indent=2) + "\n")
    (run / "split.json").write_text(json.dumps(split, indent=1) + "\n")
    writer = SummaryWriter(str(run))
    if args.arch == "hyper":
        if "res" in inputs:
            raise SystemExit("--arch hyper: Res sets the weights; use --inputs audio,cv")
        model = HyperRNN(len(inputs), args.hidden, args.hyper_degree).to(device)
        what = f"Hyper-GRU-{args.hidden} (weights of degree {args.hyper_degree} in Res)"
    elif args.arch == "wavenet":
        model = WaveNetModel(len(inputs), "" if args.film == "none" else args.film).to(device)
        (run / "wavenet_config.json").write_text(json.dumps(model.config, indent=1) + "\n")
        what = f"WaveNet (A2-Lite layout, FiLM {args.film})"
    else:
        model = RNNModel(len(inputs), args.hidden, args.cell, args.layers, args.z_bias).to(device)
        what = f"{args.cell.upper()}-{args.hidden} x{args.layers}"
    n_params = sum(p.numel() for p in model.parameters())
    scale = target_scale(train, args.output, args.target_dbfs) if args.target_dbfs is not None else 1.0
    (run / "norm.json").write_text(json.dumps({"target_scale": scale}) + "\n")
    print(f"{args.name}: {what}, inputs {inputs}, {n_params} parameters, seed {args.seed}, on {device}, "
          f"loss {args.loss}" + (f" + {args.mrstft_weight} MRSTFT" if args.loss == "mse" and args.mrstft_weight else "")
          + f", target x{scale:.3f}, step per {args.step}", flush=True)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    if args.schedule == "exp":
        sched = torch.optim.lr_scheduler.ExponentialLR(opt, gamma=args.gamma)
    else:
        sched = torch.optim.lr_scheduler.ReduceLROnPlateau(opt, factor=0.5, patience=10)

    def step(loss):
        """Backward, clip (and log the gradient's size), step: unless the gradient is not a
        finite number, when the step is skipped rather than written into the weights (an
        infinite gradient turned a run's weights to NaN, 2026-10-19)."""
        opt.zero_grad()
        loss.backward()
        norm = float(torch.nn.utils.clip_grad_norm_(model.parameters(), args.clip or float("inf")))
        if np.isfinite(norm):
            opt.step()
        return norm
    best, best_epoch, t0, norms = float("inf"), -1, time.monotonic(), []
    for epoch in range(args.epochs):
        model.train()
        losses = []
        for _ in range(args.steps_per_epoch):
            if args.arch == "hyper":
                x, y, batch_res = segments(train, inputs, args.output, seg, rng, args.batch, context, same_res=True)
                model.set_res(batch_res)
            else:
                x, y = segments(train, inputs, args.output, seg, rng, args.batch, context)
            x, y = x.to(device), (y * scale).to(device)
            if args.arch == "wavenet":
                pred, _ = model(x)
                loss = train_loss(args, pred, y)
                norms.append(step(loss))
                losses.append(loss.item())
                continue
            with torch.no_grad():  # warm the state up on the first samples; no loss there
                _, h = model(x[:, :args.warmup])
            preds = []
            for a in range(args.warmup, seg, args.tbptt):
                b = min(a + args.tbptt, seg)
                pred, h = model(x[:, a:b], h)
                if args.step == "chunk":
                    loss = train_loss(args, pred, y[:, a:b])
                    norms.append(step(loss))
                    losses.append(loss.item())
                else:
                    preds.append(pred)
                h = detach(h)  # truncated backpropagation: the history goes on, its gradient stops
            if args.step == "batch":  # one loss over the whole segment, one step (NAM's way)
                loss = train_loss(args, torch.cat(preds, 1), y[:, args.warmup:])
                norms.append(step(loss))
                losses.append(loss.item())
        sums, _ = evaluate(model, valid, inputs, args.output, device, out_scale=scale)
        v = pooled(sums)
        if args.schedule == "exp":
            sched.step()
        else:
            sched.step(v)
        lr = opt.param_groups[0]["lr"]
        epoch_norms = np.array(norms[-len(losses):])
        clipped = float(np.mean(epoch_norms > args.clip)) if args.clip else 0.0
        writer.add_scalar("train/loss", float(np.mean(losses)), epoch)
        writer.add_scalar("valid/ESR", v, epoch)
        for r, e in by_res(valid, sums).items():
            writer.add_scalar(f"valid_res/{r}", e, epoch)
        writer.add_scalar("lr", lr, epoch)
        writer.add_scalar("grad/median", float(np.median(epoch_norms)), epoch)
        writer.add_scalar("grad/max", float(epoch_norms.max()), epoch)
        writer.add_scalar("grad/clipped", clipped, epoch)
        np.save(run / "grad_norms.npy", np.array(norms, dtype=np.float32))
        mark = ""
        if v < best:
            best, best_epoch, mark = v, epoch, "  *"
            torch.save(model.state_dict(), run / "best.pt")
        minutes = (time.monotonic() - t0) / 60
        print(f"epoch {epoch:4d}  train {args.loss.upper()} {np.mean(losses):.4g}  valid ESR {v:.4f}  lr {lr:.1e}  "
              f"grad {np.median(epoch_norms):.3g} (max {epoch_norms.max():.3g}, {100 * clipped:.0f}% clipped)  "
              f"{minutes:5.1f} min{mark}", flush=True)
        if args.patience and epoch - best_epoch >= args.patience:
            print(f"no improvement for {args.patience} epochs: stopping")
            break

    model.load_state_dict(torch.load(run / "best.pt"))
    vsums, _ = evaluate(model, valid, inputs, args.output, device, out_scale=scale)
    tsums, outs = evaluate(model, test, inputs, args.output, device, keep=args.listen, out_scale=scale)
    result = {
        "best_epoch": best_epoch, "valid": pooled(vsums), "valid_by_res": by_res(valid, vsums),
        "test": {item[0].label: pooled([s]) for item, s in zip(test, tsums)},
        "test_take_by_res": by_res(test, tsums, only="test"),
        "held_out": pooled([s for item, s in zip(test, tsums) if item[4]]) if any(i[4] for i in test) else None,
        "parameters": n_params, "target_scale": scale, "minutes": round((time.monotonic() - t0) / 60, 1),
    }
    if args.listen:
        (run / "listen").mkdir(exist_ok=True)
        for item, y in zip(test, outs):
            sf.write(run / "listen" / f"{item[0].session}_{item[0].name}.wav", np.clip(y, -1, 1), SR,
                     subtype="PCM_24")
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
