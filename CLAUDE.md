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
