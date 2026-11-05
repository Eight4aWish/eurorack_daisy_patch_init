#!/usr/bin/env python3
"""
amp_breakdown.py - what is inside each starter capture, in rack terms: its EQ, its bend, its
ringing and its slow memory. For the Mirth video's explanation of what a network has to reproduce.

Everything runs through the real engine (a2_host, built from the firmware's own runtime). Beside the
captures are controls whose answers are known, so each test can be seen to tell things apart, and
Mirth's SINE BUG, a real network turned into a wavefolder.

    .venv/bin/python tools/amp_breakdown.py

The tests, all at 48 kHz, with full scale taken as 1.0:

  EQ       A log sweep at 0.01 peak, deconvolved. The response's peak, and where it first falls
           12 dB below that peak on each side. Then the level at 8 kHz against the peak.
  ring     How long the same impulse response stays above -40 dB of its peak. A control with no
           memory at all reads about 13 ms here, so that is this test's floor.
  bend     A 220 Hz tone at six levels. Gain lost from quietest to loudest, in dB; the 2nd harmonic
           against the fundamental (a symmetric clip has none); and turning points per cycle at
           0.03, 0.3 and 1.0, ignoring ripple under 10 % of peak to peak. A clip stays near 2. A
           fold climbs with level. A clip followed by a resonance can also read above 2, so this
           test can't tell a small fold from ringing after a clip.
  droop    A 0.3 tone: level over its first 4-24 ms, against 150-250 ms. A compressor drops here.
  beyond   The same tone: 150-250 ms against 900-1000 ms. The window is 6,346 samples, about 132 ms,
           so a capture has to read zero. A control with a 50 ms compressor doesn't quite.
  duck     A quiet tone straight after a loud one, against the same quiet tone settled, over
           windows up to 240 ms after the change. Positive means the loud note left the quiet one
           quieter. A loud note's own tail can push this negative, which says nothing about gain.
"""
import pathlib
import subprocess
import tempfile

import numpy as np

SR = 48000
F = 220.0
LEVELS = [0.003, 0.01, 0.03, 0.1, 0.3, 1.0]
HERE = pathlib.Path(__file__).resolve().parent
STARTER = HERE.parent / 'captures' / 'starter'
TMP = pathlib.Path(tempfile.mkdtemp())
# From README.md's list of captures and STARTER_CAPTURES.md
KIND = {'SVT-2_PRO': 'bass amp+cab', 'ORANGE_TH': 'amp', 'FENDER_57': 'amp', 'BLUESBRKR': 'pedal', 'KLON': 'pedal',
        'PLEXI_LORE': 'amp+cab', 'SUNBENDER': 'fuzz', 'TWO_ROCK': 'amp+cab', 'KAY_703': 'small amp',
        'TRAYNOR': 'amp', 'BUGERA_G5': 'amp, DI', 'DIY_BJT': 'pedal'}


def db(v):
    return 20 * np.log10(np.maximum(np.abs(v), 1e-12))


def rms(x):
    return np.sqrt(np.mean(np.square(x)))


def tone(f, peak, sec):
    return peak * np.sin(2 * np.pi * f * np.arange(int(sec * SR)) / SR)


def capture(path, *notamp):
    """A function running a signal through one capture, or through a not-amp built on it."""
    w = TMP / (path.stem + '.f32')
    w.write_bytes(path.read_bytes()[32:])       # the .a2nb header is 32 bytes: see a2nb.py

    def run(x):
        i, o = TMP / 'i.f32', TMP / 'o.f32'
        np.asarray(x, '<f4').tofile(i)
        cmd = [str(HERE / 'a2_notamp'), str(w), '-', str(i), str(o), *map(str, notamp)] if notamp \
            else [str(HERE / 'a2_host'), str(w), str(i), str(o)]
        subprocess.run(cmd, check=True, capture_output=True)
        return np.fromfile(o, '<f4')[:len(x)].astype(float)
    return run


def harmonic(y, n):
    return 2 * np.abs(np.mean(y * np.exp(-2j * np.pi * F * n * np.arange(len(y)) / SR)))


def turns_per_cycle(seg):
    """Turning points per cycle, ignoring ripple under 10 % of peak to peak."""
    cycles = 20
    y = seg[:int(round(SR / F * cycles))]
    gate = 0.1 * (y.max() - y.min())
    last, up, turns = y[0], None, 0
    for v in y:
        if up is None:
            if abs(v - last) > gate:
                up, last = v > last, v
        elif (v > last) == up:
            last = v
        elif abs(v - last) > gate:
            turns, up, last = turns + 1, not up, v
    return turns / cycles


def bend(run):
    out = {}
    for level in LEVELS:
        y = run(tone(F, level, 0.5))
        seg = y[int(.25 * SR):int(.25 * SR) + int(round(SR / F * 44))]
        seg = seg - seg.mean()
        h1, h2 = harmonic(seg, 1), harmonic(seg, 2)
        out[level] = dict(gain=db(h1 / level), h2=db(h2 / h1), turns=turns_per_cycle(seg), peak=max(seg.max(), -seg.min()))
    return out


def sweep(run, peak=0.01):
    t_len = 8.0
    t = np.arange(int(t_len * SR)) / SR
    k = t_len / np.log(20000.0 / 20.0)
    x = peak * np.sin(2 * np.pi * 20.0 * k * (np.exp(t / k) - 1))
    x[:480] *= np.linspace(0, 1, 480)
    x[-480:] *= np.linspace(1, 0, 480)
    pad = np.zeros(SR // 2)
    x = np.concatenate([pad, x, pad])
    y = run(x)
    n = 2 * len(x)
    fr = np.fft.rfftfreq(n, 1 / SR)
    h = np.where((fr > 20) & (fr < 20000), np.fft.rfft(y, n) / (np.fft.rfft(x, n) + 1e-9), 0)
    points = np.geomspace(30, 18000, 160)
    mag = np.array([np.sqrt(np.mean(np.abs(h[(fr > p * 2 ** (-1 / 12)) & (fr < p * 2 ** (1 / 12))]) ** 2)) for p in points])
    m = db(mag)
    m -= m.max()
    top = points[m.argmax()]
    above, below = points[(points > top) & (m < -12)], points[(points < top) & (m < -12)]
    ir = np.roll(np.fft.irfft(h), 4800)[:4800 + int(.3 * SR)]
    smooth = np.convolve(np.abs(ir), np.ones(48) / 48, 'same')
    start = smooth.argmax()
    tail = np.nonzero(db(smooth[start:] / smooth.max()) > -40)[0]
    return dict(top=top, lo=below[-1] if len(below) else np.nan, hi=above[0] if len(above) else np.nan,
                at8k=float(np.interp(8000, points, m)), ring=tail[-1] / SR * 1000 if len(tail) else 0)


def memory(run):
    y = run(tone(F, 0.3, 1.2))
    level = lambda a, b: db(rms(y[int(a * SR):int(b * SR)]))
    droop, beyond = level(.004, .024) - level(.15, .25), level(.15, .25) - level(.9, 1.0)
    # a quiet tone straight after a loud one
    change = int(.4 * SR)
    yy = run(np.concatenate([tone(F, 0.5, 0.4), tone(F, 0.01, 0.8)]))
    settled = db(rms(yy[change + int(.4 * SR):change + int(.7 * SR)]))
    duck = [settled - db(rms(yy[change + int(a * SR):change + int(b * SR)]))
            for a, b in ((.002, .022), (.022, .052), (.052, .102), (.102, .132), (.14, .24))]
    return dict(droop=droop, beyond=beyond, duck=duck)


def controls():
    def clip(x):
        return np.tanh(6 * x)

    def fold(x):
        return np.sin(np.pi * 3 * x)

    def compressor(x, tau=0.05):
        env, out, a = 0.0, np.zeros(len(x)), np.exp(-1 / (tau * SR))
        for i, v in enumerate(x):
            env = a * env + (1 - a) * abs(v)
            out[i] = v / (1 + 6 * env)
        return out

    def resonance(x):       # Q about 25 at 800 Hz
        w = 2 * np.pi * 800 / SR
        r = np.exp(-w / 50)
        a1, a2, y = 2 * r * np.cos(w), -r * r, np.zeros(len(x))
        for i in range(2, len(x)):
            y[i] = x[i] - x[i - 2] + a1 * y[i - 1] + a2 * y[i - 2]
        return y

    return [('control: clipper', 'known', clip), ('control: wavefolder', 'known', fold),
            ('control: compressor', 'known', compressor), ('control: resonance', 'known', resonance)]


def main():
    rows = controls()
    for path in sorted(STARTER.glob('n*.a2nb')):
        name = path.stem.split('_', 1)[1]
        rows.append((name, KIND.get(name, '?'), capture(path)))
    # SINE BUG, with its steer at the top and halfway: src/notamps.h has its range
    bugera = STARTER / 'n11_BUGERA_G5.a2nb'
    for u in (0.5, 1.0):
        rows.append((f'SINE BUG, steer {u}', 'not-amp', capture(bugera, 'sine', 0.3, 8.0, 1, 0, 0, u, u)))

    print(f"{'':22}{'':13}|     EQ, Hz          8 kHz |  ring | loses   2nd   turns at     | droop beyond | quiet after loud, dB, by ms after")
    print(f"{'':22}{'':13}|  peak  -12 below/above  dB |    ms |    dB    dB  0.03 0.3  1  |    dB     dB | 2-22 22-52 52-102 102-132 140-240")
    for name, kind, run in rows:
        s, b, m = sweep(run), bend(run), memory(run)
        q, top = b[LEVELS[0]], b[1.0]
        ducks = ' '.join(f'{d:6.1f}' for d in m['duck'])
        print(f"{name:22}{kind:13}| {s['top']:5.0f} {s['lo']:6.0f} {s['hi']:6.0f} {s['at8k']:6.1f} | {s['ring']:5.1f} | {q['gain'] - top['gain']:5.1f} {top['h2']:6.1f}"
              f"  {b[.03]['turns']:3.1f} {b[.3]['turns']:3.1f} {top['turns']:3.1f} | {m['droop']:5.2f} {m['beyond']:6.3f} | {ducks}")


if __name__ == '__main__':
    main()
