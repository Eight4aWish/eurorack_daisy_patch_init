# daisy_neural

Neural audio networks on the Daisy Patch.Init — the firmware side of the project
briefed in [CLAUDE.md](CLAUDE.md).

**Status: engine in, all five captures loadable from the card.** The NAM A2 engine runs
captures read off the microSD card, selected live with CV_3. A JCM800 is compiled in, when its header is on disk,
as a fallback, so a missing or unreadable card gives a working module rather than
silence.

Built `BOOT_SRAM`, which needs the Daisy bootloader installed once on the unit. This is
going on a **fresh patch.init()** rather than repurposing the MultiFX one, so there is no
reason not to. `BOOT_SRAM` is also what the engine was written for: weights in DTCMRAM,
history in RAM_D2, both on-chip.

**Runs on hardware (first bench session, 2026-09-28).** All five captures load off the
card and sound like the amps they are. **CPU load: 64%** average and peak with the engine
in (1% in bypass), against the references' 30–61% for A2 on a 480 MHz H7 — so phase 1's
number is recorded.

**Two banks since 2026-09-29: AMPS and NOT-AMPS.** The random-seed slot came out: every
seed was a variation on one "evil cello". In its place are nine *not-amps*, real captures
bent inside the network, each with one control that CV can steer while it plays. See
"The not-amps" below. **Not yet run on hardware.**

## What it does today

Two things, and the second is the reason it exists this early:

1. **Runs NAM A2 captures off the card** — five of them, chosen with CV_3 — with input
   trim, output level, bypass for A/B against the dry input, an input peak meter for
   setting the trim, and a CPU load readout. Recording that CPU figure closes phase 1.
2. **Phase 0 bench check 2** — bypass on turns it into the pass-through the brief asks
   for, and the HPF page measures the audio input's high-pass corner. That the input is
   AC-coupled is already settled; the corner frequency is not, and it decides where
   phase 5 has to split audio-rate CV between a CV jack and an audio jack.

## Signal path

```
IN_L ──► trim (CV_1) ──► [ engine slot ] ──► level (CV_2) ──► OUT_L
                                                          └─► OUT_R
```

IN_R is normalled to IN_L on the carrier, so only IN_L is read. Patching IN_R breaks
that normal, which is what makes it available later as the per-sample input for
audio-rate CV — the CV jacks are only read once per block, about 1 kHz.

Bypass takes the dry input to the same output level, so an A/B compares the engine
against the input rather than against a level change.

## Controls

| Control | Function |
|---|---|
| **CV_1** (+ CV_5 jack) | Input trim into the engine, −20…+20 dB, unity at noon |
| **CV_2** (+ CV_6 jack) | Output level, 0…1 |
| **CV_3** (+ CV_7 jack) | Slot within the current bank |
| **CV_4** (+ CV_8 jack) | **STEER** — the not-amp's one control. Does nothing in AMPS, on purpose |
| **B7** short press | Bypass on/off |
| **B7** long press (600 ms) | Change page, RUN ↔ HPF |
| **B7** longer press (1.5 s) | Change bank, AMPS ↔ NOT-AMPS |
| **CV_OUT_2** LED | Lit when the engine is in circuit, dark when bypassed |

The trim exists so the signal can be set to the level the capture was trained at, which is
why the peak meter reads the input *before* the trim rather than after.

## Pages

**RUN** — capture name (drawn inverted while bypassed), input peak meter, average and
maximum CPU load, `CAP 2/5` for the selection, and the raw trim/level knob reads on the
bottom line as `T+0.5L+0.8`. With no card readable, the `CAP` line shows the reason
instead — `mount`, `no captures`, `bad crc` — so a card problem reads as a card problem
rather than a dead engine. The CPU figures are the ones
phase 1 is done when it can record; the brief's references put A2 on a 480 MHz H7
somewhere between 30% and 61%.

That bottom line exists because the pot scaling is **not confirmed**. libDaisy inits
CV_1–CV_8 alike as bipolar while the pots are wired 0–5 V, so what a knob actually spans
is unknown until it is seen. The summing here is exactly what `daisy_multifx_oled` does
and is known to work on this unit, so it stays — but **if there is no sound, read that
line before suspecting anything else.** `L` at 0.0 with the knob turned up is the whole
explanation, and the fix is four lines in `main.cpp`, not in the engine.

**HPF** — the bench check 2 page. AC coupling is settled; this measures the *corner*. Send
one LFO to both IN_L and CV_5, and read `RAT` — the audio span over the CV span. CV_5 is
DC-coupled so its span is the truth, so RAT is the audio input's response at that
frequency: 1.00 passes intact, 0.71 is the −3dB corner. Holds reset on entering the page.

The CV leg is there to disambiguate: without it, a collapsed audio span could equally
mean AC coupling or an unplugged cable.

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
`build/daisy_neural.bin` to the card (`cp -X`), put it in, power-cycle. Keep exactly one
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
bkshepherd ships the **whole path**: the runtime, the `.nam` → C array converter
(`nam/nam_to_cpp_array.py`), and five already-converted captures. nam-pedal's converter
is not in its repo, so its engine cannot be fed without writing one first. Both are MIT
and both are active; this is the one that reaches a first trial today.

**Captures:** all five that ship with the runtime — BE-100, JCM800, Ampeg SVT, Mesa Dual
Rectifier and Marshall 1959BJA — exported to the card by `tools/export_captures.py`. Not
all the same kind: the BE-100 and JCM800 are DI amp captures, the Mesa is a preamp
capture, and the Ampeg (MD 421) and 1959BJA were recorded through a cabinet and mic, so
those two have a cab baked in. Sources and licences are under "Note on the captures".

When its header is on disk (it is no longer in git; see "Note on the captures"), the
JCM800 also stays compiled in as a fallback and shows as `JCM800*`, the trailing asterisk
marking it as the built-in rather than one off the card. The other four stay unreferenced
in `nam/model_data_nam_a2.h`, so `--gc-sections` drops them from the binary.

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

If flash ever gets tight again, dropping `-u _printf_float` and formatting the HPF page
with integer maths is the largest single saving.

## The not-amps

Nine slots in the NOT-AMPS bank, each a real capture bent inside the network, with one
control on the steer knob (CV_4 + the CV_8 jack) that can move while it plays:

| OLED | Built from | Steer controls |
|---|---|---|
| `FREEZE` | JCM800, layer 11's output held | hold length, 1 → 1,024 samples |
| `PAST JCM` | the Ampeg's weights pushed past the JCM800 | how far past, 1.0 → 1.3× |
| `PAST BJA` | the Ampeg's weights pushed past the 1959BJA | how far past, 1.0 → 1.3× |
| `NO LONG` | JCM800, the three gap-239 layers faded out | fade, in → out |
| `FOLDED` | JCM800, a wavefolder on all lanes at layer 11 | fold threshold, 1.0 → 0.3 |
| `PAST MESA` | BE-100's weights pushed past the Mesa | how far past, 1.0 → 1.3× |
| `OFFSET` | JCM800, an offset on one lane at layer 3 | offset −2 → +2; the plain amp at noon |
| `MUTATE` | JCM800 plus a fixed noise vector | noise amount, 0 → 0.4 |
| `FREEZE ERL` | JCM800, layer 3's output held | hold length, 1 → 1,024 samples |

They are built from the captures on the card, found by name, so those five captures must be
there; a missing one shows `N need cap`. The RUN page shows `N3/9 S0.42`: not-amp 3 of 9,
steer at 0.42.

**How they were chosen.** On the Mac, through the real engine: first a sweep of other ways
to draw random weights, then network bending (operations inserted between layers while it
plays, after Broad, Leymarie and Grierson), then a steerability test on the candidates —
how far one control moves the sound, how smoothly, and how much the level changes. The
morph ranges stop at 1.3× because past that the network's gain climbs about 20 dB for
every eighth of a step. `tools/notamp_design.py` holds the nine definitions and generates
`src/notamps.h`: the definitions, a nine-point level-correction table for each so the
steer changes the sound rather than the volume, and MUTATE's noise vector (the exact one
auditioned). `tools/a2_host_steer` renders any of them with the control swept, as CV would
move it.

**The bends live in the engine.** `nam/nam_a2_runtime.h` carries a small `bend` addition —
blend, offset, fold and freeze applied to a layer's output. Inactive, the output is
bit-identical to the unmodified runtime, and a real amp always plays with every bend
cleared. MORPH and MUTATE move by rewriting the weights with no prewarm, in the audio
callback so it never overlaps the engine; on the Mac that matches the real engine exactly.

**The seed slot, retired.** Random weights drawn with one amp's statistics all came out as
variations on one resonant, octave-flavoured sound — deep networks with random weights go
toward delay and resonance (Steinmetz and Reiss, 2020, [arXiv:2010.04237](https://arxiv.org/abs/2010.04237)).
Its generator also assumed the weights file grouped all conv weights first; the file
interleaves each layer's conv weights with its extras, so its per-region spreads and its
TILT landed on the wrong parameters. It is in git history (`src/seed_weights.*`, removed
2026-09-29). The DC blocker it needed stays: even trained captures show a small offset,
and some bends move it.

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

Straight from download to card: no C++, no rebuild, no reflash. The older path through
`nam_to_cpp_array.py` and `model_data_nam_a2.h` means editing a header and recompiling
per capture, which stops being reasonable past about five.

**It validates the weight count, and that matters.** `nam_to_cpp_array.py` emits
`float x[kA2WeightCount] = {...}` whatever it extracted — too many weights is a compile
error, but **too few is silently zero-padded**, giving a capture that loads cleanly and
sounds wrong. This fails at conversion instead, naming the count it found:

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

`tools/export_captures.py` reads `nam/model_data_nam_a2.h` and writes one `.a2nb` file
per capture into `captures/`:

```sh
python3 tools/export_captures.py
```

All five — BE-100, JCM800, Ampeg, Mesa and 1959BJA — export at 7,516 bytes each. Copy
them to the root of the card. They are numbered `0_`…`4_` so the on-module order
matches the table they came from, whatever order the filesystem returns them in.

**The card needs a FAT32 partition of 2 GB or less.** Not exFAT (libDaisy builds FatFs
without it), and not one full-size FAT32 volume on a big card. Found on the first bench
session, 2026-09-28, with a 64 GB card: formatted as one 64 GB FAT32 volume it failed
at card start-up (`hal 100000` on the `CAP` line); repartitioned to a single 2 GB FAT32
volume with the rest left unallocated, all five captures loaded. The card itself is
fine — the partition size is what matters. Why is not known. On a Mac, with the card at
`/dev/diskN` (check with `diskutil list external` first — this erases it):

```sh
diskutil partitionDisk /dev/diskN MBR "MS-DOS FAT32" DAISY 2G "Free Space" REST R
cp -X captures/*.a2nb /Volumes/DAISY/
```

`cp -X` leaves out macOS's `._` metadata files, which end in `.a2nb` and would otherwise
be offered as captures and fail their CRC.

The container is deliberately minimal: 32-byte header (magic, version, weight count,
output gain, name, CRC32) followed by 1,871 float32 in exactly the order the engine's
`load_weights()` takes. Upstream's `.namb` was the obvious alternative but it is a full
NAM Core model format whose weight ordering is NAM Core's, not this runtime's — a wrong
translation would load cleanly and sound wrong, which is the worst kind of bench bug. The
CRC is there so a bad card is caught rather than fed to the network as weights.

## Weight depth: tried, measured, dropped

The idea was good and it did not survive measurement, which is worth recording so it is
not reinvented.

In the guitar world, quantising the weights is pure loss — the product *is* fidelity to
one specific amp. In Eurorack there is no target, so the same parameter looked like a
free timbre control: it does not add noise to the signal, it **moves the model**, so the
distortion characteristic changes shape and a clean input stays clean. Not a bitcrusher.

It was built, it worked, and **it is inaudible**. Here is why, measured against the same
synth sequence:

| comparison | difference |
|---|---|
| one capture versus another | **+2.3 to +9.8 dB** |
| 16-bit versus 8-bit of the same capture | **−19.3 dB** |

A positive figure means the difference is larger than the signal itself. So changing
capture is roughly **30 dB** more of a change than the whole depth range — a factor of a
thousand in error power. No knob mapping rescues that; it was never going to compete for
attention against the control sitting next to it.

So CV_4 is free, and the code is gone rather than left dormant. The quantisation
machinery lives in `tools/` where it belongs, because it still answers the question it
was written for: what precision an FPGA port needs.

**The same result is good news there.** If 8-bit weights are perceptually free on musical
material, the ECP5 memory arithmetic gets easy — A2-Full stops being marginal at 12-bit
and becomes comfortable at 8.

## Global versus chunked scaling

Quantising to B bits gives you 2^(B−1)−1 steps either side of zero. The **scale** is what
one step is worth, and it has to be large enough that the biggest weight is still
representable: `scale = peak / qmax`.

**Global** uses one scale for all 1,871 weights, set by the single largest weight
anywhere in the network. If one weight is 5.0 and most are 0.05, then at 6 bits one step
is 5/31 ≈ 0.16 — and every weight of 0.05 rounds to **zero**. Enough of the network is
zeroed that nothing propagates, which is why global collapses to actual silence below
7 bits rather than just sounding worse.

It is the metre rule problem: measure a building and a matchbox with the same one, and
the matchbox reads zero.

**Chunked** gives each run of 64 weights its own scale, set by that chunk's own largest
weight. A chunk of small weights gets fine steps, a chunk of large ones gets coarse
steps, and nothing is annihilated merely because something elsewhere in the network is
big. It costs a table of ~30 scales and a lookup, which is nothing.

Measured, that granularity is worth **1.5–2 bits throughout** — 7 to 11 dB better at
every depth. Smaller chunks buy a little more at the bottom (8 bits goes from −18.7 to
−22 dB at chunk 8). A production engine would align chunks to the network's actual layer
or channel boundaries rather than to an arbitrary 64; that would do better still.

The two also **fail differently**, which is musically useful rather than merely academic:
chunked stays coherent all the way down to 6, global falls off a cliff. One is a
character control, the other has a destruction edge with a hard wall just past it.

## Quantisation study

How few bits A2's weights survive — the question worth answering before any
fixed-point work on the Tiliqua, and it needs nothing but the Mac.

```sh
c++ -std=c++17 -O2 -I. -o tools/a2_host tools/a2_host.cpp
python3 tools/quantisation_study.py --keep-wavs
```

The float reference is the **real engine**, not a reimplementation:
`nam_a2_runtime.h` is portable, so `tools/a2_host.cpp` compiles it natively and the
script drives it. Quantising the weights and handing them back to the same engine
isolates one variable — everything else is bit-identical between reference and test. A
numpy rewrite of A2 would have risked measuring its own bugs instead.

The default test signal is a **four-second subtractive-synth sequence** — eight notes
varying in both pitch and level, two detuned saws each, band-limited so nothing aliases
at the source. The level variation is the important part: an amp model's most
characteristic behaviour is how its distortion changes with drive, and a sustained note
sits at one point on the transfer curve the whole time. `--signal pluck` and
`--signal sweep` are also there, and `--input yours.wav` for real material.

`--keep-wavs` writes `in_dry.wav` alongside the processed files, so there is something to
judge them against, plus `diff_*.wav` residuals at true level.

Error-to-signal ratio against the float reference, JCM800, synth sequence:

| bits | global scale | per-64 scale |
|---|---|---|
| 16 | −53.0 dB | **−66.5 dB** |
| 12 | −28.9 dB | **−40.6 dB** |
| 10 | −25.2 dB | −28.4 dB |
| 8 | −9.2 dB | −19.3 dB |
| 7 | collapses to silence | −14.3 dB |
| 6 | collapses to silence | −9.7 dB |

Three things fall out of it:

- **Scaling granularity is worth ~1.5–2 bits.** One scale per 64 weights beats a single
  global scale by 7–11 dB throughout. In hardware that costs a small table of scales,
  and it is the difference between 12-bit being usable and not.
- **12-bit with per-chunk scaling lands at −39.8 dB**, about where differences stop being
  obvious on most material. That is the plausible floor.
- **8-bit is out** either way, and global scaling collapses completely at 7 bits — the
  output goes to silence.

This matters for the ECP5 beyond curiosity. A2-**Full** at 16-bit needs roughly 128 KB
against 126 KB of block RAM — just over. At 12-bit it is about 96 KB, which fits with
room to spare, and the 18×18 DSP slices multiply 12-bit and 16-bit operands at the same
cost, so the saving is pure memory. Quantisation is what makes the larger architecture
arguable at all.

**What this does not measure:** weights only. A real fixed-point engine also quantises
activations and accumulators, which stay float here. Treat the numbers as a veto rather
than a permit — they can rule a precision out, not rule one in.

WAVs land in `quant_study/` for A/B listening, which is the judge that counts.

## Next — at the bench

In rough order, because each answers something the next depends on:

0. **Install the Daisy bootloader** on the fresh unit — once, then `make flash` works.
   Card with a 2 GB FAT32 partition and the five `.a2nb` files at the root. **Done
   2026-09-28**; `CAP 1/5` on the RUN page.
1. ~~Flash it and confirm it makes a sound.~~ **Done 2026-09-28**: all five captures sound
   like their amps.
2. ~~Read the CPU load.~~ **64%** average and peak, `BOOT_SRAM` with the history in D2 —
   phase 1 closed.
3. ~~Design the seed landscape.~~ Replaced by the nine not-amps (2026-09-29). **Next: hear
   them on the module** — each across its steer range, then with a CV on CV_8.
4. **Bench check 2**, while the unit is out: bypass on, one LFO to both IN_L and CV_5, long-press
   to the HPF page, sweep the LFO and find where RAT hits 0.71. Record the corner in
   [CLAUDE.md](CLAUDE.md) under Hardware.

If the trim range or the meter ballistics turn out wrong in use, those are one-line
changes.

## Note on the captures

The engine code is MIT and attributed. The five captures are a different matter. bkshepherd's
repo carries them with no licence of their own, but all five are on TONE3000 (checked
2026-10-01), and all five are under its T3K licence: use, and publish the results, freely;
do not redistribute the file without the author's permission.

| Capture | File | TONE3000 page | Creator |
|---|---|---|---|
| BE-100 | `[AMP] BE100DLX-BE TEST - DI` | [Friedman BE100 Deluxe (EL34) community pack](https://www.tone3000.com/tones/friedman-be100-deluxe-el34-community-pack-41359) | @2dor |
| JCM800 | `[AMP] JCM800-2203-MODIFIED-HI The Sound - DI` | [Marshall JCM800 2203 Modified (EL34) community pack](https://www.tone3000.com/tones/marshall-jcm800-2203-modified-el34-community-pack-44209) | @2dor |
| Ampeg | `Ampeg SVT - Gain 10 Ultra Lo and Hi MD 421` | [Ampeg SVT Classic with 6x10](https://www.tone3000.com/tones/ampeg-svt-classic-with-6x10-28202) | TONE3000 |
| Mesa | `3. MESA DUAL RECTIFIER 2025 _ RHYTHM #3` | [MESA DUAL RECTIFIER 2025](https://www.tone3000.com/tones/mesa-dual-rectifier-2025-45026) | @deathblossomaudio |
| 1959BJA | `Marshall 1959BJA SUPER BOWL SETTINGS` | [Marshall 1959BJA](https://www.tone3000.com/tones/marshall-1959bja-78832) | @rjcproductions |

The two @2dor files are the `- DI` variants; the free community pages list the `- SM57`
and blend versions, and the JCM800 page says the DI ones are in the full pack. Whether
these DI files came from a paid pack is worth asking @2dor directly.

So: fine on your own card, not in a download. The options for a release are permission
from four creators (TONE3000 itself is one), CC0/CC-BY A2 captures instead, captures of
your own gear, or no captures at all and the converter. Only the first keeps NOT-AMPS as
it is; the others need it to stop depending on these five by name.

**CC0 captures, found 2026-10-01.** CC0 can be redistributed, so these could ship with
Mirth rather than only be linked. Each page states CC0 and offers A2-Full and A2-Lite —
from the page, not yet confirmed by downloading and running `match_captures.py` /
`nam_to_a2nb.py` on the files. TONE3000 has no licence filter; these came from a web search.

| Tone | Gear | Models | Creator |
|---|---|---|---|
| [Bugera G5 Infinium Pack](https://www.tone3000.com/tones/bugera-g5-infinium-pack-6151) | amp head, clean to distorted, DI | 8 | durchschnittsmusiker |
| [Traynor TS 120 B](https://www.tone3000.com/tones/traynor-ts-120-b-6333) | bass amp head, Mesa 2×15 IR baked in | 5 | @rbrt |
| [Kay 703](https://www.tone3000.com/tones/kay-703-6302) | vintage amp + cab, SM57, lo-fi | 4 | @aazuspan |
| [DIY drive pedal (BJT silicon)](https://www.tone3000.com/tones/diy-drive-pedal-bjt-silicon-celestion-eight-15-ir-5700) | home-built pedal + cab IR | 1 | test98425988 |
| [Dead Robot Guitar Profiles v2](https://www.tone3000.com/tones/dead-robot-nam-guitar-profiles-v2-6745) | free plugins (BLOCKFISH, EpiCentre, Distroyr) | 9 | chrisdeadrobot |
| [Dead Robot Vocal Profiles v1](https://www.tone3000.com/tones/dead-robot-nam-vocal-profiles-v1-6726) | free-plugin vocal chains | 4 | chrisdeadrobot |

The two Dead Robot packs are captures of software. CC0 covers the creator's capture; the
plugins are free, and TONE3000's policy only bars captures of *commercial* software made
without permission. The hardware captures carry no such question.

**Not in this repo.** `nam/model_data_nam_a2.h` and `captures/*.a2nb` were committed
here until 2026-10-01, when they were removed from the whole history; `.gitignore` now
keeps them, and any `.nam`, out. They live on the working machine only. A fresh clone
builds without them — no compiled-in fallback, so no card means pass-through and
`NO CAP` — and gets captures by downloading the `.nam` files from the pages above with a
TONE3000 account and running `tools/nam_to_a2nb.py`. `tools/match_captures.py` takes the downloaded zips as they
come and says which `.nam` in them is each of the five, by comparing the network's own
parameters with the header's — so a capture retrained since shows as a near miss. With the header on disk the build
compiles the JCM800 fallback back in, and `export_captures.py` and the host tools that
read it work as before.
