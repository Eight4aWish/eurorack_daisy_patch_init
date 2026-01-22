#!/usr/bin/env python3
"""Extract Grids drum-map nodes (node_0..node_24) into a portable C++ source.

This repo vendors Mutable Instruments sources under deps/mutable/eurorack.
Grids (AVR) is GPL-3.0-or-later; this extractor is used to generate a
non-AVR-friendly representation of the drum map tables.

Input:
  deps/mutable/eurorack/grids/resources.cc
Output:
  drumseq_mi/src/grids_nodes.cpp

Run from repo root:
  python3 drumseq_mi/tools/extract_grids_nodes.py
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO_ROOT / "deps/mutable/eurorack/grids/resources.cc"
OUT_CPP = REPO_ROOT / "drumseq_mi/src/grids_nodes.cpp"

NODE_RE = re.compile(
    r"const\s+prog_uint8_t\s+node_(\d+)\[\]\s+PROGMEM\s*=\s*\{(.*?)\n\};",
    re.S,
)


def main() -> None:
    src_text = SRC.read_text(encoding="utf-8")

    nodes: dict[int, list[int]] = {}
    for match in NODE_RE.finditer(src_text):
        idx = int(match.group(1))
        if not (0 <= idx <= 24):
            continue
        nums = [int(n) for n in re.findall(r"\b\d+\b", match.group(2))]
        if len(nums) != 96:
            raise SystemExit(f"node_{idx} length {len(nums)} != 96")
        nodes[idx] = nums

    missing = [i for i in range(25) if i not in nodes]
    if missing:
        raise SystemExit(f"Missing nodes: {missing}")

    lines: list[str] = []
    lines.append("// Auto-generated file. Do not edit by hand.")
    lines.append("//")
    lines.append("// Extracted from deps/mutable/eurorack/grids/resources.cc")
    lines.append("// Original author: Emilie Gillet")
    lines.append("// License: GPL-3.0-or-later")
    lines.append("")
    lines.append('#include "grids_nodes.h"')
    lines.append("")
    lines.append("namespace drumseq_mi::grids_port {")
    lines.append("")

    for i in range(25):
        arr = nodes[i]
        lines.append(f"const uint8_t node_{i}[96] = {{")
        for j in range(0, 96, 16):
            chunk = ", ".join(str(n) for n in arr[j : j + 16])
            lines.append(f"    {chunk},")
        lines.append("};")
        lines.append("")

    lines.append("} // namespace drumseq_mi::grids_port")
    lines.append("")

    OUT_CPP.parent.mkdir(parents=True, exist_ok=True)
    OUT_CPP.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {OUT_CPP.relative_to(REPO_ROOT)} ({OUT_CPP.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
