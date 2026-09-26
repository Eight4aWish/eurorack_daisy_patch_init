# Neural networks on Eurorack (Daisy patch.init) — project brief

Handoff from a planning conversation (September 2026), corrected on 2026-09-24 against
the actual repos. Revised the same day: the conditioned model targets my Dual Pingable
LPG rather than the folder, with that module's as-built details taken from the
`eurorack_electronics` repo. Claude Code loads this file when working in
`daisy_neural/`. The repo-root `CLAUDE.md` still applies.

## Goal

Run neural audio networks on Eurorack hardware, in phases:

1. Run someone else's capture on my hardware.
2. Measure my Dual Pingable LPG and build the white-box baseline the networks must beat.
3. Capture the gate with NAM A2 — static, one capture per setting.
4. Add CV control: a conditioned GRU, so the gate has a real knob.
5. Train for audio-rate CV modulation, then design my own panel hardware.
6. Move to the Tiliqua for phase 5 — not as a fallback, as the platform it needs.

**Two platforms, and the split is deliberate.** The Daisy carries phases 1–4: it has a
working MIT A2 engine to lift, builds in seconds, and runs float, so it is where the data
and the architecture get settled cheaply. The Tiliqua carries phase 5, because audio-rate
sample-accurate conditioning is not something the M7 is slow at — it is something the M7
cannot do, its CV being read once per block. Knowing that in advance is what stops
phase 3 turning into M7 micro-optimisation that the project then throws away.

**One device, all the way through: the vactrol low-pass gate.** A vactrol's slow,
asymmetric lag is memory, and memory is the thing a static curve cannot reproduce and a
network can. Every phase from 2 onward is the same module seen at increasing fidelity,
which means each phase's measurements are the next phase's training data and each
phase's failure is the argument for the next.

The Befaco Chopping Kinky was the original phase 2–3 subject and has been **dropped from
the main line** — see the appendix. It is very likely gain plus a memoryless curve, which
makes it a poor argument for neural modelling: the honest result would be "a lookup table
won". It remains available as a pipeline-validation exercise, which is a real use, but it
is no longer on the critical path.

## Words used here

"Model" and "patch" were both ambiguous enough to cause trouble, so this project uses a
narrower vocabulary. Keep to it in code, commits and on the panel.

| Word | Means | Not |
|---|---|---|
| **engine** | the inference code that runs a network (the A2 engine, RTNeural) | the weights |
| **capture** | one trained weights file for one device at one setting (a `.nam`) | the architecture |
| **architecture** | the shape of the network — A2/WaveNet, GRU, TCN | a specific trained thing |
| **slot** | where an engine runs in the firmware (A2 takes a 48-sample block) | a preset list |

- **Do not use "patch" in this project at all.** It already means a selectable effect in
  `daisy_multifx_oled` (`PatchDef`, four banks of four), the hardware is called
  Patch.Init(), `patch` is the C++ variable for it in every `main.cpp` here, and in
  Eurorack it is what you do with cables. A fifth meaning is indefensible.
- **"Model" is reserved for the architecture**, or avoided by naming it. Say "the A2
  engine runs a capture of the gate at medium CV", where each word does one job.
- **"Engine" matches the repo already** — `daisy_multiosc` calls FM4OP, INTVL, SCAN and
  BYTEBEAT engines, not patches.
- NAM upstream calls its `.nam` files "models". Write "a `.nam` capture" and accept the
  seam rather than fight their naming.

## Working rules for this project

- **Avoid inactive code bases.** Before adding any dependency, check its recent
  commit and release activity and tell me what you found. Prefer official sources.
- **Verify, don't guess.** Check APIs, file formats and build flags against the actual
  repos. If something in this brief turns out to be wrong, say so and correct it here.
- **Pin what you lift.** Code copied from another repo records its source commit in a
  comment at the top of the file. Several of the sources below live on non-default
  branches that move.
- **Carry the licence with it.** Lifted code also puts the upstream licence text in
  `daisy_neural/LICENSE-<project>.txt` and a line in the repo README pointing at it, as
  `daisy_bytebeat/` and `daisy_interval_osc/` already do. Phase 1 lifts from two separate
  MIT projects, so expect two files.
- **Keep the build standalone.** This firmware repurposes the Daisy MultiFX unit;
  MultiFX goes back on by re-flashing `daisy_multifx_oled/`. Nothing is merged into it.
- **Explain in digestible steps.** Keep code simple over clever. I am new to neural
  networks.

## Hardware

- **Electrosmith patch.init()** — the unit that normally runs Daisy MultiFX, with a
  64×48 SSD1306 OLED in place of the B8 toggle (soft I2C on A2 = SDA, A3 = SCL; driver
  `common/oled_soft_i2c`). About 10 characters per line.
  - This is the **commercial Electrosmith unit**, not one of the hand-built modules, so
    the shared +5V rail comb that affects those does not apply. The blind A/B tests in
    phases 1–3 start from the factory noise floor.
  - Daisy Patch Submodule: STM32H750 at 480MHz, 64MB SDRAM, 8MB QSPI flash,
    128KB internal flash, 128KB DTCM, 512KB AXI SRAM, 288KB D2 SRAM.
  - libDaisy class: `patch_sm::DaisyPatchSM`. **Defaults to 48kHz, 48-sample blocks**
    (`daisy_patch_sm.cpp`), so nothing needs changing to match the benchmarks.
  - MultiFX builds `BOOT_NONE`. ~~This firmware will need the Daisy bootloader~~ — it
    does not, yet: the A2 engine plus one capture fits `BOOT_NONE` at 84.5% of flash.
    `BOOT_SRAM` is still the move once the DTCM placement is needed, as Sorrow and Joy do.
  - The carrier has a **microSD slot** (see `patch_init_schematic.pdf`).
  - **IN_R is normalled to IN_L** on the carrier.
  - CV_OUT is 12-bit, 0–5V. CV_5–CV_8 are ±5V inputs.
  - **The audio inputs are AC-coupled.** The carrier adds nothing — `J_LIN`/`J_RIN` go
    straight to submodule pins B4/B3 with no series caps (`patch_init_schematic.pdf`), so
    the coupling is inside the Patch SM. What is *not* yet known is the corner frequency,
    which is what the HPF page measures and what phase 5 needs.
  - **`IN_R` is normalled to `IN_L`, and patching `IN_R` breaks that normal** — the R
    jack's NORM contact carries `SIG_LIN`. So IN_R is a second per-sample input whenever
    something is plugged into it, which is the only route for audio-rate CV.
  - **Pots are wired 0–5V, CV jacks −5V to +5V** (schematic note). libDaisy inits
    CV_1–CV_8 alike with `InitBipolarCv`, so a pot only covers part of the bipolar range.
    This firmware sums pot + CV and clamps exactly as `daisy_multifx_oled` does, so the
    knobs behave the same as that unit's — whatever that turns out to feel like, it is
    not novel here. Confirm on hardware before tuning any knob range.
- **Befaco Chopping Kinky.**
  - Dual wavefolder; channel A symmetric, channel B asymmetric (per the VCV model).
  - Two fold CV inputs per channel, one through an attenuverter.
  - DC-coupled.
  - The Chop output switches between channels (zero-crossing detector or gate).
    Do not capture it.
  - Input level and fold CV range: _read from Befaco's user manual and add here._
  - Schematics are published by Befaco (CC BY-NC-SA) at <https://www.befaco.org/docs/>.
- **Dual Pingable LPG** — the subject of phases 2–5, and my own design: 10HP, dual
  channel, on the N8Synth solderable breadboard, canonical Buchla 292 audio path in a
  Make Noise Optomix shape. Design docs live in the `eurorack_electronics` repo under
  `docs/lpg_*` — reference review, netlist, BOM, placement and generated schematics.
  - A vactrol is an LED facing a light-dependent resistor. The LDR lags the LED by tens
    of milliseconds, asymmetrically (decay far slower than attack), and that lag *is*
    memory: the thing a static curve cannot represent and a recurrent network can.
  - **Per channel:** audio in/out, **Strike** (pingable gate, 1µF/10K differentiator,
    positive edge only), **CV in** (0–8V through a 100K attenuator), **MANUAL** offset,
    **DEPTH** (500K log rheostat in series with the LED drive — it can close the gate
    fully). Plus a summed mix out.
  - **The channels are independent.** DAMP (layer L4), the mode switch (L5) and resonance
    (L7) have tap points and board space reserved but are **not built**. Mode is "both" —
    level drop and high-frequency roll-off together, the LPG signature.
  - **One envelope per channel.** The driver feeds DEPTH → 470R → *both* of that
    channel's vactrol LEDs in series (status LED between them), so the two LDRs — one in
    the filter, one in the attenuator — always see the same current. The model therefore
    needs a single vactrol state per channel, not two. That is exactly the Parker &
    D'Angelo structure, whose Eq. 4/12 placement of Rα this build already follows.
  - **The vactrols are socketed and swappable.** Day-one per the netlist and BOM is
    2× **VTL5C3** (DIP-4, single-LDR) per channel, LEDs in series, Vf chain ≈ 3.2V, with
    6v8 zener clamps giving ≈ 7.7mA peak through the 470R. The same socket also takes
    molded VTL5C3/2, LCR0202/0203, or a DIY shroud, and the drawer holds a six-value
    GL55xx LDR pack and four LED colours for exactly that experimentation. The filter cap
    is socketed too (layer L6). **The device under capture is a configuration, not a
    constant** — record the fitted parts with every capture, and expect a swap to
    invalidate one. It cuts the other way too: one circuit yields several ground truths,
    which is a free generalisation test for phase 3.
  - **Expect the two channels to differ.** LDR part-to-part spread is wide (~50% on DIY
    parts; the molded Senba parts vary too). Capture each channel separately and do not
    assume symmetry.
  - **Hand-built, so its noise floor is its own** — unlike the patch.init. Measure it
    before asking a model to match it (phase 0, check 4).
  - White-box reference: Parker & D'Angelo, DAFx-13 — already the analytic basis of the
    build itself.
- **Befaco Instrument Interface** and a guitar, for test input.
- **Recording chain:** Mac → Focusrite 16i16 → ADAT → **Expert Sleepers ES-10** → modular,
  and back the same way.
  - The ES-10 is DC-coupled in both directions, with **one global jumper for AC/DC**.
    Set it to DC. ADAT carries plain sample values, so DC survives (bench check 1).
  - Do **not** use the 16i16's own analog jacks for CV or DC — Focusrite states none of
    its interfaces are DC-coupled.
  - No ES-9 is needed.
  - **The ES-10 can drive the fold CV too:** one ADAT channel plays the test audio,
    another plays the fold CV from the same file, sample-aligned. That is how phase 3
    data gets recorded.
- **Development machine:** Mac (MacBook Air or Mac mini). The NAM trainer runs on Apple
  silicon (`accelerator: "mps"`); Colab is optional.
- **Alternative host, later:** the Hermetic Modular **Alchemy Lab** in the rack is the
  same STM32H750 (Daisy Seed 2 DFM) with 6 pots, 3 buttons, 6 firmware-configurable CV
  jacks, USB MIDI and microSD, but no screen. Same CPU budget. Worth moving to only if
  phase 3–4 needs more CV than the patch.init has. Keep the model code separate from the
  `DaisyPatchSM` code (as `common/multifx_core` does) so the move is cheap.

## Code bases and their activity (checked 2026-09-24)

| Project | Role | What is actually there |
|---|---|---|
| `tone-3000/nam-pedal` | **Phase 1 engine** (MIT) | **`main` is not the A2 engine**: it is a Feb 2026 Daisy **Pod** demo using full NeuralAmpModelerCore + Eigen and a legacy model from SD. The dependency-free A2 engine (`nam_model.c/.h`, generated by a `nam2c.py` that is not in the repo) is on branches `a2-nano` (`afbfb9f`, 2026-04-16), `terrarium` (`262656f`, 2026-06-25) and **`t3k-pedal` (`6dc47a4`, 2026-09-22, most active)**. All target `DaisySeed`, `BOOT_QSPI`, `-O2`, 48 samples at 48kHz. No patch.init support. |
| `tone-3000/nam-binary-loader` | `.nam` → `.namb` (`nam2namb`, C++/CMake, needs NAM Core) | `92c9e85`, 2026-04-16. `.namb` = "NAMB" magic, 32-byte header, CRC32, raw float32 weights. |
| `bkshepherd/DaisySeedProjects` v1.6.0 | **Simplest reference** (MIT) | NAM A2 module added 2026-06-09. One header, `Effect-Modules/Nam/nam_a2_runtime.h`, credited to "nadavb" on the Daisy forum. Weights compiled in by `nam_to_cpp_array.py`. `BOOT_SRAM`. Weights in DTCM; ~76KB history buffer in RAM_D2 via `nam_a2_sections.lds`. **SDRAM was tried and missed the audio deadline.** |
| `roccoagain/nam-seed3` | Reference (measured) | A2-Lite on Daisy Seed at 48kHz / 48 samples, **~61% CPU**. Last commit 2026-09-17. |
| `sdatkinson/NeuralAmpModelerCore` | C++ inference core | A2 via `SlimmableContainer` from v0.5.0 (2026-04-16); latest release v0.5.4 (2026-06-23); main active 2026-09-18. |
| `sdatkinson/neural-amp-modeler` | Training (PyTorch) | v0.13.0 (2026-06-02), last commit 2026-08-22. Trains A2. **No conditioned / parametric option** — `PackedWaveNet` rejects conditioning. |
| `jatinchowdhury18/RTNeural` | Inference for small GRU/LSTM. **Phase 3 engine.** | Last commit 2026-08-20; no tagged releases. Issue #167 (open) is clicks on `reset()` — crossfade on model switch, as MultiFX already does. |
| `GuitarML/NeuralSeed` | Phase 3 precedent (MIT) | **Inactive** (last commit 2023-04-11). RTNeural GRU-10 snapshot, GRU-8 with 2–3 knobs fed as extra network inputs. Reference only. |
| `oyama/pico-neural-amp-modeler-demo` | Reference | A2-Lite on RP2350 via NAM Core's `a2_fast` path: 4,533 cycles/sample at 300MHz (73%). Cites **2,965 cycles/sample on a Cortex-M7** (~30% at 480MHz). |
| `VCVRack/Befaco` `src/ChoppingKinky.cpp` | Appendix only (folder dropped) | Active (2026-09-17). Static, memoryless curve per channel (256-point table), preceded by a gain set by knob + CVs. Oversampled ×4 by default. This is the evidence the folder is memoryless. |

**What A2 is.** A WaveNet exported as a `SlimmableContainer` holding two sizes: A2-Lite /
"nano" (3 channels) and A2-Full (8 channels). Both have 23 layers, LeakyReLU, dilations
up to 239 samples, head kernel 16. **The embedded engines run the 3-channel size only
— exactly 1,871 weights.** An ordinary (non-A2) `.nam` will not load.

**CPU, measured on the Daisy's STM32H750 at 480MHz:** ~30% (oyama's cycle count), 40%
(forum, nano), 53% (forum, a larger model), 61% (nam-seed3). The "50% at 600MHz" figure
often quoted is for a generic 600MHz M7, not the Daisy.

**Prior art.** No neural firmware for patch.init / Patch SM / Daisy Patch / Versio was
found, and no NAM or ToneX capture of any Eurorack module. Open ground.

## Phase 0 — bench checks (one evening)

1. **ES-10 passes DC.** Jumper to DC. Play a +2V DC step out of an ES-10 output, patch
   it to an ES-10 input, record it; confirm the offset holds.
2. **Patch.init audio input — find the high-pass corner.** AC-coupled is already settled,
   so this is now a measurement, not a yes/no. Send one LFO to **both** `IN_L` and `CV_5`,
   run `daisy_neural`, long-press to the HPF page. `CV_5` is DC-coupled, so its span is
   the truth and `RAT` is the audio input's response at that frequency. Sweep the LFO up
   from ~0.1Hz and find where `RAT` reaches 0.71 — that is the −3dB corner. Record it
   under Hardware; it sets the crossover for phase 5's split-path CV.
3. ~~Chopping Kinky levels.~~ Dropped with the folder; see the appendix.
4. **Dual LPG noise floor and channel match.** Record both channels quiet, then ping each
   with Strike, noting which vactrols are fitted. It is a hand-built module, so know its
   noise floor before phase 3 asks whether a model matches it — and the two ping
   responses show up front how far apart the channels are.

## Phase 1 — run an existing A2 capture on patch.init (START HERE)

**Done when:** a downloaded A2 capture processes guitar audio on patch.init at 48kHz with
no dropouts, and the measured CPU load is recorded here.

1. **Hear it on the Mac first.** Download an **A2** overdrive or fuzz *pedal* capture
   from TONE3000 (no cabinet IR needed) and play it in the NAM plugin.
2. ~~**Set up the build in `daisy_neural/`**, copying the Makefile pattern from
   `daisy_multifx_oled/` (`make/common.mk`, `common/oled_soft_i2c.cpp`).~~ **Done** —
   `Makefile` + `src/main.cpp` carry the whole chain with `EngineSlot::Process()` as a
   passthrough, which is also the pass-through firmware bench check 2 needs, so that
   check can run before the engine exists. Built `BOOT_NONE` for now so it flashes
   without the bootloader; see the README. **Compiles clean, not yet run on hardware.**
   Skeleton footprint with the slot empty: FLASH 99,508 B (75.9% of 128 KB), SRAM
   16,764 B, RAM_D2 16,896 B, DTCM unused — so `BOOT_NONE` has only ~28 KB spare, which
   is the practical reason step 3 moves to `BOOT_SRAM`.
3. ~~Lift the engine from nam-pedal `t3k-pedal` @ `6dc47a4`.~~ **Done — from bkshepherd
   instead.** That repo ships the whole path: the runtime, the `.nam` → C array converter,
   and five already-converted captures. nam-pedal's `nam2c.py` is not in its repo, so its
   engine cannot be fed without writing a converter first. `nam/nam_a2_runtime.h` @
   `ccae0f2` (2026-09-08), MIT, licence in `LICENSE-daisyseedprojects.txt`.
   **Correction to this brief:** A2's API here is a fixed **48-sample block**, not sample
   by sample. The Patch SM's default block is also 48, so they line up exactly.
4. ~~Start with one capture compiled in, `BOOT_SRAM`.~~ **Done, and it fits `BOOT_NONE`**
   — JCM800, one of the five shipped; the other four stay unreferenced and `--gc-sections`
   drops them. Engine + capture cost 11.3 KB of flash (84.5% used) and 88.5 KB of SRAM
   (the history buffer). The DTCM/D2 placement macros are neutralised so it runs without
   the bootloader; move to `BOOT_SRAM` with `nam/nam_a2_sections.lds` if measured CPU
   says the placement matters. QSPI capture bank or microSD comes later.
5. **Audio path:** IN_L → input trim (knob 1) → A2 → output level (knob 2)
   → OUT_L and OUT_R. The button toggles bypass for A/B comparison.
6. **Display (64×48 OLED):** CPU load (libDaisy `CpuLoadMeter`), an input peak meter
   (to set the trim so the signal reaches the engine at the level it was trained on),
   and the capture name, truncated to fit.
7. **Measure and record:** CPU load, and memory use by region (DTCM, AXI SRAM, D2 SRAM,
   SDRAM, QSPI). Keep hot buffers out of SDRAM.

## Phase 2 — measure the gate before modelling it

Nothing gets trained on this module until its behaviour is written down. The
measurements here are the baseline every later phase is judged against, and they are
also what sizes the network in phase 4 — so this is data collection, not a detour.

**Strike makes it easy.** A ping is a repeatable impulse, so the vactrol's decay can be
recorded in isolation with nothing else moving.

1. Set MANUAL to minimum so CV and Strike alone drive the LED.
2. Ping each channel at several DEPTH settings and record the response. Fit the decay:
   you want its rough time constant, and whether attack and decay differ (they will).
3. Step the CV and record the attack side the same way.
4. Sweep a slow triangle through the audio path at several fixed CV levels — the
   input/output plot at each level is the static transfer curve.
5. Build the **white-box baseline** on the patch.init: the Parker & D'Angelo envelope
   driving a VCA plus the filter pole, from this module's own netlist, no network.

**Done when:** the decay time constants are written down, the white-box baseline runs,
and there is an honest note on how close it already gets. If it gets very close, say so —
that is a real result and it raises the bar the networks have to clear.

**These are the same measurements the pot re-range needs** (`eurorack_electronics`,
rev 0.27), so do them before the circuit changes and again after; the second set is the
training data.

## Phase 3 — capture the gate with NAM A2 (and find out where it breaks)

A2 is static: one capture describes the device at one CV setting, so a full set means
capturing a grid of settings. That limitation is the point of this phase.

- Capture each channel separately at several fixed CV levels, Strike unpatched.
- Re-amp through the ES-10 at modular levels and match the input level on playback.
- Use NAM's standard training input **plus oscillator sweeps at several amplitudes** —
  the gate's response depends on input level, and oscillators are its real use.
- Aliasing: the *recording* will not alias (the interface filters its input), but the
  capture will, because it cannot produce harmonics above 24kHz and folds them back.
  Recording and training at 96kHz helps — but a capture must run at the rate it was
  trained at, because a WaveNet's dilations are counted in samples, so a 96kHz capture
  played back at 48kHz has its whole time structure halved. Running 96kHz on the Daisy
  doubles the CPU cost, which A2 likely cannot afford, so train at 48kHz unless the
  measurements say otherwise. (Carson et al. on sample-rate independence, below, is the
  way out if 96kHz training proves necessary.)
- Store captures in QSPI (or SD) and switch at runtime, crossfading on switch.
- **Chasing aliasing on the Daisy is optional.** Oversampling 2–4× multiplies the CPU
  cost and would need int16 quantisation or aggressive DTCM tiering to fit. That work
  does not transfer to phase 5's platform, so do it only if you want a good-sounding
  Daisy build for its own sake. Record how bad the aliasing is and move on otherwise.

**Watch the tail, and measure how it fails.** A2's largest *dilation* is 239 samples, but
that is not how far back it sees. Each layer reaches (kernel − 1) × dilation samples, and
the kernels are 6 taps (15 on two layers), so the 23 layers sum to **6,331 samples, about
132ms at 48kHz** (`kKernelSizes`/`kDilations` in `nam/nam_a2_runtime.h`; the 76.5KB
history buffer is that window × 3 channels). An earlier version of this brief read the
239 as the window, called it 5ms, and predicted the ping would smear. It will not smear
for that reason: 132ms covers a decay of tens of milliseconds. Whether A2 reproduces the
tail is now an open measurement, not a foregone failure. If it does fail, the argument for
phase 4 is the knob, not the window: a recurrent network carries state with no fixed
horizon, and conditioning needs a network that takes the CV as an input.

**Done when:** a capture holds up under sustained audio at a fixed setting, *and* the
ping response is compared against the real gate and against the phase 2 white-box
baseline, with the gap written down.

## Phase 4 — CV-conditioned GRU: give the gate a knob

Phase 3 leaves a grid of static captures and no way to move between them. Conditioning
is what turns that into a control: the CV becomes an input to the network, so one
network covers the whole range and responds continuously.

**The LPG is not a frozen target, and that dictates the order.** A pot re-range is
specified and awaiting the bench — little of DEPTH's and MANUAL's travel does anything
audible — with the diagnosis in `eurorack_electronics/docs/lpg_netlist.md` and a bench
procedure in `docs/lpg_mod_pot_rerange.md` there. **L7 resonance is deferred**, which is
the happier order for this project: it keeps the phase 3 target free of a feedback path.
The re-range changes the device a capture would describe, so **do the circuit work first
and capture afterwards** — no hurry, phases 0 through 2 come first. Two consequences:

- The phase 3 measurements and the re-ranging want the same data. Ping responses and CV
  steps say where the LED current actually does something audible, which is exactly what
  chooses a pot value and taper. Measure before changing anything, then again after, and
  the second set is the training data.
- Resonance makes the model harder, not just different. A feedback path that can approach
  self-oscillation is a known weak spot for black-box RNNs. It is an argument for the
  structure-first approach above: put the known filter topology in as fixed maths and
  leave the network only the nonlinearity.

- **Size it from phase 2's measurements, don't guess.** The decay time constants say how
  much history the network must hold, and that sets the unit count. A network too small
  cannot hold the tail; too large and it will not fit the CPU budget.
- **Stack:** RTNeural running a small GRU (8–16 units) with a Dense output layer, the
  NeuralSeed pattern: the CV is an extra network input alongside the audio. Use a short
  PyTorch training script I own; NAM's trainer cannot do this.
- **Structure first:** Parker & D'Angelo (DAFx-13) model a vactrol as a slow envelope
  driving a VCA — and this build follows that paper's own Rα placement, so the structure
  comes from the netlist rather than from guesswork. Put the phase 2 white-box envelope
  in front of the network as fixed maths and let it learn only what is left over. That
  is the idea behind Hayes et al. (NEWT), and it keeps audio-rate CV well-behaved in
  phase 5. One envelope state per channel, per the series LED chain.
- **Conditioning method is an open choice, not a settled one.** Feeding CV in as an extra
  input alongside the audio is the simplest option and the one NeuralSeed used, but it is
  not the only one — FiLM modulates each layer instead, and hypernetworks generate the
  weights. Simionato & Fasciani (below) compare them directly; read it before committing,
  because this decides how smoothly the knob behaves between trained points.
- **Prototype data:** a white-box vactrol simulation, built from that paper and this
  module's own netlist, generates unlimited audio + CV including fast CV, for rehearsing
  the pipeline. It is a model of a model: final accuracy needs real recordings.
- **Real data:** ES-10 drives audio, CV and Strike from one sample-aligned file. Check
  the ES-10 covers 0–8V at the attenuator setting used.
- **Pipeline check:** confirm the C++ output matches PyTorch sample for sample on the
  Mac before flashing.
- **Done when:** the model tracks the real gate under slow CV *and* under ping, decay
  tail included, and a static-curve model of the same gate audibly fails to — which is
  the evidence that the network was necessary. Ping is the sharper test of the two.

## Phase 5 — audio-rate conditioning, then hardware

**How audio-rate CV actually gets in.** This is settled by the hardware, so decide it
before training anything. The CV jacks are read by `ProcessAllControls()` once per
block — about 1kHz — which is fine for a knob and useless for audio rate. The only
per-sample input is an audio jack, and `IN_R` is available the moment something is
patched into it, since that breaks its normal from `IN_L`. But the audio inputs are
**AC-coupled**, so that path loses the DC and the slowest part of the control signal.

Neither path carries the whole signal, so **split it**: the slow component through a CV
jack (DC-accurate, block rate) and the fast component through `IN_R` (per-sample,
AC-coupled), summed in firmware. Each path covers exactly what the other cannot. The
crossover belongs at the audio input's high-pass corner, which is what the firmware's
HPF page measures — send one LFO to both `IN_L` and `CV_5`, sweep its frequency, and
read the span ratio.

- Train with fast-moving CV and test under audio-rate modulation. No paper found
  conditions on audio-rate CV — the published conditioning work assumes slow controls.
  This is the novel part.
- Audio-rate CV into a vactrol is where it gets interesting: the real vactrol cannot
  follow it, so the network has to learn the lag itself rather than track the control.
  The hardware's own limitation becomes the thing being modelled.
- Candidate next targets: two-input networks (ring modulation, cross-modulation).
- Design panel and interface hardware once the requirements are known (or move to the
  Alchemy Lab if it is only more CV).

## Phase 6 — the Tiliqua is where phase 5 lands

**This used to read "FPGA, optional, only if phases 4–5 produce a measured bottleneck."
That framing was wrong**, and it was wrong in a way that would have wasted effort: it
treats the FPGA as an escape hatch for when the M7 runs out, when for audio-rate
sample-accurate conditioning the M7 is not slow but *structurally incapable*. CV is read
once per audio block (~1kHz), and the only per-sample way in is an AC-coupled audio jack.
No amount of optimisation fixes that; it is the shape of the platform.

The numbers, from `eurorack_electronics`'s sibling — the `tiliqua` repo's own build
figures rather than datasheet optimism:

| | needed | available on the ECP5-25F |
|---|---|---|
| A2 WaveNet, 1,871 weights | ~2–3k MAC/sample | 28 mult × 1250 cycles = **35,000** at 48kHz |
| GRU-16 + 2 CV inputs | ~880 MAC/sample | 28 × 312 = **8,700** at 192kHz (4× oversampled) |
| weights storage | 3.7 KB at 16-bit | **1008 kbit** block RAM |

So a network of this size has ten to forty times the arithmetic it needs, keeps it under
oversampling, and fits entirely in BRAM — no PSRAM, no tiering, none of the placement
problem that constrains the Daisy build. The mesh in Silver and Gold is a far harder
workload than any of this.

**Precision is measured, not assumed** (`tools/quantisation_study.py`, run against the
real engine compiled natively). A2's weights at 16-bit sit at −63 dB ESR with per-64
scaling; 12-bit reaches −39.8 dB, about where differences stop being obvious; 8-bit is
unusable. Scaling granularity is worth 1.5–2 bits on its own. Weights only — activations
and accumulators are still float in that study, so read it as a veto, not a permit.

That result is what makes A2-**Full** arguable on this board: 13,066 weights and a
204 KB float history come to ~128 KB at 16-bit against 126 KB of BRAM, but ~96 KB at
12-bit, and the 18×18 DSP slices cost the same either way. No M7 can run A2-Full at all
(7× A2-Lite's ~1,660 MAC/sample puts it at 210–427% CPU), so the FPGA is not merely
better there — it is the only platform where the larger architecture is on the table.

What it actually costs, which is not silicon and not skills:

- **It is a third bitstream, not an addition.** Silver takes 22 of 28 multipliers at
  48×48, Gold 19 of 28 at 32×32. A neural engine gets its own top-level and its own
  board time; it does not ride along.
- **Quantisation is new work** — though the same discipline as the mesh's fixed-point
  scaling, not a different one.
- **The training pipeline is unaffected.** PyTorch on the Mac either way; the platform
  only changes what runs the result.
- Upstream's policy permits AI assistance in non-pedagogical top-level bitstreams with
  clear provenance, but **not** in shared library or platform code — so a neural
  top-level is in the permitted category and anything touching `dsp/` is not.

**Therefore: squeezing the M7 is optional.** int16 quantisation via the Cortex-M7's
integer SIMD, DTCM tiering beyond what phase 1 already needs, shaving the engine — do
these only if you want phase 3's oversampling *on the Daisy*, not because the project
needs them. On the path to phase 5 they are throwaway. The Daisy's job is phases 1–4:
get the data right, get the architecture and conditioning right, in float, where
iteration costs seconds. Then port a known-good network.

A faster processor is still worth a thought before committing, but it buys throughput,
not the per-sample structure, so it does not address the thing phase 5 needs.

## Appendix — the Befaco Chopping Kinky (dropped from the main line)

The folder was the original phase 2–3 subject. It is off the critical path because it is
very likely **gain plus a memoryless curve**: the VCV model and the one derivative
schematic found (`kraakenstuff/KinkyNoChop`, 2019: OTA VCAs driving diode folders) both
point that way. If so, a measured 256-point curve beats a network on it outright — near
zero CPU, anti-aliased by ordinary means (oversampling or ADAA), and CV, including
audio-rate CV, is just a multiply in front of the curve. Modelling it neurally would be
choosing the harder tool to get the worse answer.

It stays useful for one thing: **validating the capture pipeline against a known
ground truth.** Measure its transfer curve, capture it with A2, and the two should agree.
If they do not, the re-amp levels, alignment or training are wrong — and finding that out
on a device whose correct answer you already know is much easier than finding it out on
the gate. Worth an evening if phase 3 produces a confusing result.

Its hardware details are kept under Hardware, and the CV range and levels still need
reading off Befaco's manual if this is ever picked up.

## Key papers

- Wright, Damskägg & Välimäki, "Real-time black-box modelling with recurrent neural
  networks", DAFx-19. The most readable place to start.
- Chowdhury, "RTNeural" (arXiv:2106.03037).
- Hayes et al., "Neural Waveshaping Synthesis" (arXiv:2107.05050) — learned shapers
  with gain in front; closest to a folder.
- Esqueda et al., Buchla 259 wavefolder (DAFx-17) and Lockhart/Serge wavefolders
  (SMC-17) — white-box folders and anti-aliasing (BLAMP, ADAA).
- Simionato & Fasciani, "Conditioning methods for neural audio effects", SMC 2024.
- Simionato & Fasciani, "Towards Neural Emulation of VCOs", DAFx-25.
- Yeh et al., "Hyper Recurrent Neural Network…", DAFx-24 (arXiv:2408.04829).
- Mikkonen, Wright & Välimäki, "Sampling the user controls in neural modeling of audio
  devices", EURASIP JASMP 2024.
- Carson et al., "Sample Rate Independent Recurrent Neural Networks for Audio Effects
  Processing" (arXiv:2406.06293).
- Aliasing in neural models: arXiv:2505.04082 (smoother activations) and
  arXiv:2505.11375 (teacher–student fine-tuning).

## Open questions

- What is the audio input's high-pass corner frequency? AC-coupled is settled; the corner
  is not, and it sets the crossover for phase 5's split-path CV. (Phase 0, check 2, now
  a measurement rather than a yes/no.)
- Which conditioning method for phase 4 — CV as an extra input, FiLM, or a hypernetwork?
  Simionato & Fasciani compare them; it decides how the knob behaves between trained
  points, so it wants reading before the training script is written.
- Which vactrols are physically fitted in the LPG right now? The netlist and BOM specify
  2× VTL5C3 per channel day-one, but the parts drawer also holds LCR0202/0203 and a
  six-value GL55xx LDR pack for DIY vactrols, and the sockets accept all of them. Only
  the bench can confirm it. Every capture is of one configuration, so this goes in the
  capture notes. (Phase 0, check 4.)
- End goal: a module for my own rig and videos, or eventual release?

## Settled

- ~~Does the ES-10 → 16i16 chain pass DC?~~ Yes with the ES-10 jumper on DC, via ADAT.
  Confirm once on the bench (phase 0, check 1).
- ~~Does Daisy MultiFX use the bootloader and DTCM?~~ Moot: the MultiFX unit is
  repurposed, not merged into. (For the record, MultiFX is `BOOT_NONE`.)
- ~~Is this unit affected by the shared +5V rail comb seen on the hand-built modules?~~
  No — it is the commercial Electrosmith patch.init.
- ~~Which vactrol low-pass gate, and what are its CV range and modes?~~ My Dual Pingable
  LPG: 0–8V CV through a 100K attenuator, "both" mode only, Strike per channel, DAMP and
  the mode switch not built. Documented in `eurorack_electronics/docs/lpg_*`.
