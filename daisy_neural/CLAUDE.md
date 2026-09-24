# Neural models on Eurorack (Daisy patch.init) — project brief

Handoff from a planning conversation (September 2026), corrected on 2026-09-24 against
the actual repos. Revised the same day: the conditioned model targets my Dual Pingable
LPG rather than the folder, with that module's as-built details taken from the
`eurorack_electronics` repo. Claude Code loads this file when working in
`daisy_neural/`. The repo-root `CLAUDE.md` still applies.

## Goal

Run neural network audio models on Eurorack hardware, in phases:

1. Run someone else's trained model on my hardware.
2. Measure, then capture, my own Eurorack module (Befaco Chopping Kinky) — the learning
   vehicle, chosen for being simple, DC-coupled and well documented.
3. Add CV control (a conditioned model) on a device that needs one: my Dual Pingable LPG.
4. Train for audio-rate CV modulation, then design my own panel hardware.
5. (Optional) FPGA, only if phases 3–4 produce a measured bottleneck.

**Why two different modules.** A wavefolder is probably gain plus a memoryless curve, so
a measured curve may beat a network on it outright (phase 1.5). A vactrol has slow
memory, which no static curve reproduces. The folder teaches the pipeline; the gate is
where a neural model earns its place. Phase 1.5 decides whether the *folder* needs a
network — it does not decide whether the project continues.

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
  - MultiFX builds `BOOT_NONE`. This firmware will need the Daisy bootloader
    (`BOOT_SRAM` or `BOOT_QSPI`), as Sorrow and Joy already do.
  - The carrier has a **microSD slot** (see `patch_init_schematic.pdf`).
  - **IN_R is normalled to IN_L** on the carrier.
  - CV_OUT is 12-bit, 0–5V. CV_5–CV_8 are ±5V inputs.
  - **Audio input/output coupling is unverified.** The carrier wires the jacks straight
    to the submodule, so it is set inside the Patch SM. Check the Patch SM datasheet or
    measure it (bench check 2) before relying on it.
- **Befaco Chopping Kinky.**
  - Dual wavefolder; channel A symmetric, channel B asymmetric (per the VCV model).
  - Two fold CV inputs per channel, one through an attenuverter.
  - DC-coupled.
  - The Chop output switches between channels (zero-crossing detector or gate).
    Do not capture it.
  - Input level and fold CV range: _read from Befaco's user manual and add here._
  - Schematics are published by Befaco (CC BY-NC-SA) at <https://www.befaco.org/docs/>.
- **Dual Pingable LPG** — the phase 3 capture target, and my own design: 10HP, dual
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
| `VCVRack/Befaco` `src/ChoppingKinky.cpp` | **Phase 1.5 baseline** | Active (2026-09-17). Static, memoryless curve per channel (256-point table), preceded by a gain set by knob + CVs. Oversampled ×4 by default. |

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
2. **Patch.init audio coupling.** Feed a very slow LFO (under 1Hz) into IN_L with
   pass-through firmware; watch whether it holds its level or decays toward zero. Record
   the answer under Hardware.
3. **Chopping Kinky levels.** Input level and fold CV range from Befaco's manual.
4. **Dual LPG noise floor and channel match.** Record both channels quiet, then ping each
   with Strike, noting which vactrols are fitted. It is a hand-built module, so know its
   noise floor before phase 3 asks whether a model matches it — and the two ping
   responses show up front how far apart the channels are.

## Phase 1 — run an existing A2 model on patch.init (START HERE)

**Done when:** a downloaded A2 capture processes guitar audio on patch.init at 48kHz with
no dropouts, and the measured CPU load is recorded here.

1. **Hear it on the Mac first.** Download an **A2** overdrive or fuzz *pedal* capture
   from TONE3000 (no cabinet IR needed) and play it in the NAM plugin.
2. **Set up the build in `daisy_neural/`**, copying the Makefile pattern from
   `daisy_multifx_oled/` (`make/common.mk`, `common/oled_soft_i2c.cpp`).
3. **Lift the engine** from nam-pedal `t3k-pedal` @ `6dc47a4`: `nam_model.c/.h`, keeping
   its `NAM_DTCM` placement. Replace `DaisySeed` with `DaisyPatchSM`. Keep bkshepherd's
   `nam_a2_runtime.h` open alongside as the easier-to-read version of the same maths.
4. **Start with one model compiled in** (fewest moving parts), `BOOT_SRAM`. Move to a
   QSPI model bank (`pack_models.py`, `BOOT_QSPI`) or the microSD slot once it runs.
5. **Audio path:** IN_L → input trim (knob 1) → A2 → output level (knob 2)
   → OUT_L and OUT_R. The button toggles bypass for A/B comparison.
6. **Display (64×48 OLED):** CPU load (libDaisy `CpuLoadMeter`), an input peak meter
   (to set the trim so the signal reaches the model at the level it was trained on), and
   the model name, truncated to fit.
7. **Measure and record:** CPU load, and memory use by region (DTCM, AXI SRAM, D2 SRAM,
   SDRAM, QSPI). Keep hot buffers out of SDRAM.

## Phase 1.5 — measure the folder before modelling it (NEW)

The VCV model — and the one derivative schematic found (`kraakenstuff/KinkyNoChop`,
2019: OTA VCAs driving diode folders) — suggest each channel is **a gain stage followed
by a fixed, memoryless curve.** If the hardware agrees, a measured curve is a better
model than a neural net: near-zero CPU, anti-aliased by standard methods (oversampling
or ADAA), and CV — including audio-rate CV — is just a multiply before the curve.

1. Through the ES-10, send a slow triangle ramp (a few seconds, full input range) through
   each channel at several fold settings; record the output. The input/output plot is
   the transfer curve.
2. Compare with `ChoppingKinky.cpp`.
3. Check for memory: repeat the ramp faster and see whether the curve changes (slew,
   hysteresis). No change → the static model will be hard to beat.
4. Implement the curve on the patch.init with 2–4× oversampling as the baseline.

**Done when:** the static model exists and there is a written answer to "does the folder
have memory?". A "no" retires the folder as a neural target and phase 2 becomes a
like-for-like comparison, not a prerequisite. Phase 3 proceeds either way, on the gate.

## Phase 2 — capture the Chopping Kinky with NAM A2

- Capture each channel separately at gentle, medium and heavy fold settings, with the
  CV inputs unpatched. That gives six `.nam` files.
- Re-amp through the ES-10 at modular levels (around 10Vpp at the folder input), and
  match the input level on playback.
- Use NAM's standard training input **plus oscillator sweeps at several amplitudes** —
  a folder's output depends heavily on input level, and oscillators are its real use.
- Aliasing: the *recording* will not alias (the interface filters its input), but the
  *model* will, because it cannot produce harmonics above 24kHz and folds them back.
  Heavy settings will suffer most. Recording and training at 96kHz helps — but a model
  must run at the rate it was trained at, because a WaveNet's dilations are counted in
  samples, so a 96kHz model played back at 48kHz has its whole time structure halved.
  Running 96kHz on the Daisy doubles the CPU cost, which A2 likely cannot afford, so
  train at 48kHz unless the measurements say otherwise. (Carson et al. on sample-rate
  independence, in the papers below, is the way out if 96kHz training proves necessary.)
- Store models in QSPI (or SD) and switch at runtime, crossfading on switch.
- **Done when:** a blind A/B test against the module is hard to call at gentle and
  medium, **and** the capture is compared blind against the phase 1.5 static model.

## Phase 3 — CV-conditioned model, on the vactrol low-pass gate

The target changes here, deliberately. The folder is very likely gain plus a static
curve, where CV is just a multiply and a network buys nothing. The gate's vactrol has
memory, so the network has something real to learn. Same rig either way: one ADAT channel
of audio, one of CV, sample-aligned.

**The LPG is not a frozen target, and that dictates the order.** Two changes are on its
roadmap: re-ranging the pots (little of DEPTH's and MANUAL's travel does anything
audible) and adding L7 resonance on the reserved spare op-amp half. Both change the
device a capture would describe, so **do the circuit work first and capture afterwards** —
there is no hurry, phases 0 through 2 come first. Two consequences worth holding on to:

- The phase 3 measurements and the re-ranging want the same data. Ping responses and CV
  steps say where the LED current actually does something audible, which is exactly what
  chooses a pot value and taper. Measure before changing anything, then again after, and
  the second set is the training data.
- Resonance makes the model harder, not just different. A feedback path that can approach
  self-oscillation is a known weak spot for black-box RNNs. It is an argument for the
  structure-first approach above: put the known filter topology in as fixed maths and
  leave the network only the nonlinearity.

- **Measure the vactrol first**, exactly as phase 1.5 measures the folder — and Strike
  makes it easy. A ping is a repeatable impulse, so recording the ping response at
  several DEPTH settings gives the vactrol's decay envelope in isolation, with nothing
  else moving. Follow it with CV steps for the attack side. Those time constants say how
  much history the model needs, which sets the GRU size — measure it, don't guess. Set
  MANUAL to minimum so CV and Strike alone drive the LED.
- **Stack:** RTNeural running a small GRU (8–16 units) with a Dense output layer, the
  NeuralSeed pattern: the CV is an extra network input alongside the audio. Use a short
  PyTorch training script I own; NAM's trainer cannot do this.
- **Structure first:** Parker & D'Angelo (DAFx-13) model a vactrol as a slow envelope
  driving a VCA — and this build follows that paper's own Rα placement, so the structure
  comes from the netlist rather than from guesswork. Put the envelope in front of the
  network as fixed maths and let it learn only what is left over: the same trick as
  phase 1.5's "gain then curve", and the idea behind Hayes et al. (NEWT). One envelope
  state per channel, per the series LED chain. It keeps audio-rate CV well-behaved in
  phase 4.
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

## Phase 4 — audio-rate conditioning, then hardware

- Train with fast-moving CV and test the model under audio-rate modulation. No paper
  found conditions on audio-rate CV — the published conditioning work assumes slow
  controls. This is the novel part.
- Audio-rate CV into a vactrol is where this gets interesting: the real vactrol cannot
  follow it, so the model has to learn the lag itself rather than track the control.
- Candidate next targets:
  - two-input models (ring modulation, cross-modulation)
  - the Chopping Kinky under audio-rate fold CV, if phase 2 earned it against the static
    model
- Design panel and interface hardware once the model's needs are known (or move to the
  Alchemy Lab if it is only more CV).

## Phase 5 — FPGA (decision point, not a commitment)

Consider the Tiliqua (ECP5) only if phases 3–4 produce a measured bottleneck:

- oversampling for aliasing control doesn't fit the M7's budget
- block-based CV response is audibly worse than sample-by-sample
- multiple models need to run at once

Check first whether a faster processor would solve it more cheaply.

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

- Is the Patch SM audio input AC- or DC-coupled? (Phase 0, check 2.)
- Does the Chopping Kinky have memory, or is it gain + static curve? (Phase 1.5.)
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
