#!/usr/bin/env python3
"""Write a derived bank out as the C++ table Sorrow compiles in.

    python3 emit_sorrow.py latin_nodes.npy jazzlatin_node_ > ../../src/grids_nodes_jazzlatin.cpp
"""
import sys
import numpy as np

npy, prefix, header = sys.argv[1], sys.argv[2], sys.argv[3]
head = sys.stdin.read() if not sys.stdin.isatty() else ""
A = np.load(npy).astype(np.uint8)
assert A.shape == (25, 96), A.shape

print("// Auto-generated file. Do not edit by hand.")
print("// Written by tools/groove_nodes/emit_sorrow.py")
if head:
    print(head.rstrip())
print()
# The tables live in daisy_grids::grids_port and are declared in the matching
# header. Emitting them at global scope links cleanly on its own and then fails
# at grids_port.o with 25 undefined references per bank.
print(f'#include "{header}"')
print()
print("namespace daisy_grids::grids_port {")
print()
for n in range(25):
    print(f"const uint8_t {prefix}{n}[96] = {{")
    for lane in range(3):
        row = A[n, lane * 32:(lane + 1) * 32]
        for half in range(2):
            vals = ", ".join(f"{v:3d}" for v in row[half * 16:(half + 1) * 16])
            print(f"    {vals},")
    print("};")
    print()

print("} // namespace daisy_grids::grids_port")
