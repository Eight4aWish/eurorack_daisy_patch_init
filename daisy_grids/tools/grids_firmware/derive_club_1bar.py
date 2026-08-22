"""Derive the Club bank at one bar, selected by rhythmic signature.

No corpus is simultaneously modern, timed and openly licensed. Lakh has the
scale and CC BY, but its clean subset is Beatles, Metallica, ABBA and Genesis -
filter it by artist for anything electronic and you get a few dozen files of
dated synth-pop.

So do not ask who played it, ask what the rhythm is. A four-to-the-floor kick
under offbeat hats IS the house vocabulary wherever it turns up, and that is
detectable directly from the notes. Three signatures, sampled evenly:

  four-to-the-floor  kick on all four beats, hats on at least three offbeats
  breakbeat          backbeat snare, kick on 1 and syncopated before the 3
  half-time          snare on 3 only, neither 2 nor 4

One bar of 16 sixteenths, written to the even slots - see derive_1bar.py for why
that and not 32nds. (Measured: 81.5% of Lakh's drum hits land exactly on a 16th
and only 1.2% at the 32nd between, so there is nothing in the gaps to keep.)
"""
import sys, pathlib, collections, random
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from derive_1bar import train_som, shape_to_nodes, coherence
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'groove_nodes'))
import extract as ex

K, S, H = 0, 1, 2
BEATS, OFFS = (0, 4, 8, 12), (2, 6, 10, 14)


def signature(g):
    """g is (3, 16). Returns a label or None."""
    on = g > 0
    if on[K][list(BEATS)].all() and on[H][list(OFFS)].sum() >= 3:
        return 'four_to_the_floor'
    if on[S][4] and on[S][12] and on[K][0]:
        if on[K][9:12].any():
            return 'breakbeat'
    if on[S][8] and not on[S][4] and not on[S][12]:
        return 'half_time'
    return None


def bars(path):
    for f in sorted(pathlib.Path(path).rglob('*.mid')):
        try:
            evs, div = ex.note_ons(f.read_bytes(), True)   # channel 10 only
        except Exception:
            continue
        if not evs or div <= 0:
            continue
        per16 = div / 4.0
        grid = collections.defaultdict(lambda: np.zeros((3, 16), dtype=np.float32))
        for tick, note, vel in evs:
            lane = next((l for l, s in enumerate(ex.LANES) if note in s), None)
            if lane is None:
                continue
            bar, off = divmod(int(round(tick / per16)), 16)
            grid[bar][lane, off] = max(grid[bar][lane, off], vel / 127.0)
        for k in sorted(grid):
            g = grid[k]
            if (g > 0).sum() >= 4:
                yield g


def main():
    src = sys.argv[1]
    per_class = int(sys.argv[2]) if len(sys.argv) > 2 else 20000
    buckets = collections.defaultdict(list)
    total = 0
    for g in bars(src):
        total += 1
        sig = signature(g)
        if sig and len(buckets[sig]) < per_class:
            buckets[sig].append(g.reshape(-1))
    print(f"scanned {total} one-bar patterns")
    for k, v in buckets.items():
        print(f"    {k:20} {len(v)}")
    n = min(len(v) for v in buckets.values())
    rng = random.Random(11)
    X = np.array([p for v in buckets.values() for p in rng.sample(v, n)], dtype=np.float32)
    print(f"  balanced to {n} each -> {len(X)} patterns")
    A = shape_to_nodes(train_som(X), np.load('orig96.npy'))
    np.save('club_1bar.npy', A)
    g = A.reshape(25, 3, 32)
    print(f"  coherence {coherence(A):.1f}%   nonzero even {(g[:,:,0::2]>0).sum()}  odd {(g[:,:,1::2]>0).sum()}")


if __name__ == '__main__':
    main()
