#!/usr/bin/env python3
"""
export_captures.py — turn the compiled-in captures into files for the microSD card.

Reads nam/model_data_nam_a2.h and writes one .a2nb file per capture into
captures/, ready to copy onto a FAT32 card.

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

Usage:
    python3 tools/export_captures.py            # writes ./captures/
    python3 tools/export_captures.py --out DIR
"""

import argparse
import pathlib
import re
import struct
import sys
import zlib

MAGIC = b"A2NB"
VERSION = 1
HEADER_SIZE = 32
NAME_LEN = 12
EXPECTED_WEIGHTS = 1871

HERE = pathlib.Path(__file__).resolve().parent
HEADER = HERE.parent / "nam" / "model_data_nam_a2.h"


def parse_header(path):
    """Return (arrays, entries).

    arrays  : {symbol: [float, ...]}
    entries : [(display_name, symbol, gain), ...] in kNamA2Models order
    """
    raw = path.read_text()

    # Strip // comments up front. The header carries a commented-out "to add a
    # new model" template that re-declares an existing symbol with an empty
    # body; left in, its empty match overwrites the real array and the capture
    # silently exports as zero weights.
    text = re.sub(r"//[^\n]*", "", raw)

    arrays = {}
    # inline constexpr float kWeightsFoo[...] = { ... };
    for m in re.finditer(
        r"constexpr\s+float\s+(kWeights\w+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    ):
        symbol, body = m.group(1), m.group(2)
        vals = [float(v) for v in re.findall(r"[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?", body)]
        # Belt and braces: never let a later empty match clobber a good one.
        if vals and len(vals) > len(arrays.get(symbol, [])):
            arrays[symbol] = vals

    entries = []
    tbl = re.search(r"kNamA2Models\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if tbl:
        for m in re.finditer(
            r'\{\s*"([^"]+)"\s*,\s*(kWeights\w+)\s*,\s*([-+0-9.eEf]+)\s*\}', tbl.group(1)
        ):
            entries.append((m.group(1), m.group(2), float(m.group(3).rstrip("f"))))

    return arrays, entries


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE.parent / "captures"))
    ap.add_argument("--header", default=str(HEADER))
    args = ap.parse_args()

    header_path = pathlib.Path(args.header)
    if not header_path.exists():
        sys.exit(f"not found: {header_path}")

    arrays, entries = parse_header(header_path)
    if not entries:
        sys.exit("no kNamA2Models entries found — has the header changed shape?")

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    written = 0
    for index, (name, symbol, gain) in enumerate(entries):
        weights = arrays.get(symbol)
        if weights is None:
            print(f"  skip {name}: no array {symbol}")
            continue

        blob = pack(name, gain, weights)
        verify(blob, name, gain, weights)

        # Numbered so the load order on the module is the order in the table,
        # whatever order the filesystem hands them back in.
        safe = re.sub(r"[^A-Za-z0-9_-]", "_", name)
        path = out / f"{index}_{safe}.a2nb"
        path.write_bytes(blob)
        print(f"  {path.name:<24} {len(weights)} weights, gain {gain}, {len(blob)} bytes")
        written += 1

    print(f"\n{written} capture(s) -> {out}")
    print("Copy the .a2nb files to the root of a FAT32 microSD card.")


if __name__ == "__main__":
    main()
