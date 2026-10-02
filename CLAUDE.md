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

## History was rewritten on 2026-10-01

Every commit from 2026-08-04 onward has a new ID, and the release tags were moved to
match. This removed third-party NAM captures (TONE3000 T3K licence, no redistribution)
that had been committed under `daisy_neural/`.

- **A checkout from before that date must be re-cloned, never merged.** Pulling the new
  `main` into old history and pushing would put the captures back on GitHub.
- **Never commit a capture in any form**: `.nam`, `.a2nb`, `daisy_neural/nam/model_data_nam_a2.h`,
  or a raw parameter dump such as `amp_compare/w_*.f32` — those slipped through the first
  purge and needed a second.
  `.gitignore` covers them; do not force-add them. The firmware builds without them.
- **The firmware's built-in fallback must stay CC-licensed.** Since 2026-10-02 it is the
  Orange TH100 starter capture (CC BY, credit in `daisy_neural/nam/fallback_capture.h`,
  from `tools/export_fallback.py`); `src/main.cpp` no longer reads `nam/model_data_nam_a2.h`,
  which until then compiled the T3K JCM800 into every `.bin` built with it on disk. Never
  point the fallback back at a T3K capture.
