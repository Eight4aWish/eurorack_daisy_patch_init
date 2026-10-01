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
| CV in | 4 × ADS1115, **~200 Hz per channel**, I²C, blocks `loop()` | Up to 6 × 16-bit, **1 kHz** through the SDK's smoothed `AnalogControl`, ±10 V (~2.7 codes/cent) |
| CV out | 2 × MCP4822, 12-bit | J7/J8 STM32 DAC, 12-bit, **<1 µs**; J3–J6 MCP4728, ~70 µs |
| Gate in | via ADS1115, up to 5 ms jitter | any J3–J8, 1 ms (not J1/J2: AC-coupled; those give edges at audio rate) |
| Display | 128×64 OLED with live **phase plot** | 102 RGB LEDs, no screen |
| Storage | none used | 16-slot CRC'd preset store, microSD |
| USB | Serial (USB audio vestigial) | USB-C MIDI |
| Framework | own (~600 lines) | `alchemy-sdk`, MIT, **beta**: `CvRouter`, `VirtualButton().Selector()`, `Presets` |
| Status | breadboard/prototype, pins "TBD" | built, in the rack — **V2 hardware** (confirmed) |

**What the port gains:** V/Oct and gates at 1 kHz with 1 ms timing instead of
200 Hz and up to 5 ms (the biggest single fix), every control live with no page,
24-bit DC-coupled outs, presets, and SDRAM for delay-based systems. *(Corrected
2026-09-30, from the SDK source: CV is read at the 1 ms control tick, not at
audio rate as first written here. Audio-rate FM would need to read the ADC
buffer from the audio callback.)*

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
| J4 | **GATE** | DC path needed for a held gate. Next to V/OCT: the pair a sequencer drives (moved from J6 after the first bench session) |
| J5 | CHAOS CV | |
| J6 | CHAR or TAME CV, via `CvRouter` | |
| J7 / J8 | **X / Y CV out** | fast STM32 DAC: audio-rate CV |
| J9 / J10 | audio L / R | 24-bit DC-coupled |

Every jack is used. J1 and J2 get jobs because AC coupling is harmless for audio
and for edges. The Teensy build had nothing like EXT DRIVE: an external signal as
the forcing term of Duffing, Ueda or forced Van der Pol turns the module into a
chaotic resonator. Check `CvRouter` before hard-coding J6; it may make CHAR CV and
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

### Prior art, searched properly (2026-10-01)

This replaces a quick check from 2026-09-30 that overstated what was new. Five
parallel searches covered the whole catalogue: the web, all 565 VCV Library
plugin manifests (cloned and grepped), GitHub code, the SuperCollider sources,
and DAFx/NIME/ICMC papers. One question was asked of every candidate: is there a
**V/Oct-pitched audio oscillator in Eurorack hardware** running this system?
Audio rate versus slow CV, and calibrated V/Oct versus a free rate knob, were
kept separate. Most chaos in Eurorack is slow CV.

**Coverage limits.** ModWiggler, lines, Reverb and Perfect Circuit returned 403,
and ModularGrid's search only renders in a browser (single module pages loaded).
The session's 200-search cap ran out, so Reddit, Hackaday, the Electro-Smith
forum, Buchla/Serge/5U, pedals and the disting NT community plugins went largely
unsearched. "No instance found" means none in what could be reached.

**The headline.** "A V/Oct chaotic oscillator with a tame control" is not new.
Joranalogue **Orbit 3** (2021) is an analogue double scroll with calibrated V/Oct,
22 Hz–22 kHz, hard sync, and a TAME/WILD switch. What is still open is narrower:

1. **TAME as one continuous control** that ends with the attractor itself locked
   to the V/Oct note. No instance found; the neighbours are listed below.
2. **Named systems that nobody has pitched in hardware.**

| Where it stands | Candidates |
| --- | --- |
| **No instance in any format** | Coupled Rössler · driven damped pendulum · forced Brusselator · chaotic Colpitts · Mackey–Glass with τ ∝ 1/f · Ikeda delay equation · Genesio–Tesi · Moore–Spiegel · Sprott D, E, G–K, N–R |
| **Papers, or control-rate software, only** | Circle map (Essl, DAFx 2006; no product) · Lü, and the Lorenz–Lü–Chen morph (chaosrack, browser) · Shimizu–Morioka and Rikitake (math-sonify, 120 Hz control rate) · hyperchaotic Rössler (chaosrack) · discrete maps re-seeded every period (none; Nozori's de Jong VCO has V/Oct but never re-seeds) |
| **Pitched audio in software, not hardware** | Forced Van der Pol with audio forcing (TriggerFish VDPO, VCV, 2018) · Hindmarsh–Rose (Coalescent Neuron·Soma, VCV, 2026) · Arneodo and Aizawa (among 36 in Attrattore, VST3) · Rössler with an uncalibrated V/Oct (ZetaCarinae Rossler Rustler) · Duffing with audio forcing (SuperCollider DoubleWell3, Gutter Synthesis) |
| **In Eurorack hardware, but CV or unpitched audio** | Lorenz and Rössler (O&C Low-rents, disting NT Chaos) · Chen, Halvorsen, Aizawa, Dadras, Nosé–Hoover, Sprott B/C (Voltage Foundry ChaosForge, Pigatron OctaSource, 4ms MetaModule running Sapphire Zoo and Glee) · Thomas (Nozori 84, audio range, unpitched) · jerk circuits (NLC Sloth family as CV; IFM Sprott, omiindustriies Ah Jerk and Zlob Triple Cap Chaos at audio without V/Oct) · driven Duffing (Ian Fritz Double Well, Elby ChaQuO) · Ueda, which is exactly Gutter Synthesis's equation (forsitan `guttur`, on MetaModule) |
| **Taken: pitched in Eurorack hardware** | Chua / double scroll (Orbit 3) · chaotic Karplus–Strong (Dobson & Fitch, ICMC 1995; Mutable Elements' tube model, Strymon Magneto, NLC Is Carp Lust Wrong?) |
| **Taken as an idea** | EXT DRIVE (ChaQuO and Double Well in analogue; VDPO, `guttur`, stoermelder RAW in software) · chaotic phase distortion (SuperCollider FBSine, HetrickCV) |

**The shipping six, reread.** Coupled Rössler has no instance anywhere.
Rössler, Lorenz, Van der Pol and Duffing are new only as calibrated V/Oct audio
voices in hardware. Chua is the one that has been done.

**TAME's neighbours.** None found is a continuous control that ends in a
guaranteed lock to the note:

- **Orbit 3:** a binary TAME/WILD switch that changes the scrolling, and a reset
  input used as hard sync. Patching its equilibrium output into reset forces a
  stable oscillation.
- **Sapphire Chaops** (VCV): trigger recall of a stored state, which is Sync at
  100%.
- **R_Ware Attractor Oscillator** (Cherry Audio): "tamed" by resetting the
  trace, with a CHAOS knob that reintroduces chaos.
- **Attrattore** (Creature From The Black, VST3): keeps 36 systems in tune
  through an FM carrier locked to the MIDI note, with a CHAOS control from tame
  to full. The musical idea is the same; the mechanism is not.
- **Newfangled/Eventide Pendulate and Generate**: fade from a sine to chaos.
  Generate's is a crossfade; Pendulate's mechanism is unpublished.
- **`guttur` and VDPO**: lock as a side effect of high drive.
- **chaosrack**: periodic windows "lock into tones".
- **US 6,137,045** (University of New Hampshire, 2000, expired): steers a Chua
  circuit onto periodic orbits for pitched waveforms.
- **Weingartner, DAFx 2025**: lists forcing tunable nonlinear oscillators as
  future work.

**Two engineering notes** for the structurally new ideas:

- A **circle map** iterated at the sample rate falls to a fixed point, which is
  silence, at low pitch: at 440 Hz / 48 kHz once K exceeds about 0.06. Essl
  reports the same. Run it at n·f with Ω near 1/n, so V/Oct sets the clock.
- **Delay loops** (Mackey–Glass, Ikeda, chaotic Karplus–Strong) oscillate near
  2τ plus the filter's lag, and period-double: Dobson & Fitch saw 3–4×L. Exact
  pitch from τ ∝ 1/f has to be engineered, not assumed.

**Key sources:**
- [Orbit 3 manual](https://cdn.shopify.com/s/files/1/1594/2421/files/orbit-3-user-manual.pdf)
- [TriggerFish VDPO](https://github.com/JTriggerFish/TriggerFish-VCV)
- [forsitan `guttur`](https://github.com/gosub/forsitan-modulare)
- [Coalescent](https://github.com/jeremycg/coalescent)
- [Attrattore](https://www.kvraudio.com/product/attrattore-tetra---four-chaotic-attractor-synthesizer-by-creature-from-the-black)
- [chaosrack](https://github.com/0magnet/chaosrack)
- [Essl, DAFx 2006](https://dafx.de/paper-archive/details/n86HtWSo8MPOrwDvKC6Mhg)
- [Dobson & Fitch, ICMC 1995](https://purehost.bath.ac.uk/ws/portalfiles/portal/665673/fractal_rev.html)
- [Elby ChaQuO manual](https://www.analoguehaven.com/elby-designs/chaquo/manual.pdf)
- [Mudd, xCoAx 2019](https://2019.xcoax.org/pdf/xCoAx2019-Mudd.pdf)
- [Weingartner, DAFx 2025](https://dafx25.dii.univpm.it/wp-content/uploads/2025/09/DAFx25_paper_68.pdf)
- [US 6,137,045](https://patents.google.com/patent/US6137045A/en)

### A first 16, in four banks

*Drawn up on 2026-09-30 from the quick check. The search above changes its
premise: Banks B and D were meant to be the distinctive ones, but forced Van der
Pol, Ueda, Duffing with audio forcing and chaotic Karplus–Strong all turn out to
exist. See the novelty-first proposal below.*

The V2 doc sets 16 as the ceiling (`DrawSlotIndicator`). One bank per family, so
B1 + a ring picks the family and the TAME behaviour is predictable within a bank:

| Bank | Slots |
| --- | --- |
| **A: Spiral** (scale-tamed) | Rössler, Coupled Rössler, Arneodo, Sprott jerk |
| **B: Driven** (drive-tamed) | Duffing, Forced Van der Pol, Ueda, Driven pendulum |
| **C: Scroll** (sync-tamed) | Lorenz, Chua, Lü, Thomas |
| **D: Locked** (exact) | Mackey–Glass, Chaotic Karplus–Strong, Circle map, Hindmarsh–Rose |

### A novelty-first next bank (proposal, 2026-10-01)

Four systems with no musical instance as a pitched hardware voice. All are ODEs,
so they fit the existing RK4 engine, TAME, the envelope and the panel with no
new infrastructure:

| Model | Why it's new | TAME class | Cost |
| --- | --- | --- | --- |
| **Driven damped pendulum** | No forced-pendulum voice in any format; only unforced pendulums exist, as CV. Rotation versus libration is a hard timbral switch | forced → FORCE | T (`sin θ`) |
| **Lorenz–Lü–Chen unified system** | One parameter morphs through three famous attractors. Lü and the morph have no hardware instance | incoherent → SYNC | P |
| **Hyperchaotic Rössler** (4-D) | Software only (chaosrack). Two positive exponents: denser than Rössler | measure | P |
| **Moore–Spiegel** or **Genesio–Tesi** | No instance at all | coherent, probably → FORCE (measure) | P |

The structurally newest ideas need engine work first, so they'd make a later
bank: a **circle map** run at n·f (published by Essl in 2006, no product),
**discrete maps re-seeded every period** (none found), and **Mackey–Glass** with
engineered pitch. The **forced Brusselator** and **chaotic Colpitts** are
unclaimed too, but that may only mean nobody has bothered. Hear them through
`tamerender` before giving them a slot.

Each needs a measured step cap on this board: the Chua episode on the bench
showed that Teensy-era figures don't carry over to 400 MHz.

## Visualising on Tiliqua

Tiliqua's **`xbeam`** bitstream is a vectorscope: `in0` = X, `in1` = Y,
`in2` = intensity, `in3` = colour, with per-channel scale and offset. It replaces
the Teensy's OLED phase plot, and at a much larger size.

- **Patch J7/J8 (X/Y CV out) to scope X/Y, not the audio outs.** J9/J10 have been
  through the output chain: a `tanh` soft-limiter flattens the lobes, the 4.9 Hz
  DC blocker skews the shape at slow rates, and the envelope scales it. J7/J8
  carry raw state times `cvScaleX/Y` on the fast DAC, updated once per audio
  block (2 kHz), so they show the true attractor. 12 bits is plenty for a picture.
- **What each model draws** (current `getX()`/`getY()` pairs): Rössler x–y, the
  spiral. Lorenz x vs z−ρ, the butterfly. Chua x–z, the double scroll. Van der
  Pol x–y, a limit cycle. Duffing x–ẋ. Coupled Rössler is the exception: x₁ vs x₂
  is two oscillators against each other, so it draws a Lissajous figure that
  beats towards a diagonal as they sync, not an attractor.
- **Colour wants a third variable.** A 3-D attractor on an X/Y scope loses z;
  putting z on `in3` gives the missing depth. Every jack is currently allocated,
  but on V2 any of J3–J6 can be an output. So a "scope" setting could turn one
  input (J6, the CHAR/TAME CV) into a Z out. Decide once there's hardware.
- **The scope is also a tuning aid.** A locked pitch draws a stationary closed
  figure, and slips show as the figure precessing. A sync reset draws a jump,
  which is the crossfade's job to hide.

## 4. Order of work

1. **Host first, no hardware. Done.** `pitchmap --emit` writes `PitchTables.h`.
   Every new algorithm gets characterised with `characterise`, `periodmap` and
   `pitchmap` before it gets a slot.
2. **TAME in `Voice`. Done**, and measured by `tametest`: see section 2. Next is
   listening to the `tamerender` WAVs and settling the two ear questions there.
3. **Alchemy platform layer. Flashed 2026-09-30**
   ([`daisy_chaos/`](../daisy_chaos/)): audio at 48 kHz / 24-sample blocks, the
   six pots, V/Oct on J3, gate on J4, CHAOS and TAME CV on J5/J6, X/Y CV out on J7/J8,
   model / envelope / TAME-mode on B1–B3, and a load governor. Next is the bench
   checklist in its README, which includes the TAME listening tests.
4. **The next bank.** Formerly Banks B and D. The 2026-10-01 prior-art search
   showed most of them already exist, so the next bank is now the
   novelty-first proposal in section 3, pending a choice. Either way it needs
   B1 to become the bank selector first.
5. Constant-rate oversampling and decimation (V2 doc), then FREEZE, EXT DRIVE and
   the LED state display.

## Open questions

- **How good is V/Oct on the stock calibration?** The factory calibration
  measures each jack's zero code and the board's VDDA, but the input gain is a
  design constant (`CvInput::SetCalibration`), so octave scale is only as good as
  the resistors. The noise at 2.7 codes per cent also needs checking. Step 4 of
  the bench checklist in [`daisy_chaos/README.md`](../daisy_chaos/README.md)
  answers both. If octaves stretch, add a two-point calibration like Joy's
  `common/voct_cal.h`.

*Resolved:* the build. The SDK repo itself builds with CMake, but its
recommended project,
[`alchemy-template`](https://github.com/hermetic-modular/alchemy-template), is a
standard Daisy Makefile, so `daisy_chaos` is one too. It builds against the SDK's
pinned libDaisy (`deps/alchemy-sdk/vendor/libDaisy`, `08f2965`), not this repo's
(`f044cdc`).
