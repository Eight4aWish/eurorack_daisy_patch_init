#!/usr/bin/env python3
"""
notamp_design.py — the nine not-amps, and the tables the firmware needs.

A not-amp is a real capture played through the same A2 engine but bent: frozen,
folded, faded, offset, mutated, or morphed past another amp. Each has ONE
control, the steer knob (CV_4 + CV_8), mapped onto a parameter range chosen on
the Mac (2026-09-29) for how smoothly and how far it moves the sound.

For each not-amp this measures the output level at nine steer positions, through
the real engine (tools/a2_host_steer), relative to the capture it is built from
played normally. The firmware applies the inverse as a gain table, so sweeping
the steer knob changes the sound rather than the volume. It also exports the
fixed noise vector the mutation not-amp adds, the same one heard in the
experiment (Python random.Random(1), per-parameter-group spreads of the JCM800).

    c++ -std=c++17 -O2 -I.. -o tools/a2_host_steer tools/a2_host_steer.cpp
    python3 tools/notamp_design.py            # writes src/notamps.h

Why the morph ranges stop at 1.3x: past that the network's gain climbs about
20 dB per eighth of a step as the extrapolated weights compound through 23
layers. The tone changes worth having happen before that.
"""

import array
import math
import pathlib
import random
import statistics as st
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(HERE))
from quantisation_study import make_synth, scale_peak  # noqa: E402

K = [6] * 14 + [15, 15] + [6] * 7
CAP_FILES = {'BE-100': 'Be100', 'JCM800': 'Jcm800', 'Ampeg': 'AmpegSvt',
             'Mesa': 'MesaDualRec', '1959BJA': 'Marshall1959BJA'}

# name on the OLED (<= 10 chars), kind, capture A, capture B, spec, lo, hi, log map,
# and the capture whose normal level is the reference (what the firmware takes the
# header gain from).
NOTAMPS = [
    ('FREEZE',     'freeze', 'JCM800', None,      'freeze:10:{a}:{b}',     1.0, 1024.0, True,  'JCM800'),
    ('PAST JCM',   'morph',  'Ampeg',  'JCM800',  'morph:{a}:{b}',         1.0, 1.3,    False, 'JCM800'),
    ('PAST BJA',   'morph',  'Ampeg',  '1959BJA', 'morph:{a}:{b}',         1.0, 1.3,    False, '1959BJA'),
    ('NO LONG',    'blend',  'JCM800', None,      'blend:6+13+22:{a}:{b}', 1.0, 0.0,    False, 'JCM800'),
    ('FOLDED',     'fold',   'JCM800', None,      'fold:10:{a}:{b}',       1.0, 0.3,    True,  'JCM800'),
    ('PAST MESA',  'morph',  'BE-100', 'Mesa',    'morph:{a}:{b}',         1.0, 1.3,    False, 'Mesa'),
    ('OFFSET',     'offset', 'JCM800', None,      'off:2:0:{a}:{b}',       -2.0, 2.0,   False, 'JCM800'),
    ('MUTATE',     'mutate', 'JCM800', 'noise',   'mutate:{a}:{b}',        0.0, 0.4,    False, 'JCM800'),
    ('FREEZE ERL', 'freeze', 'JCM800', None,      'freeze:2:{a}:{b}',      1.0, 1024.0, True,  'JCM800'),
]


def read_f32(p):
    a = array.array('f')
    a.frombytes(pathlib.Path(p).read_bytes())
    return list(a)


def groups():
    """Parameter groups in load_weights() order: interleaved per layer."""
    g = {k: [] for k in ('rechannel', 'conv', 'convB', 'mixin', 'l1x1', 'l1x1B',
                         'head', 'headB', 'headScale')}
    p = 0
    g['rechannel'] = list(range(p, p + 3)); p += 3
    for li in range(23):
        g['conv'] += list(range(p, p + 9 * K[li])); p += 9 * K[li]
        for name, n in (('convB', 3), ('mixin', 3), ('l1x1', 9), ('l1x1B', 3)):
            g[name] += list(range(p, p + n)); p += n
    g['head'] = list(range(p, p + 48)); p += 48
    g['headB'] = [p]; p += 1
    g['headScale'] = [p]; p += 1
    assert p == 1871
    return g


def mutation_noise(jcm):
    g = groups()
    sig = {k: (st.pstdev([jcm[i] for i in v]) if len(v) > 1 else abs(jcm[v[0]])) for k, v in g.items()}
    rng = random.Random(1)
    noise = [0.0] * 1871
    for name, idx in g.items():
        if name == 'headScale':
            continue
        for i in idx:
            noise[i] = rng.gauss(0, sig[name])
    return noise


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x))) or 1e-12


def dcblock(x, fc=20.0, sr=48000):
    r = math.exp(-2 * math.pi * fc / sr); y = []; x1 = y1 = 0.0
    for v in x:
        o = v - x1 + r * y1; x1, y1 = v, o; y.append(o)
    return y


def main():
    host = HERE / 'a2_host_steer'
    if not host.exists():
        sys.exit('build tools/a2_host_steer first (see the docstring)')
    caps = {n: read_f32(PROJECT / f'amp_compare/w_kWeights{f}.f32') for n, f in CAP_FILES.items()}
    noise = mutation_noise(caps['JCM800'])
    synth = make_synth()
    scale_peak(synth, 0.5)

    tmp = pathlib.Path(tempfile.mkdtemp())
    def run(A, B, spec):
        pa, pb, pi, po = (tmp / n for n in ('a.f32', 'b.f32', 'i.f32', 'o.f32'))
        array.array('f', A).tofile(open(pa, 'wb'))
        array.array('f', B).tofile(open(pb, 'wb'))
        array.array('f', synth).tofile(open(pi, 'wb'))
        subprocess.run([str(host), str(pa), str(pb), str(pi), str(po), spec], check=True)
        return dcblock(read_f32(po)[:len(synth)])

    rows = []
    for name, kind, an, bn, tmpl, lo, hi, lg, ref in NOTAMPS:
        A = caps[an]
        B = noise if bn == 'noise' else (caps[bn] if bn else A)
        ref_level = rms(run(caps[ref], caps[ref], 'morph:0:0'))
        gains = []
        for i in range(9):
            u = i / 8
            v = math.exp(math.log(lo) + (math.log(hi) - math.log(lo)) * u) if lg else lo + (hi - lo) * u
            lvl = 20 * math.log10(rms(run(A, B, tmpl.format(a=v, b=v))) / ref_level)
            gains.append(-lvl)
        rows.append((name, kind, an, bn, tmpl, lo, hi, lg, ref, gains))
        print(f'{name:10s} correction dB: ' + ' '.join(f'{g:+6.1f}' for g in gains))

    # ---- write src/notamps.h -------------------------------------------------
    def layer_of(t):
        return t.split(':')[1] if ':' in t else '0'
    L = []
    L.append('// GENERATED by tools/notamp_design.py — do not edit by hand; rerun it.')
    L.append('// The nine not-amps: a real capture, bent, with one steer control.')
    L.append('#pragma once')
    L.append('')
    L.append('#include <cstdint>')
    L.append('')
    L.append('namespace notamps')
    L.append('{')
    L.append('enum class Kind : uint8_t { Freeze, Morph, Blend, Fold, Offset, Mutate };')
    L.append('')
    L.append('struct Def')
    L.append('{')
    L.append('    const char* name;     // shown on the OLED')
    L.append('    Kind        kind;')
    L.append('    const char* cap_a;    // capture name on the card')
    L.append('    const char* cap_b;    // second capture for Morph, else nullptr')
    L.append('    const char* cap_ref;  // capture whose header gain sets the level')
    L.append('    float       lo, hi;   // parameter at steer 0 and steer 1')
    L.append('    bool        log_map;  // map the steer logarithmically')
    L.append('    uint8_t     layer, lane;')
    L.append('    float       gain_db[9]; // level correction at steer 0, 1/8, ... 1')
    L.append('};')
    L.append('')
    L.append('// Blend acts on these layers (the three with a gap of 239).')
    L.append('constexpr uint8_t kBlendLayers[3] = {6, 13, 22};')
    L.append('')
    def cf(x):
        t = f'{x:.7g}'
        return t + ('f' if ('.' in t or 'e' in t) else '.0f')
    kinds = {'freeze': 'Freeze', 'morph': 'Morph', 'blend': 'Blend', 'fold': 'Fold', 'offset': 'Offset', 'mutate': 'Mutate'}
    L.append('constexpr Def kDefs[] = {')
    for name, kind, an, bn, tmpl, lo, hi, lg, ref, gains in rows:
        parts = tmpl.split(':')
        layer = int(parts[1]) if kind in ('freeze', 'fold', 'offset') else 0
        lane = int(parts[2]) if kind == 'offset' else 0
        cb = f'"{bn}"' if (bn and bn != 'noise') else 'nullptr'
        g = ', '.join(cf(round(x, 2)) for x in gains)
        L.append(f'    {{"{name}", Kind::{kinds[kind]}, "{an}", {cb}, "{ref}", {cf(lo)}, {cf(hi)}, '
                 f'{"true" if lg else "false"}, {layer}, {lane}, {{{g}}}}},')
    L.append('};')
    L.append('constexpr int kCount = sizeof(kDefs) / sizeof(kDefs[0]);')
    L.append('')
    L.append('// Added to the JCM800 by MUTATE, scaled by the steer. Random.Random(1) draws')
    L.append('// with each parameter group\'s JCM800 spread, in load_weights() order.')
    L.append('constexpr float kMutateNoise[1871] = {')
    for i in range(0, 1871, 8):
        L.append('    ' + ', '.join(cf(v) for v in noise[i:i + 8]) + ',')
    L.append('};')
    L.append('} // namespace notamps')
    (PROJECT / 'src/notamps.h').write_text('\n'.join(L) + '\n')
    print('wrote src/notamps.h')


if __name__ == '__main__':
    main()
