# Secret — chaos on the Alchemy Lab: port plan, pitch taming, algorithm catalogue

*Seven for a secret, never to be told.* A deterministic system you still can't
predict. **Secret** is the release name; the firmware folder is
[`daisy_chaos/`](../daisy_chaos/) and the DSP is
[`common/chaos_core/`](../common/chaos_core/).

It succeeds the Teensy 4.1 **Chaos** module in `eurorack_modules`, whose firmware
is frozen there and whose hardware will be repurposed. `chaos_core` moved here
from `eurorack_modules/libs/chaos_core` on 2026-09-30, so this copy is the
live one.

> **Status: plan, nothing implemented.** Follows on from
> [TEENSY_CHAOS_V2.md](https://github.com/Eight4aWish/eurorack_modules/blob/main/docs/TEENSY_CHAOS_V2.md) (in `eurorack_modules`), which left the platform undecided. This
> takes the Alchemy Lab (Hermetic Modular, in the rack; see `MODULES.md`) as the
> target, adds a pitch-tracking strategy borrowed from Ogham, and replaces the
> scattered algorithm lists in the older docs with one catalogue.

## 1. Teensy build vs Alchemy Lab

| | Homebrew Teensy (`teensy_chaos`) | Alchemy Lab |
| --- | --- | --- |
| MCU | i.MXRT1062, M7 @ 600 MHz | STM32H750 (Daisy Seed 2 DFM), M7 @ 480 MHz |
| Budget @ 1 voice | ~13,600 cyc/sample @ 44.1k | ~10,000 cyc/sample @ 48k (~5,000 @ 96k) |
| RAM | 1 MB on-chip | 1 MB on-chip + **64 MB SDRAM** (enough for long delay lines) |
| Audio out | SGTL5000, 16-bit / 44.1k, AC-coupled | Codec, 24-bit, **DC-coupled** J9/J10 (audio *or* CV) |
| Audio in | SGTL5000 line in (unused) | J1/J2, AC-coupled |
| Pots | 4 (CHAOS, RATE, CHAR, DEPTH), ENV on a second page | **6, each with a 16-LED ring** |
| Buttons | 1 (short/long press) | 3 + chords |
| CV in | 4 × ADS1115, **~200 Hz per channel**, I²C, blocks `loop()` | Up to 6 × 16-bit, **audio rate**, ±10 V (~2.7 codes/cent) |
| CV out | 2 × MCP4822, 12-bit | J7/J8 STM32 DAC, 12-bit, **<1 µs**; J3–J6 MCP4728, ~70 µs |
| Gate in | via ADS1115, up to 5 ms jitter | any J3–J8 at audio rate (not J1/J2: AC-coupled) |
| Display | 128×64 OLED with live **phase plot** | 102 RGB LEDs, no screen |
| Storage | none used | 16-slot CRC'd preset store, microSD |
| USB | Serial (USB audio vestigial) | USB-C MIDI |
| Framework | own (~600 lines) | `alchemy-sdk`, MIT, **beta**: `CvRouter`, `VirtualButton().Selector()`, `Presets` |
| Status | breadboard/prototype, pins "TBD" | built, in the rack — **V2 hardware** (confirmed) |

**What the port gains:** V/Oct and gates at audio rate instead of 200 Hz (the
biggest single fix; it enables FM and sample-accurate sync), every control live
with no page, 24-bit DC-coupled outs, presets, and SDRAM for delay-based systems.

**What it loses:** the phase-space plot, the one feature the Teensy has that the
Lab can't reproduce. The nearest substitute is colour and brightness on the rings
driven from state (x → hue, |v| → brightness), which the SDK encourages.

**CPU is not the constraint.** `chaos_core` is ~88 cycles per RK4 step for
Rössler-class systems, so 16 steps/sample of oversampling is ~15% at 48 kHz.
Run at **48 kHz**, not 96: the headroom is better spent on oversampling and a
decimation filter (V2 doc, "Audio quality direction") than on a higher output rate.

**The code is already portable.** `chaos_core` depends only on `<math.h>`, and
`Voice` renders to float buffers at any sample rate. The port is a new platform
layer: audio callback, CV/gate reads, LEDs, presets.

### Proposed I/O mapping

This differs from the V2 doc's six-pot mapping in one place: LEVEL gives way to a
new **TAME** control (section 2). A fixed-level codec output doesn't need a trim,
and one can live in Settings if it's missed.

| Control | Assignment | Why here |
| --- | --- | --- |
| P1 | **TUNE**, exponential | |
| P2 | **CHAOS**, the bifurcation parameter | |
| P3 | **CHAR**, the secondary parameter | |
| P4 | **TAME**, from free chaos to locked pitch | new, see section 2 |
| P5 | **AD** envelope macro | |
| P6 | **SR** envelope macro | |
| B1 | model select (bank-of-4 selector across the rings) | |
| B2 | lock mode: Scale / Force / Sync (section 2) | |
| B3 | **FREEZE**: capture the current cycle as a wavetable | |
| J1 | **EXT DRIVE**: audio into a forced system | AC coupling is fine for audio |
| J2 | **SYNC** in (edge-triggered reset) | AC coupling passes edges |
| J3 | **V/OCT** | 16-bit in; calibrate per unit |
| J4 | CHAOS CV | |
| J5 | CHAR or TAME CV, via `CvRouter` | |
| J6 | **GATE** | DC path needed for a held gate |
| J7 / J8 | **X / Y CV out** | fast STM32 DAC: audio-rate CV |
| J9 / J10 | audio L / R | 24-bit DC-coupled |

Every jack is used. J1 and J2 get jobs because AC coupling is harmless for audio
and for edges. The Teensy build had nothing like EXT DRIVE: an external signal as
the forcing term of Duffing, Ueda or forced Van der Pol turns the module into a
chaotic resonator. Check `CvRouter` before hard-coding J5; it may make CHAR CV and
TAME CV both routable for free.

## 2. Taming for pitch: what Ogham does, and what chaos needs

**What Ogham does.** A bytebeat isn't a pitched source either: it's a formula in
`t`, periodic only by accident of its shifts. Ogham's V/Oct (`SetPitchSync`,
vendored in `daisy_bytebeat/bytebeat_engine.cpp`) runs an internal accumulator at
the V/Oct frequency and **hard-syncs the master phase every time it wraps**. The
output is exactly periodic at the requested pitch, and the Rate knob stops being
pitch and becomes timbre: it now sets how much of the formula fits in one cycle.

A chaotic flow can be tamed the same way, but it doesn't always need to be:
some of these systems are already nearly pitched. I measured that on the host
first, because it decides which technique suits which algorithm.

### Measurement: natural frequency and phase coherence

Method: run each shipping algorithm at mid CHAR and sweep CHAOS (`pitchmap` does the full CHAOS × CHAR plane). Measure the mean
upward zero-crossing rate of X per unit of simulated time (`f_nat`), and the
coefficient of variation of the crossing intervals (the jitter: 0 means
perfectly periodic, ~0.3 or more means no pitch).

| Algorithm | f_nat across CHAOS | Jitter | Class |
| --- | --- | --- | --- |
| Rössler | 0.175–0.179, **±1.2% (~20 cents)** | 0.00–0.22 | **coherent** |
| Coupled Rössler | 0.170–0.175 | 0.00–0.18 | **coherent** |
| Van der Pol | 0.159 → 0.062, **1.35 octaves** of drift | ≤0.005 | periodic, drifts with μ |
| Duffing | **0.175 = ω/2π** (the drive), or ÷3 / ÷5 | 0.00–0.27 | **forced**, drive-locked |
| Lorenz | 0.46–1.52, erratic | 0.00–0.45 | incoherent |
| Chua | 0.07–0.44, erratic | 0.00–0.47 | incoherent |

This gives three families, each with its own way to tame it:

1. **Coherent (Rössler-type spirals): scale compensation.** The rotation rate
   hardly moves as CHAOS sweeps from periodic to fully chaotic: ~20 cents at mid
   CHAR, before any correction. CHAR is a different story. The full-plane map
   (`common/chaos_core/tools/pitchmap.cpp`) has Rössler's `f_nat` spanning 0.61
   octaves as `a` rises from 0.10 to 0.36. The top-right corner (high `a`, high
   `c`) reaches jitter 0.3–0.46, where it stops being pitched; 67 of 91 points
   stay under 0.25. So the table has to be 2-D, and that corner will want sync
   rather than scaling. Set
   `simRate = f_target / f_nat(chaos, char)` from a small 2-D table the host tool
   generates, and pitch tracks while the chaos stays audible, as jitter around a
   stable fundamental. Nothing is reset, so it can't click. Van der Pol belongs
   here too: it's perfectly periodic, but without the table it would drift 1.35
   octaves as μ changes.
2. **Forced (Duffing, Ueda, forced VdP, driven pendulum): drive the pitch.** The
   response locks to the drive oscillator, so pitch follows the drive phase
   increment, which V/Oct can set directly. Chaos then appears as **subharmonics**:
   Duffing's period-3 and period-5 windows put the fundamental a twelfth or two
   octaves and a major third below. That's a musical interval, not noise. EXT
   DRIVE on J1 replaces the internal oscillator with an outside signal.
3. **Incoherent (Lorenz, Chua, double scrolls): Ogham-style sync.** Nothing in
   the dynamics holds a pitch, so impose one: re-seed the state every 1/f. Two
   refinements over a raw reset:
   - **Re-seed from a snapshot on the attractor**, captured at a Poincaré
     crossing, not from `init()`. That removes the initial transient, and a
     snapshot taken at an upward zero crossing of x makes the reset nearly
     continuous in x. A 16–32 sample crossfade removes what's left.
   - **Sync every N cycles** (N = 1, 2, 4, 8): the loop repeats every N periods,
     which gives sub-octaves and longer, evolving patterns. This is the same
     bar-structure effect bytebeat gets from powers of two.

   As in Ogham, TUNE then becomes timbre: it sets how much simulated time fits in
   one cycle.

### The lesson from Lorenz and Chua: don't tame too far

Twice, clean has lost to character. Lorenz's old ρ 24–32 was all chaos,
so it read as noise everywhere. Widening it to 24–180 brought in the 148–166
period-doubling cascade. It also brought in fragile islands like ρ 104.5 /
σ 6.69, a plucked-string voice on a period-2/4 island. Before that, the Chua
clamp removed every guard trip and was reverted after playing it, because the
stutter was the point. The sweet spots are at the **edges** of periodic windows,
where the system is nearly periodic but keeps slipping out. That is *intermittency*,
and it's where "gritty but pitched" lives.

So TAME must not be a correction applied everywhere:

- **Default 0.** A fully free voice keeps today's behaviour, including the
  sweet-spot hunt.
- **The middle of the knob is the target, not the end.** Weak coupling gives
  exactly the edge-of-locking behaviour: pitched, with phase slips. The aim is to
  make that region reachable on every model instead of on a 0.4%-wide island.
  Full lock is the far end, for when a clean note is wanted.
- **Sync keeps the grit inside the cycle.** Re-seeding every 1/f imposes the
  period but leaves the trajectory within each cycle as raw as before.
- **Make the islands findable instead of removing them.** Warp the CHAOS taper
  from `periodmap` data so windows and their edges get more pot travel. The ρ≈100
  window is currently 0.4% of the knob. Presets can store bench-found spots like
  ρ 104.5 exactly.

Lorenz is therefore not simply "incoherent". It has real windows, and sync is
for the chaotic stretches between them.

### TAME: one control across all three (built and measured, 2026-09-30)

The plan here was a coupling strength `k·(A·cos φ_ref − x)` on every coherent
system. Building it changed three things. `common/chaos_core/README.md` ("Pitch
and TAME") has the detail, and `tools/tametest.cpp` holds the numbers.

- **Forcing, not diffusive coupling.** The `−k·x` half of diffusive coupling is
  extra damping, which moved Rössler's own frequency up to 160 cents flat before
  it locked. A pure drive, `+F·cos φ_ref` on dX, leaves the system alone.
- **Drive only goes so far.** Up to ~5–7.5% of X's own rate, forced Rössler and
  Coupled Rössler **phase-lock with their chaos intact**: the crossing rate is
  exact while the waveform's clarity stays at 0.6–0.85. That's the pitched,
  gritty middle, and it's a known effect in forced Rössler systems (phase
  synchronisation of chaos). Past ~10% the drive breaks the lock into period-2
  and chaos instead of tightening it.
- **So the knob hands over.** FORCE runs the drive over TAME 0–0.5, then fades
  in SYNC's per-cycle pull over 0.5–1. Lorenz and Chua don't entrain to forcing
  at all, as expected, so they use SYNC across the whole knob, with a
  `1 − (1 − t)²` taper. That puts their pitched-gritty zone (pull ~0.8–0.97,
  clarity 0.75–0.9 on Chua) around TAME 0.6–0.8.

Measured at A1–A5 (55–880 Hz), three CHAOS settings each:

| Model | TAME 0 (scale only) | Middle | TAME 1 |
| --- | --- | --- | --- |
| Rössler | 0–16 cents (crossing rate), clarity ~0.7 | locked within 0–6 cents, clarity 0.6–0.75 | exact |
| Coupled Rössler | 0–3 cents, clarity ~0.85 | locked, clarity ~0.85 | exact |
| Van der Pol | 1–26 cents, periodic | locked from TAME 0.125 | exact |
| Duffing | on its ÷3 / ÷5 subharmonics, by design | locks from ~0.625 | exact |
| Lorenz | chaotic; pitch 80–180 cents off | locks from ~0.75, sometimes the octave below at 0.6 | exact |
| Chua | chaotic | exact pitch at 0.62–0.88, clarity 0.75–0.9 | exact |

One knob goes from "noise" to "note" on every model, and **TAME 0 is the old
voice**, with its sweet spots. Two ear questions stay open, and the renders exist
for them (`tools/tamerender.cpp`):
- whether SYNC's lower half (a jump each cycle into chaos that stays chaotic) is
  useful grit or just noise;
- whether FORCE's period-2 band around TAME 0.6 (an octave-down undertone) is a
  feature.

A later refinement is a **phase-locked loop.** For coherent systems,
`φ = atan2(y, x)` is a good phase estimate. A PLL that trims the step rate to hold
φ to the reference gives exact pitch with no coupling term and no reset, leaving
the amplitude chaos untouched. It's worth trying if the drive colours the tone too
much.

**FREEZE (B3)** covers the case these don't: capture N cycles of the tamed output
into a buffer and play it as a wavetable. That's Ogham's decouple/drone applied to
chaos. The wavetable tracks V/Oct perfectly, and the live attractor can keep
running into the other output.

## 3. Algorithm catalogue

This merges three sources: the 14-algorithm roadmap in [`TEENSY_CHAOS.md`](https://github.com/Eight4aWish/eurorack_modules/blob/main/docs/TEENSY_CHAOS.md), the
"parked" list in the V2 doc (Sprott A–S, the named 3-D attractors, Thomas, forced
VdP), and the Fractal Bits maps. Additions are marked **new**. Every entry is
tagged with the taming family from section 2, since that now decides how it
plays. Cost classes:

- **P**: polynomial, Rössler class (~90 cycles/step)
- **T**: one or more transcendental calls per step (Duffing class, 3–6×)
- **D**: needs a delay line (SDRAM)
- **M**: a map, one iteration per sample or per tick

The coherent and forced classes below come from the measurement in section 2;
the other classes are expected, not yet measured.

### Shipping (6)

| Algorithm | Pitch class | Cost | Notes on the port |
| --- | --- | --- | --- |
| Rössler | coherent | P | best candidate for the first tamed build |
| Coupled Rössler | coherent | P×2 | stereo; coupling `k` already exists |
| Van der Pol | periodic, drifts with μ | P | **make it forced**: CHAR = drive amplitude, which also fixes its dead CHAR |
| Duffing | forced | T | pitch = ω; move ω onto V/Oct, CHAR → damping δ |
| Lorenz | incoherent | P | sync; range already widened to ρ 24–180 |
| Chua | incoherent | P | sync; keep the stutter corner (deliberate) |

### Continuous flows to add

| Algorithm | Equations (brief) | Pitch class | Cost | Character |
| --- | --- | --- | --- | --- |
| **Forced Van der Pol** | `ÿ − μ(1−x²)ẏ + x = A cos ωt` | forced | T | relaxation → chaotic, sub-harmonic jumps |
| **Ueda** (new) | `ẍ + kẋ + x³ = B cos t` | forced | T | Duffing without linear stiffness; wild period-n windows |
| **Driven pendulum** (new) | `θ̈ + γθ̇ + sin θ = A cos ωt` | forced | T | rotation vs libration: a hard timbral switch |
| **Forced Brusselator** (new) | chemical oscillator + `A cos ωt` | forced | T | gentle, bell-like locking |
| Sprott A–S | 19 minimal polynomial flows | mostly coherent (measure) | P | a dozen cheap, structurally distinct voices; Sprott A is the Nosé–Hoover case |
| **Sprott jerk** (new) | `x''' = −A x'' − x' + |x| − 1` | coherent | P | simplest chaotic jerk; buzzy |
| **Arneodo** (new) | `x''' = −a x'' − x' + b x − x³`… | coherent | P | Rössler-like spiral, brighter |
| **Genesio–Tesi** (new) | jerk family, quadratic | coherent | P | clean spiral, period-doubling cascade |
| Chen | Lorenz-family | incoherent | P | harsher two-lobe |
| Lü | between Lorenz and Chen | incoherent | P | one parameter morphs Lorenz ↔ Chen |
| **Shimizu–Morioka** (new) | Lorenz-like, lobe switching | incoherent | P | slower switching; rhythmic |
| Halvorsen | cyclic, quadratic | incoherent | P | three-fold symmetric |
| Thomas | `ẋ = sin y − bx` (cyclic) | incoherent | T | `b` sweeps order → chaos → "random walk" |
| Aizawa | 6 params, torus-like | semi-coherent (measure) | P | smooth, breathy |
| Dadras, Rikitake | multi-scroll / dynamo | incoherent | P | Rikitake gives slow polarity reversals, good for CV |
| **Rabinovich–Fabrikant** (new) | cubic, multiple attractors | incoherent | P | dramatic; tight stability range |
| **Colpitts** (new) | the transistor oscillator ODE | coherent | T (exp) | a *real* circuit's chaos, analogue-sounding |
| **Hindmarsh–Rose** (new) | neuron model, slow/fast | bursting | P | spike bursts: **percussive** and rhythmic |
| **Hyperchaotic Rössler** (new) | 4-D | semi-coherent | P | two positive exponents; denser than Rössler |
| **Moore–Spiegel** (new) | `x''' = −x'' − (T − R + Rx²)x' − Tx` | coherent | P | "stellar" oscillator; smooth to gritty |

### Pitch-exact by construction (new family)

These put the pitch in a structure the chaos can't move: a delay length, or a
phase increment. They track V/Oct exactly with no taming at all, which makes
them the closest match to what Ogham achieves.

| Algorithm | Idea | Cost | Character |
| --- | --- | --- | --- |
| **Mackey–Glass** (new) | delay differential equation, `ẋ = βx_τ/(1+x_τⁿ) − γx`; delay τ ∝ 1/f | D | chaos locked to a delay-line period; Karplus-like but self-exciting |
| **Chaotic Karplus–Strong** (new) | delay loop with a logistic or tanh map in the feedback | D + M | plucked → screaming as the map gain rises |
| **Ikeda DDE** (new) | the optical-cavity delay form of Ikeda | D + T | metallic, ring-mod-like |
| **Circle map oscillator** (new) | `θ ← θ + Ω − (K/2π) sin 2πθ`, out = `sin 2πθ`, Ω from V/Oct | M + T | *the* model of mode locking: K < 1 locked, K > 1 chaotic phase distortion |
| **Chaotic phase distortion** (new) | a sine at f, with its phase modulated by a logistic or Hénon orbit, reset per cycle | M | pitch exact; the chaos lives entirely in the timbre |

### Discrete maps (from the Fractal Bits list)

Logistic, Hénon, Ikeda map and the standard map, clocked at `iteration rate =
n·f` in a period-n window and re-seeded every n iterations. That's the map form
of sync, so they can join the oscillator suite and don't need a percussion mode.
Mandelbrot/Julia and cellular automata stay out: their pitch comes from orbit
length, which V/Oct can't set.

### What's already in Eurorack, and where Secret differs

A quick check (web search 2026-09-30, not exhaustive) found the shipping six are
largely covered already:

- **Lorenz and Rössler** are CV generators in Ornament & Crime (the Low-rents
  app, and Hemisphere's Lorenz applet). The O&C in the rack runs Phazerville, so
  these may already be in the rack.
- **Double scroll (Chua family)** is Joranalogue **Orbit 3**, an analogue
  double-scroll oscillator with calibrated pitch tracking, 2.4 mHz–22 kHz.
  It's the benchmark for a pitched scroll.
- **Jerk circuits** are NLC's **Sloth** family, but as slow CV only, with orbits
  of seconds to minutes.
- VCV Rack has software versions of most of the named attractors. That isn't
  hardware, but it means the equations themselves are well known.

No hardware turned up for the **driven** systems (Duffing, Ueda, forced Van der
Pol, driven pendulum), the **delay** systems (Mackey–Glass, chaotic
Karplus–Strong), the circle map, Hindmarsh–Rose, or the less famous flows
(Arneodo, Moore–Spiegel, Rabinovich–Fabrikant, Shimizu–Morioka). So Secret's
distinctive parts are Banks B and D, EXT DRIVE, and TAME across every model.
Bank A and C's classics are table stakes; they don't set it apart.

### A first 16, in four banks

The V2 doc sets 16 as the ceiling (`DrawSlotIndicator`). One bank per family, so
B1 + a ring picks the family and the TAME behaviour is predictable within a bank:

| Bank | Slots |
| --- | --- |
| **A: Spiral** (scale-tamed) | Rössler, Coupled Rössler, Arneodo, Sprott jerk |
| **B: Driven** (drive-tamed) | Duffing, Forced Van der Pol, Ueda, Driven pendulum |
| **C: Scroll** (sync-tamed) | Lorenz, Chua, Lü, Thomas |
| **D: Locked** (exact) | Mackey–Glass, Chaotic Karplus–Strong, Circle map, Hindmarsh–Rose |

## Visualising on Tiliqua

Tiliqua's **`xbeam`** bitstream is a vectorscope: `in0` = X, `in1` = Y,
`in2` = intensity, `in3` = colour, with per-channel scale and offset. It replaces
the Teensy's OLED phase plot, and at a much larger size.

- **Patch J7/J8 (X/Y CV out) to scope X/Y, not the audio outs.** J9/J10 have been
  through the output chain: a `tanh` soft-limiter flattens the lobes, the 4.9 Hz
  DC blocker skews the shape at slow rates, and the envelope scales it. J7/J8
  carry raw state times `cvScaleX/Y`, at audio rate on the fast DAC, so they show
  the true attractor. 12 bits is plenty for a picture.
- **What each model draws** (current `getX()`/`getY()` pairs): Rössler x–y, the
  spiral. Lorenz x vs z−ρ, the butterfly. Chua x–z, the double scroll. Van der
  Pol x–y, a limit cycle. Duffing x–ẋ. Coupled Rössler is the exception: x₁ vs x₂
  is two oscillators against each other, so it draws a Lissajous figure that
  beats towards a diagonal as they sync, not an attractor.
- **Colour wants a third variable.** A 3-D attractor on an X/Y scope loses z;
  putting z on `in3` gives the missing depth. Every jack is currently allocated,
  but on V2 any of J3–J6 can be an output. So a "scope" setting could turn one
  input (J5, the CHAR/TAME CV) into a Z out. Decide once there's hardware.
- **The scope is also a tuning aid.** A locked pitch draws a stationary closed
  figure, and slips show as the figure precessing. A sync reset draws a jump,
  which is the crossfade's job to hide.

## 4. Order of work

1. **Host first, no hardware. Done.** `pitchmap --emit` writes `PitchTables.h`.
   Every new algorithm gets characterised with `characterise`, `periodmap` and
   `pitchmap` before it gets a slot.
2. **TAME in `Voice`. Done**, and measured by `tametest`: see section 2. Next is
   listening to the `tamerender` WAVs and settling the two ear questions there.
3. **Alchemy platform layer**: audio callback at 48 kHz, J3 V/Oct with
   calibration, gate on J6, the six pots, and a bare selector. Port the six
   shipping algorithms and check on hardware that it sounds the same as the Teensy.
4. **Banks B and D.** Forced and delay systems give the biggest pitch-tracking
   gain per line of code.
5. Constant-rate oversampling and decimation (V2 doc), then FREEZE, EXT DRIVE and
   the LED state display.

## Open questions

- **The build is CMake, not this repo's Makefiles.** `alchemy-sdk` builds with
  CMake + Ninja and pins **its own libDaisy** as a submodule (`vendor/libDaisy`
  at `08f2965`), while `deps/daisy/libDaisy` here is at `f044cdc`. Its
  recommended starting point is
  [`alchemy-template`](https://github.com/hermetic-modular/alchemy-template).
  Plan: add `alchemy-sdk` as `deps/alchemy-sdk` and build `daisy_chaos` with
  CMake against the SDK's libDaisy, leaving the Makefile apps on theirs.
  `chaos_core` needs only `<math.h>`, so it doesn't care which.
- **How the SDK's calibration handles V/Oct.** A ±10 V front end gives 2.7 codes
  per cent, which is enough only if noise stays under a code or two.
