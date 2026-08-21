"""Extract 2-bar, 3-lane drum patterns from a corpus of MIDI.

Takes a zip, a directory tree, or a single file.

Minimal MIDI reader - only note-on events and the header's tick division are
needed, so there is no reason to pull in a dependency.
"""
import zipfile, struct, sys, collections, pathlib
import numpy as np

KICK  = {35, 36}
# 39 is hand clap. In electronic music it plays the snare's role - often instead
# of one - so it belongs in this lane rather than being discarded.
SNARE = {37, 38, 39, 40}
HAT   = {42, 44, 46, 51, 59}          # closed, pedal, open, ride, ride bell
LANES = [KICK, SNARE, HAT]

def varlen(b, i):
    v = 0
    while True:
        c = b[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, i

def note_ons(data, drum_channel_only=False):
    """Yield (tick, note, velocity) plus ticks-per-quarter.

    drum_channel_only matters for any corpus of full arrangements rather than
    drum-only files: GM puts drums on channel 10 (index 9), and melodic parts
    live squarely in the 35-59 note range this cares about. Without the filter a
    bassline reads as a kick drum.
    """
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
                        if not drum_channel_only or (status & 0x0F) == 9:
                            out.append((tick, note, vel))
        pos = end
    return out, div

def patterns_from(data, drum_channel_only=False):
    ev, div = note_ons(data, drum_channel_only)
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

def sources(path):
    """Yield (label, midi bytes) from a zip, a directory tree, or a single file.

    A directory is the useful case for a personal corpus: export patterns out of
    whatever produced them, drop them in a folder per genre, and the immediate
    parent directory becomes the label. Nothing is redistributed, so a corpus of
    commercially licensed content is fine to derive a bank from - it just cannot
    be shipped, and does not need to be.
    """
    p = pathlib.Path(path)
    if p.is_dir():
        for f in sorted(p.rglob('*')):
            if f.is_file() and f.suffix.lower() in ('.mid', '.midi'):
                yield f.parent.name, f.read_bytes()
    elif p.suffix.lower() == '.zip':
        z = zipfile.ZipFile(p)
        for n in z.namelist():
            if n.lower().endswith('.mid'):
                # Groove MIDI encodes style in the filename: 12_funk_81_beat_4-4
                stem = n.split('/')[-1]
                label = stem.split('_')[1] if '_' in stem else p.stem
                yield label, z.read(n)
    else:
        yield p.stem, p.read_bytes()


def main():
    all_pats, styles = [], []
    # A corpus of full arrangements needs the channel filter; a drum-only one
    # like Groove MIDI does not and would lose nothing by it either way.
    drums_only = '--gm-drum-channel' in sys.argv
    for label, data in sources(sys.argv[1]):
        try:
            pats = patterns_from(data, drums_only)
        except Exception:
            continue
        all_pats.extend(pats)
        styles.extend([label] * len(pats))
    if not all_pats:
        sys.exit(
            f"No drum patterns found in {sys.argv[1]}.\n"
            "Looked for note-on events on the GM drum notes (kick 35/36, "
            "snare 37/38/40, hats 42/44/46/51/59), keeping only 2-bar windows\n"
            "with at least 8 hits. Melodic MIDI, or drums mapped to other "
            "notes, will find nothing - see LANES at the top of this file.")

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
