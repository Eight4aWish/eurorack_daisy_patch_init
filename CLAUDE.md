# eurorack_daisy_patch_init

Makefile firmware for the Electrosmith Daisy Patch Submodule, built on **libDaisy** and
**DaisySP**, with some ports from Mutable Instruments' **eurorack** sources. Self-contained
via git submodules under `deps/` — there is no shared `../deps` tree.

## Module inventory

Before answering anything about which modules exist, what hardware is in the rack, or
which repo a module lives in, read the canonical inventory:

**`MODULES.md`** in the [`eight4awish`](https://github.com/Eight4aWish/eight4awish) repo
— <https://github.com/Eight4aWish/eight4awish/blob/main/MODULES.md>

If that repo is checked out alongside this one, read it from disk; otherwise fetch the URL.

It covers all ten repos: the released modules, the built-but-undrafted ones, the
purchased rack with HP and function, companion software, and what is deliberately *not*
a module. No single repo sees all of it, so do not infer the full picture from this one.

## Captures and licences

- **A checkout made before 2026-10-01 must be re-cloned, never merged.**
- **Never commit a capture in any form**: `.nam`, `.a2nb`, `daisy_neural/nam/model_data_nam_a2.h`,
  or a raw parameter dump such as `amp_compare/w_*.f32`. `.gitignore` covers them; do not
  force-add them. The firmware builds without them. Release assets carry the CC0/CC BY
  starter set, credited in `daisy_neural/STARTER_CAPTURES.md`.
- **The firmware's built-in fallback must stay CC-licensed.** It is the Orange TH100
  starter capture (CC BY, credit in `daisy_neural/nam/fallback_capture.h`, from
  `tools/export_fallback.py`). Never point it at a capture that is not CC0 or CC BY.
- **Private matters stay private.** Licence questions, known weaknesses and repo history
  go in the private `eight4awish_private` repo (`REPO_NOTES.md`, `ROADMAP.md`), never in
  this public repo's files, commit messages or release notes.
