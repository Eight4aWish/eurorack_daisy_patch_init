# multifx-core (staging)

Portable, hardware-agnostic DSP + UI core for the unified Daisy MultiFX.

**This folder is staged here for migration.** Its permanent home is the
[`eurorack_daisy_patch_init`](https://github.com/Eight4aWish/eurorack_daisy_patch_init)
repo, copied to `common/multifx_core/`. It was extracted from
`eurorack_modules/src/daisy-mfx/main.cpp` (the Daisy **Seed** MultiFX), because
that firmware holds the voiced effects and output stage worth sharing.

> Not built in this repo (no Daisy toolchain here). The headers are a faithful,
> constant-for-constant extraction of the Seed v1 DSP, intended to be compiled in
> the target repo against its existing libDaisy/DaisySP submodules.

## Why this exists

The two MultiFX modules share the **same DSP library (DaisySP)** but diverge in
everything around it. The Seed module sounds better not because of its hardware
(it's the noisier, home-built board) but because of DSP craft: feedback-path
low-passing, de-metallised reverb tails, time smoothing, an output voicing stage,
and click-free patch changes. This core packages those so the Patch SM module —
and any future module — inherits them.

## Dependencies

DaisySP only (`ReverbSc`, `DelayLine`, `PitchShifter`, `fonepole`). **No** Arduino,
DaisyDuino, Adafruit, or Wire. That is what makes it portable across both shells.

## Files

| File | What it provides |
|---|---|
| `dsp_helpers.h` | `clampf`, `sin01`, `map_exp01/lin01`, `onepole_lp`, `SmoothedParam` |
| `voicing.h` | `OutputStage` = DC-block → ~14.5 kHz LPF → soft clamp (the "warm" voicing) |
| `effect_switch.h` | `EffectSwitch` — per-sample wet fade for click-free patch changes |
| `reverb_bank.h` | `ReverbBank` — Classic / Plate / Tank / Shimmer (from Seed Bank A) |
| `delay_bank.h` | `DelayBank` — Ping / Tape / MultiTap / EchoVerb (from Seed Bank B) |
| `tone_bank.h` | `ToneBank` — Ladder / SVF morph / Comb / WF+Chorus (from Patch SM v1) |
| `misc_bank.h` | `MiscBank` — Resonator / Pitch / Drive / Crush |
| `ui_model.h` | `NavModel` — platform-agnostic two-level Bank/Patch + menu state machine (per-bank patch counts) |

## How a shell uses it (sketch)

```cpp
#include "multifx_core/reverb_bank.h"
#include "multifx_core/delay_bank.h"
#include "multifx_core/voicing.h"
#include "multifx_core/effect_switch.h"
#include "multifx_core/ui_model.h"

// Big buffers must live in SDRAM:
DSY_SDRAM_BSS static mfx::ReverbBank g_reverb;
DSY_SDRAM_BSS static mfx::DelayBank  g_delay;

static mfx::OutputStage  g_out;
static mfx::EffectSwitch g_switch;
static mfx::NavModel     g_nav;   // num_banks + per-bank patches[] set at init

// init: g_reverb.Init(sr); g_delay.Init(sr); g_out.Init(sr);
// on patch change: g_switch.Trigger(); and g_reverb/g_delay.Reset(mode);

void AudioCallback(...) {
  for (size_t i = 0; i < size; i++) {
    float dryL = IN_L[i], dryR = IN_R[i], wetL, wetR;
    if (g_nav.bank == 0)
      g_reverb.Process((mfx::ReverbMode)g_nav.patch, dryL, dryR, p1, p2, p3, wetL, wetR);
    else
      g_delay.Process((mfx::DelayMode)g_nav.patch, dryL, dryR, p1, p2, p3,
                      tap_active, tap_samps, wetL, wetR);

    float g = g_switch.NextGain();
    wetL *= g; wetR *= g;
    float outL = (1.f - mix) * dryL + mix * wetL;
    float outR = (1.f - mix) * dryR + mix * wetR;
    g_out.Process(outL, outR);
    OUT_L[i] = outL; OUT_R[i] = outR;
  }
}
```

## Migration plan (into `eurorack_daisy_patch_init`)

**Phase 1 — land the core, no behaviour change.**
1. Copy this folder to `common/multifx_core/`.
2. Add `-I$(_P_UP1)/common` (already present in `daisy_multifx_oled/Makefile`) so
   `multifx_core/*.h` resolves.
3. Build a tiny test target that just instantiates the banks — confirms it
   compiles against the repo's DaisySP and that the SDRAM placement links.

**Phase 2 — give the Patch SM module the voicing + anti-click (highest value).**
4. Route the existing 8 patches' output through `mfx::OutputStage` (kills the
   harshness and protects the hot Ladder/Comb/Wavefolder patches from clipping).
5. Call `mfx::EffectSwitch::Trigger()` on patch change and apply `NextGain()`.
6. Reserve CV4 = global Mix across all patches for a consistent dry/wet metaphor.

**Phase 3 — unify the catalog into banks.**
7. Adopt `mfx::NavModel` (3 banks): **A** Reverb (this core), **B** Delay (this
   core), **C** Tone = Ladder / SVF-morph / Comb / Wavefolder+Chorus (port from
   the Patch SM v1's `main.cpp`, which already has them in DaisySP).
8. Wrap the bank-C effects into a `ToneBank` mirroring `ReverbBank`/`DelayBank`.

**Phase 4 — shared niceties.**
9. `SmoothedParam` for the bank-C params (removes their zipper noise).
10. CV-takeover + tap-tempo-from-gate ports (from Seed `loop()`); the gate jack
    `gate_in_1`/B10 already exists on Patch SM.
11. Preset save/recall: a `PatchState` struct (bank, patch, params) persisted to
    QSPI. Persistence is platform-specific, so it lives in each shell, not here.

## Notes / intentional differences from Seed v1

- `ReverbBank` and `DelayBank` each own a `ReverbSc` (v1 shared one global verb
  across the reverb patches *and* EchoVerb). SDRAM is ample on both boards, so
  self-contained banks are cleaner; the audible result is identical.
- Tap tempo is passed in (`tap_active`, `tap_samps`) instead of read inside the
  effect, so the gate/CV source is a shell decision.
- The global Mix and CV-takeover stay in the shell (they depend on the control
  hardware), matching how v1 kept `P1`/takeover outside the DSP.
