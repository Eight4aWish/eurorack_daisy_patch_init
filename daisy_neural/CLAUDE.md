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

**A side line runs phases 3 and 4 on a filter first:** the Doepfer A-124 (Wasp), one
series of recordings trained three ways. See "The Wasp" below. The LPG stays the main
line.

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
- **Doepfer A-124 (Wasp)**, the side line's subject (Doepfer's manual and product page).
  - A 12 dB/oct multimode filter after the EDP Wasp. It uses CD4069 digital inverters as
    op-amps, which is where its dirt comes from. The CD4069 is socketed because it
    occasionally fails.
  - **Two outputs:** BP, and an LP/HP mix whose **Mix** knob runs LP → notch → HP.
  - **Inputs:** Audio In through a **Lev** attenuator; **CV 1** with no attenuator,
    "approximately" 1 V/oct and "not very precise"; **CV 2** through its own attenuator.
    Frq and Res are panel knobs only.
  - **It cannot self-oscillate**, unlike most A-100 filters. That suits the networks:
    feedback near self-oscillation is their known weak spot.
- **Befaco Instrument Interface** and a guitar, for test input.
- **Recording chain:** Mac → Focusrite 16i16 → ADAT → **Expert Sleepers ES-10** → modular,
  and back the same way.
  - The ES-10 is DC-coupled in both directions, with **one global jumper for AC/DC**.
    Set it to DC. ADAT carries plain sample values, so DC survives (bench check 1).
  - **8 in, 8 out** (manual rev 1.0). Outputs reach **±11 V**. Inputs clip at **±12 V**
    at modular level (101 kΩ), or ±3.5 V at line level, set per input by a jumper. Expect
    DC offsets on DC-coupled inputs.
  - **Measured on the bench, 2026-10-18** (WASP.md, step 1): **10.96 V per full scale
    out** (a meter read 5.48 V at half scale), **11.97 V per full scale in**, input offsets
    about +0.02 of full scale (240 mV). The DC jumper had been in its factory AC position
    until that day: block J6, upright on the HPF column for DC.
  - Through a Scarlett 16i16 4th Gen at 48 kHz: ADAT 1–8 are playback 7–14 and inputs
    11–18. The round trip with sounddevice's large buffers is about 12,370 samples
    (258 ms) and moves by a few samples each time a stream starts, so every take
    measures its own. The converters add almost exactly one sample on top of what a
    broadband burst reports; `wasp_capture.py split` measures the fraction and cuts the
    training data 2 samples late, so no network has to predict the future.
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
found, and no NAM or ToneX capture of any Eurorack module (searched 2026-09-24). Close to
it, found 2026-10-20: Simionato and Fasciani (arXiv 2405.04124, EURASIP JASMP 2025)
modelled a Behringer Neutron's low-pass filter among other analog effects, with the
controls as conditioning inputs to recurrent and state-space networks, on a computer;
Neurorack (IRCAM ACIDS, 2021) runs deep generative audio in a Eurorack module on a Jetson
Nano; GuitarML's NeuralSeed runs a GRU-10 with knob inputs on a Daisy Seed pedal. The
video's "similar, not the same" list is in eight4awish-video `slides/mirth-storyboard.md`.
David's line, always: these people have done something similar but not the same; never
"first".

**Parametric (knob-conditioned) captures are not new** (found 2026-10-19, from NAM's own
history; a broader search is still owed before anything public says "first"):
- **NAM 0.5–0.7.4 (2022–23) shipped them**: `nam/models/parametric/` with CatLSTM and
  CatWaveNet (knob values concatenated with the audio at every sample: our approach), a
  hypernetwork (HyperConvNet), and a ParametricDataset giving each recording its fixed
  knob settings. **0.8.0 removed them** (PR #367, merged 2024-01-14, resolving #334): not
  dropped as an idea, moved downstream, to be registered through NAM's model and dataset
  hooks. NAM 0.13.0 cannot train one as shipped (its dataset is mono only), but its A2
  WaveNet now has FiLM layers that take a conditioning input.
- **Live again: issue #694, "Conditional NAM"** (opened 2026-08-19). One contributor plans
  to test A2's FiLM primitives against downstream ConcatWaveNet and HyperWaveNet, judged on
  settings never trained. Another (michalwarda) reports three weeks and ~500 experiments on
  a parametric A2: about 100 recordings at different positions for a five-knob amp, a
  "global weighted ESR" around 0.01, and that architecture and training data cannot be
  tuned apart. Steven Atkinson is open to re-adding low-level primitives, not opinions.
- All of it is guitar amps with knobs **held fixed per recording**. What is still ours,
  as far as found: a Eurorack module, a control swept **at audio rate within a take**
  (the cutoff CV), and the result running on the module (the Daisy).

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

**Done 2026-09-28.** Captures run off the card on a fresh patch.init and sound like
their amps. **CPU load 64%** average and peak (`BOOT_SRAM`, history in RAM_D2; 1% in
bypass), just above the references' 30–61%. The card needs a FAT32 partition of 2 GB or
less — see the README.

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
   instead.** That repo ships the whole path: the runtime and a `.nam` converter.
   nam-pedal's `nam2c.py` is not in its repo, so its engine cannot be fed without writing
   a converter first. `nam/nam_a2_runtime.h` @
   `ccae0f2` (2026-09-08), MIT, licence in `LICENSE-daisyseedprojects.txt`.
   **Correction to this brief:** A2's API here is a fixed **48-sample block**, not sample
   by sample. The Patch SM's default block is also 48, so they line up exactly.
4. ~~Start with one capture compiled in, `BOOT_SRAM`.~~ **Done, and it fits `BOOT_NONE`**.
   Engine + capture cost 11.3 KB of flash (84.5% used) and 88.5 KB of SRAM
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

## The Wasp — phases 3 and 4 on one dataset

A side line, started 2026-10-14. The A-124 is the best capture subject in the rack apart
from the LPG. It processes audio, its memory is short, its distortion is its character,
and it is nothing like an amp. Capturing it rehearses phases 3 and 4 on a device with no
re-range pending. It also gives Mirth not-amps that were never amps, recorded from
David's own unit and his to license, unlike the TONE3000 captures. No NAM capture of any
Eurorack module was found (Prior art, above).

**Only the cutoff is under CV on the A-124** (Doepfer's manual): CV 1 at roughly
1 V/oct, and CV 2 through an attenuator. Res, Lev and Mix are knobs. A filter wants both
cutoff and resonance as controls, so **the resonance is stepped between sessions**
while the ES-10 sweeps the cutoff inside every take. One session per Res position, six
across the dial plus one held out; Lev and Frq stay fixed throughout.

**Pure low-pass and pure high-pass** (decided 2026-10-17; the notch and BP are less
interesting). They are the two end stops of the Mix knob on the one LP/HP output, so
they cannot be recorded together: **each is its own series** of those seven sessions.
The end stops are exactly repeatable, which no mark on a dial is. BP is recorded on a
second input anyway, since it costs nothing, but no network is planned on it.

**One series, three networks** (decided 2026-10-17):

| Network | Inputs | Trained on |
|---|---|---|
| **A2 snapshots** (NAM's WaveNet) | audio | NAM's standard `input.wav` at five cutoff CVs, in each series' Res 6 session: one capture each |
| **GRU, cutoff** | audio, cutoff CV | The LP comparison session: those five takes plus its 4 minutes of sweeps |
| **GRU, cutoff and resonance**, one per series (LP, HP) | audio, cutoff CV, Res dial position | Every session of its series except the held-out Res |

The second costs no extra recording: it is a training run on a subset. It is there because
it is the clean match for A2, the same data at the same setting. The third is the one
worth playing, as LP and as HP; one GRU taking the end stop as a further input is a later
option. The A2-against-GRU comparison is made on LP, the classic Wasp sound; HP gets A2
snapshots too, because A2 is the only kind of capture Mirth plays today. A GRU-24 with
three inputs is about 2,100 parameters, the same order as A2-Lite's 1,871 weights. NeuralSeed (GRU-8, two or three knobs fed in as inputs, on a
Daisy Seed) is the precedent.

**Four comparisons, each on held-out takes no network trained on:**
1. **Architecture:** A2-Lite against the cutoff GRU, trained on the same take. Any
   difference comes from the architecture.
2. **The cutoff knob:** between snapshots A2 can only jump, while the GRU should glide.
3. **The cost of a second knob:** the LP two-control GRU against the cutoff GRU, at the
   comparison Res.
4. **Filling the gap in resonance:** each two-control GRU, LP and HP, at the held-out Res
   position, against the real Wasp. Res only ever moves between takes, so this is the open question.
   Mikkonen, Wright & Välimäki (Key papers) is about choosing those positions.

Judge by A/B against the real Wasp, by listening to each network's leftovers (the real
Wasp minus the network), and by ESR on the same passage.

**If comparison 4 fails:** the structure-first route from phase 4, a two-pole
state-variable filter (Doepfer likens the Wasp to its SEM filter) with cutoff and
resonance as real coefficients and a small network learning only the CMOS dirt. The same
recordings train it. And **Swords** (`MODULES.md`) is the rack's filter with both cutoff
and resonance under CV: analog, after Mutable's Blades, so a two-control GRU there needs no
stepping at all. Keep its resonance below where it self-oscillates.

**Established while building the rig (2026-10-14):**
- **NAM's simplified trainer (v0.13.0) takes only its own input file.** It recognises
  the file by MD5, or by hashing its opening and closing samples, so the file plays
  unscaled and the **Lev knob sets the drive**.
- **That trainer always builds A2** (its packed WaveNet config), so the snapshot takes go
  straight into the `nam` GUI.
- **`input.wav` (v3.0.0)** is 190 s long, peaks at −0.1 dBFS and averages −22 dBFS RMS
  (−21 over its sounding parts). It is played material with about half its energy below
  200 Hz; to David's ear (2026-10-19) "a guitar riff sped up and then repitched, veering
  into synth rather than guitar territory". Its validation passage, which opens and
  closes the file, is short plucked fragments around about 4 s of silence, its sounding
  parts 9 dB quieter than the training section. A sustained Eurorack oscillator drives
  the Wasp harder and more steadily, which is why the sweep takes add oscillator
  material from 0.5 to 9 V peak.
- **`nam-full` trains on any paired audio.** That is a later route to an A2 trained on the
  sweep material, using the same packed config.
- **The GRU trainer is phase 4's own PyTorch script, `tools/train_gru.py`.** It reads
  `captures/wasp/<session>/gru/`, where each take's `.npz` carries `res` and `mix`
  alongside the audio and the cutoff CV.

**Status, 2026-10-18: the LP series is recorded; HP is next.** All 11 Res marks at Frq 0,
with the A2 snapshots in `lp-res6` and the two Res 6 drift repeats: every take passed,
and NAM's own check passed on all five A2 takes. The drift check found the Wasp still
over the whole series (level within 0.03 dB, the BP resonance peak on the same
frequency at every CV hold); the repeats differ from the first by about −28 dB, all of
it where the Wasp clips hard, from resetting the Res knob by eye, and no more at the end
than in the middle.
The settings are chosen (WASP.md, "Settings chosen on the bench"): Lev 10; LP at Frq 0
with CV 1 from 0 to +10 V, HP at Frq max with −10 to 0 V; every Res mark 0 to 10 with
4 minutes of sweeps each, 5 recorded but held out of training; the comparison session at
Res 6 with snapshots at +2, +3.5, +5, +6.5 and +8 V (LP) and −9, −7, −5, −3 and −1 V (HP,
confirmed by an HP ladder: HP changes over the whole −10 to 0 V range).
What the tour measured: the Frq knob's travel is worth about 13 V on CV 1; the Wasp's
outputs clip at about +5.9/−5.1 V (LP/HP) and ±5.5 V (BP); Lev 5 is about 20 dB below
Lev 10, while the Wasp's hiss stays at about −77 dBFS. **The HP series is recorded too** (Mix and Frq at their clockwise
stops, the same order). Every take passes; NAM's check fails on HP snap5 (−1 V) only
because the output there is 16 dB above the hiss, so it trains with NAM's override. HP
showed a memory of a few minutes: the cutoff shifts slightly with what the Wasp did
before, the same test take matching itself to −24 to −32 dB after a pause but only −15
to −18 dB against one recorded straight after a sweep. Best guess: heat in the cutoff
circuit, which with Frq at max is hottest at the 0 V between takes. Not learnable by
either network; it sets how closely the real Wasp in HP matches itself, and so the
floor for judging them. The dataset lives on the Crucial drive since 2026-10-18;
`captures/wasp` is a link to it.

**First A2 capture, 2026-10-19** (LP, Res 6, +5 V; `tools/train_a2.py`, NAM 0.13.0, 100
epochs, **28.8 min on the MacBook Air's GPU**). On NAM's own validation passage A2-Lite
scores ESR 0.048 and A2-Full 0.016: NAM's "might sound ok". On a held-out synth riff at
the same setting (test_snaps' +5 V hold) both fall away: Lite 0.110, Full 0.18–0.21, and
Full predicts peaks of 6.5 V past the real Wasp's 5.7 V ceiling. Alignment and level
were ruled out. The riff is sustained saw, 3 dB hotter on average than NAM's training
section and about 11 dB hotter than its validation passage's sounding parts: NAM's
material does not drive the Wasp the way a sustained Eurorack oscillator does. That is
the limit of NAM's simplified trainer (only its own input file) and the reason the sweep
takes are full of oscillator material. Listening files are in `lp-res6/a2/listen/`.

**Comparison 1, a pilot (2026-10-19).** Seven recurrent networks (`tools/train_gru.py`,
ESR loss, one seed each) trained on that same take with NAM's own split. On NAM's
passage every GRU from 16 units up scored 0.0010–0.0015, against A2-Lite's 0.048. On the
held-out riff, final checkpoints: GRU-16 0.050, LSTM-24 0.063, GRU-32 0.075, GRU-24
0.091, LSTM-16 0.099, A2-Lite 0.105, GRU-8 0.255. Those riff scores swing with the
checkpoint chosen (GRU-24: 0.047 at epoch 83, 0.085 at 108), because selection runs on
NAM's passage, which is nothing like the riff. **Informal listening: David's family could
not tell the real Wasp from GRU-16 on the riff**, so ESR overstates what is audible on
this material. David's verdict on the method as a whole: "badly designed from the
start". The fixed-cutoff sessions (`lp-res6-fixed`, `hp-res6-fixed`, made for a
like-for-like A2 comparison on synth material) are shelved: A2 has no cutoff input, and
the playable GRU is the goal.

**The playable GRUs, LP first (designed with David, 2026-10-19).** The split is fixed by
the recordings, before any training (`train_gru.py --series`):

| Network | Inputs | Trained on | Validated on (picks the epoch) | Tested on (scored once) |
|---|---|---|---|---|
| **Cutoff and Res**, GRU-16 and GRU-24, 3 seeds each | audio, cutoff CV, Res dial | the sweeps at the ten trained marks | three blocks per sweep take | every `test` take, `test_snaps`, and all of Res 5 |
| **Cutoff only**, GRU-16 and GRU-24, 2 seeds each | audio, cutoff CV | Res 6: its sweeps and the five NAM-file takes | three blocks per sweep take | Res 6's `test` takes and `test_snaps` |

- **Validation blocks:** at the beginning, middle and end of every sweep take, so a Wasp
  that lags or drifts over a take is validated across it (David's point: not the last
  20%). Each is 1 s of unscored run-in, then 8 s scored; no training segment touches
  one. About 31 minutes train, 8 validate.
- **Res 5 is the only held-out setting:** never trained or validated on, tested whole.
- **The same procedure for every run:** Wright et al.'s training shape with a plain ESR loss (not their pre-emphasised one, nor NAM's; see "NAM's trainer, reviewed") as in the
  pilot, selection on pooled validation ESR, seeds varying the start and the segments,
  and the gradient norm clipped at 10. The first launch had no clipping, and four runs of
  ten blew up: train and validation ESR jumping together in one epoch (cutoff GRU-24
  seed 1 from 0.056 to 1.15), then early stopping before they recovered. Rerun unclipped
  with the same seeds (they retrace exactly), the gradient's median was about 2, its
  99th percentile 8 with Res as an input and about 20 without, and the cutoff-only runs
  hit spikes of 131 and 328; the NAM-file takes with their quiet passages are half of
  their training. A limit of 10 clips about 3% of steps. Aborted runs and the
  measurement are kept in `gru_runs/_aborted_noclip_2026-10-19` and `_grad_measure`.
  Respawning only the runs that failed was ruled out: the same seed repeats the blow-up,
  and a new one is choosing seeds by result.
- Ten runs side by side, one thread each, on the Mac mini (Apple M4, 16 GB), with the
  Crucial drive attached there: `tools/train_lp_playable.sh`. Takes are memory-mapped,
  so the ten share one copy of the data in memory.

**Results, all ten runs (2026-10-19, gradient clipped at 10).** Test ESR on the `test`
take, mean of the seeds (range in brackets):

| Res | 0 | 1 | 2 | 3 | 4 | **5 (held out)** | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Cutoff + Res GRU-24 (3 seeds) | 0.015 | 0.014 | 0.009 | 0.012 | 0.023 | **0.037** (0.033–0.042) | 0.057 | 0.10 | 0.25 | 0.49 | 0.67 |
| Cutoff + Res GRU-16 (3 seeds) | 0.014 | 0.014 | 0.009 | 0.011 | 0.020 | **0.031** (0.020–0.037) | 0.067 | 0.17 | 0.30 | 0.51 | 0.72 |
| Cutoff only GRU-24 (2 seeds) | | | | | | | **0.012** (0.0117–0.0125) | | | | |
| Cutoff only GRU-16 (2 seeds) | | | | | | | 0.017 (0.016–0.019) | | | | |

Validation ESR (pooled, all marks): cutoff + Res GRU-24 0.148 (0.143–0.153), GRU-16
0.157 (0.155–0.159); seeds agree closely. On the pilot's own 4 s riff (Res 6, +5 V) the
best cutoff-only GRU-24 scores 0.0082, against the pilot's best GRU 0.050 and A2-Lite
0.105: training on synth sweeps was what the pilot lacked. Held-out Res 5 sits between
its neighbours, so one network fills the gap in resonance (comparison 4, at Res 5). At
Res 6 the cutoff-only network is about 5 times better than cutoff + Res, but it had six
times the Res 6 training material (the NAM-file takes), so that is not a clean cost of
the second knob. Training: gradients still reached 10¹⁸ and twice infinity; clipping
held, but one run (cutoff + Res GRU-24 seed 1) went to NaN at epoch 113, two after its
best. Skipping any step whose gradient is not finite is the fix for the next batch.
David's ears: the Res 6 cutoff examples are
"stunning"; Res 8–10 is "just lacking in wildness, chaotic, randomness". Likely a property
of the ESR loss as much as the network: where the Wasp is unpredictable, the
error-minimising output is the average of the possibilities, which is tamer than any of
them. Options recorded for later: a spectral or adversarial loss, a noise input, FiLM
conditioning (NAM 0.13's WaveNet has FiLM layers and a condition input; its data pipeline
is mono only), a hypernetwork, or the grey-box filter. Listening page: claude.ai artifact
`8fizKnpeRxyMEaVrd73Ej9`.

**The GRU port to the Daisy (started 2026-10-19).** Step 1: `nam/gru_runtime.h`, the
engine (PyTorch's nn.GRU plus the dense output, portable C++, sizes from the file), the
`.grnb` card file (`tools/grnb.py`, `tools/gru_export.py`) and the desktop check
(`tools/gru_host.cpp`, `tools/gru_check.py`). The C++ output matches PyTorch to an ESR
of 10⁻⁹–10⁻¹¹ at Res 2 and 6 with the fast table activations; at Res 10 to 7 × 10⁻⁵,
the network amplifying rounding there; the score against the real Wasp is unchanged to
four places everywhere. Step 2: a test firmware, `make GRU_TEST=1` (`src/gru_test.cpp`),
with the cutoff + Res GRU-24 and the cutoff-only GRU-24 compiled in. Controls, agreed
with David: Res on CV_3 (+ CV_7), cutoff offset 0–10 V on the CV_4 knob plus CV in
volts on the CV_8 jack, trim and mix as in Mirth, B7 steps between the networks. Built
(DTCMRAM 59%); CPU on the module not yet measured. The weights header
(`nam/gru_test_nets.h`, from `tools/gru_to_header.py`) is gitignored until David decides
to publish the Wasp captures. Step 3, merged into Mirth (`b5070b9`): a FILTERS bank of
`.grnb` networks off the card, and David's knob layout for every bank (1–4 = patch, mix,
drive, steer in AMPS and NOT-AMPS; patch, mix, cutoff, resonance in FILTERS; B7 held
cycles the three banks). The engine is capped at GRU-24 in Mirth, with a 2,049-point tanh
table; Mirth's DTCMRAM is at 77%. The test firmware took the same FILTERS layout. Open:
how Mirth Lite reaches FILTERS (its toggle has two positions; answered 2026-10-21, B7's
hold); level compensation for
DRIVE; the patch.init's input full scale, so the filters can map volts to volts. Card
files for the Wasp: `captures/wasp_mirth/f1_WASP_LP.grnb` (cutoff + Res) and
`f2_WASP_LP_R6.grnb` (cutoff only). Not yet run on hardware.

**On the module (2026-10-19): the cutoff plays, Res does not.** Mirth with the GRU-16
cutoff + Res network: 61% CPU, the cutoff "works perfectly", but turning Res does nothing
audible (the screen's R follows the pot). GRU-24 at 99%: too slow in the first engine. Not
the firmware and not a cut-down model: the card file through the C++ engine matches
PyTorch, and moving only the Res input changes the output by ESR 0.3–0.8. But measured
as band levels during the test riff's cutoff holds, the networks' Res response is weak or
wrong: at 6 V, real Res 6→10 is +8 to +18 dB across the spectrum, GRU-16 −2 to −4 dB;
at 2 V the real Wasp does not change with Res at all, GRU-24 moves its highs by ±5 to
±16 dB. ESR counts phase that the ear ignores, and ESR-trained networks can keep ESR low
with an averaged Wasp; nothing in the evaluation checked that the control does what the
real control does. Proposed: a control-response test (band levels per Res and cutoff hold
against the real Wasp) in the evaluation, and a spectral term in the loss. Until then the
cutoff-only networks, faithful at their one Res, are the playable ones.

**NAM's trainer, reviewed (2026-10-19, from the installed v0.13.0 source).** Done after
the module showed Res doing nothing; earlier only its A2 config had been read, and its
loss was misdescribed here as ESR. What it does, against `train_gru.py`:

| | NAM 0.13.0 (A2 config, `lightning_module.py`, `data.py`, `recurrent.py`) | `train_gru.py` |
|---|---|---|
| Training loss | **MSE** (weight 1) on an output **normalised to −18 dBFS RMS**, plus **MRSTFT** (auraloss: FFT 512/1024/2048, spectral convergence + log magnitude) at 0.0005, which at that level pulls about as hard as the MSE does at ESR ~0.05 | plain ESR per 2,048-sample chunk |
| ESR | validation only, to keep the best epoch; the code warns that ESR on minibatches misbehaves when a target is near silent | the training loss: the 10¹⁸ gradients and the NaN |
| Optimiser steps | one per batch of whole segments (truncation inside the segment) | one per 2,048-sample chunk |
| Schedule | Adam 0.004, weight decay 3e-7, ×0.994 per epoch, fixed epochs; an epoch is one pass over every segment | Adam 0.005, halved on plateau, early stopping; an epoch is 320 s drawn at random |
| Gradient clipping | none (MSE does not explode) | at 10 |
| Recurrent start | LSTM forget-gate bias +2 ("up the remembering"), learned initial state | PyTorch defaults, zero state |
| Knob inputs | the LSTM takes extra input dimensions for parametric models ("catnet") | the same idea |
| Pre-emphasis, DC term | available, off for A2; Wright et al. (MDPI 2020, cited in NAM's code) pre-emphasise with 0.95 | none |
| Seeds | one, 0 | three per design |

What it says about the playable GRUs: the training loss rewarded getting each sample's
waveform right and nothing else, so an averaged Wasp scored well and Res's spectral effect
went unlearned; and ESR as a per-chunk loss is the likely cause of the exploding
gradients. Candidate changes, for David's decision: MSE on a normalised output plus
MRSTFT, ESR kept for selection and reporting; one optimiser step per batch; the GRU's
update-gate bias started towards keeping its history; and the control-response test in
the evaluation whatever else changes.

**The second LP recipe, launched 2026-10-19 22:35** (`tools/train_lp_v2.sh`, approved by
David): MSE on a −18 dBFS-normalised target plus NAM's MRSTFT at 0.0005, one step per
batch, Adam 0.004 ×0.999 an epoch over a fixed 300 epochs, ESR picks the epoch, clip 10
with non-finite steps skipped; data, splits and seeds as the first recipe (the baseline).
GRU-16 and GRU-24 (update gate +1), seeds 1–3, on the Mac mini from the Crucial drive
(`gru_runs/lp2_gru*`); NAM's WaveNet in A2-Lite's layout with audio, cutoff and Res as
conditioning and FiLM before each activation (2,566 parameters), seeds 1–3 one after
another on the laptop's GPU, from a local copy (`~/wasp_lp`, runs `lp2_wn_s*`; copy them
to the drive afterwards). All four changes go in together, so a win does not say which
change made it. Each batch ends with `control_response.py`, whose first version averages
over every Res mark, hold and band and so rates the first GRUs near right (Res movement
0.9–1.7) although on the module their Res was inaudible: to be sharpened towards the cells
where the real Wasp changes audibly, and the direction of change, before it is believed.

**Overnight results (2026-10-20), with the control test sharpened to the audible cells**
(`control_response.py`, `711dfff`; cells where the real Wasp moves 3 dB or more):
the FiLM WaveNet (A2-Lite size) learned neither control well (Res: right way 0.08–0.30,
amount 0.02–0.06; cutoff error 10–13 dB, amount 0.5–0.6; validation ESR 0.225). The
second-recipe GRU-16s made the cutoff more faithful (error 3.7–5.2 dB against 5.5–6.8)
and lost Res almost entirely (amount 0.04–0.13, right way 0.30–0.49, against 0.36–0.68
and 0.59–0.78 for the first recipe). Res fed in as a third input number has now failed
three ways; the hypernetwork or the grey-box filter is next. The test's cutoff "spurious"
figure (0.73–0.87 for every network) is bands at the noise floor and needs a level gate.

**How much the Wasp varies from itself (2026-10-20, WASP.md step 7).** The `test` take at
Res 6–10, three times each (two back to back, the third after turning Res off and back),
against itself and against the 18 October sessions; full-band ESR after removing any
sub-sample offset, and band levels during the cutoff holds:

| Res | back to back | after knob reset | vs 18 Oct | first-recipe networks | spectrum, take to take |
|---|---|---|---|---|---|
| 6 | 0.0003 | 0.010 | 0.0006 | 0.05–0.09 | within 0.1 dB |
| 7 | 0.0015 | 0.013 | 0.030 | 0.09–0.21 | within 0.3 dB |
| 8 | 0.073 | 0.065 | 0.072 | 0.24–0.32 | within 0.1 dB |
| 9 | 0.39 | 0.59 | 0.73 | 0.47–0.59 | within 0.2 dB |
| 10 | 0.87 | 0.91 | 0.92 | 0.66–0.79 | within 0.5 dB |

Read by the rule fixed beforehand: at Res 6–7 the Wasp repeats itself 15–100 times better
than the networks match it, so they are short of something. From Res 8 its waveform is
chaotic, a fresh realisation every take, while its spectrum holds within a fraction of a
dB: sample matching stops meaning anything, and at Res 10 the networks score better than
the Wasp matches itself only by predicting the average, which is the tameness David heard.
More data cannot fix that; training must reward the spectrum (and its distribution), not
the waveform. Setting Res by eye costs ~0.01 ESR at 6–7, nearly all phase, and nothing
measurable above: half-marks are practical. Takes: `lp-res<N>-rep1..3` on the drive.

**The GRU engine on the module, measured (2026-10-20).** Mirth now shows its build's commit
on the splash and, on a filter, the network's cycles per sample timed around it (the
budget is 10,000). The first engine: GRU-16 61%, GRU-24 99% and glitching. The repacked
engine (one history load feeding three gates, unrolled; `9ae4047`): GRU-16 6,116 cycles
(62%), cutoff-only GRU-16 6,019, GRU-24 10,330, over budget. It cut instructions about in
half on paper and barely moved the clock: about 1.7 cycles an instruction. A rearranged
engine (two units at once, the gate functions in batches; `67615a6`) measured slower
(GRU-24 10,920 and 11,660) and was reverted (`e3c811e`). Libdaisy's Boost is already on, at
480 MHz, the H750's rated maximum. So GRU-16 is the Daisy's size for now; GRU-24 waits for
a build that times each part of the step on the module, or for the Teensy's faster core.
No more speed rewrites from instruction counts alone.

**The hypernetwork arm, launched 2026-10-20 07:30 on the mini** (`tools/train_lp_hyper.sh`,
David's option (a)): every weight a smooth curve in Res (w0 + P1 w1 + P2 w2, Legendre in
the dial scaled to −1..1), Res no longer an input, each batch from one Res mark; otherwise
the first recipe exactly, so only the architecture changes against `lp_cr_*`. GRU-16 and
GRU-24 (2,931 and 6,123 training parameters), seeds 1–3; the control test runs after.

**The hypernetwork arm: no better (finished 2026-10-20 10:48).** Validation ESR 0.166–0.282,
against the baseline's 0.143–0.159. Three of the six runs stopped early (epochs 28–46) at
0.25–0.28 and failed (held-out Res 5: 0.26–0.32). The other three (epochs 118–168) tie the
baseline on held-out Res 5 (0.033–0.050 against 0.032–0.045) and on the test take at Res
0–8, and lose at Res 9–10 (0.52–0.97 against 0.47–0.79). On the control test, the deciding
measure, all six lose. Medians against `lp_cr_*`:

| Res response (audible cells) | hypernetwork | baseline |
|---|---|---|
| right way | 0.54 | 0.72 |
| amount | 0.28 | 0.48 |
| error | 6.5 dB | 4.9 dB |
| spurious movement in calm cells | 0.25 | 0.11 |

The cutoff response is unchanged (right way 0.92–0.99). Making every weight a curve in Res
is not what was missing. One-Res batches and three times the training parameters may also
have made it harder to train, as the three short runs suggest. Not pursued further without
a new design.

**What the Wasp's randomness is (2026-10-20).** Four recordings of identical input at each of
Res 6–10 (the three repeat takes and 18 October's): their average is the repeatable part,
each take minus it the random part. In absolute terms the random part peaks at the cutoff
and moves with it (2 and 4 V holds: under 200 Hz; 6 V: 500–1,000 Hz; 8 V: 2–4 kHz; 10 V:
4–8 kHz; fit f ≈ 2^(3.1–3.5 + 0.91–0.96 V) Hz). As a share of the output it is everything
at and above the cutoff: at Res 9–10, −42 to −24 dB of the output two to six octaves below
it, −13 to −5 dB within an octave below, −5 to −1.5 dB above, where it is most of what
there is. An earlier reading here, narrowband noise centred on the cutoff, was wrong: a
layer built that way was 10 dB off in band levels. Its level barely follows the input's
(r 0.05–0.30) and moderately the output's (0.23–0.60): the resonance near
self-oscillation ringing with a fresh phase and amplitude every take.

Across the dial (`tools/wasp_res_ladder.py`, the `test` take 8–24 s, dB of the output):

| Res | whole clip | 2→4 V glide | 4 V hold | 6 V hold | 8 V hold | 10 V hold |
|---|---|---|---|---|---|---|
| 6 | −27.6 | −25.4 | −23.5 | −39.0 | −41.8 | −42.5 |
| 7 | −22.6 | −19.9 | −19.0 | −35.3 | −38.4 | −41.6 |
| 8 | −16.9 | −12.5 | −17.7 | −25.3 | −37.8 | −41.5 |
| 9 | −7.0 | −6.9 | −5.0 | −7.0 | −14.8 | −19.8 |
| 10 | −4.7 | −4.9 | −3.4 | −3.6 | −9.4 | −22.2 |

It climbs steadily from 6 to 8, first where the cutoff is low, then jumps at 9, reaching
every cutoff up to 6 V; the 2 V hold stays near −30 dB at every mark. The low end does not
move with Res (50–200 Hz at the 10 V hold within 0.1 dB of Res 0 at every mark, as a
state-variable filter should); the level rises 6 dB RMS from Res 0 to 10 instead. David's
question (2026-10-20): Res above 6 may be out of the usable range anyway; the ladder is on
the listening page for him to judge by ear before anything is narrowed.

**Faking the randomness on top (2026-10-20, offline, `tools/wasp_noise_layer.py`).** The
networks learn the repeatable part, so the random part can be layered on rather than
learned. This is the deterministic + stochastic split of Serra's Spectral Modeling
Synthesis (1989–90) and of DDSP's harmonic + filtered noise (2020), with a network as the
deterministic part. It is needed because a network trained to match the waveform learns
the average. Scored by band gap: mean |dB| between a clip's octave-band levels and the four
takes' average at the 4–10 V holds (two real takes: 0.12–0.28 dB, flattered, being two of
the four).

Round 1 drew its bank and table from the clip it was judged on (in-sample). David's
verdict on it: the real takes sound different at each CV step, smudged and muffled
(4 V), pinging resonant mids (6 V), then grainy (8–10 V). The random-phase layer best
brings out the harmonics at the 6 V step. The granular EQ is best at higher CV and best
overall: very good at Res 9, with audible digital artefacts at Res 10, on both the
average and the network. Measured at Res 10, its gains hit the ±12 dB limit more often
(4.2% against 2.9%) and move faster (1.3–2.2 dB per 10.7 ms frame against 1.0–1.7). The
roughest spot is at the cutoff at 4 V: 2.2 dB frame to frame and 2.4 dB band to band.

Round 2 (`tools/wasp_noise_layer.py layers`) learns everything from 24–56 s of the
take, a slow sweep 0 → 10 → 0 V, and is judged on 8–24 s. Band gap, mean of seeds 1–3
(spread up to ±0.2 dB):

| | Res 9 | Res 10 |
|---|---|---|
| repeatable part (the four takes' average) | 1.85 | 2.31 |
| + g1, granular EQ as in round 1 | 0.76 | 0.92 |
| + g2, gentle (16-frame grains, ±9 dB, quarter-octave smoothing, grains from within 1 V) | 0.83 | 0.95 |
| + rp, random-phase layer | 0.94 | 1.15 |
| + both (rp, then g2) | 1.28 | 1.75 |
| + noise shaped to the clip's own random part (ceiling, in-sample) | 0.20 | 0.18 |
| GRU-24 cutoff + Res (`lp_cr_gru24_s2`, Mirth's f1) | 2.54 | 4.92 |
| + g1 | 2.32 | 4.27 |
| + g2 | 2.19 | 4.32 |
| + rp | 2.50 | 4.35 |
| + both | 2.51 | 3.93 |

Learned out of sample, the granular EQ scores as well as in round 1 (better, in fact):
the leak did not flatter it. g1 and g2 tie on the numbers. Stacking both overshoots on
the average, too much added energy.

David on round 2: the gentle g2 cured the digital artefact. It keeps a little of the real
Wasp's warble at the 6 V step, less pronounced; to him that warble is self-oscillation and
not that musical, a bit over the top. The random-phase layer was weaker than in round 1,
the pinging muffled, with some digital noise at Res 10. Its sweep-learned table is within
2–3 dB of a clip-learned one, with the same shape, so the cause is unexplained; parked.

Round 3 (`spectrum` mode) puts the granular EQ's character on one setting s: grains
8·2^s frames, limit ±(12 − 3s) dB, a Gaussian across bands of 0.816·s twelfth-octave
bands; s 0 is round 1's settings, s 1 g2's. Every step draws grains from within 1 V. On
the average, gain change at and above the cutoff falls steadily, at Res 10 1.39 → 1.29 →
1.04 → 0.96 → 0.76 dB per frame for s 0, 0.5, 1, 1.5, 2, while band gaps barely
separate (0.76–1.23 dB across marks and steps). **David chose s 0.5** (~117 ms grains,
±10.5 dB, eighth-octave smoothing), **with no depth control** (2026-10-20).

The chosen setting on the network (`chosen` mode, marks 6–10; band gap):

| Res | average → + s 0.5 | GRU-24 → + s 0.5 |
|---|---|---|
| 6 | 0.43 → 1.20 | 0.99 → 2.22 |
| 7 | 0.43 → 1.05 | 1.10 → 2.20 |
| 8 | 0.31 → 1.45 | 1.49 → 2.24 |
| 9 | 1.85 → 1.08 | 2.54 → 2.32 |
| 10 | 2.31 → 1.23 | 4.92 → 4.17 |

At Res 6–8 the layer makes things worse. There the real takes sit on the average (0.0 dB)
in every band that carries energy, while the layer lifts those same bands by 0.5–2 dB
(Res 8, 8 V: +2.1 dB at 500 Hz, +2.2 dB at 2 kHz, bands 16–21 dB below the loudest). Its
bank moves 0.8 dB on average below the cutoff even at Res 6, where the random part is
−40 dB. Two explanations tested and rejected:
1. The sweep is noisier than the held steps it is applied to. No: the random share in the
   sweep, at the same CV, is within 1–2 dB of the holds at every mark (except Res 8 at
   4 V: −11.8 against −17.7 dB).
2. Twelfth-octave bands between the riff's harmonics hold almost no energy and swing.
   Smoothing powers over 5 bands before the ratio made it worse (bank below the cutoff
   0.81 → 1.35 dB at Res 6; band gaps up about 0.2 dB at every mark).

So the cause is open. The same spurious lift is probably part of what is heard at Res 9–10
(Res 9, 6 V: +1.6 dB at 200 Hz, where the real take moves −0.1). The MP3s for every round
are in eight4awish_private `mirth/listening/2026-10-20-wasp/`.

**Decision (David, 2026-10-20): phase it in.** The layer runs only where the Wasp is
random, by Res, with no knob (`phase()` in the tool). Its gains in dB scale from 0 at
Res 8 to 1 at Res 9, linear between. From Res 9 to 10 each grain draws from the Res 10
bank with probability Res − 9, otherwise from the Res 9 bank: a choice per grain, since
blending two independent grains would be less random than either. At the marks: Res 6–8
play the network alone, and Res 9–10 are unchanged (checked: band gaps as above). For the
module, the granular EQ analyses nothing; its gains come from the bank, the CV and the
cutoff. So it need not be an STFT with 43 ms of latency: a time-varying graphic EQ (a
biquad per band) would add none, at coarser bands. Which way it goes is still to be
decided.

**The layer on the module (2026-10-20).** David asked for GRU-16 + granular on the card,
and the GRU-24s off it. Built as heard: the STFT version, since that is what he chose by
ear and what can be checked against the listening tool; the latency-free graphic EQ is
the follow-up if the delay bothers him.
- `nam/gran_layer.h`: 2,048-point frames every 512 samples, a plain radix-2 FFT (1,024
  complex points, packed real) so the desktop and the Patch SM run identical code. Grains
  as the tool's s 0.5, the phase-in as `phase()`, xorshift32 random numbers. Two
  simplifications against the offline tool: a grain is drawn with the CV at its start,
  not its centre, and grains never run past the sweep's end. The dry side is delayed to
  match (also when bypassed, so A/B keeps its timing).
- **First build (`5f8d8b0`), on the bench:** FLT1 (GRU-16 cutoff + Res) 62%, 6,119 cycles,
  fine to play, a bit tame, nice at F 8 and R 9.9; FLT2 (cutoff only, Res 6) 61%, 6,012,
  very boring next to FLT1; LFOs into F and R move both nicely. FLT3 (with the layer)
  72% on average but its worst block 10,990–11,043 cycles a sample, over the 10,000
  budget: glitching badly, worse from R 8.0 where the gains start, the layer audible
  between the glitches. That build did a frame's work in three blocks, a third each:
  about 235,000 cycles a block, 2.7 times the estimate (about 39 cycles a butterfly,
  its working arrays in RAM_D2). An estimate from instruction counts again, which this
  brief says not to trust.
- **Second build:** the frame's work in ten slices (a new frame comes every 10.67
  blocks): two or three FFT passes a block, the gains in two. The FFT's working arrays
  and twiddles (12 KB) moved to DTCMRAM (now 86.6%, which leaves the stack, growing down
  from its top, about 17 KB); the rest of the engine (82 KB) stays in RAM_D2. Expected
  from FLT3's 72% average: a worst block of about 7,200–7,500 cycles. Latency kDelay =
  2,048 + 11 × 48 = 2,576 samples (53.7 ms). C++ against Python unchanged (ESR 1e-12).
  Card order changed at David's request so the two cut + Res networks sit together:
  FLT1 cutoff only, FLT2 cutoff + Res, FLT3 the same with the layer.
- **Second build (`0654694`), on the bench:** FLT1 61%, 5,999; FLT2 62%, 6,119; FLT3 70%,
  7,637, up to about 8,600 once the gains run (above R 8): no more glitches. David:
  above F 8 and R 8.7 "like a strobing effect on top of the signal, almost like noise
  being added rather than grains"; past the point where the layer comes in, at the
  extremes, "almost like hearing grains". FLT2 alone (no layer) has notes that set up "a
  not unpleasant vibration, a bit like resonance" with the knobs: mid-range beating at F
  5.9 R 9.0, deeper rumbling at F 4.2 R 9.0. FLT3 exaggerates these, "perhaps a bit too
  much", and at F 5.4 R 8.9 gives "a reverb type thing", the rumbling "coming from a
  cave". LFOs into F and R: "not unpleasant". Lowering the mix does not change it, so
  it is not the output clipping.
- **Read against the reference** (`wasp_layer.Stream`, which the firmware matches, on
  the riff at its training level, F and R held): FLT3's peaks rise 4–7 dB over FLT2's
  (F 8 R 9.5: −6.3 → +1.0 dBFS; F 8 R 8.7: −6.3 → −2.4; F 6 R 9.5: −6.7 → −0.7) while
  its average level rises 0.3–1.3 dB; the real Wasp's peaks hold at −6.2 dBFS from Res 5
  to 10. Some grains lift the resonance region by up to 10 dB: the exaggerated beating.
  The envelope's fluctuation at 5–25 Hz is no greater with the layer than without. The
  cave fits the frame: gains that change frame to frame smear a ringing resonance over
  each 43 ms window, most audibly where the cutoff is low (about 320 Hz at F 5.4).
  Options, not yet chosen: a peak guard holding the layer's peaks to the network's
  (adds no delay); the latency-free graphic EQ, which has no frames to smear.
- `tools/wasp_layer.py`: builds the banks exactly as the listening tool (slopes 1.008 and
  0.959 octaves per volt, as its fits), packs them into a version-2 `.grnb`'s layer
  section (int8 at 0.1 dB, 1.9 MB for Res 9 and 10), and holds the same algorithm in
  float64. `tools/gran_check.py` runs the header on the desktop (`tools/gran_host.cpp`)
  against it: ESR 5e-13 to 7e-13 on the real layer, and below the phase-in the output is
  the input delayed by exactly kDelay samples (to 1.2e-7).
- The streaming version scores as the offline one (band gap, seeds 1–3): average 1.85 →
  1.04 at Res 9 and 2.31 → 1.15 at Res 10 (offline 1.08 and 1.23); GRU-24 2.54 → 2.31 and
  4.92 → 3.96 (offline 2.32 and 4.17). On the GRU-16 that goes on the card: 3.13 → 3.34
  at Res 9 (slightly worse, within the seeds' spread) and 4.78 → 4.10 at Res 10.
- Mirth: `SwapFilter` reads a version-2 file's layer straight into a 4 MB SDRAM buffer
  (sd_diskio cleans and invalidates whole cache lines, so the offset into a sector does
  not matter) and refuses the file whole if it will not load or parse. On a filter the
  cycles figure is now the worst block, easing back over about a second: for a filter
  without a layer the same number as before, for the layer the one that decides a glitch.
- The card (second build): `f1_WASP_R6_16.grnb` (cutoff only, Res 6, `lp6_c_gru16_s1`),
  `f2_WASP_LP_16.grnb` (cutoff + Res GRU-16, `lp_cr_gru16_s3`) and `f3_WASP_LP_GR.grnb`
  ("WASP LP GR": f2's network plus the layer), so f2 and f3 A/B the layer above Res 8.
  The GRU-24s (f1, f2, f5 before 2026-10-20) are off the card and kept in
  `captures/wasp_mirth/off_card/`.

The granular EQ is David's idea: no added signal, the recorded misbehaviour used to boost
and cut the spectrum. A bank of how takes 2–4 lift and dip against the average, in
twelfth-octave bands per STFT frame (2,048 points, hop 512), limited to ±12 dB and filed by
octaves from that frame's cutoff; grains of 8 frames (~85 ms) drawn from it at random, two
staggered triangular streams, re-mapped to the target's cutoff and applied as magnitude
gains with the target's phase kept. A first version measured the gains per FFT bin and
scored worse than no layer (2.94 and 3.21): near-empty bins made wild ratios. The
random-phase layer is a copy of the output with its phases scrambled, scaled per bin by
how random the Wasp is at that distance from the cutoff. On the repeatable part either
layer closes about half the gap; on the network a fifth or less, because the network's own
band-level error at Res 9–10 is larger than the randomness.

**Wrapped (David, 2026-10-20).** "This is as far as I want to take this." The story, in
his words: to get something more Wasp-like we added some granular, and while it is not a
perfect copy of the Wasp, it has some of its own character, which is audibly interesting.
What stands behind each part: the networks learn the cutoff and Res but only the average
Wasp (the control tests, the Res ladder); above Res 8 the Wasp stops repeating itself
(the repeat takes); the granular layer borrows that take-to-take movement back from the
recordings (three listening rounds, David's s 0.5, phased in from Res 8 to 9); it runs on
the module within budget (FLT3 worst block 7,637 cycles, about 8,600 at full depth),
matching the desktop reference exactly. No further bench tests are needed for that. The
card as left: FLT1 cutoff only (Res 6), FLT2 cutoff + Res, FLT3 the same with the layer,
firmware `0654694`. The recording rig can come down; the Res 6–10 repeat takes exist only
on the Crucial drive. Left open on purpose, none needed for the story: the patch.init's
input full scale (for any "at the Wasp's own level" claim); the peak guard and the
latency-free graphic EQ; why the layer lifts bands at Res 6–8 that the Wasp holds still;
GRU-24 on the Teensy.

**What the Wasp's wildness is: three tests, not yet run** (2026-10-21, after Swords turned
out to repeat everything but the phase of its oscillation, SWORDS.md "Above the
threshold"). Each needs the Res 8–10 repeat takes, on the Crucial drive; `swords_osc.py`
part B is the first test almost as it stands. The layer's bank cannot answer them: it
measures each take against the *averaged waveform*, and averaging cancels whatever has a
random phase, so a ring that repeated in everything but its phase would still read as a
large lift.
1. **Phase only, or the spectrum too?** STFT magnitude ESR and band levels across the four
   takes. If the magnitudes repeat (Swords: ESR under 0.03, within 0.5 dB), the
   wildness is deterministic but for phase, and a grey-box loop just short of the threshold
   could stand in for the granular layer; if they differ by dB frame to frame, it is
   genuinely random and a statistical layer is the right kind of model.
2. **Noise or chaos?** The Wasp cannot self-oscillate, so after a rest it sits still and
   every note starts from the same state. Takes that differ from a note's first
   milliseconds: circuit noise amplified by a nearly undamped ring (a noise source in the
   loop would model it). Takes that start together and part exponentially over the note:
   chaos from the CD4069s (a grey-box with the right nonlinearity gets the character,
   never the waveform).
3. **Is the warble beating?** The ring at the filter's own frequency against the input's
   partial nearest it, beating at their difference: the rate would follow each note and
   the cutoff, and repeat take to take. **David's view: the warble comes from the
   network's geometry. Checked on FLT2's network** (`lp_cr_gru16_s3`,
   `tools/wasp_warble.py`, the same riff at its training level as the real Wasp's Res 9
   take): from 2 to 8 V the network's gain at the resonance wobbles at 3–20 Hz *less* than
   the Wasp's (6.3/8.8, 7.9/10.5, 3.0/5.5, 2.9/2.5 dB rms), at the same main rate at 4 and
   6 V (6.8 and 10.7 Hz): there it learned part of the Wasp's warble, input-driven, rather
   than making its own. At 10 V it wobbles five times the Wasp's (5.2 against 1.0 dB):
   that one is the network's own. Linearised at rest, its main mode tracks the cutoff
   (46, 204, 886, 2,607, 5,141 Hz at 2–10 V) and dies within milliseconds everywhere but
   there: 3.6 ms at Res 9 and 10 V (Q about 58), against 0.4 ms at Res 6. So David is
   right at the top of the range; lower down the warble he heard (F 5.9 and 4.2, R 9.0) is
   the Wasp's, learned weaker. On Mirth's own input level, still unmeasured, the network
   may be off its training ground, where its own behaviour could take over.

**Controls rearranged (David, 2026-10-21).** Bypass gone; B7 short steps to the next patch
in the bank (each bank keeps its place), held 1.5 s the next bank; knobs 1 MIX, 2 FREQ
(STEER on a not-amp), 3 RES, 4 DRIVE in every bank. "Two different drives is confusing!"
settled it by measurement: input level is a drive on the filters too. The Wasp's cutoff +
Res GRU-16 (`lp_cr_gru16_s3`, 110 Hz sine, cutoff 10 V, Res 2): third harmonic −71, −59,
−47, −31, −16 dB at 0.5, 1, 2, 5, 9 V peak, gain −0.7 dB to 2 V then −3.6 dB at 9 V; Res
8 the same shape. So one DRIVE, the same job in every bank; noon is unity, as the filters
had before. Patch by CV is lost. The screen has six rows now: the patch line shows its
place in the bank (`FLT 3/3`), the two knob lines sit two by two as the knobs do. Lite
(David, same day): B7 as Mirth, held for the bank, so the B8 toggle is retired "for now"
and FILTERS reaches Lite; the LED blinks the bank (one to three quick flickers) before
the patch's number, and the 0.6 s re-blink has gone with the toggle.

**Tooling:** `tools/wasp_capture.py`, bench sheet `WASP.md`. It plays and records in one
stream, with a loopback cable for latency. Recordings live in `captures/wasp/`, which is
gitignored. Tested offline against a simulated filter first, then used for both series
on the bench (2026-10-18).

**Done when:** all four comparisons have been heard on the held-out takes, for both
outputs, and the gaps are written down here.

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

**Precision is measured, not assumed** (a quantisation study run against the real engine
compiled natively). A2's weights at 16-bit sit at −63 dB ESR with per-64
scaling; 12-bit reaches −39.8 dB, about where differences stop being obvious; 8-bit is
unusable. Scaling granularity is worth 1.5–2 bits on its own. Weights only — activations
and accumulators are still float in that study, so read it as a veto, not a permit.

That result is what makes A2-**Full** arguable on this board: 13,066 weights and a
204 KB float history come to ~128 KB at 16-bit against 126 KB of BRAM, but ~96 KB at
12-bit, and the 18×18 DSP slices cost the same either way. No M7 can run A2-Full at all
(7× A2-Lite's ~1,660 MAC/sample puts it at 210–427% CPU), so of the Daisy, the Teensy and
the Tiliqua, the FPGA is the only one where the larger architecture is on the table. A
Raspberry Pi CM5 (4 × Cortex-A76, 2.4 GHz) would run it in floating point: a Pi 4 runs two
or three NAM "standard" WaveNets, the same order of size, at once (nostatic.org, February
2025; no Pi 5 figure found). That costs a carrier board (power, codec, CV in) and Linux
audio latency of a few milliseconds, against the FPGA's sample-level timing (2026-10-20).

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
  Confirmed on the bench 2026-10-18 (phase 0, check 1): a held level keeps 100% over
  13 s. With the jumper in its factory position the inputs are AC-coupled, a high-pass
  near 1.6 Hz.
- ~~Does Daisy MultiFX use the bootloader and DTCM?~~ Moot: the MultiFX unit is
  repurposed, not merged into. (For the record, MultiFX is `BOOT_NONE`.)
- ~~Is this unit affected by the shared +5V rail comb seen on the hand-built modules?~~
  No — it is the commercial Electrosmith patch.init.
- ~~Which vactrol low-pass gate, and what are its CV range and modes?~~ My Dual Pingable
  LPG: 0–8V CV through a 100K attenuator, "both" mode only, Strike per channel, DAMP and
  the mode switch not built. Documented in `eurorack_electronics/docs/lpg_*`.
