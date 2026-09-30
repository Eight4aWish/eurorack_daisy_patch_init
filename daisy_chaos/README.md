# daisy_chaos — Secret

> **Status: planning. No firmware yet.** The plan is
> [`docs/SECRET.md`](../docs/SECRET.md).

A chaotic oscillator for the **Hermetic Modular Alchemy Lab (V2)**. It runs
continuous strange attractors as audio, with V/Oct, and a **TAME** control that
goes from free chaos to locked pitch without losing the grit in between.

*Seven for a secret, never to be told.* **Secret** is the release name. It
succeeds the Teensy 4.1 **Chaos** module in
[`eurorack_modules`](https://github.com/Eight4aWish/eurorack_modules), whose
firmware is frozen and whose hardware will be repurposed.

## What's here, and where the rest is

| Piece | Where |
| --- | --- |
| Attractor DSP, oversampling, envelope | [`common/chaos_core/`](../common/chaos_core/) (moved from `eurorack_modules`) |
| Host tools: `characterise`, `periodmap`, `pitchmap` | [`common/chaos_core/tools/`](../common/chaos_core/tools/) |
| Plan: I/O, pitch taming, algorithm catalogue | [`docs/SECRET.md`](../docs/SECRET.md) |
| Platform layer for the Alchemy Lab | this folder, **to be written** |

## Panel (planned)

| Control | Job |
| --- | --- |
| P1–P6 | TUNE, CHAOS, CHAR, **TAME**, AD, SR |
| B1 / B2 / B3 | model select / lock mode (Scale, Force, Sync) / FREEZE |
| J1 / J2 | EXT DRIVE (audio into forced systems) / SYNC in |
| J3–J6 | V/OCT, CHAOS CV, CHAR or TAME CV, GATE |
| J7 / J8 | X / Y CV out (fast STM32 DAC) |
| J9 / J10 | audio L / R |

## Build (planned): not like the rest of this repo

Every other app here is a libDaisy **Makefile** build against `deps/daisy/`.
The [Alchemy SDK](https://github.com/hermetic-modular/alchemy-sdk) (MIT, beta)
builds with **CMake + Ninja** and pins **its own libDaisy** (`vendor/libDaisy`,
currently a different commit from `deps/daisy/libDaisy`). So this app will:

- add the SDK as a submodule at `deps/alchemy-sdk`, laid out like
  [`alchemy-template`](https://github.com/hermetic-modular/alchemy-template);
- build with CMake against the SDK's libDaisy, leaving the Makefile apps on theirs;
- use `chaos_core` by relative path. It needs only `<math.h>`, so either libDaisy
  is fine.
