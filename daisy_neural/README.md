# daisy_neural

Neural audio models on the Daisy Patch.Init — the firmware side of the project
briefed in [CLAUDE.md](CLAUDE.md).

**Status: phase 1 skeleton.** The signal chain, controls, metering and display are
built and the model slot runs as a straight passthrough. No neural engine yet — that
arrives at phase 1 step 3, when the NAM A2 engine is lifted from
[tone-3000/nam-pedal](https://github.com/tone-3000/nam-pedal) branch `t3k-pedal`.

**Builds clean.** Not yet run on hardware — every behaviour below is written but
unobserved, so the first bench session is also the first test.

## What it does today

Two things, and the second is the reason it exists this early:

1. **A2/B monitor** — input trim, output level, bypass, an input peak meter and a CPU
   load readout. Everything the A2 engine needs around it, ready for the engine to drop
   into one function.
2. **Phase 0 bench check 2** — the passthrough *is* the pass-through firmware the brief
   calls for. Feed a sub-1Hz LFO into IN_L, watch the DC page, and see whether the
   reading holds its level or sags toward zero. That answers whether the Patch SM's
   audio input is AC- or DC-coupled, which is an open question the whole capture plan
   depends on and which no datasheet has settled.

## Signal path

```
IN_L ──► trim (CV_1) ──► [ model slot ] ──► level (CV_2) ──► OUT_L
                                                          └─► OUT_R
```

IN_R is normalled to IN_L on the carrier, so only IN_L is read. Bypass takes the dry
input to the same output level, so an A/B compares the model against the input rather
than against a level change.

## Controls

| Control | Function |
|---|---|
| **CV_1** (+ CV_5 jack) | Input trim into the model, −20…+20 dB, unity at noon |
| **CV_2** (+ CV_6 jack) | Output level, 0…1 |
| **B7** short press | Bypass on/off |
| **B7** long press (600 ms) | Change page, RUN ↔ DC |
| **CV_OUT_2** LED | Lit when the model is in circuit, dark when bypassed |

The trim exists so the signal can be set to the level the model was trained at, which is
why the peak meter reads the input *before* the trim rather than after.

## Pages

**RUN** — model name, input peak meter, average and maximum CPU load. The CPU figures
are the ones phase 1 is done when it can record; the brief's references put A2 on a
480 MHz H7 somewhere between 30% and 61%.

**DC** — the bench check 2 page. A ~50 ms one-pole reading of the input, with a min/max
hold and the span between them. A DC-coupled input tracks a slow LFO and the hold keeps
its full excursion; an AC-coupled input sags back toward zero and the span collapses.
The hold resets each time you enter the page.

## Hardware

- **Electrosmith Patch.Init()** (commercial unit, not a hand-built module)
- **64×48 SSD1306 OLED** on soft I2C via the expansion header — A2 = SDA, A3 = SCL,
  driver in [`../common/oled_soft_i2c.h`](../common/oled_soft_i2c.h). About ten
  characters per line.

## Build

```sh
cd daisy_neural
make
```

Builds `BOOT_NONE` while the model slot is a passthrough, so it flashes straight over
DFU without the Daisy bootloader being installed — which matters, because this unit
currently runs MultiFX, also `BOOT_NONE`. Switch when the engine lands and the weights
need DTCM:

```sh
make APP_TYPE=BOOT_SRAM
```

**Toolchain.** `arm-none-eabi-gcc` on PATH is an x86-64 binary and this host is arm64
with no Rosetta, so it fails with `Bad CPU type in executable`.
[`make/common.mk`](../make/common.mk) now detects that and falls back to the first
toolchain that can actually run — currently the native arm64 xPack GCC 12.3.1 that came
with PlatformIO. Plain `make` works; `GCC_PATH=/path/to/bin` still overrides.

## Footprint (skeleton, model slot empty)

| Region | Used | Size | % |
|---|---|---|---|
| FLASH | 99,508 B | 128 KB | 75.9% |
| DTCMRAM | 0 | 128 KB | 0% |
| SRAM | 16,764 B | 512 KB | 3.2% |
| RAM_D2 | 16,896 B | 288 KB | 5.7% |

Worth knowing before the engine arrives: **the skeleton alone takes three-quarters of
internal flash**, leaving about 28 KB. A2's 1,871 weights are only ~7.5 KB, but the
engine code goes on top, so `BOOT_NONE` will be tight — which is the practical argument
for the `BOOT_SRAM` move at phase 1 step 3 rather than a theoretical one. If more
`BOOT_NONE` room is ever wanted, dropping `-u _printf_float` and formatting the DC page
with integer maths would give back the largest single chunk.

## Next

Phase 1 step 3: lift `nam_model.c/.h` from nam-pedal `t3k-pedal` @ `6dc47a4`, keeping
its `NAM_DTCM` placement, and replace the body of `ModelSlot::Process()`. The seam is
one sample in, one sample out, which is the shape A2 already has. Per the working rules
in [CLAUDE.md](CLAUDE.md), lifted code records its source commit at the top of the file
and carries its licence into `LICENSE-<project>.txt` here.
