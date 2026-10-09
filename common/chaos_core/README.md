# chaos_core

Platform-independent chaotic-attractor DSP: the `ChaosBase` interface, twelve
continuous-ODE attractors integrated with RK4 (two banks of six), and the panel-order
registry.

The only dependency is `<math.h>`. Nothing here includes Arduino, a vendor HAL,
or an audio library, so the same sources build for Teensy 4.1 (Cortex-M7), Daisy
/ STM32H7, or a host compiler for offline testing. Integration into an audio
callback, CV/gate I/O, calibration and UI all belong to the platform layer that
owns these objects — see [`src/teensy_chaos/main.cpp`](https://github.com/Eight4aWish/eurorack_modules/blob/main/src/teensy_chaos/main.cpp) in `eurorack_modules`
for the reference consumer.

## Files

- `include/chaos_core/ChaosBase.h`: abstract base, the metadata each algorithm
  publishes about itself, and the pitch/TAME hooks (`naturalFreq`, `stableDt`,
  the drive terms, state save/load)
- `include/chaos_core/Voice.h`: one playable voice: oversampling schedule, TAME,
  envelope, DC blocking, limiter
- `include/chaos_core/PitchTables.h`: **generated** by `tools/pitchmap.cpp
  --emit`; the natural-frequency grids `Voice::setPitch()` tunes with
- `include/chaos_core/Attractors.h` — bank 1, the six original attractors, RK4
  written out per `stepSample()`
- `include/chaos_core/Bank2.h` — bank 2, six more (2026-10-01); tables in
  `PitchTablesBank2.h`, **generated** by `pitchmap --emit-bank2`
- `include/chaos_core/OdeModel.h` — the RK4 stepper bank 2 and the candidates
  share: a model supplies its derivative, outputs and escape test
- `include/chaos_core/Registry.h` / `src/Registry.cpp` — `algos[]` and `N_ALGOS`
  (twelve), the shipping set in panel order, in banks of `kBankSize` (six)
- `include/chaos_core/Candidates.h` / `src/Candidates.cpp` — measured models in
  reserve for a later bank; host tools only (below)
- `tools/characterise.cpp`: bounds, cost and level gains (below)
- `tools/periodmap.cpp`: where on the CHAOS × CHAR plane the attractor is periodic
- `tools/pitchmap.cpp`: natural frequency and jitter per point; `--emit` writes
  `PitchTables.h`
- `tools/tametest.cpp`: plays notes through `setPitch()` and measures them,
  in cents and clarity, across pitch and TAME; fails if a model with no push
  (`tameDriveMax` 0) changes at all with TAME
- `tools/registermap.cpp`: the octave a driven model actually sounds in, over its
  whole CHAOS × CHAR plane
- `tools/tamerender.cpp`: audition WAVs of TAME sweeps and arpeggios

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

| Algorithm | X / Y outputs | cyc/step, Teensy estimate | cyc/step, Alchemy Lab, TAME 1 | cap (steps/s) |
| --- | --- | ---: | ---: | ---: |
| Rössler | x, y | ~88 | ~466 | 550,000 |
| Van der Pol | x, y | ~83 | ~392 | 650,000 |
| Lorenz | x, z−ρ (centred) | ~89 | ~298 | 860,000 |
| Chua | x, z | ~178 | ~335 | 770,000 |
| Duffing | x, y | ~543 | ~844 | 300,000 |
| Coupled Rössler | x₁, x₂ (the other oscillator) | ~178 | ~631 | 410,000 |

The Teensy column was static counts from emitted Cortex-M7 code. The Alchemy Lab
column is measured on the board (400 MHz, 2026-10-01) by Secret's `make BENCH=1`,
which runs each model at its cap before audio starts. It is 2–3× higher, and TAME
adds up to half again. The caps are now set from it, so the worst block takes about
65% of its time; bank 2's are in `Bank2.h`, and the table with top pitches is in
`docs/SECRET.md`. A cap is per chip: re-measure on new hardware rather than
carrying one over. `tametest` lifts the caps, because it tests TAME, not the chip.

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

A platform normally drives a `Voice` rather than an attractor directly:

```cpp
#include "chaos_core/Registry.h"
#include "chaos_core/Voice.h"
using namespace chaos_core;

Voice voice;
voice.setSampleRate(48000.0f);
voice.setAlgo(algos[0]);                       // Rössler

// control rate (~1 kHz), audio held off across the call
voice.setPitch(/*chaos*/ 5.6f, /*char*/ 0.23f, /*hz*/ 220.0f, /*tame*/ 0.4f);

// audio callback
voice.render(outL, outR, blockSize);           // floats, nominally -1..+1
```

## Divergence guard

RK4 runs away at the edges of some parameter ranges — Rössler above `a`≈0.38,
Chua at its `chaosMax`. Once the state is non-finite every later step inherits
it, so the voice goes silent with X/Y stuck on a rail until the algorithm is
changed. Every `stepSample()` therefore ends by testing its state and re-seeding
via `reseed()` (`init()`, counted in the public `guardTrips`) if it has escaped,
which turns a dead module into a brief glitch. The count lets a host tell a guard
re-seed from the other causes of a jump in the output. On the Alchemy Lab, Secret's
click log showed every Chua click in its documented stutter corner (below) as a
guard trip.

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

## Pitch and TAME

`Voice::setPitch(chaos, char, hz, tame)` plays a note in Hz, where `setParams()`
took a simulated-time rate. The full design and its measurements are in
[`docs/SECRET.md`](../../docs/SECRET.md); in short:

- **Scale.** The rate is `hz / naturalFreq(chaos, char)`: the attractor's own
  rotation, measured by `pitchmap` over the whole CHAOS × CHAR plane, lands on the
  note. That is TAME 0, the free voice. The driven models override `naturalFreq`
  with their drive: Duffing and the pendulum sound at it (ω/2π) across most of
  their range, so their subharmonic windows stay intervals below the note; the
  Brusselator's limit cycle entrains at half its drive, so it is tuned to ω/4π.
- **TAME is a push.** It adds a cosine at the note to dX, rising with TAME² to the
  model's own ceiling, `tameDriveMax`, as a fraction of X's own rate: 7.5% by
  default, 10% for Moore–Spiegel, and 0 for Lorenz and Chua. That is the equation
  plus a weak outside drive. Coherent systems go from free, through phase slips,
  to **phase-locked chaos**: an exact pitch under a waveform that is still chaotic.
  Driven systems keep their own subharmonics. A model with a ceiling of 0 is
  bit-identical at every TAME.

Until 2026-10-09 the top half of TAME also pulled the whole state back to a stored
snapshot once a cycle (SYNC), and Lorenz and Chua used that pull across the whole
knob. It was hard sync: the pitch came from the reset clock, the output was a slice
of attractor looped, and Hindmarsh–Rose's bursts and Lorenz's lobe switching never
happened. It was removed so that every sound the module makes is the equations'
own; the models that will not be tamed are left untamed.

What the measurements changed along the way, since each is a trap for the next
edit:

- **Forcing, not diffusive coupling.** `K(ref − x)` was tried first. Its `−Kx`
  half is extra damping, which changed the system's own frequency, pulling Rössler
  up to 160 cents flat before it locked. Pure forcing leaves the dynamics alone.
- **Strong drive does not lock; it breaks.** Past ~10% of X's rate, forced Rössler
  goes to period 2 and chaos rather than a cleaner lock. That is why the ceiling is
  per model and mostly 7.5%.
- **A push makes Lorenz and Chua worse, not tamer.** They never lock, and their
  clarity falls from ~0.5 to ~0.2 under a push. Hence their ceiling of 0.
- **Measure a driven model's register over the whole plane.** `tametest` samples
  three CHAOS settings at mid CHAR, and for Duffing those happen to sit in
  subharmonic windows, which read as "an octave below". `tools/registermap.cpp`
  covers a 9 × 7 grid: Duffing and the pendulum sound at their drive in 39 of 63
  cells, the Brusselator at half its drive in 50.
- **Measure lock by zero crossings, not autocorrelation alone.** McLeod's method
  reads a period-2 waveform (alternating big and small loops) as the octave below.
  A phase-locked chaotic voice has an exact crossing rate. `tametest` shows both.

`tools/tametest.cpp` holds the current results, push only (2026-10-09): Rössler
locks within ~5 cents at full TAME, Coupled Rössler within 2, Moore–Spiegel within
5, Van der Pol exactly; Lorenz–Lü–Chen and Hindmarsh–Rose are within a few cents
from the scaling alone; the Brusselator is within a cent with clarity 0.97–1.00.
With TAME at 0, `Voice` is bit-identical to the Teensy build's, trajectories and
`setParams()` path both, checked against the frozen copy in `eurorack_modules`.

Cost, host x86: the push adds ~60% per sample on Rössler (three reference cosines
per step, against ~90 cycles of RK4), and nothing at TAME 0 or on a model whose
ceiling is 0. Reusing each step's end value as the next one's start would cut it
to two cosines.

## Candidates (host tools only)

`include/chaos_core/Candidates.h` and `src/Candidates.cpp` hold models being
auditioned for future banks, with their own measured pitch tables in
`PitchTablesCandidates.h`. The firmware builds `src/Registry.cpp` and nothing
here, so a candidate never changes what Secret plays until it is promoted.
They share one RK4 stepper (`OdeModel`): a model supplies only its derivative,
outputs and escape test.

Every tool takes them when built with `-DCHAOS_CANDIDATES` and
`src/Candidates.cpp`; indices 0–5 are still the shipping six, and candidates follow
(`tools/models.h`):

```
g++ -O2 -std=c++17 -DCHAOS_CANDIDATES -I common/chaos_core/include -I common/chaos_core/tools \
    common/chaos_core/tools/<tool>.cpp common/chaos_core/src/Registry.cpp \
    common/chaos_core/src/Candidates.cpp -o /tmp/<tool>
```

- `periodmap <index>`: where the candidate is pitched, chaotic, silent.
- `characterise`: levels, guard trips, plot windows, cost.
- `pitchmap --emit-candidates > include/chaos_core/PitchTablesCandidates.h`:
  their tables, written separately so the firmware's `PitchTables.h` is never
  touched.
- `tametest <index>`: TAME 1 must be periodic and in tune.
- `audition <dir>`: one WAV per candidate: CHAOS, CHAR and TAME sweeps and an
  arpeggio, free and tamed.

Bank 2 was chosen from nine candidates on 2026-10-01 and promoted into the
registry (`Bank2.h`, models 6–11). The three in reserve are models 12–14 in the
tools: Rikitake, Shimizu–Morioka and Genesio–Tesi.

## Adding an algorithm

Subclass `ChaosBase` in `Attractors.h`, set the metadata in the constructor
(including `maxStepsPerSecond`, scaled by the new system's per-step cost), then add
an instance to `Registry.cpp` and bump `N_ALGOS`. No platform file changes.

For TAME it also needs:

- the drive, `D0` / `DH` / `D1`, added to dX at the four RK4 stages (start, the
  two midpoints, end), exactly as the existing six do;
- `saveState` / `loadState`;
- a `pitchClass`, a `pitchGrid` from `PitchTables.h`, a `tameDriveMax` if 7.5%
  doesn't suit it, and `stableDt` if its safe step moves with a parameter;
- then regenerate the tables, `pitchmap --emit`, and run `tametest`.

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
- `Voice`'s envelope ramps the VCA over `kModeRampMs` (5 ms) when it is switched
  between drone and gated, in either direction. It used to snap open or shut in a
  single sample, which clicked on every switch.
- For hosts hunting clicks, each model counts its divergence-guard re-seeds
  (`guardTrips`), and `Voice` reports `stepsPerSample()`, the figure that sets the
  CPU cost.
- Not thread-safe. `stepSample()` is expected to run in one audio context while
  `setParams()` is called from a control context — the field writes are
  word-sized, and a torn parameter update is at worst one sample of a stale
  coefficient.
- Compatible with Teensy 4.1, STM32H7 / Daisy, and host builds.

See [`docs/SECRET.md`](../../docs/SECRET.md) for the Alchemy Lab module (**Secret**) and [`TEENSY_CHAOS.md`](https://github.com/Eight4aWish/eurorack_modules/blob/main/docs/TEENSY_CHAOS.md) for the original Teensy build,
its I/O map and the algorithm-suite roadmap.
