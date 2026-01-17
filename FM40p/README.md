# FM40p (4-Operator FM Synth) — Daisy Patch SM

A compact 4-operator FM synthesizer for the Daisy Patch SM, implemented in `fm4op.cpp`. It provides multiple FM algorithms (parallel, serial, and feedback), pitch via knob and 1V/oct CV, and simple LED feedback.

## Features
- 4 operators with per-operator frequency ratios.
- Algorithms:
  - Mode 0: Parallel FM (carriers summed, each modulated).
  - Mode 1: Serial/stack FM (modulators chained into a single carrier).
  - Mode 2: Feedback FM (operator self-feedback for harsher tones).
- Pitch: Coarse via knob, fine via `CV_5` (1V/oct).
- LED pulse visualizer tied to the current mode.

## File Layout
- `fm4op.cpp`: Core implementation of operators, algorithms, audio callback, control reads, and LED indication.
- `Makefile`: Project build settings targeting STM32H750 (Patch SM) and linking against LibDaisy/DaisySP.
- `build/`: Compiler outputs (`.d`, `.lst`) generated during build.

## Controls
- Knob 0 (`ADC0`) + `CV_5`: Pitch. `ADC0` spans multiple octaves; `CV_5` is 1V/oct.
- Knob 1 (`ADC1`): FM index / modulation depth.
- Knob 2 (`ADC2`): Algorithm- or timbre-specific parameter (e.g., ratio skew).
- Knob 3 (`ADC3`): Brightness/lowpass or feedback emphasis depending on mode.
- `gate_in_2`: Cycles FM algorithm (0 → 1 → 2).
- LED: Short pulse count equals current mode+1 (1–3).

## Build (macOS)
Prereqs: DaisyToolchain installed and LibDaisy + DaisySP sources available locally.

```zsh
# Point this at a folder containing libDaisy/ and DaisySP/
export DAISY_ROOT="$HOME/Documents/eurorack/deps/daisy"

# Build libraries (if not already built)
make -C "$DAISY_ROOT/libDaisy"
make -C "$DAISY_ROOT/DaisySP"

# Build firmware
cd FM40p
make clean && make VERBOSE=1 DAISY_ROOT="$DAISY_ROOT"
```

## Flash
Put the Patch SM into DFU mode (hold BOOT, tap RESET, release BOOT), then:

```zsh
cd FM40p
make flash DAISY_ROOT="$DAISY_ROOT"
```

## Usage Tips
- Start with low FM index (Knob 1) and raise slowly to avoid harsh aliasing in higher registers.
- Use `Knob 2` to explore different ratio relationships if mapped; small changes yield large timbral differences.
- Feedback mode (2) benefits from lower brightness and careful index settings.

## Notes
- FM can produce wideband spectra; consider placing an external filter downstream in the Eurorack chain if needed.
- If you previously experimented with supersaw or alternative oscillators in this folder, ensure `fm4op.cpp` is restored to its FM version before building.
- Paths and STM32/HAL defines are aligned with LibDaisy; builds may require updating include paths if your library locations differ.