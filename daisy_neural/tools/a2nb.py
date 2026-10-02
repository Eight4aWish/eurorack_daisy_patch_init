#!/usr/bin/env python3
"""
a2nb.py — the .a2nb capture file: one A2-Lite network, ready for the microSD card.

Shared by nam_to_a2nb.py (which writes them) and anything that reads them. The firmware's
reader is src/capture_store.cpp, and the two must agree.

Why not .namb (tone-3000/nam-binary-loader)?
    That is a full NAM Core model format — architecture ids, head layout,
    SlimmableContainer support — and its weight ordering is NAM Core's, not the
    flat 1871-float array bkshepherd's A2 runtime takes. Using it would mean a
    translation step, and a wrong translation would present as a capture that
    loads cleanly and sounds wrong, which is the worst kind of bench bug.

    The payload here is byte-for-byte the array the engine already accepts, so
    there is no reordering to get wrong. The container borrows .namb's good
    ideas: magic, version, explicit count, and a CRC32 so a bad card is caught
    rather than silently fed to the network as weights.

Format (little-endian, 32-byte header + payload):

    off  size  field
    0    4     magic 'A2NB'
    4    2     format version (1)
    6    2     reserved (0)
    8    4     weight count (uint32) — must be 1871
    12   4     output gain (float32), bkshepherd's loudness match
    16   12    name, ASCII, NUL-padded
    28   4     CRC32 (IEEE 802.3) of the payload only
    32   ...   weight count x float32
"""

import struct
import zlib

MAGIC = b"A2NB"
VERSION = 1
HEADER_SIZE = 32
NAME_LEN = 12
EXPECTED_WEIGHTS = 1871


def pack(name, gain, weights):
    if len(weights) != EXPECTED_WEIGHTS:
        raise ValueError(f"{name}: {len(weights)} weights, expected {EXPECTED_WEIGHTS}")

    payload = struct.pack(f"<{len(weights)}f", *weights)
    crc = zlib.crc32(payload) & 0xFFFFFFFF

    encoded = name.encode("ascii", "replace")[: NAME_LEN - 1]
    header = (
        MAGIC
        + struct.pack("<HH", VERSION, 0)
        + struct.pack("<I", len(weights))
        + struct.pack("<f", gain)
        + encoded.ljust(NAME_LEN, b"\0")
        + struct.pack("<I", crc)
    )
    assert len(header) == HEADER_SIZE, len(header)
    return header + payload


def verify(blob, name, gain, weights):
    """Read our own file back and check it round-trips. Cheap, and it is the
    only part of this that can be tested without the hardware."""
    magic, ver, _, count, g = struct.unpack("<4sHHIf", blob[:16])
    got_name = blob[16:28].split(b"\0")[0].decode("ascii")
    (crc,) = struct.unpack("<I", blob[28:32])
    payload = blob[HEADER_SIZE:]

    assert magic == MAGIC, magic
    assert ver == VERSION, ver
    assert count == len(weights), (count, len(weights))
    assert abs(g - gain) < 1e-6, (g, gain)
    assert got_name == name[: NAME_LEN - 1], (got_name, name)
    assert crc == (zlib.crc32(payload) & 0xFFFFFFFF)
    back = struct.unpack(f"<{count}f", payload)
    # float32 round-trip, so compare at float32 precision
    for a, b in zip(back, weights):
        assert abs(a - b) < 1e-6 * max(1.0, abs(b)), (a, b)
