#!/usr/bin/env python3
"""
grnb.py — the .grnb file: one GRU from train_gru.py, ready for the microSD card.

The GRU counterpart of a2nb.py. The firmware's reader is gru::Parse() in
nam/gru_runtime.h, and the two must agree. Little-endian, 64-byte header + payload:

    off  size  field
    0    4     magic 'GRNB'
    4    2     format version (1)
    6    2     reserved (0)
    8    2     hidden units (multiple of 4, at most 32)
    10   2     inputs (at most 4)
    12   4     output gain (float32)
    16   12    name, ASCII, NUL-padded
    28   4     CRC32 (IEEE 802.3) of the payload only
    32   4     input kinds, one byte each: 0 audio, 1 CV 1 in volts, 2 the Res dial
    36   16    input scales, float32 each: what each raw input is multiplied by
    52   12    reserved (0)
    64   ...   payload, float32, PyTorch's nn.GRU layout: weight_ih (3H x I),
               weight_hh (3H x H), bias_ih (3H), bias_hh (3H), then the dense
               layer's weight (H) and bias (1)
"""

import struct
import zlib

MAGIC = b"GRNB"
VERSION = 1
HEADER_SIZE = 64
NAME_LEN = 12
MAX_HIDDEN = 32
MAX_INPUTS = 4
KINDS = {"audio": 0, "cv": 1, "res": 2}


def weight_count(hidden, inputs):
    return 3 * hidden * inputs + 3 * hidden * hidden + 6 * hidden + hidden + 1


def pack(name, gain, hidden, inputs, scales, weights):
    """inputs: names in network order (audio, cv, res); scales: one float each."""
    if hidden % 4 or not 0 < hidden <= MAX_HIDDEN or not 0 < len(inputs) <= MAX_INPUTS:
        raise ValueError(f"{name}: hidden {hidden}, {len(inputs)} inputs: out of range")
    if len(weights) != weight_count(hidden, len(inputs)):
        raise ValueError(f"{name}: {len(weights)} weights, expected {weight_count(hidden, len(inputs))}")
    payload = struct.pack(f"<{len(weights)}f", *weights)
    kinds = bytes(KINDS[k] for k in inputs).ljust(MAX_INPUTS, b"\0")
    sc = struct.pack(f"<{MAX_INPUTS}f", *(list(scales) + [0.0] * (MAX_INPUTS - len(scales))))
    header = (MAGIC + struct.pack("<HHHH", VERSION, 0, hidden, len(inputs)) + struct.pack("<f", gain)
              + name.encode("ascii", "replace")[: NAME_LEN - 1].ljust(NAME_LEN, b"\0")
              + struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF) + kinds + sc).ljust(HEADER_SIZE, b"\0")
    assert len(header) == HEADER_SIZE
    return header + payload


def unpack(data):
    """(name, gain, hidden, kinds, scales, weights), checking magic, size and CRC."""
    if data[:4] != MAGIC:
        raise ValueError("not a .grnb file")
    version, _, hidden, inputs = struct.unpack_from("<HHHH", data, 4)
    if version != VERSION:
        raise ValueError(f"version {version}")
    (gain,) = struct.unpack_from("<f", data, 12)
    name = data[16:16 + NAME_LEN].split(b"\0")[0].decode("ascii")
    (crc,) = struct.unpack_from("<I", data, 28)
    payload = data[HEADER_SIZE:]
    if len(payload) != 4 * weight_count(hidden, inputs) or zlib.crc32(payload) & 0xFFFFFFFF != crc:
        raise ValueError("payload size or CRC mismatch")
    kinds = list(data[32:32 + inputs])
    scales = list(struct.unpack_from(f"<{inputs}f", data, 36))
    return name, gain, hidden, kinds, scales, list(struct.unpack(f"<{len(payload) // 4}f", payload))
