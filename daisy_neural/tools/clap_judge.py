#!/usr/bin/env python3
"""
clap_judge.py — a second, perceptual opinion on the not-amp search.

notamp_search.py decides "amp-like or not" from hand-chosen descriptors. This asks
CLAP (LAION's contrastive audio-text model, laion/clap-htsat-unfused — the
larger music checkpoint is broken as published; see requirements-clap.txt),
which learned its sense of similarity from audio paired with descriptions, two
questions about every rendered clip:

  1. Zero-shot: does it sound more like a guitar amp than like the alternatives?
     Each clip is scored against text prompts; "amp-ness" is the probability mass
     on the amp prompts. The best-matching prompt is kept as a provisional tag.
  2. Embedding: how far is it from the nearest real capture, in CLAP's space,
     compared with how far real captures sit from each other?

Run in the CLAP environment, on the clips notamp_search.py --dump writes:
    .venv-clap/bin/python tools/clap_judge.py amp_compare/notamp_search/clips
Writes clap.json beside the clips.
"""

import json
import pathlib
import sys

import numpy as np
import soundfile as sf
import torch
from transformers import ClapModel, ClapProcessor

MODEL = "laion/clap-htsat-unfused"

AMP = [
    "a synthesizer played through a guitar amplifier",
    "an overdriven electric guitar amp",
    "a clean electric guitar amplifier tone",
    "a fuzz guitar pedal",
]
NOT_AMP = [
    "a ring modulator",
    "a bitcrushed lo-fi digital synthesizer",
    "a metallic resonant drone",
    "a glitchy stuttering digital effect",
    "a wavefolder synthesizer",
    "a vocal formant filter",
    "a comb filter or flanger",
    "a worn tape machine",
    "an old broken radio",
    "screaming audio feedback",
    "a sub-octave bass synthesizer",
    "a noisy industrial texture",
]


def _emb(o):
    """transformers 5 returns a model output (the projection is pooler_output);
    earlier versions return the tensor itself."""
    return o.pooler_output if hasattr(o, "pooler_output") else o


def main():
    clips = pathlib.Path(sys.argv[1])
    manifest = json.loads((clips / "manifest.json").read_text())
    model = ClapModel.from_pretrained(MODEL).eval()
    proc = ClapProcessor.from_pretrained(MODEL)
    sr = proc.feature_extractor.sampling_rate

    prompts = AMP + NOT_AMP
    with torch.no_grad():
        t = proc(text=prompts, return_tensors="pt", padding=True)
        te = _emb(model.get_text_features(**t))
        te = te / te.norm(dim=-1, keepdim=True)
        scale = model.logit_scale_a.exp() if hasattr(model, "logit_scale_a") else torch.tensor(100.0)

    names = list(manifest)
    embs = []
    for i in range(0, len(names), 16):
        batch = []
        for n in names[i:i + 16]:
            y, r = sf.read(clips / manifest[n]["file"], dtype="float32")
            assert r == sr, f"{n}: {r} Hz, CLAP wants {sr}"
            batch.append(y)
        with torch.no_grad():
            try:
                a = proc(audio=batch, sampling_rate=sr, return_tensors="pt")
            except TypeError:
                a = proc(audios=batch, sampling_rate=sr, return_tensors="pt")
            e = _emb(model.get_audio_features(**a))
        embs.append((e / e.norm(dim=-1, keepdim=True)).numpy())
        print(f"  {min(i + 16, len(names))}/{len(names)}", end="\r", flush=True)
    E = np.vstack(embs)

    probs = _softmax(E @ te.numpy().T * float(scale))
    ampness = probs[:, : len(AMP)].sum(1)
    tag = [prompts[j] for j in probs.argmax(1)]

    kind = np.array([manifest[n]["kind"] for n in names])
    amp_i = np.where(kind == "amp")[0]
    cos = E @ E[amp_i].T                          # similarity to every real capture
    for k, i in enumerate(amp_i):
        cos[i, k] = -np.inf                       # an amp is not its own neighbour
    nn = 1 - cos.max(1)                           # distance to the nearest real capture
    tau = float(np.percentile(nn[amp_i], 95))

    out = {n: dict(ampness=float(ampness[i]), tag=tag[i], dist=float(nn[i]),
                   outside=bool(nn[i] > tau)) for i, n in enumerate(names)}
    (clips / "clap.json").write_text(json.dumps(dict(tau=tau, clips=out), indent=1))
    np.savez(clips / "clap_emb.npz", names=np.array(names), emb=E)  # for diversity in CLAP's space
    a = ampness[amp_i]
    c = ampness[kind != "amp"]
    print(f"\nreal captures: amp-ness median {np.median(a):.2f}; candidates: median {np.median(c):.2f}")
    print(f"embedding threshold τ = {tau:.3f}; candidates outside: {np.mean(nn[kind != 'amp'] > tau):.0%}")


def _softmax(x):
    x = x - x.max(1, keepdims=True)
    e = np.exp(x)
    return e / e.sum(1, keepdims=True)


if __name__ == "__main__":
    main()
