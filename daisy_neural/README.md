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
seed was a variation on one "evil cello". Since 2026-10-02 NOT-AMPS holds twelve *not-amps*,
the starter captures each with one transform measured to leave the region where real amps
sound — see "The not-amps" below — and knob 2 is a dry/wet MIX. **Not yet run on hardware.**

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
IN_L ─┬─► trim (CV_1) ──► [ engine slot ] ──► wet ─┐
      └─► dry (delayed to match a not-amp's latency) ┴─► mix (CV_2) ──► DC block ──► OUT_L, OUT_R
```

IN_R is normalled to IN_L on the carrier, so only IN_L is read. Patching IN_R breaks
that normal, which is what makes it available later as the per-sample input for
audio-rate CV — the CV jacks are only read once per block, about 1 kHz.

The dry side is the input before the trim. Bypass is the dry side alone, so an A/B
compares the engine with its input. The engine runs within the same block as its input,
so dry and wet line up sample for sample — except the RATE not-amps, which stream 48·R
samples late; the dry side is delayed by the same amount while one plays.

## Controls

| Control | Function |
|---|---|
| **CV_1** (+ CV_5 jack) | Input trim into the engine, −20…+20 dB, unity at noon |
| **CV_2** (+ CV_6 jack) | **MIX**, dry (0) to wet (1). Replaced output level, 2026-10-02 |
| **CV_3** (+ CV_7 jack) | Slot within the current bank |
| **CV_4** (+ CV_8 jack) | **STEER** — the not-amp's one control. Does nothing in AMPS, on purpose |
| **B7** short press | Bypass on/off |
| **B7** long press (600 ms) | Change page, RUN ↔ HPF |
| **B7** longer press (1.5 s) | Change bank, AMPS ↔ NOT-AMPS |
| **CV_OUT_2** LED | Lit when the engine is in circuit, dark when bypassed |

The trim exists so the signal can be set to the level the capture was trained at, which is
why the peak meter reads the input *before* the trim rather than after. Free for later:
Gate In 1 and 2, Gate Out, CV_OUT_1.

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
`build/daisy_neural.bin` to the card (then `rm /Volumes/DAISY/._*`), put it in, power-cycle. Keep exactly one
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

Twelve slots in the NOT-AMPS bank since 2026-10-02, each a starter capture with one
transform and one control on the steer knob (CV_4 + the CV_8 jack) that can move while
it plays. Chosen by measurement to sit outside the region where real amps sound, then by
ear — "The measured search" below has the numbers.

| OLED | Built from | Transform | Steer controls |
|---|---|---|---|
| `SINE PLX` | pLEXI-LORE | every neuron's activation sin(g·x)/g | g, 0.3 → 8 (log) |
| `SINE BUG` | Bugera G5 | the same | g, 0.3 → 8 (log) |
| `LINEAR TR` | Two Rock | every neuron's leaky-ReLU slope | slope, 0.01 (as trained) → 1 (linear) |
| `FB100 F57` | Fender 57 | output fed back after 480 samples (100 Hz) | loop gain, 0 → 0.95 |
| `FB PCH PLX` | pLEXI-LORE | output fed back at gain 0.8 | loop delay — the pitch — 48 → 1,200 samples (log) |
| `FB PCH KAY` | Kay 703 | the same | the same |
| `FRZ E TR` | Two Rock | layer 3's output held | hold, 1 → 1,024 samples (log) |
| `FRZ M F57` | Fender 57 | layer 11's output held | the same |
| `FRZ M KAY` | Kay 703 | the same | the same |
| `RATE SVT` | SVT-2 Pro | the engine at 1/R of the sample rate, held back up | R = 1, 2, 3, 4, 6 (stepped) |
| `RATE BUG` | Bugera G5 | the same | the same |
| `PAST BLU` | Bluesbreaker | weights pushed past it, away from the Bugera G5 | how far past, 1.0 → 1.3× |

They find their captures on the card by name — the starter set, which ships with Mirth
(CC0 / CC-BY) — so a missing one shows `N need cap`. The RUN page shows `N3 S0.42`:
not-amp 3, steer at 0.42.

**One processor, two places.** `src/notamp_dsp.h` is the whole of the not-amps'
processing — the bend, feedback loop, rate streaming and level table — and the firmware
and `tools/a2_notamp.cpp` both run it. `tools/notamp_design.py` renders each through it and
through the search harness they were chosen with (`tools/a2_explore.cpp`) and refuses to
write `src/notamps.h` unless they agree: eleven bit for bit, PAST BLU to −102 dB (the
harness's first-block transient). It then measures a nine-point level table for each,
against its source capture played plainly, so the steer changes the sound and not the
volume — corrections run from −24 dB (LINEAR TR, PAST BLU at full steer) to +35 dB
(SINE BUG).

**The activation is a firmware choice.** `nam/nam_a2_runtime.h` compiles each activation
— as trained, a variable slope, a sine — as its own copy of the layer kernels, chosen once
per block, so a real amp pays nothing. The sine is `bend::FastSin`, 11th-order, within
−57 to −104 dB of `sinf` through whole networks. Freeze and the morph use the `bend`
operations and live weight rewrites the nine introduced.

**The seed slot, retired.** Random weights drawn with one amp's statistics all came out as
variations on one resonant, octave-flavoured sound — deep networks with random weights go
toward delay and resonance (Steinmetz and Reiss, 2020, [arXiv:2010.04237](https://arxiv.org/abs/2010.04237)).
Its generator also assumed the weights file grouped all conv weights first; the file
interleaves each layer's conv weights with its extras, so its per-region spreads and its
TILT landed on the wrong parameters. It is in git history (`src/seed_weights.*`, removed
2026-09-29). The DC blocker it needed stays: even trained captures show a small offset,
and some bends move it.

### How the nine were found, and what was left on the table

Written up 2026-10-01 from the session that did the work (28–29 Sep), because until now it
lived only in a transcript, and a later session re-proposed most of it as new.

**Step 1 — 86 variants, by ear** (listening page: claude.ai/artifact/NASvpkmkPMvM6xZQUSqdHZ).
Five directions came out of it, most promising first:

| Direction | What was heard | Since |
|---|---|---|
| Mutating a real capture | amp at 0.1, bright and strange at 0.3, further at 0.6 | built: MUTATE |
| Morphing between captures, and past them | halfway sounds like *an* amp; past one exaggerates it | built: PAST ×3 (past, not between) |
| Activation slope (LeakyReLU, 0.01 in the code) | at −1 a bright, fuzzy, full-wave sound; changeable live, even at audio rate | **not built** |
| Bias size | the "gain feel" — whether it cleans up when played softly | not built |
| Crossbreeding layers between captures | a lottery, some very different results | not built |

**Step 2 — the wider menu**, offered 2026-09-29. David chose network bending; the rest
was never rejected, only not taken up:

- *Setting the weights:* evolutionary search (MAP-Elites) over the listening map; training
  toward a property rather than a recording; training one sound into another; principal
  components of many TONE3000 captures as knobs (needs the lanes aligned first).
- *Changing it while it plays:* FiLM (scale and shift every layer from a knob);
  **network bending — chosen**; weights drifting under an LFO.
- *Changing the machine:* other activation functions (sine as a wavefolder stack, tanh,
  one per lane); a **second-input sidechain** from IN_R into the network, the cheap route to
  the brief's two-input target; **feedback** with a delay, a neural Karplus-Strong that
  makes the effect an instrument; a **rate knob** that stretches the gaps; destruction edges
  below 7-bit weights.

**Step 3 — ten bending candidates, scored** for range, smoothness, level safety and
whether they move live: freeze middle and early layers, four morphs past an amp (one
JCM800/Ampeg direction did not ship), early lane offset, fold, fading the gap-239 layers,
mutation. David took the proposed five plus four reserves: the nine that shipped first. He also
decided real amps stay exactly as captured, so steer acts only in NOT-AMPS.

**Measured 2026-10-01**, on the twelve: captures do not share an inner layout — the same
parameter in two captures correlates at 0.01, no better than shuffled (step 2's
lane-alignment caveat, confirmed) — and a 50/50 blend drops 7–32 dB in level and sits
spectrally away from *both* ends (Klon→Orange: 10 and 13 dB from them, which are 3.8 dB
apart). Consistent with "sounds like an amp": an amp, but not one between the two. So
blends and crossbreeds make third sounds, not intermediate ones.

### The measured search, 2026-10-02

Asked for not-amps that "definitely do not sound like synths through guitar amps", this
time chosen by measurement first and ears second:

- `tools/a2_explore.cpp` runs the real engine with any transform on the 29 Sep menu, its
  knob held or swept: activation (slope, sine, tanh), biases, the raw-input mix-in
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

So two of the nine shipped not-amps, OFFSET and NO LONG, measure as amps by both judges,
and the mix-in path — every variant — barely changes anything.

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
2026-10-02:** every one has zones where it does not sound like a guitar amp. These replace the
nine. Built into the firmware the same day (`src/notamp_dsp.h`, `src/notamps.h`); not yet
on the module.

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
3. ~~Design the seed landscape.~~ Replaced by the nine not-amps (2026-09-29), then by the
   twelve (2026-10-02). **Next: the twelve on the module.**
   - **CPU on `SINE PLX` and `SINE BUG` first** — the max on the RUN page. The Mac puts the
     sine at 1.5× the engine (~96% if that held); counting cycles says nearer 80%. Over
     ~90% and it needs a cheaper sine before anything else.
   - The five amps' worth of captures still play as before (the trained path is
     bit-identical on the Mac, but the kernels are now templates).
   - MIX: dry at 0, wet at 1, and on `RATE SVT` at 50% no comb-filter hollowness — that
     would mean the dry delay is not lining up.
   - Each not-amp across its steer, then with CV on CV_8; the feedback ones at full steer
     for runaway.
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
Kept — the starter set and the sources for retargeting the not-amps:

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
contributor, and a gate's threshold has to be adjustable to the source. CV_4 (+ CV_8),
which does nothing in AMPS by design, is free to set it there and keep STEER in NOT-AMPS.

**Not in this repo.** `nam/model_data_nam_a2.h` and `captures/*.a2nb` were committed
here until 2026-10-01, when they were removed from the whole history; `.gitignore` now
keeps them, and any `.nam`, out. They live on the working machine only. A fresh clone
builds without them — no compiled-in fallback, so no card means pass-through and
`NO CAP` — and gets captures by downloading the `.nam` files from the pages above with a
TONE3000 account and running `tools/nam_to_a2nb.py`. `tools/match_captures.py` takes the downloaded zips as they
come and says which `.nam` in them is each of the five, by comparing the network's own
parameters with the header's — so a capture retrained since shows as a near miss. After adding or removing the header, run `make clean` first: make does not see a header
appear, because `__has_include` leaves no dependency, so a stale build silently keeps the
previous choice. With the header on disk the build
compiles the JCM800 fallback back in, and `export_captures.py` and the host tools that
read it work as before.
