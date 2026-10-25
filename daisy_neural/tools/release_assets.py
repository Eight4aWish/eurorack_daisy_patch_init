#!/usr/bin/env python3
"""
release_assets.py — build and check a Mirth release's assets, ready to attach.

    .venv/bin/python tools/release_assets.py v2.0.0 <out_dir> [--filters DIR]

Writes to out_dir:

    mirth-vX.bin, mirth_lite-vX.bin     both builds, rebuilt here from a clean tree
    mirth-vX-NOTICES.txt                NOTICES-mirth.txt with the version filled in
    mirth_lite-vX-NOTICES.txt           the same for Lite
    mirth-vX-card.zip                   everything for the card, in mirth-card/:
        n01_…n12_*.a2nb                 the starter set (captures/starter/)
        notamps/                        the weight steers' bases (captures/random/rNN_x2p0)
                                        and KAY MUTATE (written by notamp_design.py)
        f*_*.grnb, f*_*.a2nb            the filters, only with --filters DIR
        STARTER_CAPTURES.md, CARD.txt   credits, and how to put it all on a card
        FILTERS.md                      the filters' credits, only with --filters

and refuses unless: the tree is clean, so the splash's build stamp names the commit; every
file's CRC holds; the twelve starters sit within 0.1 dB of level_captures.py's −27.0 dB
A-weighted target (the v1.0.0 zip had the older RMS levels); the starters' weights are the
v1.0.0 ones; and every not-amp's capture is in the zip by name.

v1.0.0 (2026-10-03) was packed by hand as mirth-v1.0.0-captures.zip: the twelve, the
credits and CARD.txt. From v2.0.0 the zip is the card's, since not-amps need notamps/.
"""

import argparse
import hashlib
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(HERE))
import notamp_design  # noqa: E402

STARTER = PROJECT / "captures" / "starter"
RANDOM = PROJECT / "captures" / "random"
GCC = pathlib.Path.home() / ".platformio/packages/toolchain-gccarmnoneeabi/bin"

CARD_TXT = """Mirth {v} — putting it on the card

1. The card must hold ONE FAT32 partition of 2 GB or less, with the rest of the card
   left unallocated. A full-size FAT32 volume on a big card fails at start-up, and
   exFAT is not read at all. On a Mac (check the disk number first with
   `diskutil list external` — this erases the card):

     diskutil partitionDisk /dev/diskN MBR "MS-DOS FAT32" DAISY 2G "Free Space" REST R

2. Copy everything in this folder to the root of the card, the notamps folder
   included, next to the firmware .bin (which must be the only .bin on the card).

3. On a Mac, Finder writes a hidden ._ twin of every file, and the twins fail their
   checksum. Delete them, in the notamps folder too:

     rm -f /Volumes/DAISY/._* /Volumes/DAISY/notamps/._*

The not-amps are built from these files, by name: the twelve starter captures, and
the five in notamps/ (four random-weight networks and one random direction for the
Kay). AMPS never lists the notamps/ files, since they are not amps. Keep them all on
the card. Your own captures can go alongside: copy an A2 .nam download to the root and
Mirth converts it at the next boot. Up to 32 captures on a card, notamps/ included.
{filters}
Every capture here is credited — creator, source, licence and the changes made — in
STARTER_CAPTURES.md. Eight are CC BY, so keep that file with them if you pass them on.
"""

FILTERS_TXT = """
The f1_ to f7_ files are the FILTERS bank (B7 held: AMPS, NOT-AMPS, FILTERS), credited
in FILTERS.md. A card with none skips the bank.
"""


def a2nb_ok(b):
    return b[:4] == b"A2NB" and struct.unpack("<I", b[28:32])[0] == (zlib.crc32(b[32:]) & 0xFFFFFFFF)


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        raise SystemExit(f"{' '.join(map(str, cmd))} failed:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version", help="e.g. v2.0.0")
    ap.add_argument("out", type=pathlib.Path)
    ap.add_argument("--filters", type=pathlib.Path, help="folder holding the card's f*_ filter files")
    ap.add_argument("--v1", type=pathlib.Path, help="mirth-v1.0.0-captures.zip, to check the weights are unchanged")
    a = ap.parse_args()
    v = a.version
    if not re.fullmatch(r"v\d+\.\d+\.\d+", v):
        raise SystemExit(f"{v}: expected vX.Y.Z")
    out = a.out
    out.mkdir(parents=True, exist_ok=True)

    # 1. the builds, from a clean tree, so the stamp is the commit with no "+"
    if run(["git", "status", "--porcelain", "--", "."], cwd=PROJECT).strip():
        raise SystemExit("daisy_neural has uncommitted changes: commit first, so the build stamp names the release")
    head = run(["git", "rev-parse", "--short", "HEAD"], cwd=PROJECT).strip()
    env = dict(os.environ, GCC_PATH=os.environ.get("GCC_PATH", str(GCC)))
    for lite, name in ((False, "mirth"), (True, "mirth_lite")):
        (PROJECT / "src" / "main.cpp").touch()           # the stamp is compiled into main.o
        run(["make", "-j8"] + (["LITE=1"] if lite else []), cwd=PROJECT, env=env)
        shutil.copyfile(PROJECT / ("build_lite" if lite else "build") / f"{name}.bin", out / f"{name}-{v}.bin")
    if head.encode() + b"\0" not in (out / f"mirth-{v}.bin").read_bytes():
        raise SystemExit(f"mirth-{v}.bin does not carry the stamp {head}")
    for src, name in (("NOTICES-mirth.txt", "mirth"), ("NOTICES-mirth_lite.txt", "mirth_lite")):
        (out / f"{name}-{v}-NOTICES.txt").write_text((PROJECT / src).read_text().replace("vX.Y.Z", v))

    # 2. the card's files
    starters = sorted(STARTER.glob("n[0-9][0-9]_*.a2nb"))
    if len(starters) != 12:
        raise SystemExit(f"{len(starters)} starter captures in {STARTER}, expected 12")
    notamps = [RANDOM / f"r{s:02d}_x2p0.a2nb" for s in (1, 4, 6, 7)] + notamp_design.write_mutations()
    filters = sorted(p for p in (a.filters.iterdir() if a.filters else [])
                     if re.fullmatch(r"f\d+_.*\.(grnb|a2nb)", p.name))
    if a.filters and not filters:
        raise SystemExit(f"no f*_ filter files in {a.filters}")
    for p in starters + notamps + [p for p in filters if p.suffix == ".a2nb"]:
        if not a2nb_ok(p.read_bytes()):
            raise SystemExit(f"{p}: bad header or CRC")
    if a.v1:
        old = zipfile.ZipFile(a.v1)
        for p in starters:
            if old.read(f"mirth-captures/{p.name}")[32:] != p.read_bytes()[32:]:
                raise SystemExit(f"{p.name}: weights differ from v1.0.0")
    level = run([sys.executable, str(HERE / "level_captures.py")] + [str(p) for p in starters], cwd=PROJECT)
    moves = [float(l.split()[-2]) for l in level.splitlines() if re.match(r"n\d\d_", l)]
    if len(moves) != 12 or max(abs(m) for m in moves) > 0.1:
        raise SystemExit(f"the starters are not at the A-weighted target:\n{level}")
    names = {p.read_bytes()[16:28].split(b"\0")[0].decode() for p in starters + notamps}
    need = {c for d in notamp_design.DEFS for c in (d[2], d[3]) if c}
    if need - names:
        raise SystemExit(f"not-amps need captures the zip lacks: {sorted(need - names)}")

    # 3. the zip, in a fixed order
    zpath = out / f"mirth-{v}-card.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for p in starters + filters:
            z.write(p, f"mirth-card/{p.name}")
        for p in notamps:
            z.write(p, f"mirth-card/notamps/{p.name}")
        z.write(PROJECT / "STARTER_CAPTURES.md", "mirth-card/STARTER_CAPTURES.md")
        if filters:
            z.write(PROJECT / "FILTERS.md", "mirth-card/FILTERS.md")
        z.writestr("mirth-card/CARD.txt", CARD_TXT.format(v=v, filters=FILTERS_TXT if filters else ""))

    print(f"built from {head}; starters within {max(abs(m) for m in moves):.1f} dB of the target; "
          f"{len(notamps)} in notamps/, {len(filters)} filters")
    for p in sorted(out.iterdir()):
        print(f"  {p.name:34s} {p.stat().st_size:>9,d}  sha256 {hashlib.sha256(p.read_bytes()).hexdigest()[:16]}")


if __name__ == "__main__":
    main()
