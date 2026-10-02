#!/usr/bin/env python3
"""
test_signal.py — the test phrase the not-amp tools render captures with.

A short subtractive-synth sequence, band-limited, with dynamics, so a capture is heard
across its drive range rather than at one level. Shared by notamp_search.py and
notamp_design.py, so the search and the firmware's level tables hear the same thing.
"""

import array
import math

SAMPLE_RATE = 48000


def make_synth(sr=SAMPLE_RATE, root=110.0):
    """A short subtractive-synth sequence — what this module will actually be
    fed in a rack, rather than one held note.

    Eight notes over four seconds, varying in pitch AND level. The level part
    matters more than it looks: an amp model's most characteristic behaviour is
    how its distortion changes with drive, so a sequence with dynamics reveals
    far more about a damaged transfer curve than a sustained note at one level,
    where the model sits at a single point on its curve the whole time.

    Two detuned saws per note. The saw is built additively into a single-cycle
    wavetable and read back, so it is exactly band-limited — no aliasing in the
    SOURCE to be mistaken for damage done by the model. Harmonic count is capped
    for the HIGHEST note in the sequence, not the root, or the top notes would
    alias.
    """
    # semitone offsets and velocities — a plain minor-key riff
    seq = [(0, 1.00), (0, 0.62), (3, 0.85), (5, 1.00),
           (0, 0.70), (-2, 0.92), (0, 0.55), (7, 1.00)]
    note_len = 0.5
    gate = 0.42  # note sounds for this long, rest is silence

    top = root * (2.0 ** (max(s for s, _ in seq) / 12.0))
    harmonics = max(1, min(int((sr / 2) / top) - 1, 160))

    table_len = 2048
    table = [0.0] * table_len
    for n in range(1, harmonics + 1):
        amp = 1.0 / n
        for i in range(table_len):
            table[i] += amp * math.sin(2.0 * math.pi * n * i / table_len)
    peak = max(abs(v) for v in table) or 1.0
    table = [v / peak for v in table]

    def read(phase):
        x = phase * table_len
        i0 = int(x) % table_len
        i1 = (i0 + 1) % table_len
        f = x - int(x)
        return table[i0] * (1.0 - f) + table[i1] * f

    total = int(len(seq) * note_len * sr)
    out = array.array("f", bytes(4 * total))

    detune = 7.0 / 1200.0  # seven cents, for slow beating
    pos = 0
    for semis, vel in seq:
        f = root * (2.0 ** (semis / 12.0))
        f1 = f * (2.0 ** -detune)
        f2 = f * (2.0 ** detune)
        p1 = p2 = 0.0

        n = int(gate * sr)
        atk = int(0.004 * sr)
        dec = int(0.12 * sr)
        rel = int(0.08 * sr)
        sus = 0.7

        for i in range(n):
            if i < atk:
                env = i / atk
            elif i < atk + dec:
                env = 1.0 - (1.0 - sus) * ((i - atk) / dec)
            elif i > n - rel:
                env = sus * max(0.0, (n - i) / rel)
            else:
                env = sus
            out[pos + i] = 0.5 * (read(p1) + read(p2)) * env * vel
            p1 = (p1 + f1 / sr) % 1.0
            p2 = (p2 + f2 / sr) % 1.0

        pos += int(note_len * sr)

    return out


def scale_peak(samples, peak):
    m = max((abs(v) for v in samples), default=0.0)
    if m <= 0.0:
        return samples
    g = peak / m
    for i in range(len(samples)):
        samples[i] *= g
    return samples
