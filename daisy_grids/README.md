# Sorrow

*"One for sorrow…"*

**Sorrow** is a drum module for the Electrosmith Daisy Patch Init: **3 synthetic drum
voices + an X/Y pattern sequencer**. It is the first in a family of firmwares named
after the nursery rhyme.

Based on the **Grids** drum pattern generator by Émilie Gillet (Mutable Instruments).
*Not affiliated with, or endorsed by, Mutable Instruments or Electrosmith.*

> **Licence: GPL-3.0-or-later.** Mutable's Grids is itself GPL-3.0-or-later (unlike most
> of the `eurorack` sources, which are MIT), so Sorrow inherits copyleft: if you
> distribute Sorrow — source *or* binary, modified or not — you must pass on the
> complete corresponding source under the GPL and may not add further restrictions.
> See [Licence](#license).

## Features

- **3 synthetic drum voices**: Kick, Snare, Hi-Hat using DaisySP
- **Grids pattern generator**: X/Y pattern morphing with density and chaos controls
- **CV modulation inputs**: CV_5-CV_8 modulate pattern parameters (±50% depth)
- **Automatic clock detection**: feed it 24/8/4/2/1 ppqn and it works out the division itself
- **External reset input** for transport sync
- **Simultaneous outputs**: the internal kit and the B5/B6/CV_OUT_1 triggers run
  from the same pattern at the same time, so external voices layer with the internal ones
- **Kit roll on B8**: flip the toggle out and back for a fresh randomized kit
- **Per-drum pan** with stereo output
- **Edit modes** for sound design with 1 beat/sec auto-audition

## Hardware I/O (Daisy Patch.Init)

### Controls

| Control | Function |
|---------|----------|
| **B7** (Momentary) | Cycle through modes (0-3 LED pulses indicate mode) |
| **B8** (Toggle) | Roll a fresh kit — flip out and back; position is meaningless, only the return edge fires |
| **B10** (Gate In 1) | External clock input (expects quarter notes) |
| **B9** (Gate In 2) | Reset input - returns to step 0; clock source unchanged |

### Outputs

| Output | Function |
|--------|----------|
| **Audio L/R** | Stereo mix of all drums (always live) |
| **B5** | Kick trigger out (always live) |
| **B6** | Snare trigger out (always live) |
| **CV_OUT_1** | Hi-Hat trigger out (always live) |
| **CV_OUT_2** | LED indicator (pulse count = mode) |

## Modes

### Mode 0: Pattern (no LED pulse)
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

### Mode 1: Edit Kick (1 LED pulse)
Sound design for kick drum with 1 beat/sec auto-trigger.

| Knob | Parameter |
|------|-----------|
| CV_1 | Frequency (30-150 Hz) |
| CV_2 | Decay (50-550 ms) |
| CV_3 | Pan (left-right) |
| CV_4 | *(unused)* |

### Mode 2: Edit Snare (2 LED pulses)
Sound design for snare drum with 1 beat/sec auto-trigger.

| Knob | Parameter |
|------|-----------|
| CV_1 | Frequency |
| CV_2 | Snappiness (noise/tone mix) |
| CV_3 | Pan (left-right) |
| CV_4 | *(unused)* |

### Mode 3: Edit Hi-Hat (3 LED pulses)
Sound design for hi-hat with 1 beat/sec auto-trigger.

| Knob | Parameter |
|------|-----------|
| CV_1 | Frequency |
| CV_2 | Decay |
| CV_3 | Pan (left-right) |
| CV_4 | *(unused)* |

## Clock Behavior

### Internal Clock (default)
- Runs automatically on startup
- ~120 BPM (8 ticks/sec for 16th notes)

### External Clock
- Connect any clock to B10 — the resolution is **detected automatically**
- First clock pulse switches to external clock
- If the external clock stops for ~4 beats (2 s minimum), the internal clock takes over

A Grids step is a 16th note, the same rate the internal clock runs at, so
internal and external always agree. Sorrow measures the incoming pulse period
and works out what the source is sending:

| Source sends | Pulse period | What Sorrow does |
|---|---|---|
| 24 ppqn (raw MIDI clock) | 14-28 ms | one step per 6 pulses |
| 8 ppqn (32nd notes) | 43-85 ms | one step per 2 pulses |
| 4 ppqn (16th notes) | 85-171 ms | one step per pulse |
| 2 ppqn (8th notes) | 171-341 ms | 2 steps per pulse |
| 1 ppqn (quarter notes) | 341-682 ms | 4 steps per pulse |

Detection assumes the tempo is in the **88-176 BPM** window — each candidate is
at least double the next, so within one octave of tempo their period bands can't
overlap. A new reading has to be seen three times running before the ratio
changes, so jitter can't flip it mid-pattern. Outside that window a sub-quarter
clock can be misread by a factor of two; a quarter-note clock stays correct far
slower, because it lands in the catch-all band. 12 and 16 ppqn are ambiguous
against 8 ppqn and are not candidates.

Dividing and 1:1 are driven by real clock edges, so they cannot drift. Only the
multiplying ratios predict intermediate steps from the last measured period, and
they are only as steady as the incoming clock.

### Bar indicator
On the Pattern page the LED marks the bar: a **long flash on step 0** (the start
of the 32-step, two-bar phrase) and a **short flash on step 16**. With no screen
this is how you confirm the detected ratio is right — if it pulses in time with
the bar you're hearing, the division is correct.
- Reset input (B9) resets the pattern position; it does not change the clock source

### Transport Control
1. Power on → internal clock runs
2. Connect external clock → follows your clock
3. Stop external clock → after ~4 beats (2 s minimum) it falls back to the internal clock
4. Press reset (B9) → pattern returns to step 0, clock source unchanged

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

Output: `build/sorrow.elf`, `build/sorrow.bin`

Sorrow builds `APP_TYPE=BOOT_SRAM` by default: the app is loaded into SRAM by
the Daisy bootloader from QSPI. This is what gives it room to grow — a
`BOOT_NONE` image already filled 77% of the 128 KB internal flash, while the
same build uses 21% of the 480 KB SRAM region.

### One-off: install the Daisy bootloader
Put Patch.Init in DFU mode (hold BOOT while pressing RESET), then:
```bash
make program-boot
```
This overwrites internal flash, so any previous `BOOT_NONE` firmware is
replaced. Afterwards the module always boots the bootloader, which waits ~2 s
for a DFU upload before starting the app.

### Flash: SD card (preferred)
The bootloader scans the card root for the first `.bin` and re-flashes QSPI only
when it differs, so leave the file on the card and replace it to iterate.
```bash
make card                            # copy to the single mounted FAT32 volume
make card SD_VOLUME=/Volumes/DAISY   # or name the volume explicitly
make card-list                       # show candidate volumes
```
Eject the card, put it in the module, power-cycle. The card must be **FAT32**
(not exFAT), and the module's SD slot must be wired for 4-bit data. A single SOS
LED blink means an unusable `.bin` on the card — usually a `BOOT_NONE` build.

**Sharing one card between modules.** The bootloader flashes the first `.bin` it
finds in the card root, so a card carrying another module's firmware would load
that firmware into *this* module. `make card` therefore moves any other root
`.bin` into `_firmware/` on the card — the bootloader only scans the root, so
the file is parked rather than lost. Move it back to the root to re-arm it for
its own module. Stray macOS `._*.bin` companions are removed for the same
reason.

### Flash: DFU
Works during the bootloader's ~2 s grace window after power-up:
```bash
make flash
```

Or with explicit device selection:
```bash
make dfu-list                           # List DFU devices
make flash DFU_SERIAL=<serial>          # Flash specific device
make flash DFU_VIDPID=0483:df11         # Flash by VID:PID
```

### Building for a module without the bootloader
```bash
make APP_TYPE=BOOT_NONE && make flash APP_TYPE=BOOT_NONE
```

## License

- Grids pattern generator port (`grids_port.h`, `grids_port.cpp`, `grids_nodes.cpp`): **GPL-3.0-or-later**
- Main firmware (**Sorrow**): **GPL-3.0-or-later** (due to Grids inclusion)

Mutable Instruments' Grids is published under GPL-3.0-or-later, so this port and the
whole firmware must remain GPL. **If you distribute a Sorrow binary you must also offer
the complete corresponding source under the GPL** — the source for every release is
this repository at the matching tag.

"Grids" and "Mutable Instruments" are marks of their owner and are used here only to
describe this firmware's origin; "Daisy" is a mark of Electrosmith. Sorrow is an
independent community work, not affiliated with or endorsed by either.

## Acknowledgments

- [Mutable Instruments](https://mutable-instruments.net/) - Original Grids module design
- [Electrosmith](https://www.electro-smith.com/) - Daisy platform and DaisySP library
