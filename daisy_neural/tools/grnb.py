#!/usr/bin/env python3
"""
grnb.py — the .grnb file: one GRU from train_gru.py, ready for the microSD card.

The GRU counterpart of a2nb.py. The firmware's reader is gru::Parse() in
nam/gru_runtime.h, and the two must agree. Little-endian, 64-byte header + payload:

    off  size  field
    0    4     magic 'GRNB'
    4    2     format version: 1, or 2 with a layer section after the payload
    6    2     reserved (0)
    8    2     hidden units (multiple of 4, at most 32)
    10   2     inputs (at most 4)
    12   4     output gain (float32)
    16   12    name, ASCII, NUL-padded
    28   4     CRC32 (IEEE 802.3) of the payload only
    32   4     input kinds, one byte each: 0 audio, 1 CV 1 in volts, 2 the Res dial
    36   16    input scales, float32 each: what each raw input is multiplied by
    52   4     version 2: the layer section's size in bytes (version 1: 0)
    56   4     version 2: CRC32 of the layer section (version 1: 0)
    60   4     reserved (0)
    64   ...   payload, float32, PyTorch's nn.GRU layout: weight_ih (3H x I),
               weight_hh (3H x H), bias_ih (3H), bias_hh (3H), then the dense
               layer's weight (H) and bias (1)
    then       version 2: the layer section (tools/wasp_layer.py, nam/gran_layer.h)
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
    layer = data[HEADER_SIZE + size:]
    if len(payload) != size or zlib.crc32(payload) & 0xFFFFFFFF != crc:
        raise ValueError("payload size or CRC mismatch")
    if len(layer) != lsize or (lsize and zlib.crc32(layer) & 0xFFFFFFFF != lcrc):
        raise ValueError("layer size or CRC mismatch")
    kinds = list(data[32:32 + inputs])
    scales = list(struct.unpack_from(f"<{inputs}f", data, 36))
    out = (name, gain, hidden, kinds, scales, list(struct.unpack(f"<{size // 4}f", payload)))
    return out + ((layer or None),) if with_layer else out
