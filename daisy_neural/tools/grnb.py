#!/usr/bin/env python3
"""
grnb.py — the .grnb file: one GRU from train_gru.py, ready for the microSD card.

The GRU counterpart of a2nb.py. The firmware's reader is gru::Parse() in
nam/gru_runtime.h, and the two must agree. Little-endian, 64-byte header + payload:

    off  size  field
    0    4     magic 'GRNB'
    4    2     format version: 1, or 2 with a layer section after the payload
    6    2     the controls section's size in bytes, 0 for none (2026-10-22)
    8    2     hidden units (multiple of 4, at most 32)
    10   2     inputs (at most 4)
    12   4     output gain (float32)
    16   12    name, ASCII, NUL-padded
    28   4     CRC32 (IEEE 802.3) of the payload only
    32   4     input kinds, one byte each: 0 audio, 1 CV 1 in volts, 2 the Res dial
    36   16    input scales, float32 each: what each raw input is multiplied by
    52   4     version 2: the layer section's size in bytes (version 1: 0)
    56   4     version 2: CRC32 of the layer section (version 1: 0)
    60   4     CRC32 of the controls section (0 for none)
    64   ...   payload, float32, PyTorch's nn.GRU layout: weight_ih (3H x I),
               weight_hh (3H x H), bias_ih (3H), bias_hh (3H), then the dense
               layer's weight (H) and bias (1)
    then       version 2: the layer section (tools/wasp_layer.py, nam/gran_layer.h)
    then       the controls section, when bytes 6-7 are not 0: how Mirth's knobs drive this
               network (tools/grnb_controls.py writes it):
                 'CTRL', version (u16, 1 or 2), points N (u16, at most 8),
                 cut_lo, cut_hi (f32): the network's cutoff volts at the FREQ knob's ends,
                 dial[N], res[N], feedback[N] (f32): the Res dial (0-11) mapped, piecewise
                 linear, to the network's Res input and the feedback of its output into
                 its input (main.cpp ProcessFilter). Without one, Mirth uses the Wasp LP's.
                 Version 2 (2026-10-23) adds an oscillator layer (nam/osc_layer.h; FLT6,
                 Swords with its oscillation): osc_res[N] (f32), the Res CV the layer sees
                 at each dial point, then its 34 constants (f32, tools/osc_layer.py's order).
"""

import struct
import zlib

MAGIC = b"GRNB"
VERSION = 1  # without a layer; with one, 2
HEADER_SIZE = 64
NAME_LEN = 12
MAX_HIDDEN = 32
MAX_INPUTS = 4
KINDS = {"audio": 0, "cv": 1, "res": 2}


def weight_count(hidden, inputs):
    return 3 * hidden * inputs + 3 * hidden * hidden + 6 * hidden + hidden + 1


def pack(name, gain, hidden, inputs, scales, weights, layer=None):
    """inputs: names in network order (audio, cv, res); scales: one float each. layer:
    a layer section's bytes (wasp_layer.pack), which makes the file version 2."""
    if hidden % 4 or not 0 < hidden <= MAX_HIDDEN or not 0 < len(inputs) <= MAX_INPUTS:
        raise ValueError(f"{name}: hidden {hidden}, {len(inputs)} inputs: out of range")
    if len(weights) != weight_count(hidden, len(inputs)):
        raise ValueError(f"{name}: {len(weights)} weights, expected {weight_count(hidden, len(inputs))}")
    payload = struct.pack(f"<{len(weights)}f", *weights)
    kinds = bytes(KINDS[k] for k in inputs).ljust(MAX_INPUTS, b"\0")
    sc = struct.pack(f"<{MAX_INPUTS}f", *(list(scales) + [0.0] * (MAX_INPUTS - len(scales))))
    layer = layer or b""
    tail = struct.pack("<II", len(layer), zlib.crc32(layer) & 0xFFFFFFFF if layer else 0)
    header = (MAGIC + struct.pack("<HHHH", 2 if layer else VERSION, 0, hidden, len(inputs)) + struct.pack("<f", gain)
              + name.encode("ascii", "replace")[: NAME_LEN - 1].ljust(NAME_LEN, b"\0")
              + struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF) + kinds + sc + tail).ljust(HEADER_SIZE, b"\0")
    assert len(header) == HEADER_SIZE
    return header + payload + layer


def unpack(data, with_layer=False):
    """(name, gain, hidden, kinds, scales, weights), checking magic, size and CRCs; with
    with_layer, the layer section's bytes (or None) as well."""
    if data[:4] != MAGIC:
        raise ValueError("not a .grnb file")
    version, _, hidden, inputs = struct.unpack_from("<HHHH", data, 4)
    if version not in (1, 2):
        raise ValueError(f"version {version}")
    (gain,) = struct.unpack_from("<f", data, 12)
    name = data[16:16 + NAME_LEN].split(b"\0")[0].decode("ascii")
    (crc,) = struct.unpack_from("<I", data, 28)
    size = 4 * weight_count(hidden, inputs)
    payload = data[HEADER_SIZE:HEADER_SIZE + size]
    lsize, lcrc = struct.unpack_from("<II", data, 52) if version == 2 else (0, 0)
    (csize,) = struct.unpack_from("<H", data, 6)
    layer = data[HEADER_SIZE + size:len(data) - csize]
    if len(payload) != size or zlib.crc32(payload) & 0xFFFFFFFF != crc:
        raise ValueError("payload size or CRC mismatch")
    if len(layer) != lsize or (lsize and zlib.crc32(layer) & 0xFFFFFFFF != lcrc):
        raise ValueError("layer size or CRC mismatch")
    kinds = list(data[32:32 + inputs])
    scales = list(struct.unpack_from(f"<{inputs}f", data, 36))
    out = (name, gain, hidden, kinds, scales, list(struct.unpack(f"<{size // 4}f", payload)))
    return out + ((layer or None),) if with_layer else out


CTRL_MAGIC = b"CTRL"
CTRL_MAX_POINTS = 8
OSC_FLOATS = 34  # nam/osc_layer.h kParams


def pack_controls(cut_lo, cut_hi, dial, res, feedback, osc_res=None, osc=None):
    """The controls section's bytes (see the docstring); version 2 with osc_res and osc."""
    n = len(dial)
    if not 0 < n <= CTRL_MAX_POINTS or len(res) != n or len(feedback) != n or list(dial) != sorted(dial):
        raise ValueError("controls: 1 to 8 dial points, ascending, with a Res and a feedback each")
    if (osc is None) != (osc_res is None) or (osc is not None and (len(osc_res) != n or len(osc) != OSC_FLOATS)):
        raise ValueError(f"controls: an oscillator layer needs a Res CV per dial point and {OSC_FLOATS} constants")
    out = (CTRL_MAGIC + struct.pack("<HH", 2 if osc is not None else 1, n) + struct.pack("<ff", cut_lo, cut_hi)
           + struct.pack(f"<{n}f", *dial) + struct.pack(f"<{n}f", *res) + struct.pack(f"<{n}f", *feedback))
    if osc is not None:
        out += struct.pack(f"<{n}f", *osc_res) + struct.pack(f"<{OSC_FLOATS}f", *osc)
    return out


def with_controls(data, controls):
    """A .grnb's bytes with its controls section replaced (or removed, controls=None)."""
    (old,) = struct.unpack_from("<H", data, 6)
    body = bytearray(data[:len(data) - old] if old else data)
    c = controls or b""
    struct.pack_into("<H", body, 6, len(c))
    struct.pack_into("<I", body, 60, zlib.crc32(c) & 0xFFFFFFFF if c else 0)
    return bytes(body) + c


def _controls(data):
    (csize,) = struct.unpack_from("<H", data, 6)
    if not csize:
        return None
    c = data[len(data) - csize:]
    if c[:4] != CTRL_MAGIC or zlib.crc32(c) & 0xFFFFFFFF != struct.unpack_from("<I", data, 60)[0]:
        raise ValueError("controls section magic or CRC mismatch")
    version, n = struct.unpack_from("<HH", c, 4)
    if version not in (1, 2) or len(c) != 16 + (16 * n + 4 * OSC_FLOATS if version == 2 else 12 * n):
        raise ValueError(f"controls section version {version}, {len(c)} bytes for {n} points")
    return c, version, n


def unpack_controls(data):
    """(cut_lo, cut_hi, dial, res, feedback), or None for a file without them."""
    got = _controls(data)
    if got is None:
        return None
    c, _, n = got
    lo, hi = struct.unpack_from("<ff", c, 8)
    vals = struct.unpack_from(f"<{3 * n}f", c, 16)
    return lo, hi, list(vals[:n]), list(vals[n:2 * n]), list(vals[2 * n:2 * n + n])


def unpack_osc(data):
    """(osc_res, osc constants) of a version-2 controls section, or None."""
    got = _controls(data)
    if got is None or got[1] != 2:
        return None
    c, _, n = got
    return (list(struct.unpack_from(f"<{n}f", c, 16 + 12 * n)),
            list(struct.unpack_from(f"<{OSC_FLOATS}f", c, 16 + 16 * n)))
