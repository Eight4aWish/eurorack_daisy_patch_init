"""Extract 2-bar, 3-lane drum patterns from the Groove MIDI Dataset.

Minimal MIDI reader - only note-on events and the header's tick division are
needed, so there is no reason to pull in a dependency.
"""
import zipfile, struct, sys, collections
import numpy as np

KICK  = {35, 36}
SNARE = {37, 38, 40}
HAT   = {42, 44, 46, 51, 59}          # closed, pedal, open, ride, ride bell
LANES = [KICK, SNARE, HAT]

def varlen(b, i):
    v = 0
    while True:
        c = b[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, i

def note_ons(data):
    """Yield (tick, note, velocity) plus ticks-per-quarter."""
    if data[:4] != b'MThd':
        return [], 0
    _, hlen, fmt, ntrk, div = struct.unpack('>4sIHHH', data[:14])
    out, pos = [], 8 + hlen
    for _ in range(ntrk):
        if data[pos:pos+4] != b'MTrk':
            break
        length = struct.unpack('>I', data[pos+4:pos+8])[0]
        i, end, tick, status = pos + 8, pos + 8 + length, 0, 0
        while i < end:
            d, i = varlen(data, i)
            tick += d
            b = data[i]
            if b & 0x80:
                status = b; i += 1
            if status == 0xFF:                       # meta
                i += 1
                n, i = varlen(data, i); i += n
            elif status in (0xF0, 0xF7):             # sysex
                n, i = varlen(data, i); i += n
            else:
                hi = status & 0xF0
                if hi in (0xC0, 0xD0):
                    i += 1
                else:
                    note, vel = data[i], data[i+1]; i += 2
                    if hi == 0x90 and vel > 0:
                        out.append((tick, note, vel))
        pos = end
    return out, div

def patterns_from(data):
    ev, div = note_ons(data)
    if not ev or div <= 0:
        return []
    per16 = div / 4.0
    grids = collections.defaultdict(lambda: np.zeros((3, 32), dtype=np.float32))
    for tick, note, vel in ev:
        lane = next((l for l, s in enumerate(LANES) if note in s), None)
        if lane is None:
            continue
        step = int(round(tick / per16))
        bar2, off = divmod(step, 32)
        g = grids[bar2]
        g[lane, off] = max(g[lane, off], vel / 127.0)
    out = []
    for k in sorted(grids):
        g = grids[k]
        if (g > 0).sum() >= 8:                        # skip near-empty windows
            out.append(g.reshape(-1))
    return out

def main():
    z = zipfile.ZipFile(sys.argv[1])
    names = [n for n in z.namelist() if n.lower().endswith('.mid')]
    all_pats, styles = [], []
    for n in names:
        try:
            pats = patterns_from(z.read(n))
        except Exception:
            continue
        style = n.split('/')[-1].split('_')[1] if '_' in n.split('/')[-1] else '?'
        all_pats.extend(pats)
        styles.extend([style] * len(pats))
    X = np.array(all_pats, dtype=np.float32)
    np.save(sys.argv[2], X)
    print(f"patterns: {X.shape[0]}  vector: {X.shape[1]}")
    c = collections.Counter(styles)
    print("styles:", ", ".join(f"{k} {v}" for k, v in c.most_common(8)))
    dens = (X > 0).sum(axis=1)
    print(f"hits per 2-bar pattern: mean {dens.mean():.1f}  min {dens.min()}  max {dens.max()}")
    for i, lane in enumerate(("kick", "snare", "hat")):
        seg = X[:, i*32:(i+1)*32]
        print(f"  {lane:5s}: {(seg > 0).sum(axis=1).mean():5.2f} hits/pattern")

if __name__ == '__main__':
    main()
