# daisy_neural

Neural audio networks on the Daisy Patch.Init — the firmware side of the project
briefed in [CLAUDE.md](CLAUDE.md).

**Status: a real engine and a real capture, ready to trial.** The NAM A2 engine is in
and a JCM800 capture is compiled in, so this is flashable and listenable now. It fits
`BOOT_NONE`, so it needs no bootloader on the unit.

**Builds clean. Not yet run on hardware** — every behaviour below is written but
unobserved, including whether it makes any sound at all, so the first bench session is
also the first test. The one number that matters most, CPU load, is on the RUN page.

## What it does today

Two things, and the second is the reason it exists this early:

1. **Runs a NAM A2 capture** — a JCM800 — with input trim, output level, bypass for
   A/B against the dry input, an input peak meter for setting the trim, and a CPU load
   readout. Recording that CPU figure is what closes phase 1.
2. **Phase 0 bench check 2** — hold bypass on and the firmware is the pass-through the
   brief asks for. Feed a sub-1Hz LFO into IN_L, watch the DC page, and see whether the
   reading holds its level or sags toward zero. That answers whether the Patch SM's
   audio input is AC- or DC-coupled, which is an open question the whole capture plan
   depends on and which no datasheet has settled.

## Signal path

```
IN_L ──► trim (CV_1) ──► [ engine slot ] ──► level (CV_2) ──► OUT_L
                                                          └─► OUT_R
```

IN_R is normalled to IN_L on the carrier, so only IN_L is read. Bypass takes the dry
input to the same output level, so an A/B compares the engine against the input rather
than against a level change.

## Controls

| Control | Function |
|---|---|
| **CV_1** (+ CV_5 jack) | Input trim into the engine, −20…+20 dB, unity at noon |
| **CV_2** (+ CV_6 jack) | Output level, 0…1 |
| **B7** short press | Bypass on/off |
| **B7** long press (600 ms) | Change page, RUN ↔ DC |
| **CV_OUT_2** LED | Lit when the engine is in circuit, dark when bypassed |

The trim exists so the signal can be set to the level the capture was trained at, which is
why the peak meter reads the input *before* the trim rather than after.

## Pages

**RUN** — capture name, input peak meter, average and maximum CPU load. The CPU figures
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

Builds `BOOT_NONE`, so it flashes straight over DFU without the Daisy bootloader being
installed — which matters, because this unit currently runs MultiFX, also `BOOT_NONE`.
The engine and one capture still fit. Switch when the measured CPU load says the DTCM
placement is needed:

```sh
make APP_TYPE=BOOT_SRAM
```

**Toolchain.** `arm-none-eabi-gcc` on PATH is an x86-64 binary and this host is arm64
with no Rosetta, so it fails with `Bad CPU type in executable`.
[`make/common.mk`](../make/common.mk) now detects that and falls back to the first
toolchain that can actually run — currently the native arm64 xPack GCC 12.3.1 that came
with PlatformIO. Plain `make` works; `GCC_PATH=/path/to/bin` still overrides.

## The engine and the capture

**Engine:** `nam/nam_a2_runtime.h`, lifted from
[bkshepherd/DaisySeedProjects](https://github.com/bkshepherd/DaisySeedProjects) @
`ccae0f2` (2026-09-08), MIT — see [LICENSE-daisyseedprojects.txt](LICENSE-daisyseedprojects.txt).

Chosen over nam-pedal's `nam_model.c`, which the brief originally picked, because
bkshepherd ships the **whole path**: the runtime, the `.nam` → C array converter
(`nam/nam_to_cpp_array.py`), and five already-converted captures. nam-pedal's converter
is not in its repo, so its engine cannot be fed without writing one first. Both are MIT
and both are active; this is the one that reaches a first trial today.

**Capture:** JCM800 (`[AMP] JCM800-2203-MODIFIED-HI The Sound - DI.nam`), one of the
five that ship with the runtime. All are DI captures, so there is no cabinet baked in.
The others — BE-100, Ampeg SVT, Mesa Dual Rectifier, Marshall 1959BJA — are in
`nam/model_data_nam_a2.h` and stay unreferenced, so `--gc-sections` drops them from the
binary. Confirmed in the map file: only `kWeightsJcm800` survives the link. Switching
capture is a three-line change at the top of `main.cpp`.

A2 processes a **fixed 48-sample block**, not one sample at a time. The Patch SM defaults
to 48 kHz with 48-sample blocks, so the two line up with no buffering.

## Footprint (engine + one capture)

| Region | Used | Size | % | vs empty slot |
|---|---|---|---|---|
| FLASH | 110,808 B | 128 KB | 84.5% | +11,300 B |
| DTCMRAM | 0 | 128 KB | 0% | — |
| SRAM | 105,316 B | 512 KB | 20.1% | +88,552 B |
| RAM_D2 | 16,896 B | 288 KB | 5.7% | — |

The engine costs about 3.8 KB of code and the capture 7.5 KB, so it fits `BOOT_NONE`
with ~17 KB to spare. The SRAM jump is the ~76 KB history buffer.

**Memory placement is deliberately naive for now.** The runtime wants its hot data in
DTCM and the history in D2 SRAM, via `nam/nam_a2_sections.lds` and the bootloader. Those
macros are neutralised in `main.cpp` so everything lands in ordinary `.bss`, which is
what lets this run `BOOT_NONE`. DTCM is therefore untouched and the history sits in the
slower AXI SRAM. **If the measured CPU load is high, that is the first thing to fix** —
move to `BOOT_SRAM`, wire in the `.lds`, and let the placement do its job. A second
option if flash ever gets tight is dropping `-u _printf_float` and formatting the DC page
with integer maths.

## Next — at the bench

In rough order, because each answers something the next depends on:

1. **Flash it and confirm it makes a sound.** Guitar or a line source into IN_L, trim at
   noon. Short-press B7 to A/B against dry.
2. **Read the CPU load off the RUN page and write it here.** That is what closes phase 1.
   The references put A2 on a 480 MHz H7 between 30% and 61%; this build's history buffer
   is in the slower AXI SRAM rather than D2, so expect the high end or worse. If it is
   uncomfortable, `BOOT_SRAM` plus `nam/nam_a2_sections.lds` is the fix.
3. **Bench check 2**, while the unit is out: bypass on, sub-1Hz LFO into IN_L, long-press
   to the DC page, watch whether the span holds or collapses. Record the answer in
   [CLAUDE.md](CLAUDE.md) under Hardware.

If the trim range or the meter ballistics turn out wrong in use, those are one-line
changes — none of it has been heard yet.

## Note on the captures

The engine code is MIT and attributed. The five captures are a different matter: they are
other people's amp captures, redistributed in bkshepherd's repo without a stated licence
of their own. Fine for bench work. Worth a thought before any firmware with them baked in
goes on the site as a download — a capture made by someone else is their work, and the
honest options are asking, swapping in a capture of your own gear, or shipping the
firmware with an empty slot and a converter script.
