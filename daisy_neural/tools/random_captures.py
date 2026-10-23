#!/usr/bin/env python3
"""
random_captures.py — A2-Lite captures with random weights, as AMPS-bank .a2nb files (David,
2026-10-22: "it would be good to have the doubled network on the card and to explore
creating some more").

NAM rebuilds an A2-Lite network from the Klon starter's config (its layout only, none of
its weights), PyTorch's default initialisation draws every weight from the seed, the biases
are zeroed and every weight is multiplied by the scale: at 1 a random network mostly
smothers the signal, at 2 it drives into fuzz (notamp_ideas.py, listening round
2026-10-22-notamps). Each file is levelled as the card's captures are (level_captures.py,
-27 dB A-weighted), and rendered on the test phrase for listening.

    .venv-nam/bin/python tools/random_captures.py <out dir> 1:2           # seed 1, x2
    .venv-nam/bin/python tools/random_captures.py <out dir> 1-8:1.5,2,3   # a grid
"""

import pathlib
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
import torch

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import a2nb  # noqa: E402
from notamp_ideas import a2_render, finish, lite_model, phrase  # noqa: E402

SR = 48000


def weights(seed, scale):
    from nam.models import init_from_nam
    torch.manual_seed(seed)
    net = init_from_nam(lite_model())
    with torch.no_grad():
        for m in net.modules():
            if hasattr(m, "reset_parameters") and m is not net:
                m.reset_parameters()
        for name, p in net.named_parameters():
            if name.endswith("bias"):
                p.zero_()
            else:
                p.mul_(scale)
    return np.asarray(net._export_weights(), np.float64)


def spec(text):
    seeds, scales = text.split(":")
    a, _, b = seeds.partition("-")
    return [int(s) for s in range(int(a), int(b or a) + 1)], [float(v) for v in scales.split(",")]


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    seeds, scales = spec(sys.argv[2])
    x, tmp = phrase(), pathlib.Path(tempfile.mkdtemp())
    files = []
    for s in seeds:
        for k in scales:
            w = weights(s, k)
            name = f"RND{s} X{k:g}"[:a2nb.NAME_LEN - 1]
            path = out / (f"r{s:02d}_x{str(k).replace('.', 'p')}" + ".a2nb")
            path.write_bytes(a2nb.pack(name, 1.0, w))
            files.append(path)
    subprocess.run([sys.executable, str(HERE / "level_captures.py"), "--write", *map(str, files)],
                   check=True, capture_output=True)
    for path in files:
        blob = path.read_bytes()
        _, gain, _, _ = a2nb.unpack(blob)
        y = a2_render(np.frombuffer(blob[a2nb.HEADER_SIZE:], "<f4"), x, tmp) * gain
        sf.write(path.with_suffix(".wav"), finish(y), SR, subtype="PCM_24")
        print(f"{path.name}: gain {gain:.3f}")


if __name__ == "__main__":
    main()
