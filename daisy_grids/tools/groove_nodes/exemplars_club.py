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
    """Classify a window by what its rhythm IS, not by what genre it is filed under.

    A step is a SIXTEENTH - both cutters use per16 = div / 4.0 - so a quarter note
    is four steps and a bar is sixteen, whatever the window length. This used to
    say `q = steps // 4`, which silently assumes the window is exactly four beats.
    True for a one-bar window and wrong for a two-bar one, where it put the "beats"
    on half notes and the "offbeat hats" on beats two and four. Every Sorrow Club
    bank up to v2.3.0 was selected by that: "four-to-the-floor" actually meant kick
    on one and three with hats on two and four, which is a rock pattern.

    A window has to hold the signature in EVERY bar to count, so a two-bar node is
    club for its whole length rather than for half of it.
    """
    on = g > 0
    nbars = max(1, steps // 16)

    def every_bar(test) -> bool:
        return all(test(b * 16) for b in range(nbars))

    if every_bar(lambda o: on[K][[o, o + 4, o + 8, o + 12]].all()
                 and on[H][[o + 2, o + 6, o + 10, o + 14]].sum() >= 3):
        return "four_to_the_floor"
    if every_bar(lambda o: on[S][o + 4] and on[S][o + 12] and on[K][o]
                 and on[K][o + 9:o + 12].any()):
        return "breakbeat"
    if every_bar(lambda o: on[S][o + 8] and not on[S][o + 4] and not on[S][o + 12]):
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
    ap.add_argument("--floor", type=float, default=0.0,
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
    # A light floor forbade that, and the default used to be 8.0 for that reason.
    #
    # Groove MIDI needs none - human timing keeps everything distinct on its own -
    # and measured on it the floor is not merely unnecessary but slightly harmful,
    # because it trades arrangement quality for movement that is already there:
    #
    #     floor   per-edge   edge/map   dead
    #     off       22.1        81%      0/40
    #     p8        21.9        80%      0/40
    #     p12       24.4        91%      0/40
    #     p25       24.8        93%      0/40
    #
    # Every setting clears all forty edges, so there is nothing left to buy. What
    # rises with the floor is edge/map, and at 100% neighbours differ as much as
    # distant cells - a shuffle, not a map. Off sits at 81%, next to Grids' own 78%.
    # Shape first, then arrange - see the note in exemplars.py main().
    E = shape(choose(X) * 255.0,
              np.load(pathlib.Path(__file__).resolve().parent / "orig.npy"), steps)
    A = arrange(E, floor_pct=args.floor, floor_w=2.0)
    report(A, "after histogram match")
    np.save(f"{args.prefix}_nodes.npy", A)
    print(f"  wrote {args.prefix}_nodes.npy")


if __name__ == "__main__":
    main()
