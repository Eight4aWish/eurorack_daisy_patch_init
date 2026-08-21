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

- **A pool of drum models**, not three fixed voices: 10 DaisySP models across
  kick, snare and hat, spanning synthetic, 808-lineage analog and physical-modelling families
- **Wildness**: one knob setting how far a kit roll may throw — which models are
  eligible, how far their parameters roam, and whether the three slots match or clash
- **Grids pattern generator**: X/Y pattern morphing, master density plus per-part trims, and chaos
- **CV modulation inputs**: CV_5-CV_8 modulate pattern parameters (±50% depth)
- **Automatic clock detection**: feed it 24/8/4/2/1 ppqn and it works out the division itself
- **External reset input** for transport sync
- **Simultaneous outputs**: the internal kit and the B5/B6/CV_OUT_1 triggers run
  from the same pattern at the same time, so external voices layer with the internal ones
- **Kit roll on B8**: flip the toggle out and back for a fresh randomized kit
- **Per-drum pan** with stereo output, spread widening with wildness
- **Bar indicator** on the LED, so the detected clock division can be checked by eye

## Hardware I/O (Daisy Patch.Init)

### Controls

| Control | Function |
|---------|----------|
| **B7** (Momentary) | Cycle pages: Home (no flash) → Kit (1 flash) |
| **B8** (Toggle) | Roll a fresh kit — flip out and back; position is meaningless, only the return edge fires |
| **B10** (Gate In 1) | Clock input — resolution detected automatically |
| **B9** (Gate In 2) | Reset input - returns to step 0; clock source unchanged |

### Outputs

| Output | Function |
|--------|----------|
| **Audio L/R** | Stereo mix of all drums (always live) |
| **B5** | Kick trigger out (always live) |
| **B6** | Snare trigger out (always live) |
| **CV_OUT_1** | Hi-Hat trigger out (always live) |
| **CV_OUT_2** | LED indicator (pulse count = mode) |

## Pages

**Short press cycles pages.** The pages differ in the LED's *character*, not a
flash count: **Home blinks** (marking the bar), **Kit is steady on**. A pulse
count is unreadable against the bar marker — one intermittent flash looks like
any other.

**Hold B7 (~0.8 s) cycles the drum-map bank**, and the module says which one it
switched to. See [Pattern banks](#pattern-banks).

The sequencer keeps running on every page: a settings page borrows the knobs,
not the transport, so you always hear what you're changing.

### Home (no LED flash)

| Knob | Parameter |
|------|-----------|
| CV_1 | X — pattern morph horizontal |
| CV_2 | Y — pattern morph vertical |
| CV_3 | Master density |
| CV_4 | Chaos — pattern randomness |

Home knobs stay **live**, so performance feel is unaffected. On this page the
LED marks the bar — see [Bar indicator](#bar-indicator).

**CV modulation** (Home parameters only):

| CV Input | Modulates | Range |
|----------|-----------|-------|
| CV_5 | X | ±50% |
| CV_6 | Y | ±50% |
| CV_7 | Master density | ±50% |
| CV_8 | Chaos | ±50% |

CV inputs are bipolar (-5V to +5V). At 0V, no modulation is applied.

### Kit (LED steady on)

| Knob | Parameter |
|------|-----------|
| CV_1 | Kick density trim |
| CV_2 | Snare density trim |
| CV_3 | Hi-hat density trim |
| CV_4 | **Wildness** |

Kit knobs use **soft-takeover**: a knob only grabs its parameter once it crosses
the stored value, so arriving on the page never makes anything jump.

**Density trims are additive** around the Home master:

```
density_part = clamp(master + (trim - 0.5))
```

Centred trims behave exactly as a single density did. The master still sweeps
the whole kit as one gesture — and because it's additive, CV_7's modulation of
the master sweeps density while preserving the balance you set between the three
parts.

## Pattern banks

Banks of 25 nodes, cycled by holding B7, each announcing itself:

| Bank | Announced | Source |
|------|-----------|--------|
| 0 | "Original patterns" | Émilie Gillet's Grids map |
| 1 | "Club patterns" | Lakh MIDI Dataset, selected by **rhythmic signature** |
| 2 | "Traditional patterns" | Groove MIDI Dataset — human drummers, rock through jazz |
| 3 | "User patterns" | Optional, generated locally — see [user_bank.h](include/user_bank.h) |

All are 25 × 96 bytes with the same per-lane value distribution, so density,
accent and everything downstream behave identically.

### Selecting by rhythm rather than by genre

The Club bank is the interesting one. There is no openly licensed corpus that is
modern, timed and large — WaivOps has the genres but its JSON carries no onset
times, Pocket Operations has the genres but not a redistributable licence, and
Lakh has scale but is overwhelmingly rock and pop by artist.

So it doesn't use genre labels at all. Of Lakh's 736,402 two-bar patterns,
73,674 are four-on-the-floor with offbeat hats, 42,447 are breakbeat-flavoured
and 29,854 are half-time — whoever happened to play them. Those three make the
corners of the map. It doesn't matter that Genesis played it: a four-to-the-floor
kick under offbeat hats *is* the house vocabulary.

### Your own bank

The best corpus for you is probably one you already own and can't redistribute.
`src/user_bank.cpp` is gitignored and compiled in only when present, so:

```bash
python3 tools/groove_nodes/extract.py ~/your/midi patterns.npy
python3 tools/groove_nodes/som.py
```

gives a fourth bank from your own library. Nothing is redistributed — the output
is 25 centroids, and no source pattern survives in it. See
[tools/groove_nodes/](tools/groove_nodes/).

### Why it speaks

The LED had four jobs on one channel — bar marker, page state, kit-roll
confirmation, bank number — and the bar marker flashes constantly, so everything
else had to shout over it. A bank number blinked in that company is unreadable.

Speech takes state changes off the contested channel. It is deliberately limited
to **rare, deliberate actions**: a bank change is a decision, so it is worth
announcing. Page changes are performative and frequent, and a module that talks
every time you reach for a knob mid-set would be intolerable — so the short
press stays silent.

Generated by [tools/speech/make_speech.py](tools/speech/make_speech.py) —
11 kHz 8-bit, about 15 KB a phrase, and the voice is one line to change.

## Rolling a kit

**Flip B8 out and back.** The toggle's position means nothing; only the return
edge fires, so the double flip is the gesture. A kit is also rolled at power-up,
and the LED blips to confirm each roll.

Rolling is deliberately orthogonal to the pattern: it never touches X, Y,
density, randomness, the step counter or the Grids LFSR. A groove you like
survives any number of rolls, so you can hear the same pattern through a
completely different kit.

### What a roll does

Each of the three slots is filled from a pool of models rather than being one
fixed voice, and **Wildness** controls how far the dice may throw:

- **Which models are eligible.** Every model carries an "exotic" rating; wildness
  raises the ceiling. At zero you get the safe ones, at full everything.
- **How far parameters roam.** Every parameter has a tame range and a wild range,
  and wildness interpolates between them. v1 capped kick dirtiness at 0.40 and
  defaulted it to 0.03, which is most of why the old kit sounded safe; at full
  wildness it now reaches the top.
- **Whether the kit agrees with itself.** One family is picked for the kit, then
  each slot may defect from it with probability equal to wildness. At zero the
  three slots match; at full they're chosen independently, so an analog kick can
  sit under a modal snare and a ring-mod hat.
- **How wide the stereo spread goes**, though never fully hard-panned.

## Voices

Three slots, each drawn from its own pool of DaisySP models:

| Slot | Models |
|------|--------|
| **Kick** | `SyntheticBassDrum` (elec) · `AnalogBassDrum` (analog, 808 lineage) · `ModalVoice` (acoustic) |
| **Snare** | `SyntheticSnareDrum` (elec) · `AnalogSnareDrum` (analog) · `ModalVoice` (acoustic) · `StringVoice` (acoustic, most exotic) |
| **Hi-Hat** | `HiHat<SquareNoise, LinearVCA>` (elec, the 808's six-square cluster) · `HiHat<RingModNoise, SwingVCA>` (analog) · `ModalVoice` (acoustic) |

All are Émilie Gillet's drum and physical-modelling models as ported into
DaisySP, from the same lineage as Peaks and Plaits. Because `HiHat` is a
template, the noise source and VCA are compile-time choices and one class yields
several genuinely different hats.

A newly selected model is re-initialised before it's randomized: an unselected
model is never processed, so its envelope state would otherwise be frozen
wherever it was left and resume as a click.

### Levels and decay

Two things the models don't give you for free:

- **Output level.** Models differ in natural loudness by several times over, so
  a roll could bury a voice in the mix. Each model's peak is measured at boot —
  the cost benchmark already triggers and processes every one — and trimmed
  toward a common level.
- **Decay.** DaisySP's `AnalogSnareDrum` rings for *seconds* at every decay
  setting including zero, and `ModalVoice`/`StringVoice` sustain by design, with
  damping shaping timbre rather than stopping the ring. Models that can't stop
  on their own get an output VCA, so their decay parameter means something. No
  model now rings past 400 ms; several were at 3000 ms.

### Sample rate and CPU budget

Sorrow runs at **32 kHz**, not 48. These models are expensive on the M7: at
48 kHz the cheapest possible kit cost 53% of a sample period and the most
expensive cost 112%, so some kits overran the audio callback. That failure mode
is nastier than it sounds — an overrun starves SysTick, which stops
`System::GetNow()` advancing, which freezes `Switch::Debounce()`, so the module
keeps playing while both switches die.

32 kHz doesn't change the work per sample, but each sample has half again as
long to do it in, taking the cheapest kit to 35% and the most expensive to 75%.
The trade is a 16 kHz Nyquist, which the hat frequency ranges respect.

On top of that the firmware **benchmarks every model on the target at boot** and
won't assemble a kit costing more than 72% of a sample period. Measuring beats
guessing: two desktop benchmarks called `AnalogSnareDrum` cheap when it was the
one model that hung the panel, because host `libm` makes `powf`/`tanf` far
cheaper than they are on a Cortex-M7. Measured total audio load runs about 3%
above the voices alone.

### Diagnostics over USB

`SORROW_LOG_USB` in `src/main.cpp` logs the boot cost table, then a CPU-load
line every two seconds naming the current kit. Connect by USB and read
`/dev/tty.usbmodem*`. Non-blocking, so it costs nothing with nothing attached.

```
=== Sorrow: model cost, pct of one sample period ===
  kick   synth BD       7 pct
  ...
  cheapest possible kit: 35 pct
cpu avg  73 pct  max  73 pct   kit: modal BD / modal SD / square HH
```

## Host checks

```bash
make -C tools/voice_check check
```

Drives the real `drum_voices.cpp` and the real DaisySP models with a PC
compiler. Every check exists because it caught a bug that reading the code did
not — non-finite output, three-second decays, NaN in the render path, and a
wildness control that gated models instead of ramping them in. It deliberately
does *not* check CPU cost; only the target can measure that honestly.

## Clock Behavior

### Internal Clock (default)
- Runs automatically on startup
- ~120 BPM (8 ticks/sec for 16th notes)

### External Clock
- Connect any clock to B10 — the resolution is **detected automatically**
- First clock pulse switches to external clock
- If the external clock stops for ~4 beats (5 s minimum), the internal clock takes over

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
3. Stop external clock → after ~4 beats (5 s minimum) it falls back to the internal clock
4. Press reset (B9) → pattern returns to step 0, clock source unchanged

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
