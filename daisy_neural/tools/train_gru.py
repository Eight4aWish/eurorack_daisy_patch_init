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

Training is Wright et al.'s recipe: half-second segments drawn from the training takes,
the hidden state warmed up before any loss counts, truncated backpropagation every 2048
samples, Adam, the learning rate cut when validation stops improving, and early stopping.
The loss is ESR, the same score NAM reports, so the two compare directly.

Comparison 1, the like-for-like test against the first A2 capture (same take, NAM's own
train/validation split, the held-out riff as the judge):

    .venv-nam/bin/python tools/train_gru.py --name lp6_+5V_gru24 --inputs audio \\
        --nam-split captures/wasp/lp-res6/gru/snap3_+5.00V.npz \\
        --judge "captures/wasp/lp-res6/gru/test_snaps.npz@24:28"

`--nam-split` trains on 10 s to the last 9 s of a NAM-file take and validates on its
closing 9 s, exactly as NAM's trainer does. `--train` and `--valid` take .npz files for
anything else. `--judge` files are scored at the end and never used for selection;
`file.npz@a:b` keeps seconds a to b of the take. Runs land in
captures/wasp/gru_runs/<name>/, with TensorBoard logs:

    .venv-nam/bin/tensorboard --logdir captures/wasp/gru_runs
"""

import argparse
import json
import pathlib
import time

import numpy as np
import torch
from torch import nn
from torch.utils.tensorboard import SummaryWriter

HERE = pathlib.Path(__file__).resolve().parent
RUNS = HERE.parent / "captures" / "wasp" / "gru_runs"
SR = 48000
NAM_TRAIN_START = 480_000  # NAM's v3 split: train from 10 s ...
NAM_VALIDATE = 432_000     # ... to the last 9 s, which validate


# ------------------------------------------------------------------------- data

def load(spec, out):
    """A take as (inputs dict, target). spec is file.npz or file.npz@a:b (seconds)."""
    path, _, span = spec.partition("@")
    z = np.load(path)
    sl = slice(None)
    if span:
        a, b = (float(v) for v in span.split(":"))
        sl = slice(int(a * SR), int(b * SR))
    x = z["x"][sl].astype(np.float32)
    feats = {"audio": x,
             "cv": z["cv_v"][sl].astype(np.float32),
             "res": np.full(len(x), float(z["res"]), np.float32)}
    return feats, z[f"y_{out}"][sl].astype(np.float32)


def nam_split(spec, out):
    feats, y = load(spec, out)
    n = len(y)
    train = ({k: v[NAM_TRAIN_START:n - NAM_VALIDATE] for k, v in feats.items()}, y[NAM_TRAIN_START:n - NAM_VALIDATE])
    valid = ({k: v[n - NAM_VALIDATE:] for k, v in feats.items()}, y[n - NAM_VALIDATE:])
    return train, valid


def stack(feats, inputs):
    """The network's input features, scaled to about ±1: audio as is, CV in tens of
    volts, the Res dial in tens."""
    scale = {"audio": 1.0, "cv": 0.1, "res": 0.1}
    return np.stack([feats[k] * scale[k] for k in inputs], axis=-1)


# ------------------------------------------------------------------------ model

class RNNModel(nn.Module):
    def __init__(self, n_inputs, hidden, cell="gru", layers=1):
        super().__init__()
        rnn = {"gru": nn.GRU, "lstm": nn.LSTM}[cell]
        self.rnn = rnn(n_inputs, hidden, num_layers=layers, batch_first=True)
        self.dense = nn.Linear(hidden, 1)

    def forward(self, x, h=None):
        z, h = self.rnn(x, h)
        return self.dense(z).squeeze(-1), h


def detach(h):
    """Cut the graph at a backprop boundary; an LSTM's state is a pair."""
    return tuple(t.detach() for t in h) if isinstance(h, tuple) else h.detach()


def esr(pred, target):
    return torch.mean((pred - target) ** 2) / (torch.mean(target ** 2) + 1e-10)


@torch.no_grad()
def run_full(model, feats_x, device, chunk=SR * 10):
    """The whole passage through the network, state carried across chunks, as on a module."""
    model.eval()
    x = torch.from_numpy(feats_x)[None].to(device)
    out, h = [], None
    for i in range(0, x.shape[1], chunk):
        y, h = model(x[:, i:i + chunk], h)
        out.append(y)
    return torch.cat(out, 1)[0].cpu()


def score(model, passages, inputs, device):
    """ESR on each passage, the first 0.1 s left out while the state settles."""
    res = {}
    for name, (feats, y) in passages.items():
        pred = run_full(model, stack(feats, inputs), device)
        k = SR // 10
        res[name] = float(esr(pred[k:], torch.from_numpy(y)[k:]))
    return res


# ---------------------------------------------------------------------- training

def segments(sets, inputs, seg, rng, n):
    """n random segments, seg samples long, from the training takes (weighted by length)."""
    lengths = np.array([len(y) - seg for _, y in sets], dtype=np.float64)
    pick = rng.choice(len(sets), size=n, p=lengths / lengths.sum())
    xs, ys = [], []
    for i in pick:
        feats, y = sets[i]
        a = int(rng.integers(0, len(y) - seg))
        xs.append(stack({k: v[a:a + seg] for k, v in feats.items()}, inputs))
        ys.append(y[a:a + seg])
    return torch.from_numpy(np.stack(xs)), torch.from_numpy(np.stack(ys))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--name", required=True, help="run name, a folder under captures/wasp/gru_runs")
    ap.add_argument("--inputs", default="audio", help="audio[,cv][,res]")
    ap.add_argument("--output", default="lphp", choices=("lphp", "bp"))
    ap.add_argument("--nam-split", action="append", default=[], help="a NAM-file take, split as NAM does")
    ap.add_argument("--train", action="append", default=[], help="training .npz (spec)")
    ap.add_argument("--valid", action="append", default=[], help="validation .npz (spec), for selection")
    ap.add_argument("--judge", action="append", default=[], help="held-out .npz (spec), scored at the end only")
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
    ap.add_argument("--patience", type=int, default=30, help="epochs without improvement before stopping")
    ap.add_argument("--device", default="cpu", help="cpu or mps")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    if args.threads:
        torch.set_num_threads(args.threads)
    rng = np.random.default_rng(args.seed)
    device = torch.device(args.device)
    inputs = args.inputs.split(",")
    train_sets, valid = [], {}
    for spec in args.nam_split:
        tr, va = nam_split(spec, args.output)
        train_sets.append(tr)
        valid[f"{pathlib.Path(spec).stem} (NAM's last 9 s)"] = va
    train_sets += [load(s, args.output) for s in args.train]
    valid.update({s: load(s, args.output) for s in args.valid})
    judge = {s: load(s, args.output) for s in args.judge}
    if not train_sets or not valid:
        raise SystemExit("need training material and a validation set")

    run = RUNS / args.name
    run.mkdir(parents=True, exist_ok=True)
    (run / "config.json").write_text(json.dumps(vars(args), indent=2) + "\n")
    writer = SummaryWriter(str(run))
    model = RNNModel(len(inputs), args.hidden, args.cell, args.layers).to(device)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"{args.name}: {args.cell.upper()}-{args.hidden} x{args.layers}, inputs {inputs}, {n_params} parameters, "
          f"{sum(len(y) for _, y in train_sets) / SR:.0f} s of training audio, on {device}")
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.ReduceLROnPlateau(opt, factor=0.5, patience=10)
    seg = int(args.seg * SR)
    best, best_epoch, t0 = float("inf"), -1, time.monotonic()
    for epoch in range(args.epochs):
        model.train()
        losses = []
        for _ in range(args.steps_per_epoch):
            x, y = segments(train_sets, inputs, seg, rng, args.batch)
            x, y = x.to(device), y.to(device)
            with torch.no_grad():  # warm the state up on the first samples; no loss there
                _, h = model(x[:, :args.warmup])
            for a in range(args.warmup, seg, args.tbptt):
                b = min(a + args.tbptt, seg)
                pred, h = model(x[:, a:b], h)
                loss = esr(pred, y[:, a:b])
                opt.zero_grad()
                loss.backward()
                opt.step()
                h = detach(h)
                losses.append(loss.item())
        val = score(model, valid, inputs, device)
        v = float(np.mean(list(val.values())))
        sched.step(v)
        lr = opt.param_groups[0]["lr"]
        writer.add_scalar("train/ESR", float(np.mean(losses)), epoch)
        writer.add_scalar("valid/ESR", v, epoch)
        writer.add_scalar("lr", lr, epoch)
        mark = ""
        if v < best:
            best, best_epoch, mark = v, epoch, "  *"
            torch.save(model.state_dict(), run / "best.pt")
        minutes = (time.monotonic() - t0) / 60
        print(f"epoch {epoch:4d}  train ESR {np.mean(losses):.4f}  valid ESR {v:.4f}  lr {lr:.1e}  "
              f"{minutes:5.1f} min{mark}", flush=True)
        if epoch - best_epoch >= args.patience:
            print(f"no improvement for {args.patience} epochs: stopping")
            break
    model.load_state_dict(torch.load(run / "best.pt"))
    result = {"best_epoch": best_epoch, "valid": score(model, valid, inputs, device),
              "judge": score(model, judge, inputs, device), "parameters": n_params,
              "minutes": round((time.monotonic() - t0) / 60, 1)}
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
