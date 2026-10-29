# daisy_neural — Mirth

> **Released: v1.1.0, 2026-10-29.** Tags `mirth-v1.1.0` and `mirth_lite-v1.1.0`; assets
> `mirth-v1.1.0.bin`, `mirth_lite-v1.1.0.bin`, the card files (`mirth-v1.1.0-card.zip`: the
> starter set, `notamps/` and the seven filters) and a notices file each. v1.0.0
> (2026-10-03) stays on the releases page.

**Mirth** runs neural amp captures on a Daisy Patch.Init, with a 64×48 OLED: NAM A2-Lite
networks read off the microSD card (AMPS), and *not-amps* — captures bent inside the
network, each with one control (NOT-AMPS: nineteen since v1.1.0, twelve in v1.0.0); and
since v1.1.0 two filters from the rack, a Doepfer Wasp and a Behringer Swords, as networks
with cutoff and resonance as knobs (FILTERS). **Mirth Lite** (`make LITE=1`) is the same
firmware for a stock patch.init with no screen. Pages and downloads:
<https://eight4awish.com/modules/mirth/> (amps and not-amps) and
<https://eight4awish.com/modules/mirth-filters/> (the filters).

Built `BOOT_SRAM`, which needs the Daisy bootloader installed once on the unit. That is
what the engine was written for: weights in DTCMRAM, history in RAM_D2, both on-chip.
**CPU load: 64%** with a capture running, against 30–61% for A2 on a
480 MHz H7 in the reference builds.

The compiled-in fallback is the Orange TH100 starter capture (CC BY, credited in
`nam/fallback_capture.h`), so a missing or unreadable card still gives a working module,
and every build carries CC-licensed data only. The twelve starter captures ship with the
release, credited in [`STARTER_CAPTURES.md`](STARTER_CAPTURES.md). Mirth Lite was checked on a
stock patch.init on 2026-10-03 (v1.0.0) and 2026-10-29 (v1.1.0).

## What it does

1. **Runs NAM A2-Lite captures off the card** (AMPS), with a drive into the network, a
   dry/wet mix (fully down is the dry input, for A/B), an input peak meter and a CPU load
   readout. (v1.0.0 had an input trim and a bypass instead of the drive.)
2. **Not-amps** (NOT-AMPS): the starter captures, each with one transform measured to leave
   the region where real amps sound, and one steer control; since v1.1.0 also four
   random-weight captures whose steer scales every weight — see "The not-amps".
3. **Filters** (FILTERS, since v1.1.0): GRU networks of the rack's Wasp and Swords, cutoff
   and resonance as knobs, RES to 11 — see "Controls" and "Filters".

## Signal path

```
IN_L ─┬─► drive (CV_4) ──► [ engine slot ] ──► wet ─┐
      └─► dry (delayed to match the engine's latency) ┴─► mix (CV_1) ──► DC block ──► OUT_L, OUT_R
```

IN_R is normalled to IN_L on the carrier, so only IN_L is read. Patching IN_R breaks
that normal, which is what makes it available later as the per-sample input for
audio-rate CV — the CV jacks are only read once per block, about 1 kHz.

The dry side is the input before the drive. MIX fully down is the dry side alone, so an
A/B compares the engine with its input. The engine runs within the same block as its
input, so dry and wet line up sample for sample — except the RATE not-amps, which stream
48·R samples late, and a filter with the granular layer, 53.7 ms late; the dry side is
delayed by the same amount while one plays. (v1.0.0 had the trim on CV_1, the mix on
CV_2, and a bypass on B7.)

## Controls

**Since v1.1.0: a FILTERS bank, B7 for patches, and the knobs rearranged** (David,
2026-10-21). v1.0.0 had the old layout: knob 1 trim, 2 mix, 3 slot, 4 steer, B7 bypass,
and two banks. Each knob is summed with the CV
jack under it; the four knobs sit two by two, 1 and 2 on top.

| Control | AMPS | NOT-AMPS | FILTERS |
|---|---|---|---|
| **CV_1** (+ CV_5 jack) | **MIX**, dry (0) to wet (1) | **MIX** | **MIX** |
| **CV_2** (+ CV_6 jack) | — | **STEER**, the not-amp's one control | **FREQ**: the knob 0–10 V, the jack adds its volts, like the Wasp's CV 1 |
| **CV_3** (+ CV_7 jack) | — | — | **RES**, the Wasp's dial, to **11**: lifted from 7, over-Res past 10 (below) |
| **CV_4** (+ CV_8 jack) | **DRIVE** into the network, −20…+20 dB, unity at noon | **DRIVE** | **DRIVE** |
| **B7** short press | next patch in the bank, wrapping round | | |
| **B7** held 1.5 s | next bank: AMPS → NOT-AMPS → FILTERS (skipped on a card with none) | | |
| **CV_OUT_2** LED | lit | | |

**Res goes to 11** (David, 2026-10-22). The Wasp's networks learned its resonance too weakly
from about 7 up: on the Wasp's test take its resonant band rises +0.7, +2.0, +4.5, +6.4 dB
from Res 6 to Res 7, 8, 9, 10; the network's at the same Res +0.6, +1.1, +1.6, +1.9. So up to
7 the dial is as marked; from 7 to 9 the network's Res is lifted to 12 (beyond that its
resonance falls instead of rising); from 9 its own output is fed back into its input, 0.15
at 10 and 0.5 at 11. Mapped, the rise is +0.6, +1.8, +2.5, +4.6 dB. The feedback makes it
ring, then oscillate, first at the highest cutoffs and lower as the dial rises; at 10 from
about cutoff 4 V, within a tenth of where the Wasp itself rings up to 8 V, lower above. The
real Wasp never quite oscillates; the network does from about 9.5 at some cutoffs, and 10
to 11 is over-Res. WASP_R6_16, trained at one Res, gets the feedback alone from 9. The
granular layer on WASP_LP_GR follows the dial itself.

**Seven filters for the card (2026-10-23):** FLT1 an A2 capture at one setting, then six
GRU-16s, each file carrying its own controls (a section at
the end of the `.grnb`, set with `tools/grnb_controls.py`: the network's cutoff volts at the
FREQ knob's ends, and its Res dial table, calibrated against its own module with
`tools/filter_dial_calibrate.py`; a file without one gets the Wasp LP's):

| | Network | FREQ knob | Res dial |
|---|---|---|---|
| FLT1 | Wasp LP as an A2-Lite capture at one setting, Res 6 and cutoff CV +5 V (`f1_WASP_A2.a2nb`, panel WASP_A2_R6: the first Wasp capture, 2026-10-19) | fixed | fixed |
| FLT2 | Wasp LP, cutoff only, Res 6 (WASP_R6_16) | 0–10 V | feedback alone from 9 (0.15 at 10, 0.5 at 11) |
| FLT3 | Wasp LP, cutoff + Res (WASP_LP_16) | 0–10 V | lifted 7 → 12 by 9, feedback 0.15 at 10, 0.5 at 11 |
| FLT4 | FLT3 + the granular layer (WASP_LP_GR) | 0–10 V | as FLT3; the layer follows the dial itself |
| FLT5 | Wasp HP, cutoff + Res (`hp_cr_gru16_s3`, WASP_HP_16) | −10 to 0 V | as marked to 8, lifted to 11.5 by 10; feedback 0.3 at 11 (its resonance shrinks with feedback, so it only whistles at the top cutoffs) |
| FLT6 | Swords, cutoff + Res 0–4 V (`sw4_all_gru16_s3`, SWORDS_16) | V/OCT 2.5–10.5 V | 0–10 → Res CV 0–4 V, no lift needed; feedback 0.2 at 11 (it oscillates only at the lowest and highest cutoffs; Swords' own oscillation is FLT7's) |
| FLT7 | FLT6 + an oscillator layer: Swords' own self-oscillation, built from measurements (`nam/osc_layer.h`; SWORDS_OSC; SWORDS.md, "SWORDS_OSC") | V/OCT 2.5–10.5 V | 0–10 as FLT6; 10–11 takes the layer's Res CV from 4 to 6 V (the network stays at 4), so it oscillates from about 10 (just under at the top cutoffs) at Swords' pitch, and locks to the input near that pitch; no feedback |

**Level match:** every GRU file's audio input is scaled by 0.666 and its output by
1.353 (the patch.init's 7.30 V input and 8.85 V output against the ES-10's 10.96 and 11.97 V
the networks were trained in), so DRIVE at noon puts volts in and gets volts out as the
module did; turn DRIVE up 3.5 dB for the level the filters played at before. The feedback is
added in the network's own units, after that scale. The FREQ line shows the knob's 0–10
whatever the volts.

**FLT1 is an A2 capture filed with the filters:** an `.a2nb` named like a filter file
(`f`, digits, an underscore) plays in FILTERS rather than AMPS, through the amps' engine at its
header gain, without the level match. It plays the one setting it was captured at, so FREQ
and RES do nothing and their lines on the panel are blank. SWORDS_OSC's controls section is
version 2 (the layer's Res CV per dial point and its constants). Both need the firmware of
2026-10-23: older firmware lists the A2 file in AMPS, and gives SWORDS_OSC the Wasp LP's
controls. **Renumbered 2026-10-23:** the A2 capture became FLT1 and the GRU filters moved
up one; notes from before then use the numbering of their day.

**On the bench (2026-10-23):** all seven play. FLT7 runs up to 76% CPU once the dial
enters its oscillation zone (a GRU-16 alone about 61%), and an LFO sweeping its cutoff
neither upsets nor glitches it.

Each bank keeps its own place, so going round the banks comes back to the patch you
left. Choosing a patch by CV, which the v1.0.0 slot knob allowed, has gone.

DRIVE sets the level into the network, and it changes the sound far more than the
volume. On the twelve starter captures (the real engine, 2026-10-19) only the SVT bass
amp behaves like a volume control; for seven the output level hardly moves while the
character changes. A filter network does the same: the Wasp's cutoff + Res GRU-16, fed
a 110 Hz sine at cutoff 10 V, has its third harmonic at −71 dB at 0.5 V peak, −47 dB at
2 V, −31 dB at 5 V and −16 dB at 9 V, where it also compresses by 3.6 dB (2026-10-21). So
there is one drive, in every bank. Noon is unity on the patch.init. **Its input reaches full
scale at about 7.3 V peak** (measured 2026-10-22 through Mirth, MIX down: the dry path's gain
+1.67 dB to 7.0 V, clipping from 7.75 V, the output flat at 8.84 V; `tools/mirth_input_fs.py`),
against the ES-10's 10.96 V that the FILTERS networks were trained in: at noon they get the
same voltage 3.5 dB hotter than the real module did. The peak meter reads
the input *before* the drive. Free for later: Gate In 1 and 2, Gate Out, CV_OUT_1.

### Mirth Lite — a stock patch.init, no screen

`make LITE=1` → `build_lite/mirth_lite.bin`. Same source (`MIRTH_LITE`), same engine,
captures, not-amps, filters, knobs and B7; no OLED code at all. The B8 toggle is unused:
it was the bank until 2026-10-21 (David), when B7's hold took the bank on both builds,
which also brings FILTERS to Lite. v1.0.0 Lite had the toggle for the bank and B7 for
bypass.

| Control | Mirth Lite |
|---|---|
| **B7** short press | next patch in the bank |
| **B7** held 1.5 s | next bank: AMPS → NOT-AMPS → FILTERS (skipped on a card with none) |
| **B8** toggle | unused |
| **LED** | on every change of patch or bank: the bank as one, two or three quick flickers (AMPS, NOT-AMPS, FILTERS), a pause, then the patch's number, a **long blink per five**: 3 is short-short-short, 12 is long-long-short-short; then lit. One blip instead: the compiled-in fallback is playing. A fast flicker that never stops: no captures on the card |

v1.0.0 Lite was run on a stock patch.init on 2026-10-03, from a card made from the
release files: everything as its release notes describe, with the toggle up for AMPS.
v1.1.0 Lite was checked the same way on 2026-10-29, from a card made only from
`mirth-v1.1.0-card.zip`.

## Pages

**RUN**, top to bottom (layout from David, 2026-10-02; six rows since 2026-10-21):

| Line | Shows |
|---|---|
| name | the capture, not-amp or filter |
| meter | the input peak, before the drive |
| `CPU 64(99)` | average CPU load, with the peak in brackets (no `%`: the panel holds ten characters); on a filter `70%  7637c`, the load and the network's worst block in cycles a sample |
| `AMP 2/12` | which patch, and how many in its bank (`AMP`, `NT`, `FLT`), since B7 steps round them |
| `M80  F5.0` | knobs 1 and 2, the panel's top row: the mix in percent; on a filter the cutoff in volts, on a not-amp the steer (`S0.42`) |
| `R6.5  D+6` | knobs 3 and 4, the bottom row: on a filter the Res dial (`R6*` for a network trained at one Res only); the drive in dB |

The lines run on without a gap, and the two knob lines sit as the knobs do on the panel,
the right-hand knob's value in its own column. A knob that does nothing in the bank shows
nothing. With no card readable, the patch line shows the reason instead — `mount`, `no
captures`, `bad crc` — so a card problem reads as a card problem rather than a dead
engine. The v1.0.0 screen showed the raw reads of knobs 1 and 2 there, while the pot
scaling was unconfirmed; the knobs read 0 to 1 across their travel on the bench, so the
raw line has gone.

## Hardware

- **Electrosmith Patch.Init()** (commercial unit, not a hand-built module)
- **64×48 SSD1306 OLED** on soft I2C via the expansion header — A2 = SDA, A3 = SCL,
  driver in [`../common/oled_soft_i2c.h`](../common/oled_soft_i2c.h). About ten
  characters per line.

## Build

```sh
cd daisy_neural
make            # Mirth, the OLED module   -> build/mirth.bin
make LITE=1     # Mirth Lite, no screen    -> build_lite/mirth_lite.bin
```

Each builds in its own folder, so the two never share objects.

Builds `BOOT_SRAM` by default. That needs the Daisy bootloader on the unit — a one-off,
installed with `make program-boot` or the Electrosmith web programmer.

For a first power-on before the bootloader is installed, `BOOT_NONE` still works and
still needs no bootloader:

```sh
make APP_TYPE=BOOT_NONE
```

In that mode the engine's placement sections do not exist, so the macros are neutralised
and everything lands in ordinary `.bss` — correct, just slower, with the history in AXI
SRAM rather than RAM_D2.

## Flashing

```sh
make flash                      # build if needed, then flash over DFU
make dfu-list                   # show every DFU device attached
make flash DFU_SERIAL=<serial>  # pick one when several are in DFU mode
```

Same pattern as `daisy_grids`, including its handling of the dfu-util quirk where a
successful write to a `:leave` address still exits 74.

Under `BOOT_SRAM` this writes to QSPI at `0x90040000` and the bootloader loads it into
SRAM at power-on, so **the bootloader has to be installed first** — once, on the fresh
unit. `DFU_ADDR` follows `APP_TYPE` via libDaisy, so the same `make flash` is correct in
either mode.

There are two different DFU modes, and each step needs its own. `make dfu-list` tells them
apart by the region it offers:

| Step | Enter it by | `dfu-list` shows |
|---|---|---|
| `make program-boot` (once) | hold BOOT, tap RESET, release BOOT — the chip's ROM DFU | `@Internal Flash /0x08000000…` |
| `make flash` (every update) | the Daisy bootloader's own DFU | `@Flash /0x90000000…/0x90040000…` |

Done this way on the fresh unit, 2026-09-28: `program-boot` from ROM DFU (it exits 74 on
the `:leave`, the same harmless quirk), after which the bootloader, finding no app, sat in
its own DFU on its own and `make flash` went straight in. For later updates, with an app
already present, the bootloader only listens for about 2 s after RESET (the
`intdfu-2000ms` build libDaisy installs); libDaisy's docs say pressing BOOT during that
window keeps it there — confirmed on this unit, several times: tap RESET, then press BOOT
while the LED pulses, and `make dfu-list` shows the `0x90040000` region. The ROM DFU
cannot write QSPI, so BOOT+RESET is the wrong mode for `make flash`.

**Or skip USB: update from the card.** The bootloader checks the card root for a `.bin` at
every boot and flashes it when it differs from what is installed. Copy
`build/mirth.bin` (or `build_lite/mirth_lite.bin`) to the card (then `rm /Volumes/DAISY/._*`), put it in, power-cycle. Keep exactly one
`.bin` in the root — the bootloader takes the first it finds — and leave it there; it is
not re-flashed while unchanged. This is the easier route for iterating.

With no module attached, `make flash` builds the binary and then stops with
`No DFU device found for 0483:df11` — so the path is verified as far as it can be without
hardware. What it cannot tell you is whether the unit enumerates or the write succeeds.

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
bkshepherd ships the **whole path**: the runtime and a `.nam` converter. nam-pedal's
converter is not in its repo, so its engine cannot be fed without writing one first. Both are MIT
and both are active; this is the one that reaches a first trial today.

**Captures:** the twelve-capture starter set, CC0 and CC BY, credited in
[`STARTER_CAPTURES.md`](STARTER_CAPTURES.md).

**The built-in fallback** is the Orange TH100 from the starter set — CC BY, by tupalosa
— generated into `nam/fallback_capture.h` by `tools/export_fallback.py` (which checks the
CRC and that every weight round-trips bit for bit) and shown as `ORANGE*`, the asterisk
marking the built-in rather than one off the card.

**Swapping a capture is not real-time safe** — `load_weights()` runs `prewarm()` over the
whole network. So the audio callback only fades the output to silence and raises a flag;
the main loop does the file read and the load; the callback fades back in. Same shape as
the crossfade in `daisy_multifx_oled`, and for the same reason.

A2 processes a **fixed 48-sample block**, not one sample at a time. The Patch SM defaults
to 48 kHz with 48-sample blocks, so the two line up with no buffering.

## Footprint (BOOT_SRAM, engine + SD loader + fallback capture)

| Region | Used | Size | % | holds |
|---|---|---|---|---|
| SRAM | 137,328 B | 480 KB | 27.9% | the program, incl. FatFS |
| RAM_D2 | **76,672 B** | 256 KB | 29.3% | the A2 history buffer |
| DTCMRAM | 46,336 B | 128 KB | 35.4% | hot weights, work buffers, 2 weight buffers |
| QSPIFLASH | 0 | 7936 KB | 0% | free for a capture bank |

That is the engine's intended tiering, and the 128 KB internal-flash ceiling the
`BOOT_NONE` build was 84.5% into no longer applies.

**Getting the history into RAM_D2 took two fixes, both silent failures.** Either one
leaves you with a build that links clean, boots, and merely runs slower with DTCMRAM 80%
full — nothing warns you. The build's region table is the only tell, so **check it
whenever the linker setup or the engine object is touched**:

1. The supplemental linker fragment must be **first** on the link line (ld takes the last
   `-T` as the main script), and with ld 12.3.1 it also needs its own `MEMORY`
   declaration or `RAM_D2` is not yet visible. See `nam/neural_a2_sections.lds`.
2. `NAM_A2_STATE_DATA` goes on the **A2Player instance**, not in the runtime header. The
   header only defines the macro; placement is the caller's job, as bkshepherd's own
   module shows.

If flash ever gets tight again, dropping `-u _printf_float` and formatting the RUN page
with integer maths is the largest single saving (Mirth Lite formats nothing).

## The not-amps

Twelve slots in the NOT-AMPS bank since 2026-10-02, each a starter capture with one
transform and one control on the steer knob (CV_2 + the CV_6 jack; CV_4 + CV_8 in v1.0.0)
that can move while it plays. Chosen by measurement to sit outside the region where real amps sound, then by
ear — "The measured search" below has the numbers. Sixteen from 2026-10-23: four **weight steers**, random-weight captures chosen by ear whose steer
scales every weight. Nineteen since 2026-10-25, released in v1.1.0, from a second audition:
MUTATE KAY in place of FRZ E TR, and RATE F57, LINEAR ORG and TANH BJT added — "The
second audition" below. In patch order, grouped by family:

| # | OLED | Built from | Transform | Steer controls |
|---|---|---|---|---|
| 1 | `SINE BUG` | Bugera G5 | every neuron's activation sin(g·x)/g | g, 0.3 → 8 (log) |
| 2 | `TANH BJT` | DIY BJT drive | every neuron's activation tanh(g·x)/g, a soft clip | g, 0.3 → 12 (log) |
| 3 | `LINEAR TR` | Two Rock | every neuron's leaky-ReLU slope | slope, 0.01 (as trained) → 1 (linear) |
| 4 | `LINEAR TRY` | Traynor TS 120 B | the same | the same |
| 5 | `LINEAR ORG` | Orange TH100 | the same | the same |
| 6 | `RECT ORG` | Orange TH100 | the same, the other way | slope, 0.01 → −1 (full-wave rectifying) |
| 7 | `FB100 F57` | Fender 57 | output fed back after 480 samples (100 Hz) | loop gain, 0 → 0.95 |
| 8 | `FRZ M F57` | Fender 57 | layer 11's output held | hold, 1 → 1,024 samples (log) |
| 9 | `FRZ M KAY` | Kay 703 | the same | the same |
| 10 | `FOLD ORG` | Orange TH100 | a triangle fold inside layer 11 | threshold, 1 → 0.05 (log); a flat +29 dB level correction |
| 11 | `RATE SVT` | SVT-2 Pro | the engine at 1/R of the sample rate, held back up | R = 1, 2, 3, 4, 6 (stepped) |
| 12 | `RATE F57` | Fender 57 | the same | the same |
| 13 | `PAST BLU` | Bluesbreaker | weights pushed past it, away from the Bugera G5 | how far past, 1.0 → 1.3× |
| 14 | `PAST PLX` | pLEXI-LORE | weights pushed past it, away from the SVT-2 Pro | the same |
| 15 | `MUTATE KAY` | Kay 703 | its weights plus t times a fixed random direction (KAY MUTATE: the search's own draw for the Kay, one weight spread in size) | t, 0 → 0.5 |
| 16 | `RND1 SCALE` | RND1 X2: random weights in the Klon's layout, drawn from seed 1, doubled (`tools/random_captures.py`) | every weight scaled | the scale, ×1.5 → ×3 of the seed's draw (log; about ×2.1 at noon) |
| 17 | `RND4 SCALE` | RND4 X2 (seed 4) | the same | the same |
| 18 | `RND6 SCALE` | RND6 X2 (seed 6) | the same | the same |
| 19 | `RND7 SCALE` | RND7 X2 (seed 7) | the same | the same |

They find their captures on the card by name — the starter set, which ships with Mirth
(CC0 / CC-BY) — so a missing one shows `N need cap`. The weight steers need RND1, RND4, RND6
and RND7 X2, and MUTATE KAY needs KAY MUTATE (`captures/random/mutate_KAY_703.a2nb`,
which `notamp_design.py` writes), in a `notamps/` folder on the card: the NOT-AMPS bank
finds captures there by name, and AMPS never lists them, since they are not amps. They
are generated, not downloaded, and carry no capture's weights. The RUN page shows `N3 S0.42`: not-amp 3,
steer at 0.42.

**One processor, two places.** `src/notamp_dsp.h` is the whole of the not-amps'
processing — the bend, feedback loop, rate streaming and level table — and the firmware
and `tools/a2_notamp.cpp` both run it. `tools/notamp_design.py` renders each through it and
through the search harness they were chosen with (`tools/a2_explore.cpp`) and refuses to
write `src/notamps.h` unless they agree: sixteen bit for bit (the weight steers against
their capture's weights scaled and played plainly, which the search harness cannot do),
PAST BLU, PAST PLX and MUTATE KAY to −102, −114 and −161 dB (the harness's first-block
transient). It then measures a nine-point level table for each, against its source
capture played plainly, so the steer changes the sound and not the volume. For the
fifteen, corrections run from −27 dB (PAST PLX at full steer) to +70 dB (TANH BJT at
full steer: a soft clip in every neuron, divided by its gain, leaves the network's output
32 to 70 dB below the drive's; the noise screen still passes it with 35 dB to spare). Each weight steer's run from +25 to
+39 dB at the bottom to between −82 and −102 dB at the top: scaling every weight compounds
through the network's layers, so its raw output rises 106–136 dB from ×1.5 to ×3.

**First bench session, 2026-10-02.** SINE PLX and SINE BUG peaked at 99% CPU: the
11th-order polynomial sine was a chain of six dependent multiply-adds and an FPU-stalling
compare, ~45 cycles, 69 times a sample. Replaced by a 2,048-entry table with linear
interpolation (`bend::FastSin`: about 14 instructions a sine, same accuracy against the
audition); not yet re-measured on the module. FB PCH KAY was too much grating feedback, and
RATE BUG imposed a tone at every reduced rate (steer past 0.12); both were replaced by
the search's next picks around the other ten (`notamp_search.py --keep … --allow …`):
RECT ORG and FOLD ORG, both cheap. The other eight were fine.

**Second bench session, 2026-10-02.** SINE BUG 75% CPU and fine — the table sine works.
SINE PLX was only noise at every setting: on the Mac, faint input noise (−70 dBFS) comes
out of it as loud as playing — a noise amplifier, which the clean test signal never
showed. Replaced by LINEAR TRY, the next pick that passes a new **noise screen** in
`notamp_design.py` (idle output within 10 dB of playing *and* spectrally flat fails; the
feedback pair's idle output is a tone, and passes). The same investigation found the
**level tables had been measuring DC**: LINEAR TR's "level" was 22 dB of an offset the
output blocker removes, so its knob got ~38 dB quieter toward the top, PAST BLU ~20 dB.
The tables are now measured after a replica of the firmware's DC blocker. FB PCH PLX
"has some feedback" — by design; it self-oscillates with no input.

**Third bench session, 2026-10-02.** FB PCH PLX's feedback grated: replaced by PAST PLX,
the next noise-screened pick. RATE SVT had "a constant tone" — **a firmware bug**, not the
not-amp: the rate streamer wrote each output sample before reading the input sample in the
same place, and the firmware processes in place, so the output was fed back as input and
sustained a tone (−11 dBFS out of silence on the Mac, once the harness processed in place).
Almost certainly RATE BUG's "tone past 0.12" too. Fixed, and `a2_notamp` now processes in
place like the firmware; RATE SVT kept for one more listen.

**The activation is a firmware choice.** `nam/nam_a2_runtime.h` compiles each activation
— as trained, a variable slope, a sine, a tanh — as its own copy of the layer kernels,
chosen once per block, so a real amp pays nothing. The sine is `bend::FastSin`, a 2,048-entry table, within
−53 to −106 dB of `sinf` through whole networks (pLEXI-LORE's network amplifies single
rounding steps; the Bugera's does not). The tanh is `bend::FastTanh`, a branch-free lookup
on the upper half of the GRU filters' own tanh table (`nam/tanh_table.h`, held once for
both: a second 8 KB table in DTCM halved the stack's headroom), within −109 to −134 dB of
`std::tanh` through TANH BJT's network; the GRU filters render bit for bit as before. Freeze and the morph use the `bend`
operations and live weight rewrites introduced earlier.

**The seed slot, retired.** Random weights drawn with one amp's statistics all came out as
variations on one resonant, octave-flavoured sound — deep networks with random weights go
toward delay and resonance (Steinmetz and Reiss, 2020, [arXiv:2010.04237](https://arxiv.org/abs/2010.04237)).
Its generator also assumed the weights file grouped all conv weights first; the file
interleaves each layer's conv weights with its extras, so its per-region spreads and its
TILT landed on the wrong parameters. It is in git history (`src/seed_weights.*`, removed
2026-09-29). The DC blocker it needed stays: even trained captures show a small offset,
and some bends move it.

### The measured search, 2026-10-02

Asked for not-amps that "definitely do not sound like synths through guitar amps", this
time chosen by measurement first and ears second:

- `tools/a2_explore.cpp` runs the real engine with any transform, its knob held or
  swept: activation (slope, sine, tanh), biases, the raw-input mix-in
  (scaled, rectified, delayed, or a sidechain), feedback, rate division, gap stretch
  (host builds at ×2–×4), and the existing bends. Its hooks in `nam_a2_runtime.h` exist
  only under `NAM_A2_EXPLORE`; the firmware builds byte-identical without them.
- `tools/notamp_search.py` maps the **amp region** from 236 real A2 captures, renders
  every transform on each of the twelve at five knob positions and two input levels
  (2,968 clips), describes each with librosa (timbre; harmonic structure counted
  against the test phrase's known notes; drive response; sustain, envelope tracking and
  stutter), and keeps what leaves the region, steers smoothly and stays level-safe.
- `tools/clap_judge.py` is a second, independent judge: CLAP's learned audio space.
  Its zero-shot "guitar amp" prompts proved useless here (CLAP hears a synth through an
  amp as a synth: amp-ness ~0.02 for real captures too), but its embedding distance
  works. **`laion/larger_clap_music` is broken as published** — use `clap-htsat-unfused`.

The two judges agree transform by transform (fraction of the knob outside the amp region):

| Transform | Descriptors | CLAP | Verdict |
|---|---|---|---|
| sine neurons, feedback (pitch / 500 Hz / 100 Hz) | 0.8–1.0 | 0.9–1.0 | furthest from amps |
| freeze a layer, tanh neurons, rate ÷, mutate | 0.8–1.0 | 0.8–1.0 | clearly outside |
| fold inside, gap stretch, past a partner, slope | 0.6–1.0 | 0.5–0.9 | outside, but nearer |
| bias, mix-in (any), lane offset, long gaps out | 0.0–0.2 | 0.0–0.25 | **still sound like amps** |

So two earlier not-amps, a lane offset and stretched long gaps, measure as amps by both
judges, and the mix-in path — every variant — barely changes anything.

Selected — twelve, to match the twelve amps. Each at least 2× the amps' own spacing from
the amps, none described by CLAP as an amp or pedal, at most three per approach and two
per capture (`--combine --family-cap 3`; a cap of two stopped at ten):

| Not-amp | Distance from amps | CLAP hears |
|---|---|---|
| sine neurons on pLEXI-LORE | 11.7× | wavefolder synthesizer |
| feedback pitch on pLEXI-LORE | 10.4× | screaming audio feedback |
| feedback 100 Hz on Fender 57 | 6.4× | bitcrushed lo-fi synth |
| slope→linear on Two Rock | 6.2× | sub-octave bass synth |
| feedback pitch on Kay 703 | 6.1× | wavefolder synthesizer |
| freeze early on Two Rock | 4.9× | bitcrushed lo-fi synth |
| sine neurons on Bugera G5 | 3.8× | comb filter or flanger |
| freeze mid on Kay 703 | 3.5× | sub-octave bass synth |
| freeze mid on Fender 57 | 3.3× | bitcrushed lo-fi synth |
| rate ÷ on SVT-2 Pro | 3.2× | sub-octave bass synth |
| rate ÷ on Bugera G5 | 2.6× | sub-octave bass synth |
| past a partner on Bluesbreaker | 2.2× | sub-octave bass synth |

Audition: `amp_compare/notamp_search/audition.wav` (gitignored). **Heard and approved by David,
2026-10-02:** every one has zones where it does not sound like a guitar amp. Built into the
firmware the same day (`src/notamp_dsp.h`, `src/notamps.h`), then settled over the three
bench sessions above.

### The second audition, 2026-10-25

Asked for more diversity, and to audition more options. `tools/notamp_options.py` took
every route that passed both judges and was not on the card, ranked them by distance from
the card's sixteen (in amp spacings, on the search's measure), and picked eight: at most
two per family, one per transform and two per capture, each past the noise screen, none a
route the bench had turned down (feedback pitch on the Kay and on pLEXI-LORE, SINE PLX).
It rendered each as the riff four times with the steer swept end to end, beside the
card's three freezes, RECT ORG and RATE SVT. The three freezes measured as each other's
nearest neighbours, 1.8–1.9 spacings apart.

| Option | From the amps | From the card | |
|---|---|---|---|
| feedback pitch on the Sunbender | 5.45 | 5.17 | |
| feedback 100 Hz on the Two Rock | 6.89 | 3.99 | |
| tanh neurons on the DIY BJT drive | 2.07 | 3.22 | **kept: TANH BJT** |
| slope→linear on the Orange TH100 | 1.65 | 3.21 | **kept: LINEAR ORG** |
| fold on the DIY BJT drive | 4.16 | 3.09 | |
| mutate on the Kay 703 | 1.81 | 2.80 | **kept: MUTATE KAY, in place of FRZ E TR** |
| long gaps out on the Bugera G5 | 1.39 | 2.58 | |
| rate ÷ on the Fender 57 | 1.67 | 2.42 | **kept: RATE F57** |

Chosen by David, 2026-10-25. Two new kinds came with them (`src/notamp_dsp.h`): Tanh,
on the table tanh above, and Mutate, the capture plus t times a direction file — the
search's own fixed draw for the Kay, which `notamp_design.py` writes for the card's
`notamps/` folder, bit for bit what the audition played. On the module (2026-10-29) all
four work; TANH BJT runs at 84% CPU, above SINE BUG's 75% though its lookup is the same
shape as the table sine.

## Getting more captures

[TONE3000](https://www.tone3000.com/) carries **700,000+ tones**, and the library more
than doubled from 300,000 in the three months after A2 launched in June 2026. A2 models
carry an **A2 badge** and the browse and search views **filter by architecture**, which
is the filter that matters: the embedded engine runs A2-Lite only.

An A2 download is a **single file holding both sizes** — A2-Full at 8 channels for DAWs,
A2-Lite at 3 channels for constrained devices. The converter takes the Lite submodel.

```sh
python3 tools/nam_to_a2nb.py downloaded.nam --out captures/
python3 tools/nam_to_a2nb.py *.nam --out captures/      # a folder at a time
```

Straight from download to card: no C++, no rebuild, no reflash.

**It validates the weight count, and that matters.** A network with too few weights would
otherwise be zero-padded into a capture that loads cleanly and sounds wrong. This fails
at conversion instead, naming the count it found:

```
legacy.nam: 9999 weights, engine needs 1871 (plain WaveNet). Not an A2-Lite model.
lstm.nam: unsupported architecture 'LSTM'. The engine runs A2-Lite only.
```

**On licensing:** the A2 *architecture* is MIT and explicitly free to ship in commercial
products. The captures are licensed one by one, by their creators, and each tone page
shows which: TONE3000's own licence (T3K) or one of the Creative Commons set, CC0
included. T3K lets anyone use a capture and publish what they play through it, but not
"upload, republish, or distribute the data file without the author's permission" — so a
T3K capture can go on your own card but not into a download. CC0 and CC-BY can ship;
ND forbids the not-amps' bending, SA would bind the bent weights, NC is murky.

## Captures on the microSD card

The release's starter set is twelve `.a2nb` files, numbered `n01_`…`n12_` so the on-module
order is fixed whatever order the filesystem returns them in. Copy them to the root of the
card. Each is 7,516 bytes.

Captures you convert yourself (`tools/nam_to_a2nb.py`, above) go on the same card.

**Or copy the `.nam` itself (since v1.1.0).** At boot Mirth imports any
`.nam` in the card's root: it takes the A2-Lite weights, names the capture from the file
name and levels it with the not-amps' test phrase run through its own engine, then writes
the `.a2nb` beside it, the same file `nam_to_a2nb.py` and `level_captures.py` would have
written: byte for byte, gain included, on the twelve starter downloads and four awkward
names (`tools/nam_import_check.cpp`, `src/nam_import.*`). About three seconds a file, at
the first boot after they are copied; the screen shows `IMPORT 2/5` and the file, then how
many were added and refused. Each `.nam` then moves into `nam_done/`, or `nam_refused/`
if it is not an A2 capture (the screen gives the reason: `not A2`, `bad file`, `too
big`), so nothing is imported twice and nothing is deleted. Where two files would get the
same name (a pack's `…_01_01` and `…_01_02` both become `Ampeg SVT-2`), the second is
numbered, `Ampeg SVT 2`, instead of overwriting the first, as the desktop tool would.
Mirth Lite imports the same way, with no screen to say so: look in the two folders.
**Run on the module (David, 2026-10-22, firmware `76eb6b0`):** two copies of a starter
download and a non-A2 file imported as described, the screens, the numbering of the clash
and the refusal all as above.

**Filters** (FILTERS bank, since v1.1.0) are `.grnb` files on the same card: GRU networks
from `tools/train_gru.py`, written by `tools/gru_export.py` (format in `tools/grnb.py`,
engine in `nam/gru_runtime.h`, checked against PyTorch by `tools/gru_check.py`). Up to 16,
in filename order, GRU-24 at most. The first are the Doepfer A-124 Wasp's (`CLAUDE.md`).
A file may also carry a **granular layer** (`gru_export.py --layer 9,10`, a version-2
`.grnb` of about 1.9 MB): the Wasp's take-to-take randomness above Res 8, played by
`nam/gran_layer.h` and checked against `tools/wasp_layer.py` by `tools/gran_check.py`.
That slot runs 53.7 ms late, dry and wet alike, and takes about half a second to load.
Level them with the rest: `tools/level_captures.py --write` plays each file through the
real engine and sets its gain so every capture sits at one **A-weighted** level
(−27.0 dB on the not-amps' test phrase). Plain RMS was tried first and left the bass-heavy
Ampeg SVT about 5 dB quiet by ear, because it counts bass the ear barely hears.
`tools/tone3000.py` searches TONE3000 by gear type, lists a tone's files, downloads
chosen ones, and scans any gear type — cab IRs included — for CC0 / CC-BY licences.

**The card needs a FAT32 partition of 2 GB or less.** Not exFAT (libDaisy builds FatFs
without it), and not one full-size FAT32 volume on a big card. Found on the first bench
session, 2026-09-28, with a 64 GB card: formatted as one 64 GB FAT32 volume it failed
at card start-up (`hal 100000` on the `CAP` line); repartitioned to a single 2 GB FAT32
volume with the rest left unallocated, every capture loaded. The card itself is
fine — the partition size is what matters. Why is not known. On a Mac, with the card at
`/dev/diskN` (check with `diskutil list external` first — this erases it):

```sh
diskutil partitionDisk /dev/diskN MBR "MS-DOS FAT32" DAISY 2G "Free Space" REST R
cp captures/*.a2nb /Volumes/DAISY/
rm -f /Volumes/DAISY/._*
```

Copying to a FAT32 card makes macOS write a `._` metadata twin of each file. Those end
in `.a2nb`, so they would be offered as captures and fail their CRC. **`cp -X` does not
prevent this on current macOS** — every file carries `com.apple.provenance`, which it
cannot strip (found 2026-10-01: twelve twins after a `cp -X`). `dot_clean` can stop
early on the card's protected `.Spotlight-V100` folder, so delete them directly.

The container is deliberately minimal: 32-byte header (magic, version, weight count,
output gain, name, CRC32) followed by 1,871 float32 in exactly the order the engine's
`load_weights()` takes. Upstream's `.namb` was the obvious alternative but it is a full
NAM Core model format whose weight ordering is NAM Core's, not this runtime's — a wrong
translation would load cleanly and sound wrong, which is the worst kind of bench bug. The
CRC is there so a bad card is caught rather than fed to the network as weights.

## The starter set, and how it was chosen

The engine code is MIT and attributed. Captures are licensed one by one by their creators
(see "On licensing" above). **What ships with Mirth is the starter set: CC0 and CC BY
only, credited in [`STARTER_CAPTURES.md`](STARTER_CAPTURES.md).**

**What can ship: the whole catalogue, scanned 2026-10-01** with `tools/find_cc0.py`. Of
11,438 A2 tones on TONE3000, 11,329 are T3K. Only **86 are redistributable: 15 CC0 and
71 CC-BY** (CC-BY needs the creator credited and changes noted, which the not-amps are).
The rest are share-alike or non-commercial. A tone is a pack: one holds 1 to 54 captures.

A shortlist from those 86, classic gear first. All A2 on the page; `amp` means head only,
which sounds harsher with no cab after it, and Mirth has none — worth hearing before
choosing. "A1 Conversion" packs were converted from the older architecture, not retrained.

| Tone | Licence | Kind | Captures |
|---|---|---|---|
| [Marshall Jubilee 2555x](https://www.tone3000.com/tones/marshall-jubilee-2555x-5730) | CC-BY | amp + cab | 1 |
| [pLEXI-LORE](https://www.tone3000.com/tones/plexi-lore-1863) | CC-BY | amp + cab | 1 |
| [1968 KT66 Marshall Plexi style amp](https://www.tone3000.com/tones/1968-kt66-marshall-plexi-style-amp-v20-6806) | CC-BY | amp | 1 |
| [Mesa Mark IIC+](https://www.tone3000.com/tones/mark-iic-simulclassgeq-2598) | CC-BY | amp | 1 |
| [Orange TH100](https://www.tone3000.com/tones/orange-th100-6554) | CC-BY | amp | 6 |
| [Fender '57 Custom Deluxe](https://www.tone3000.com/tones/fender-57-custom-deluxe-a1-conversion-5590) | CC-BY | amp (A1 conversion) | 26 |
| [EVH 5150 III](https://www.tone3000.com/tones/evh-5150-iii-6l6-amp-only-moderate-boost-205) | CC-BY | amp | 1 |
| [Two Rock Studio Signature + 2x12](https://www.tone3000.com/tones/two-rock-studio-signature-ox-box-2x12-two-rock-cab-v2-5367) | CC-BY | amp + cab | 22 |
| [Ampeg SVT-2 Pro](https://www.tone3000.com/tones/ampeg-svt-2-pro-5728) | CC-BY | bass amp + cab | 1 |
| [Klon Centaur (Silver)](https://www.tone3000.com/tones/klon-centaur-silver-2599) | CC-BY | pedal | 1 |
| [ProCo RAT, 1990s](https://www.tone3000.com/tones/proco-rat-1990s-6335) | CC0 | pedal | 2 |
| [Marshall Bluesbreaker pedal](https://www.tone3000.com/tones/marshall-bluesbreaker-pedal-original-1778) | CC-BY | pedal | 1 |
| [Analogman Sunface fuzz](https://www.tone3000.com/tones/analogman-sunface-fuzz-rca-bart-nkt-red-dot-bc109-a1-conversion-5494) | CC-BY | germanium fuzz (A1 conversion) | 16 |
| [Analogman Sun Bender MKIV](https://www.tone3000.com/tones/analogman-sun-bender-mkiv-3-nos-mullard-5609) | CC-BY | Tone Bender fuzz | 32 |
| [Boss CE-1 preamp](https://www.tone3000.com/tones/boss-ce-1-pre-amp-1789) | CC-BY | pedal preamp | 1 |
| [Studer A807, pushed](https://www.tone3000.com/tones/studer-a807-pushed-1101) | CC-BY | tape machine | 2 |
| [1950s Tandberg tape recorder](https://www.tone3000.com/tones/1950s-tandberg-tape-recorder-6025) | CC-BY | tape machine | 6 |
| [UREI 1176](https://www.tone3000.com/tones/ureiuniversal-audio-1176-1148) | CC-BY | compressor | 2 |

The full list of 86 is `amp_compare/cc0_tones.csv` after a scan (gitignored; re-run the
script to regenerate). The earlier CC0 finds — Bugera G5, Traynor TS 120 B, Kay 703, a DIY
BJT drive — are in it too; auditioned 2026-10-01, they all sounded much alike.

**Auditioned on the module, 2026-10-01**, by guitar (through the Befaco I4) and by synth,
one capture per pack from the shortlist plus the first CC0 batch, on a 27-slot card
built from the downloads (`captures/card1/`, gitignored, with a `CARD.txt` of slots).
Kept — the starter set, and the not-amps' sources:

| Capture | Guitar | Synth | Licence |
|---|---|---|---|
| pLEXI-LORE | ✓ | | CC-BY |
| Two Rock Studio Signature + 2x12 | | ✓ | CC-BY |
| Ampeg SVT-2 Pro | | ✓ | CC-BY |
| Orange TH100 | ✓ | ✓ | CC-BY |
| Fender '57 Custom Deluxe | ✓ | ✓ | CC-BY |
| Klon Centaur | | ✓ | CC-BY |
| ProCo RAT | | ✓ | CC0 |
| Marshall Bluesbreaker | ✓ | ✓ | CC-BY |
| Boss CE-1 preamp | | ✓ | CC-BY |
| Analogman Sunface | ✓ | ✓ | CC-BY |
| Analogman Sun Bender | ✓ | | CC-BY |
| Studer A807 | | ✓ | CC-BY |
| UREI 1176 | ✓ | ✓ | CC-BY |
| Bugera G5 | ✓ | | CC0 |
| Traynor TS 120 B | | ✓ | CC0 |
| Kay 703 | | ✓ | CC0 |
| DIY BJT drive | | ✓ | CC0 |

Dropped: every high-gain amp (Jubilee, KT66 Plexi, Mark IIC+, 5150 III) and the Tandberg.
For multi-capture packs this was one capture, the middle one by measured drive; the
others in those packs are unheard on the module.

**Starter set, decided 2026-10-01.** Twelve, all heard and kept on the module, chosen
for spectral diversity (`tone shape at two input levels; greedy farthest-first from the
17 kept`: closest pair 2.5 dB apart, against 1.9 dB for a hand-picked set), and a mix of
names. Two gain settings per pack were measured and dropped: the Fender's cleanest and
dirtiest sit 2.3 dB apart, nearer than the Fender is to the Orange, so drive differs more
in feel than in tone shape and a second setting buys little diversity per slot.

| # | Capture | File in the pack | Licence |
|---|---|---|---|
| 1 | Ampeg SVT-2 Pro | `Ampeg SVT-2 Pro_01_01` | CC-BY |
| 2 | Orange TH100 | `031000ETH100Clean100WMV9` | CC-BY |
| 3 | Fender '57 Custom Deluxe | `57 DLX JMP M9 I9 T9` | CC-BY |
| 4 | Marshall Bluesbreaker pedal | `marshall-bluesbreaker-pedal-setting1` | CC-BY |
| 5 | Klon Centaur | `KLON 2` | CC-BY |
| 6 | pLEXI-LORE | `model (1)` | CC-BY |
| 7 | Analogman Sun Bender MKIV | `SunBender MKIV F10 T2 V7` | CC-BY |
| 8 | Two Rock Studio Signature | `BMD All 5s Traditional A1 Conversion` | CC-BY |
| 9 | Kay 703 | `Kay 703 - G5 T5` | CC0 |
| 10 | Traynor TS 120 B | `traynor 7 2 6 4` | CC0 |
| 11 | Bugera G5 Infinium | `Bugera G5 Infinium Dist 0.5 USA DI` | CC0 |
| 12 | DIY BJT drive | `diy_drive_pedal_bjt_silicon` | CC0 |

Dropped as near-duplicates of something kept: RAT (~Bluesbreaker), Sunface (~Klon),
1176 and Boss CE-1 (~each other), Studer (~Klon). They stay as links. pLEXI-LORE, Sun
Bender and Bugera hiss at rest — idle output 7–16 dB under playing level, the module's
input noise amplified, since A2 makes nothing from silence — so the set assumes an input
noise gate. Without one, swap them for RAT, Sunface and 1176.

**The source decides the noise** (David, on the module, 2026-10-01): through the same
distorted captures, a digital oscillator is much noisier than an analogue one. The noise
is the digital source's own — the hash of its processor and clocks riding on its output,
the same character as OLED bus traffic but from a source with no OLED. Far below hearing
through a clean path, it comes up through a capture that lifts low-level signal hardest,
which is why only the distorted amps show it. So the module's own input is not the main
contributor, and a gate's threshold has to be adjustable to the source. CV_2 (+ CV_6),
which does nothing in AMPS by design, is free to set it there and keep STEER in NOT-AMPS;
so is CV_3 (+ CV_7).

**Not in this repo.** No capture is committed: `.gitignore` keeps out
`captures/` and any `.nam` or `.a2nb`. A fresh clone builds
without them — the firmware's fallback is the CC BY Orange, committed — and gets captures
from the release's starter set, or by downloading `.nam` files from the pages above with a
TONE3000 account and running `tools/nam_to_a2nb.py`.
