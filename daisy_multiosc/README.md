# daisy_multiosc

Multi-voice firmware for the hand-built Daisy Patch Init with an OLED. Hosts four
small voice engines behind one panel. The control contract, and what every knob
does in each engine: [../docs/PANEL.md](../docs/PANEL.md); engine roadmap:
[../docs/ROADMAP.md](../docs/ROADMAP.md).

## How it works

Redesigned with David on 2026-11-22 to work as his other patch.Init apps do; the
old interface (a TUNE knob, a chooser at power-on, a Play/Edit page) is in git
history before that.

- **It starts in the engine it was left in.** Hold **B7 for 2 s without turning a
  knob** for the next one: FM4OP → INTVL → SCAN → WAVES. Its name shows large.
- **Pot 1 picks** what the engine is making (FM4OP's algorithm, INTVL's mode,
  SCAN's excitation, WAVES' shape). It has no CV: the jack below it is V/OCT.
- **Pots 2–4 shape it**, each with the MOD jack below it.
- **Hold B7 and turn a knob** for the second bank: pot 1 Volume, pot 3 Attack,
  pot 4 Decay (SCAN: Damping). Each knob catches its value before moving it.
- **Tap B7:** INTVL's waveform pair; nothing in the other engines.
- **Pitch** is V/OCT alone, C3 at 0 V. **GATE IN 1** ends the drone: until the
  first gate the voice plays continuously, then attack and decay take over.
- The engine, INTVL's waveform and each engine's second bank are saved, 3 s after
  the last change.

## Structure

- Host/framework: [`../common/multiosc_core/`](../common/multiosc_core/)
  (`Engine` interface and `Controls`, B7 gestures, the shared envelope and volume,
  the saved settings, the OLED legend, soft-takeover).
- Engines live in their own folders and are compiled in via the Makefile. All
  four registered in `main.cpp`, in the order a long hold steps through:
  - `FmFourOp` — [`../daisy_fm4op/fm4op_voice.{h,cpp}`](../daisy_fm4op/)
  - `IntervalOscVoice` — [`../daisy_interval_osc/intervalosc_voice.{h,cpp}`](../daisy_interval_osc/)
  - `ScannedVoice` — [`../daisy_scanned/scanned_voice.{h,cpp}`](../daisy_scanned/)
  - `WavesEngine` — `waves_engine.h` (plain VCO: sine, triangle, saw, square
    with pulse width; grown from the old SINE test voice)

To add an engine: implement `multiosc::Engine` in its folder, add its source to
`CPP_SOURCES`, and `host.AddEngine(&inst)` in `main.cpp`.

## Calibration

Hold **B7 at power-up** to calibrate V/OCT, before the engine starts: the OLED asks for **1 V** into
V/OCT, press B7, then **3 V**, press B7, then shows DONE, with `OCT`: how far the module's input is from
an ideal 1 V/oct, in cents per octave, as on Joy. A capture that doesn't make sense shows FAIL and saves
nothing, so the module keeps what it had.
The host turns V/OCT into the note every engine plays through it; until a module is calibrated it
uses Joy's defaults, converted to libDaisy v9's scaling.

**The calibration is the module's, not the app's** (since 2026-11-21): it is stored once, in its own block of the QSPI flash ([`../common/unit_cal.h`](../common/unit_cal.h)), and Joy, Joy Lite, Wish and MultiOsc all read it. Calibrate a module once with any of them and the others track too, and loading another app from the SD card keeps it, because the bootloader only erases as far as the new program reaches. A calibration an app kept in its own settings before then is carried over the first time it starts. MultiOsc runs from SRAM for this (it was `BOOT_QSPI`, which can't write
the flash it runs from); it uses 22 % of SRAM.

## Build & flash

```sh
make                 # build/daisy_multiosc.bin (APP_TYPE=BOOT_SRAM, so it can save a calibration)
make program-dfu     # flash via DFU during the bootloader grace period
```

Or drag `build/daisy_multiosc.bin` onto the SD card root and power-cycle. See
[../docs/ROADMAP.md](../docs/ROADMAP.md) for the full dev loop.

## Status

**The 2026-11-22 interface is built, not yet flashed or played.** FM4OP's new
DSP ran on the Mac over every algorithm, ratio set, Index and Feedback at their
ends and notes from C1 to C8: every sample finite and inside full scale. The panel
check passes. To try on the module: load it by SD card; a module already
calibrated for Joy keeps its calibration.

Before the redesign, FM4OP, INTVL and SCAN were hardware-verified on the module,
and WAVES (2026-10-24) was never bench-tested.

**BYTEBEAT was taken out on 2026-10-24.** The Ogham in the rack is the module its
code came from, so the engine duplicated hardware already there. It was never
bench-tested in MultiOsc. `daisy_bytebeat/` was deleted the same day; the port,
with Ogham's MIT notice, is in git history before that commit.

A fifth engine once exposed two panel bugs, both fixed: the old boot menu drew its
last entry off the bottom of the 48 px screen, and a title longer than ten
characters underflowed the centring arithmetic and rendered as a wrapped fragment
(long strings now clip at the right edge instead). Possible follow-ups: tune SCAN's spring constants to
taste; converge the standalone `fm4op.cpp` /
`IntervalOsc.cpp` onto their `*_voice` modules (currently duplicated). Future
"different" engines: see [../docs/ROADMAP.md](../docs/ROADMAP.md).

**Joy and Sorrow stay separate firmwares** on their own SD cards, not engines
here: the host runs every engine at 48 kHz (Joy needs 96 kHz, Sorrow 32 kHz),
Sorrow's kit roll is on B8, which the OLED panel lacks, and Sorrow's GPL would
cover the whole image.
