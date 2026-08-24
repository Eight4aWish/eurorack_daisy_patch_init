#!/usr/bin/env python3
"""Rhythmic-signature selection: find club vocabulary by what the rhythm IS.

SUPERSEDED for the shipped Club bank, which now comes from Groove MIDI via
exemplars.py using this file's `signature()`. Kept because the signature logic is
the reusable part and because Lakh may be the right corpus for something else.

DO NOT ship a Lakh-derived bank made with exemplars.py. The exemplar method
embeds real patterns rather than averages, and every one of the 25 nodes traced
back to a two-bar window of a named commercial recording - Led Zeppelin, Nirvana,
Kraftwerk, Metallica, two from a John Bonham drum solo - with eight reproducing
their source rhythm bit for bit. Centroids used to make that moot. They no longer
apply. Groove MIDI is CC BY 4.0 and measures the same or better.

The original note follows.

The Club bank, by the exemplars method - Lakh, selected by rhythmic signature.

Lakh has no usable genre labels (its clean subset is Beatles, Metallica, ABBA),
so the club vocabulary is found by what the rhythm IS rather than who played it.
Three signatures, sampled evenly, then handed to exemplars.py's choose/arrange/
shape - see that file for why centroids were the wrong thing and archetypes are
the right one.

    python3 exemplars_club.py <lakh-dir> <out-prefix> [--bars 2] [--per-class 20000]
"""
from __future__ import annotations

import argparse
import collections
import pathlib
import random
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import extract as ex  # noqa: E402
from exemplars import LANES, arrange, choose, report, shape  # noqa: E402

K, S, H = 0, 1, 2


def signature(g: np.ndarray, steps: int) -> str | None:
    """g is (3, steps). Beats land every steps//4."""
    q = steps // 4
    beats = [0, q, 2 * q, 3 * q]
    offs = [q // 2, q + q // 2, 2 * q + q // 2, 3 * q + q // 2]
    on = g > 0
    if on[K][beats].all() and on[H][offs].sum() >= 3:
        return "four_to_the_floor"
    if on[S][q] and on[S][3 * q] and on[K][0] and on[K][2 * q + 1:3 * q].any():
        return "breakbeat"
    if on[S][2 * q] and not on[S][q] and not on[S][3 * q]:
        return "half_time"
    return None


def bars(path: str, steps: int, per_class: int):
    buckets: dict[str, list] = collections.defaultdict(list)
    scanned = 0
    for f in sorted(pathlib.Path(path).rglob("*.mid")):
        try:
            evs, div = ex.note_ons(f.read_bytes(), True)  # channel 10 only
        except Exception:
            continue
        if not evs or div <= 0:
            continue
        per16 = div / 4.0
        grid = collections.defaultdict(lambda: np.zeros((LANES, steps), dtype=np.float32))
        for tick, note, vel in evs:
            lane = next((l for l, s in enumerate(ex.LANES) if note in s), None)
            if lane is None:
                continue
            bar, off = divmod(int(round(tick / per16)), steps)
            grid[bar][lane, off] = max(grid[bar][lane, off], vel / 127.0)
        for k in sorted(grid):
            g = grid[k]
            if (g > 0).sum() < max(4, steps // 4):
                continue
            scanned += 1
            sig = signature(g, steps)
            if sig and len(buckets[sig]) < per_class:
                buckets[sig].append(g.reshape(-1))
    return buckets, scanned


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus")
    ap.add_argument("prefix")
    ap.add_argument("--bars", type=int, default=2, choices=(1, 2))
    ap.add_argument("--per-class", type=int, default=20000)
    ap.add_argument("--floor", type=float, default=8.0,
                    help="percentile floor on edge length; 0 disables")
    args = ap.parse_args()
    steps = 32 if args.bars == 2 else 16

    buckets, scanned = bars(args.corpus, steps, args.per_class)
    print(f"{args.prefix}: scanned {scanned} windows of {steps} steps")
    for k, v in buckets.items():
        print(f"    {k:<20} {len(v)}")
    n = min(len(v) for v in buckets.values())
    rng = random.Random(11)
    X = np.array([p for v in buckets.values() for p in rng.sample(v, n)], dtype=np.float32)
    print(f"  balanced to {n} each -> {len(X)}")

    # Lakh's four-to-the-floor really does repeat, so a few chosen patterns land
    # close together and the plain arrangement buries them next to each other.
    # A light floor forbids that. Groove MIDI needs none - human timing keeps
    # everything distinct on its own.
    E = arrange(choose(X) * 255.0, floor_pct=args.floor, floor_w=2.0)
    A = shape(E, np.load(pathlib.Path(__file__).resolve().parent / "orig.npy"), steps)
    report(A, "after histogram match")
    np.save(f"{args.prefix}_nodes.npy", A)
    print(f"  wrote {args.prefix}_nodes.npy")


if __name__ == "__main__":
    main()
