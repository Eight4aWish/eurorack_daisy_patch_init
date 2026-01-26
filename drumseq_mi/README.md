# drumseq_mi

A Daisy Patch.Init drum module: **3 synthetic drum voices + Grids-style sequencer** inspired by Mutable Instruments Grids.

This project includes a portable port of the Grids drum pattern generator (GPL-3.0), so downstream distribution must comply with GPL terms.

## Features

- **3 synthetic drum voices**: Kick, Snare, Hi-Hat using DaisySP
- **Grids pattern generator**: X/Y pattern morphing with density and chaos controls
- **CV modulation inputs**: CV_5-CV_8 modulate pattern parameters (±50% depth)
- **External clock input** with 4× multiplier (quarter notes → 16th notes)
- **External reset input** for transport sync
- **External trigger outputs** for driving other modules
- **Per-drum pan/volume** with stereo output
- **Edit modes** for sound design with 1 beat/sec auto-audition

## Hardware I/O (Daisy Patch.Init)

### Controls

| Control | Function |
|---------|----------|
| **B7** (Momentary) | Cycle through modes (1-4 LED pulses indicate mode) |
| **B8** (Toggle) | Internal synth (off) / External trigger outputs (on) |
| **B10** (Gate In 1) | External clock input (expects quarter notes) |
| **B9** (Gate In 2) | Reset input - returns to step 0, clears external clock mode |

### Outputs

| Output | Function |
|--------|----------|
| **Audio L/R** | Stereo mix of all drums (internal mode) |
| **B5** | Kick trigger out (external mode) |
| **B6** | Snare trigger out (external mode) |
| **CV_OUT_1** | Hi-Hat trigger out (external mode) |
| **CV_OUT_2** | LED indicator (pulse count = mode) |

## Modes

### Mode 0: Pattern (1 LED pulse)
Grids pattern generator - all 3 drums play sequenced patterns.

| Knob | Parameter |
|------|-----------|
| CV_1 | X - pattern morph horizontal |
| CV_2 | Y - pattern morph vertical |
| CV_3 | Density - trigger density (0-100%) |
| CV_4 | Chaos - pattern randomness |

**CV Modulation Inputs** (Pattern mode only):

| CV Input | Modulates | Range |
|----------|-----------|-------|
| CV_5 | X | ±50% |
| CV_6 | Y | ±50% |
| CV_7 | Density | ±50% |
| CV_8 | Chaos | ±50% |

CV inputs are bipolar (-5V to +5V). At 0V, no modulation is applied. Positive voltage increases the parameter, negative decreases it.

### Mode 1: Edit Kick (2 LED pulses)
Sound design for kick drum with 1 beat/sec auto-trigger.

| Knob | Parameter |
|------|-----------|
| CV_1 | Frequency (50-200 Hz) |
| CV_2 | Decay (50-500ms) |
| CV_3 | Pan (left-right) |
| CV_4 | Volume |

### Mode 2: Edit Snare (3 LED pulses)
Sound design for snare drum with 1 beat/sec auto-trigger.

| Knob | Parameter |
|------|-----------|
| CV_1 | Frequency |
| CV_2 | Snappiness (noise/tone mix) |
| CV_3 | Pan (left-right) |
| CV_4 | Volume |

### Mode 3: Edit Hi-Hat (4 LED pulses)
Sound design for hi-hat with 1 beat/sec auto-trigger.

| Knob | Parameter |
|------|-----------|
| CV_1 | Frequency |
| CV_2 | Decay |
| CV_3 | Pan (left-right) |
| CV_4 | Volume |

## Clock Behavior

### Internal Clock (default)
- Runs automatically on startup
- ~120 BPM (8 ticks/sec for 16th notes)

### External Clock
- Connect quarter-note clock to B10
- First clock pulse latches into external mode
- 4× clock multiplier converts quarter notes to 16th notes for Grids
- Sequencer stops when external clock stops (no fallback)
- Reset input (B9) returns to internal clock mode

### Transport Control
1. Power on → internal clock runs
2. Connect external clock → switches to external, follows your clock
3. Stop external clock → sequencer stops (stays quiet)
4. Press reset (B9) → returns to internal clock mode

## Voices

Three synthetic voices using DaisySP:

- **Kick**: `SyntheticBassDrum` - punchy bass drum with variable frequency and decay
- **Snare**: `SyntheticSnareDrum` - snare with adjustable tone/noise balance
- **Hi-Hat**: `HiHat<>` - metallic hi-hat with frequency and decay control

## Build & Flash

### Prerequisites
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- `dfu-util` for flashing (`brew install dfu-util` on macOS)
- libDaisy and DaisySP (in `deps/daisy/`)

### Build
```bash
make clean && make
```

Output: `build/drumseq_mi.elf`, `build/drumseq_mi.bin`

### Flash
Put Patch.Init in DFU mode (hold BOOT while pressing RESET), then:
```bash
make flash
```

Or with explicit device selection:
```bash
make dfu-list                           # List DFU devices
make flash DFU_SERIAL=<serial>          # Flash specific device
make flash DFU_VIDPID=0483:df11         # Flash by VID:PID
```

## License

- Grids pattern generator port (`grids_port.h`, `grids_port.cpp`, `grids_nodes.cpp`): **GPL-3.0-or-later**
- Main firmware: **GPL-3.0-or-later** (due to Grids inclusion)

## Acknowledgments

- [Mutable Instruments](https://mutable-instruments.net/) - Original Grids module design
- [Electrosmith](https://www.electro-smith.com/) - Daisy platform and DaisySP library
