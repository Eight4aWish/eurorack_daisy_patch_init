# drumseq_mi

A Daisy Patch SM drum module concept: **3 modeled drum voices + built-in sequencer** inspired by Mutable Instruments Grids.

This repo already vendors Mutable Instruments sources under `deps/mutable/eurorack/`. This project includes a small portable port of the Grids drum pattern generator (GPL), so downstream distribution must comply with GPL terms.

## High-level behavior

### Modes

The firmware exposes 4 modes (selected by the Patch SM panel button `B7`):

1. **Grids**: 3-lane trigger generator driving the internal drum kit (self-contained).
2. **Kick**: kick voice focus page (auto-audition tick, no external trigger required).
3. **Snare**: snare voice focus page (auto-audition tick).
4. **Hat**: hat voice focus page (auto-audition tick).

There are no external drum trigger inputs in this design, and we are not outputting triggers to other modules.

### Voices (3 drum module)

We implement three independent voices using DaisySP modeled drums:

- **Kick**: `daisysp::AnalogBassDrum`
- **Snare**: `daisysp::SyntheticSnareDrum`
- **Hat**: `daisysp::HiHat<>` (template-based; can later expose alternate metallic noise flavors)

### Model set switching (Shift)

User preference: **Shift toggles between model sets**, not “extended Grids controls”.

- **Shift OFF** (Analog set): `AnalogBassDrum` + `AnalogSnareDrum` + `HiHat<>`
- **Shift ON** (Synthetic set): `SyntheticBassDrum` + `SyntheticSnareDrum` + `HiHat<>`

Hat remains the same “family” (a templated model) in both sets.

### Sequencer

A Grids-like approach:

- Generates triggers for 3 lanes (BD/SD/HH) with **X/Y pattern morph**, **density**, and **randomness/swing**.
- External clock preferred (CV input used as clock); internal clock fallback can be added.

Clock/reset I/O (final):

- `gate_in_1` (B10): **external clock in**
- `gate_in_2` (B9): **reset**

Licensing note: the Grids generator port lives in `include/grids_port.h`, `src/grids_port.cpp`, and the extracted pattern tables `src/grids_nodes.cpp` and is GPL-3.0-or-later.

## Hardware I/O (Daisy Patch SM)

### Audio

- Stereo audio out
- Optional audio in passthrough / sidechain: not required for MVP

### Trigger/Gate inputs

- Two dedicated gate inputs available on Patch SM
- Additional triggers can be derived from CV inputs via ADC thresholding, but MVP targets 3 drums (so 3 triggers max is sufficient)

### Clock

- External clock via `gate_in_1` rising edge (`Trig()`)
- Reset via `gate_in_2` rising edge

### Controls

- 4 knobs (CV_1..CV_4 used as pots)
- Shift switch/button for model set selection
- LED indicates current patch slot and/or edit state

## Control mapping (Mode 1: DrumSeq)

`B8` (Shift) toggles the **drum kit** (analog vs synthetic).

Grids mode controls:

- CV_1: X (pattern morph)
- CV_2: Y (pattern morph)
- CV_3: Density (macro)
- CV_4: Chaos (randomness)

Grids mode CV modulation (inputs may float when unpatched; we apply deadzone + hysteresis):

- CV_5: X mod
- CV_6: Y mod
- CV_7: Density mod
- CV_8: Chaos mod

Override rule: if CV_5..CV_8 are detected as patched, they **override** the corresponding CV_1..CV_4 knob values for that parameter.

LED: the Patch SM user LED is driven via `DaisyPatchSM::SetLed()`; pulse count indicates mode (1..4).

For convenience, the firmware also mirrors this pulse to `CV_OUT_2` (0V/2V) in case you have an external LED wired there.

## Control mapping (Kick/Snare/Hat modes)

Single-voice edit pages auto-trigger internally so changes are audible with nothing patched.

- CV_1..CV_2: per-voice parameters (varies by voice)
- CV_3: pan
- CV_4: level

## MVP acceptance criteria

- Builds as a Patch SM firmware target.
- Runs 3 drum voices with audible output.
- External clock advances an internal step counter.
- Sequencer produces 3 trigger streams and drives the drum voices.
- Shift switches between analog/synthetic model sets.
- LED shows patch slot; optionally indicates shift/model-set.

## Build + flash (smoke test)

The only meaningful “test” without hardware is: **does it compile and produce a .bin/.elf**.

From this folder:

- Build: `make clean && make -j`
- Output artifacts: `build/drumseq_mi.elf`, `build/drumseq_mi.bin`, `build/drumseq_mi.map`

To flash via DFU (Patch SM in DFU mode):

- Flash: `make flash`

If you have more than one DFU-capable device connected (common on dev setups), dfu-util may refuse to flash unless you select the right device.

- List DFU devices: `make dfu-list`
- Flash with explicit serial: `make flash DFU_SERIAL=<serial>`
- Flash with explicit VID:PID (defaults to STM32 DFU): `make flash DFU_VIDPID=0483:df11`

Notes:

- `make flash` uses `dfu-util` and writes to `0x08000000`.
- Some `dfu-util` builds return exit code `74` when using `:leave`; the Makefile treats that case as success.
- If you don’t have `dfu-util` installed on macOS: `brew install dfu-util`.

## Roadmap (post-MVP)

- Add per-drum parameter pages (tone/decay/character) with LED feedback.
- Add internal clock, reset, and swing.
- Add optional 4th derived lane (accent/clap) using CV-as-gate.
- Add per-lane density/chaos scaling options (currently density is shared across BD/SD/HH).

