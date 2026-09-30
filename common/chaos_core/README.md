# chaos_core

Platform-independent chaotic-attractor DSP: the `ChaosBase` interface, six
continuous-ODE attractors integrated with RK4, and the panel-order registry.

The only dependency is `<math.h>`. Nothing here includes Arduino, a vendor HAL,
or an audio library, so the same sources build for Teensy 4.1 (Cortex-M7), Daisy
/ STM32H7, or a host compiler for offline testing. Integration into an audio
callback, CV/gate I/O, calibration and UI all belong to the platform layer that
owns these objects — see [`src/teensy_chaos/main.cpp`](https://github.com/Eight4aWish/eurorack_modules/blob/main/src/teensy_chaos/main.cpp) in `eurorack_modules`
for the reference consumer.

## Files

- `include/chaos_core/ChaosBase.h` — abstract base: four pure-virtual methods plus
  the metadata each algorithm publishes about itself
- `include/chaos_core/Attractors.h` — the six attractors, RK4 per `stepSample()`
- `include/chaos_core/Registry.h` / `src/Registry.cpp` — `algos[]` and `N_ALGOS`,
  the shipping set in panel order
- `tools/characterise.cpp`: bounds, cost and level gains (below)
- `tools/periodmap.cpp`: where on the CHAOS × CHAR plane the attractor is periodic
- `tools/pitchmap.cpp`: natural frequency and jitter per point, the input to TAME's
  scale compensation ([`docs/SECRET.md`](../../docs/SECRET.md))

This library moved here from `eurorack_modules/libs/chaos_core` on 2026-09-30,
with the Alchemy Lab port (**Secret**, [`daisy_chaos/`](../../daisy_chaos/)). The
copy left there is frozen with the Teensy firmware that uses it, so this one is
live.

## ChaosBase

Subclasses fill in the metadata fields in their constructor and implement
`init()`, `setParams(chaos, rate, charV)`, `stepSample()`, `getX()`, `getY()`.

| Field | Meaning |
| --- | --- |
| `name`, `chaosLabel`, `charLabel` | display strings |
| `chaosMin/Max`, `charMin/Max` | parameter ranges the panel maps onto |
| `simRateMin/Max` | pitch range, in simulated time units per **second** |
| `dtBase` | largest numerically-safe integration step |
| `divergeBound` | magnitude past which the state is treated as diverged |
| `maxStepsPerSecond` | CPU ceiling, in integration steps per **second** (see below) |
| `modScale` | chaos-parameter units per volt of MOD CV |
| `modMin`, `modMax` | absolute limits MOD CV may drive the chaos parameter to |
| `gainL`, `gainR` | pre-saturation amplitude scale for the audio outs |
| `xMin`, `xRange`, `yMin`, `yRange` | plot window |
| `cvScaleX`, `cvScaleY` | state → ±5 V CV scaling |

The platform layer reads these rather than hard-coding any of it, so adding an
algorithm needs no changes outside this library.

## Pitch, `dtBase` and `maxStepsPerSecond`

RK4 diverges if `dt` grows past what a given system tolerates, so pitch above
what `dtBase` reaches is produced by running **more integration steps per audio
sample**, not by enlarging `dt`. That buys pitch with CPU, and a step is not the
same price in every system, so the ceiling is per-algorithm:

| Algorithm | X / Y outputs | cyc/step (M7) | steps/sample at 44.1 kHz |
| --- | --- | ---: | ---: |
| Rössler | x, y | ~88 | 64 |
| Van der Pol | x, y | ~83 | 64 |
| Lorenz | x, z−ρ (centred) | ~89 | 64 |
| Chua | x, z | ~178 | 32 |
| Duffing | x, y | ~543 | 8 |
| Coupled Rössler | x₁, x₂ (the other oscillator) | ~178 | 32 |

Costs are static counts from emitted Cortex-M7 code. See
[`TEENSY_CHAOS.md`](https://github.com/Eight4aWish/eurorack_modules/blob/main/docs/TEENSY_CHAOS.md) for the budget arithmetic
and for the on-screen CPU figure to raise a cap against.

## Usage

```cpp
#include "chaos_core/Registry.h"
using namespace chaos_core;

ChaosBase* algo = algos[0];          // Rössler
algo->init();
algo->setParams(/*chaos*/ 5.7f, /*rate (dt)*/ 0.05f, /*char*/ 0.2f);

// per audio sample, `steps` from the V/Oct oversampling calculation
for (int k = 0; k < steps; k++) algo->stepSample();
float outL = algo->getX(), outR = algo->getY();
```

`setParams` may be called at any rate; it is just field assignment plus, in
some algorithms, a stability clamp on `dt`. Swapping the active `ChaosBase*`
is safe on a core with word-sized atomic pointer stores, but call `init()`
before making a new one live.

## Divergence guard

RK4 runs away at the edges of some parameter ranges — Rössler above `a`≈0.38,
Chua at its `chaosMax`. Once the state is non-finite every later step inherits
it, so the voice goes silent with X/Y stuck on a rail until the algorithm is
changed. Every `stepSample()` therefore ends by testing its state and re-seeding
via `init()` if it has escaped, which turns a dead module into a brief glitch.

Two tests are available to subclasses:

- `diverged(v)` — non-finite **or** `|v| > divergeBound`. Apply to the variable
  the bound was chosen for.
- `nonFinite(v)` — the unrecoverable case only. For state that legitimately
  ranges wider than that bound: Chua's `z` exceeds the value that catches a
  runaway in `x`, as does Van der Pol's `y`, so bounding them would reset
  healthy trajectories.

Bounds sit well above each attractor's measured extent, because divergence is
exponential — a runaway crosses any threshold within a handful of samples, while
a healthy trajectory never approaches one. Verified across a 9x9x5 sweep of every
algorithm's parameter space (8.1M steps each): no algorithm reaches a non-finite
state, no guard fires at nominal settings, and stable-region trajectories are
bit-identical to the unguarded code.

## Parameter ranges are a safety boundary, not just taste

Several of these systems lose their bounded attractor just outside — and in one
case inside — the range the panel exposes. The guard makes that survivable, but
it is a backstop: the ranges themselves have to exclude it, and they are set
from measurement.

- `charMin`/`charMax`, `chaosMin`/`chaosMax` bound what the pots reach.
- `modMin`/`modMax` bound what MOD CV can add on top. They are always at least
  `[chaosMin, chaosMax]`, so the pot's own range stays reachable, but they are
  **not** a fixed margin — Rössler escapes below c≈1.25 and Coupled Rössler
  below c≈0.5, while Lorenz and Duffing are stable far past ±2.

Note this is genuinely a range problem, not a step-size one. Rössler above
a≈0.385 escapes even at a `dt` 500× smaller than `dtBase`, so the Van der Pol
trick of clamping `dt` against the parameter (`setParams`) cannot help; only
excluding the parameter value can.

### Chua — the one case a range cannot fix

Chua loses its bounded attractor **inside its own pot range**, so unlike the
others it cannot be fixed by moving a limit: above a≈9.25 the attractor stays
bounded only while b clears a floor that rises with a. Excluding that corner by
range means either `chaosMax` ≈ 9.25 (losing 58% of the CHAOS travel, including
part of the double-scroll band) or `charMin` ≈ 14.75 (losing 69% of CHAR, and
putting canonical b=14.286 out of reach).

The floor is measured at `dtBase` across the whole MOD-reachable range and is
linear in a to within the sweep resolution:

| a | ≤9.25 | 9.50 | 10.00 | 10.50 | 11.00 |
| --- | --- | --- | --- | --- | --- |
| min stable b | 12.00 | 12.35 | 13.15 | 13.95 | 14.75 |

giving `b ≥ 12.0 + 1.6·(a − 9.25)`.

**This is deliberately not enforced.** A `charInUse()` clamp holding b above that
floor was written, verified (73,629 combinations, zero guard trips) — and
reverted after playing it. Past the floor the guard re-seeds repeatedly, which is
a stuttering burst, not a dead voice; that texture is what the algorithm is *for*.
The clamp also silenced the bottom of the CHAR pot across the top third of CHAOS,
so a knob stopped responding over the exact region the character lives in. Zero
guard trips is a numerical goal, not a musical one, and this is an instrument.

The lesson generalises: `divergeBound` exists to stop an unrecoverable state
killing the voice. It is not licence to engineer away every region that trips it.

For the record, it is genuinely the ODE and not the integration: a=11, b=14
escapes at a `dt` 1024× smaller too, so `ChaosVanDerPol`'s trick of clamping `dt`
against the parameter could not have helped here in any case.

## Why the rate fields are per second

`dt` means simulated time advanced per audio sample, so the pitch heard is
`dt x sampleRate`. That makes any per-sample rate figure sample-rate dependent,
and `dtBase` was carrying two meanings at once: the largest numerically-safe
integration *step* (a property of the equations, independent of sample rate) and
the top of the pot's pitch range (not independent at all).

They are now separate. `dtBase` is the stability limit only; `simRateMin/Max`
hold the pitch range in simulated time units per second, and `maxStepsPerSecond`
holds the CPU ceiling per second. `ChaosBase::scheduleFor(simRate, sampleRate)`
turns a pitch request into a step size and a fractional step count.

The reason is portability: a range measured at 44.1 kHz would have come out about
1.1 octaves sharp at 96 kHz, and a per-sample step cap would have been more than
twice as permissive as the CPU budget it was measured against. The characterisation
harness asserts both properties — identity with the old per-sample formula at
44.1 kHz, and pitch invariance across 44.1 / 48 / 96 kHz.

## Adding an algorithm

Subclass `ChaosBase` in `Attractors.h`, set the metadata in the constructor
(including `maxStepsPerSecond`, scaled by the new system's per-step cost), then add
an instance to `Registry.cpp` and bump `N_ALGOS`. No platform file changes.

The metadata is the expensive part, because most of it can only be arrived at by
measurement. `tools/characterise.cpp` does that sweep on a host compiler:

```
g++ -O2 -I common/chaos_core/include \
    common/chaos_core/tools/characterise.cpp common/chaos_core/src/Registry.cpp \
    -o /tmp/characterise && /tmp/characterise
```

It runs the same 5x5x3 sweep and `atanh(0.90)/median` gain fit described above and
prints the measured values against what the constructor declares — per-step cost,
guard trips (with `--verbose` for a divergence map), gains, plot-window coverage
and `divergeBound` headroom. It reproduces all six shipped gains to within a
fraction of a percent.

Two things it cannot tell you. `ns/step` is host x86: out-of-order execution
flatters algorithms with instruction-level parallelism, and glibc's transcendentals
are not newlib's, so use it to *rank* arithmetic cost and set `maxStepsPerSecond`
against the on-device CPU readout at full oversampling. And a guard trip is not
automatically a fault — see Chua above.

## Notes

- Single-precision throughout; assumes a hardware FPU.
- Not thread-safe. `stepSample()` is expected to run in one audio context while
  `setParams()` is called from a control context — the field writes are
  word-sized, and a torn parameter update is at worst one sample of a stale
  coefficient.
- Compatible with Teensy 4.1, STM32H7 / Daisy, and host builds.

See [`docs/SECRET.md`](../../docs/SECRET.md) for the Alchemy Lab module (**Secret**) and [`TEENSY_CHAOS.md`](https://github.com/Eight4aWish/eurorack_modules/blob/main/docs/TEENSY_CHAOS.md) for the original Teensy build,
its I/O map and the algorithm-suite roadmap.
