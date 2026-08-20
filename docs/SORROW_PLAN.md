# Sorrow v2 — development plan

Plan for the next revision of **Sorrow** (`daisy_grids/`): more drum-model
variety, more pattern variety, opened-up randomisation, two settings pages, and
a later SD-sample path.

Status: **Phases 0-4 complete and confirmed on hardware.** Written 2026-08-20.

> **Design intent.** Sorrow is a dice machine, not an editable drum synth. The
> B8 toggle rolls a new kit; that gesture is the instrument. There is
> deliberately no per-drum macro editing — the v1 edit pages went unused because
> rolling the dice was more rewarding than tweaking. Everything below serves
> "roll again and get something genuinely different", not "dial in a sound".

## Target hardware

Electrosmith Daisy Patch.Init, hand-built module, **B8 toggle fitted, no OLED**
(the two compete for the same panel position — see [PANEL.md](PANEL.md)). Micro
SD slot present and wired 4-bit (`SDMMC_CK/CMD/D0..D3` in
`patch_init_schematic.pdf`). 64 MB SDRAM on the Patch SM.

### Prerequisite: flash the Daisy bootloader

v1 is a `BOOT_NONE` build living in internal flash. v2 needs the bootloader.

```sh
cd daisy_grids
make program-boot      # one-off: writes the Daisy bootloader to internal flash
make                   # BOOT_SRAM by default
make card              # copy build/sorrow.bin to the mounted FAT32 SD volume
```

Notes:

- `program-boot` overwrites internal flash, so the v1 image is replaced. After
  this the module always boots the bootloader, which waits ~2 s for a DFU upload
  before starting the app.
- **SD is the primary flashing route.** `make card` finds the single mounted
  FAT32 volume (or takes `SD_VOLUME=`), refuses to copy a `BOOT_NONE` build, and
  parks any other root `.bin` in `_firmware/` so a card shared with the
  `daisy_multiosc` unit cannot flash the wrong firmware into this module.
  `make card-list` shows candidates. The card must be FAT32, not exFAT.
- `make flash` (DFU during the grace window) remains as the fallback and rescue
  path, which is why the 2000 ms bootloader variant is the right one to install.
- `APP_TYPE` stays overridable, so `make APP_TYPE=BOOT_NONE` still produces a
  v1-style image for a unit without the bootloader.

## Verified constraints

Baseline `arm-none-eabi-size` on the v1 build:

```
text 99,512 + data 1,788 = 101,300 bytes of 131,072 (BOOT_NONE)  = 77% full
```

Roughly 29 KB spare. **The extra voice models do not fit** — each model is more
code, and every `HiHat<>` template instantiation is a separate copy. Switching
to `BOOT_SRAM` (the pattern already used by `daisy_joy_lite`) raises the app
budget to ~480 KB and leaves QSPI writable for persistent settings.

## Bugs to fix (found in the v1 code)

| # | Bug | Where |
| --- | --- | --- |
| 1 | **Kit RNG is effectively deterministic.** Both RNGs seed from `System::GetNow()`, which is ~480 ms at that point on every boot (six 80 ms LED self-test delays). Same kit sequence every power-up. | `src/main.cpp:724-727` |
| 2 | **External-clock latch never clears.** `g_use_ext_clock` is set and never reset; a patched clock latches until power-cycle. README claims reset returns to internal. | `src/main.cpp:492` |
| 3 | **Voices freeze in external mode.** `Process()` is skipped, so envelopes stall and resume mid-tail when flipping back. | `src/main.cpp:670` |
| 4 | README says a 4× clock multiplier; code uses 8×. | `src/main.cpp:173` |
| 5 | README's LED pulse counts are off by one vs `LedPulseState(g_sub_mode)`. | README lines 73-94 |
| 6 | Header comment promises `CV4=Volume`; never implemented. | `src/main.cpp:527` |
| 7 | Cosmetic: `t = g_led_ctr / sr` grows unbounded as float32, so LED pulse timing quantises after some hours. | `src/main.cpp:420` |

Fix for #1: free-run the kit RNG in the audio callback so its state when B8 is
flipped depends on how long the user waited. That is real entropy and costs two
lines.

## Control map (v2)

**B8 toggle — roll a new kit.** Either direction. The internal/external output
mode is deleted: audio always renders *and* triggers always fire on
B5/B6/CV_OUT_1. B8's positions carry no meaning, only its transitions; if the
panel is ever re-silked it wants to read `ROLL`.

**B7 short press — cycle pages**, LED flash count shows where you are. No long
press, no held shift.

| Page | LED | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- |
| **Home** | no flash | X | Y | Master density | Randomness |
| **Kit** | 1 flash | Kick density | Snare density | Hat density | **Sound wildness** |
| **Time** | 2 flashes | Tempo / clock ratio | Swing | Pattern length | **Pattern wildness** |

`CV_5`–`CV_8` modulate the Home row (X / Y / density / randomness) at ±50%, as
today. Settings pages use soft-takeover (already implemented as `SoftTakeover`);
Home stays live so performance feel is unaffected.

### Why K4 is wildness on both pages

Sound wildness and pattern wildness are genuinely different things, and the
three-page layout has room for both without cramming. Putting them at the same
knob position on both pages makes the pairing self-documenting on a module with
no screen: **K4 is always "how wild", the page says wild at what.**

Build both scalars from the start. If they turn out to want to move together,
linking them later is one line; splitting them later is not.

### Amount vs character

The same split applies in both domains, which keeps one mental model:

| | Amount (live, Home page) | Character (set-and-forget, settings) |
| --- | --- | --- |
| Pattern | Home K4 — Grids `randomness` | Time K4 — how far it reaches, how often it re-rolls |
| Sound | B8 — roll the dice | Kit K4 — how far the dice may throw |

### Master vs per-part density

Per-part knobs are **additive trims** around the Home master:

```
density_part = clamp(master + (trim_part - 0.5))
```

Trims centred reproduces v1 behaviour exactly, so nothing regresses. Master
still sweeps the whole kit as one gesture, and `CV_7`'s ±50% modulation of it
becomes a density sweep that preserves the kit's internal balance.

## Phases

### Phase 0 — Foundations ✅

- `APP_TYPE ?= BOOT_SRAM`; libDaisy's core Makefile derives the linker script,
  `-DBOOT_APP` and the DFU address from it. Sorrow's own `DFU_ADDR` now follows
  `FLASH_ADDRESS` instead of being pinned to internal flash.
- SD card flashing as the primary route: `make card` / `make card-list`.
- Bug fixes 1, 2, 4, 5, 6, 7.
- Clock drop-out timeout: 4 measured beats, 2 s floor, so slow clocks are not
  mistaken for a stopped clock.

Measured result: **101,344 B of 480 KB SRAM (20.6%)**, against 77% of internal
flash before — the headroom the later phases need.

### Phase 1 — Always-on outputs ✅

- Deleted `g_external_output` and every branch that depended on it. Audio renders
  every block and B5/B6/CV_OUT_1 fire on every step, so the internal kit and an
  external voice share one pattern (fixes bug 3 as a side effect).
- **B8 keeps its one-directional reroll** — flip out and back. The owner plays
  the double flip deliberately, so the edge was left as it was; only the audio
  mute on the way past is gone. The flag survives as `g_b8_prev`, now purely
  edge detection.

### Phase 1b — Clock rate detection ✅

The clock was the biggest day-to-day irritation: three sources (a Teensy module
emitting quarter notes, a EuroPi Pam's clone, a real Ornament & Crime running a
MIDI app) sending different and partly unknown resolutions, against a firmware
that hardcoded an 8x multiply on an assumed quarter-note input. The internal
clock ran 16ths and the external path ran 32nds, so patching a clock doubled the
tempo.

Rather than enumerate sources, Sorrow now measures the incoming pulse period and
derives the ratio itself, targeting a 16th-note step to match the internal clock.
Candidates are 24/8/4/2/1 ppqn — each at least double the next, so their period
bands don't overlap within one octave of tempo. The window is 88-176 BPM.

- Sticky detection: a band must be seen 3 times running before the ratio changes,
  so jitter can't flip it mid-pattern.
- Ratio is forgotten on clock drop-out, so a different source starts clean.
- Reset realigns the divider group with the bar.
- Divide and 1:1 ratios are edge-driven and cannot drift; only multiplying
  predicts intermediate steps from the measured period.
- **Bar LED**: long flash on step 0, short on step 16, on the Pattern page. With
  no screen this is how the detected ratio gets verified by eye.

Verified against a host-side sweep: correct for all five resolutions at 90, 100,
110, 120, 128, 140, 150, 160 and 174 BPM. Only the exact window edges (88 and
176) mis-map, which is inherent to a 2:1 window.

The Time page's manual ratio (Phase 5) becomes the override for sources outside
the window; it sets the same variable the detector does.

### Phase 2 — Voice pool ✅ (with Phases 3 and 4 folded in)

Delivered together, because they are not separable in practice: a pool needs a
selection rule, the selection rule *is* wildness, and wildness needs a knob.

`src/drum_voices.{h,cpp}` holds a `Voice` interface over per-model adapters,
three slots, and a pool per slot tagged by family and by an "exotic" rating. A
`ModelVoice<Model>` template carries the shared Init/Trig/Process plumbing, so
each concrete model only supplies its own `Randomize()` — which is where the
per-parameter tame and wild ranges live.

Ten models: `SyntheticBassDrum`, `AnalogBassDrum`, `ModalVoice` (kick);
`SyntheticSnareDrum`, `AnalogSnareDrum`, `ModalVoice`, `StringVoice` (snare);
`HiHat<SquareNoise, LinearVCA>`, `HiHat<RingModNoise, SwingVCA>`, `ModalVoice`
(hat). All in DaisySP's MIT `Source/` tree.

Wildness drives four things at once: the exotic ceiling, per-parameter range
width, the probability a slot defects from the kit's family, and pan spread.

The edit pages are gone, as intended — they were incoherent once a slot could be
any model. B7 now cycles Home → Kit. **The sequencer runs on every page**: a
settings page borrows the knobs, not the transport, so density changes are
audible as they are made. Home knobs stay live, Kit knobs use soft-takeover.

Per-part density landed here too, as additive trims around the Home master.

Cost: SRAM 101,664 → 124,500 B (25% of 480 KB). That is +22.8 KB for ten models,
which would have left about 7 KB of headroom under the old BOOT_NONE build.

Gotcha worth remembering: an unselected model is never processed, so its envelope
state stays frozen wherever it was left and resumes as a click when selected
again. Newly selected models are re-initialised before being randomized.

#### What bench testing changed

Five things that reading the code would never have surfaced:

1. **`AnalogSnareDrum` hangs the panel.** It recomputes two `powf` calls and five
   `Svf` `SetFreq`/`SetRes` pairs — each a `tanf` — per sample. That overruns the
   audio callback, which starves SysTick, which stops `System::GetNow()`
   advancing, which freezes `Switch::Debounce()` — so the module plays on while
   both switches die. Dropped from the pool. Every one of those computations
   depends only on parameters that change once per roll, so a forked,
   coefficient-hoisted version would be cheap and would earn it back.
2. **48 kHz was not affordable.** Measured on target: cheapest kit 53% of a
   sample period, most expensive 112%. Now 32 kHz, taking those to 35% and 75%,
   plus FPU flush-to-zero, which libDaisy never enables.
3. **Costs must be measured on the target.** Two desktop benchmarks called
   `AnalogSnareDrum` cheap, because host `libm` makes `powf`/`tanf` far cheaper
   than an M7 does. The firmware now benchmarks every model at boot and refuses
   kits over 72% of a sample period.
4. **The exotic rating gated instead of ramping.** Models went from never
   appearing to a third of all rolls across 0.1 of wildness travel, leaving most
   of the pool unreachable. Replaced with a weighted ramp.
5. **Models sustain.** `AnalogSnareDrum` rang for seconds at *every* decay
   setting; the physical models sustain by design. Both needed an output VCA.
   Worst case is now 400 ms, down from 3000 ms.

Also: per-model level normalisation from the same boot benchmark, a bar-marker
LED, USB telemetry, and `tools/voice_check` — a host harness whose every check
corresponds to one of the bugs above.

### Superseded plan for Phase 2

A thin `DrumVoice` interface (`Init / Trig / Process / Randomize`) over
per-model adapters, with three slots (kick / snare / hat), each holding a pool
of eligible models tagged by **family** and by how **exotic** they are.

Available in DaisySP, all in the MIT `Source/` tree:

| Group | Classes |
| --- | --- |
| Drums | `AnalogBassDrum`, `AnalogSnareDrum`, `SyntheticBassDrum`, `SyntheticSnareDrum`, `HiHat<NoiseSource, VCA, resonance>` |
| Physical modelling | `ModalVoice`, `StringVoice`, `Drip`, `KarplusString`, `Resonator` |
| Noise / exotica | `Particle`, `Dust`, `ClockedNoise`, `FractalNoise`, `Grainlet` |
| Synthesis | `VOSIM`, `ZOscillator`, `FormantOsc`, `HarmonicOsc`, `VariableShapeOsc` |

`ModalVoice` and `StringVoice` already expose the exact drum-voice interface
(`Init(sr)`, `Trig()`, `SetAccent()`, `SetFreq()`, `Process(bool)`), so they
drop into the existing `TrigWithAccent` template with no adapter. The `HiHat`
template alone yields 8 distinct hats (`SquareNoise`/`RingModNoise` ×
`LinearVCA`/`SwingVCA` × resonance).

### Phase 3 — Sound wildness ✅ (landed with Phase 2)

One scalar that widens every roll:

- Scales each model's parameter ranges. v1's are conservative — dirtiness caps
  at 0.40, kick FM at 0.50, default dirtiness 0.03.
- Raises the probability that the three slots pull from **different** families
  rather than agreeing, so a high-wildness roll can give an analog kick, a modal
  snare and a `Particle` hat.
- Widens which exotic models are eligible at all.

**Invariant:** rolling the dice must never touch X, Y, density, randomness, the
step counter or the Grids LFSR. Pattern and kit stay orthogonal, so a groove you
like survives any number of rerolls.

### Phase 4 — Kit page ✅

Landed with Phase 2 above.

### Phase 5 — Time page and pattern wildness

Tempo / clock ratio, swing, pattern length, pattern wildness.

- **Tempo / ratio.** Unpatched, K1 is internal tempo (v1 hardcodes 120 BPM).
  Patched, it becomes a manual override for the Phase 1b detector — an AUTO
  position plus explicit ratios (÷6 … ×4) for sources outside the 88-176 BPM
  detection window. Bug 2's timeout is load-bearing here: the knob changes
  meaning with the clock state, so that state must be able to change back.
- **Swing** needs a pending-trigger scheduler — a tick schedules a trigger N
  samples out instead of firing it synchronously. This is the only structurally
  awkward item in the plan. It also unlocks step-skip and ratcheting.
- **Pattern wildness** drives four mechanisms in `grids_port`:
  1. **Perturbation refresh rate.** `grids_port.cpp:99` re-rolls perturbation
     only at `step_ == 0`, once per 32-step bar. Make that interval
     wildness-dependent: 32 → 16 → 8 → 4 → 1. One changed condition takes the
     machine from coherent bar-length variation to per-step chaos.
  2. **Perturbation ceiling.** `randomness >> 2` at `grids_port.cpp:101` is a
     fixed scale; let wildness shift it.
  3. **Map drift.** At bar boundaries, nudge effective X/Y with probability
     rising with wildness, so the groove itself wanders.
  4. **Accent threshold jitter.** Pinned at 192 today, so accents always land in
     the same places.

### Phase 6 — SD sampled kits (post-launch)

Out of scope for the launch video. The design goal is the Bastl Citadel
workflow — one pattern, banks of sounds, CV modulation — without Citadel's
storage limit.

- WAV loader from SD, folder-per-kit convention on the card.
- One-shots decode into **SDRAM** at boot: 64 MB is ~680 s of mono 48 k/16-bit,
  so whole kits live in RAM and no streaming is needed.
- Sample kits join the Phase 2 model pool, so wildness can mix sampled and
  synthesised voices within one kit.
- **Ship no sample content.** The firmware loads user-supplied kits from the
  card. Extracting one's own Maschine expansions for personal use on one's own
  module is fine; redistributing those samples is not. Designing for
  user-supplied content from the start avoids a painful retrofit.

## Licensing

Unchanged: `daisy_grids` is **GPL-3.0-or-later** because `grids_port` is derived
from Mutable Instruments Grids. All DaisySP classes named above are in the MIT
`Source/` tree, not `DaisySP-LGPL/`; either way GPL absorbs them. See
`daisy_grids/LICENSE` and the root README's Credits & Licenses.

## Docs to update as work lands

- `daisy_grids/README.md` — control map, voices, bugs 4-6.
- `docs/PANEL.md` — Sorrow is absent from Per-firmware maps and should be listed
  alongside the Joy family as a non-contract firmware.
