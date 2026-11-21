# daisy_multiosc

Boot-selectable multi-voice firmware for the hand-built Daisy Patch Init. Hosts
several small voice engines behind one universal panel, so a single labelled
panel works for every engine. Control contract and panel layout:
[../docs/PANEL.md](../docs/PANEL.md); engine roadmap: [../docs/ROADMAP.md](../docs/ROADMAP.md).

## How it works

- At power-on the OLED shows a chooser. Turn the **TUNE** knob to scroll, press
  **B7** to select. (Auto-select when only one engine is registered.)
- In an engine: **B7 short** = cycle (algorithm / model / waveform); **B7 long**
  = toggle **Play ↔ Edit** page. The screen shows the live meaning of MOD 1–3.
- **Hold B7 for 3 s** to go back to the chooser, with no power cycle. The page
  flips at 0.6 s on the way; the engine stops and its outputs go silent. The
  chooser then waits for a fresh press: the hold itself never picks.

## Structure

- Host/framework: [`../common/multiosc_core/`](../common/multiosc_core/)
  (`Engine` interface, boot menu, B7 gestures, OLED legend, soft-takeover).
- Engines live in their own folders and are compiled in via the Makefile. All
  four registered in `main.cpp`, in chooser order:
  - `FmFourOp` — [`../daisy_fm4op/fm4op_voice.{h,cpp}`](../daisy_fm4op/)
  - `IntervalOscVoice` — [`../daisy_interval_osc/intervalosc_voice.{h,cpp}`](../daisy_interval_osc/)
  - `ScannedVoice` — [`../daisy_scanned/scanned_voice.{h,cpp}`](../daisy_scanned/)
  - `WavesEngine` — `waves_engine.h` (plain VCO: sine, triangle, saw, square
    with pulse width; grown from the old SINE test voice)

To add an engine: implement `multiosc::Engine` in its folder, add its source to
`CPP_SOURCES`, and `host.AddEngine(&inst)` in `main.cpp`.

## Calibration

Hold **B7 at power-up** to calibrate V/OCT, before the chooser: the OLED asks for **1 V** into
V/OCT, press B7, then **3 V**, press B7, then shows DONE. A bad capture keeps what was there.
Every engine adds V/OCT to its TUNE note through it (`multiosc::VoctSemitones`); until a
calibration is taken it is the old law exactly, the reading times 60.

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

Hardware-verified on the Daisy Patch Init for **FM4OP**, **INTVL** and **SCAN**.
**WAVES** (2026-10-24) builds and passes the panel check but is **not yet
bench-tested**.

**Hold B7 for 3 s to return to the chooser** works on the module (2026-11-01). That build was
installed from the SD card: a 2 GB FAT32 `DAISY` partition, loaded with `make program-sd`.

**BYTEBEAT was taken out on 2026-10-24.** The Ogham in the rack is the module its
code came from, so the engine duplicated hardware already there. It was never
bench-tested in MultiOsc. `daisy_bytebeat/` was deleted the same day; the port,
with Ogham's MIT notice, is in git history before that commit.

A fifth engine once exposed two panel bugs, both fixed: the boot menu drew its
last entry off the bottom of the 48 px screen (it now scrolls to keep the
highlighted engine visible, for any number of engines), and a title longer than
ten characters underflowed the centring arithmetic and rendered as a wrapped
fragment (long strings now clip at the right edge instead). Possible follow-ups: tune SCAN's spring constants to
taste; converge the standalone `fm4op.cpp` /
`IntervalOsc.cpp` onto their `*_voice` modules (currently duplicated). Future
"different" engines: see [../docs/ROADMAP.md](../docs/ROADMAP.md).

**Joy and Sorrow stay separate firmwares** on their own SD cards, not engines
here: the host runs every engine at 48 kHz (Joy needs 96 kHz, Sorrow 32 kHz),
Joy spends all four knobs
on its voice so it has no TUNE knob, Sorrow's kit roll is on B8, which the OLED
panel lacks, and Sorrow's GPL would cover the whole image.
