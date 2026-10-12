# daisy_beads_patch_init: Wish on Daisy patch.Init()

> **Released: v1.0.0, 2026-10-12.** Tag `wish_patch_init-v1.0.0`, assets
> `wish_patch_init-v1.0.0.bin` and `wish_patch_init-v1.0.0-NOTICES.txt`; the page is
> at <https://eight4awish.com/modules/wish-patch-init/>.
>
> First bench the same day, on David's patch.init(), flashed over USB: steps 1–5 of the
> list at the bottom pass — sound, the knobs and their catch, all four qualities
> including the live switch to 32 kHz, the gates, and V/OCT. The first flash had PLAY
> and TONE on the wrong toggle positions; fixed in e753495. Steps 6 (load) and 7
> (saving) were not run before release. Panel: `build123d/panels/daisy_wish_patch_init.py`.

**Wish** — the granular texture synthesizer based on Mutable Instruments **Beads**
(Émilie Gillet, MIT) — on a **stock** Electrosmith Daisy patch.init(). No screen and
no hardware changes: flash it and play.

It is the same Beads DSP as [`daisy_beads`](../daisy_beads/) (Wish for the Hermetic
Modular Alchemy Lab), compiled the same way, with the same pot laws and the same load
governor. The Patch SM is the same STM32H750 at the same 480 MHz, so what was measured
there holds here. This app is the panel: four knobs, one button, one toggle and one LED
in place of Beads' twelve knobs and four buttons.

## Controls

Three layers of four knobs. The toggle picks **PLAY** or **TONE**; holding **B7** gives
**RANDOM**. The four PLAY knobs are the four with jacks.

| | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| **PLAY** (toggle down) | TIME | SIZE | DENSITY | PITCH |
| **TONE** (toggle up) | SHAPE | DRY/WET | FEEDBACK | REVERB |
| **RANDOM** (B7 held) | TIME random | SIZE random | SHAPE random | PITCH random |

A knob takes a parameter over only once it reaches the parameter's value (catch), so
changing layer never makes anything jump. DENSITY at noon is no grains; anticlockwise
regular, clockwise random. PITCH snaps through 0, 1, 3, 4, 7, 12 and 24 semitones. A
randomiser at noon does nothing. Clockwise, every new grain gets the same degree of
randomness, set by the knob; anticlockwise the degree is itself random, so some grains
stay put and others land anywhere. In delay mode they become a smooth random wander.

| Button | Job |
| --- | --- |
| **B7 tap** | FREEZE on/off. GATE IN 2 holds it too. The LED is lit while frozen. |
| **B7 held** | the RANDOM layer, while held |
| **B7 held 2 s**, turning nothing | SETTINGS (below); the LED breathes. Tap B7 to leave. |
| **B7 at power-on** | V/OCT calibration |

| Jack | Job |
| --- | --- |
| **CV_5** | V/OCT |
| **CV_6** | TIME CV |
| **CV_7** | SIZE CV |
| **CV_8** | DENSITY CV |
| **GATE IN 1** | SEED: triggers start grains |
| **GATE IN 2** | FREEZE, while high |
| **GATE OUT 1** | Beads' grain gates, with GATE OUT on in SETTINGS |
| **IN L / IN R** | audio in. IN R follows IN L when unpatched. |
| **OUT L / OUT R** | audio out |

CV is added at full depth; the ±5 V scaling is Beads' default calibration, as Wish.

There is no SEED button. The internal clock runs, as Beads boots, and DENSITY sets it.
Triggers on GATE IN 1 take over: SEED counts as patched while they arrive and for a few
seconds after, DENSITY divides or thins them, and the internal clock comes back once they
stop.

The LED is lit while frozen and flashes on each trigger at GATE IN 1.

## Settings

Hold B7 for 2 s without turning a knob; the LED breathes. A knob changes its setting
only once it is turned, and the LED blinks the new value's number.

| Knob | Setting | Values, anticlockwise to clockwise |
| --- | --- | --- |
| **KNOB 1** | QUALITY | 1 Digital · 2 Vintage digital · 3 Clean tape · 4 Dirty tape |
| **KNOB 2** | INPUT | 1 Mono (IN L, twice the buffer) · 2 Stereo · 3 Synth (no input: Beads' wavetables) |
| **KNOB 3** | GATE OUT | 1 off · 2 on: GATE OUT 1 carries the grain gates, and both audio outs Beads' mono mix |

A new quality or input waits while frozen, as on Beads. Settings, the values of the two
layers not under the hand, and the calibration are saved to QSPI a few seconds after the
last change. At power-on the layer the toggle picks takes the knobs as they are.

## Calibration

As Joy Lite, with Joy's two-point routine (`common/voct_cal.h`): hold B7 through
power-on. LED solid: patch 1 V into CV_5 and press B7. LED blinking: patch 3 V and
press. A flurry means done. Until then it uses Joy Lite's defaults.

## Build & flash

```sh
make                 # build/wish_patch_init.bin
make program-dfu     # through the Daisy bootloader's DFU, in its grace period
```

It runs from SRAM through the Daisy bootloader (`BOOT_SRAM`), as Joy and Mirth do: copy
`wish_patch_init.bin` to a FAT32 SD card on its own and power-cycle, or use `make
program-dfu`. A module without the bootloader needs it once (`make program-boot`).

## Shared with daisy_beads

- Beads' sources, from `deps/mutable/eurorack/beads`, the same list.
- `daisy_beads/src/upstream/`, searched first: its two headers shadow Beads' own
  (dirty tape at 48 kHz, as libDaisy has no 24 kHz rate; Codec::Frame alone).
- `daisy_beads/src/pot_laws.h`: Beads' pot laws and CV filters.
- The load governor and the SEED handling, copied, as the Alchemy app is released.

## Differences from Wish for the Alchemy Lab

- **Three layers of four knobs** in place of two of six. DENSITY and PITCH are on PLAY,
  SHAPE and DRY/WET on TONE.
- **No SEED button.** Grains come from the internal clock, or from GATE IN 1.
- **GATE OUT has its own jack**, so the gates no longer replace audio R; the audio is
  mono while it is on, as Beads makes it.
- **V/OCT is calibrated** by the module's own routine, not a board calibration.
- **No gesture to restart the input gain.** It is automatic, through Beads' AGC, as on
  Wish, and restarts when the input mode changes.
- **No USB log.** The Patch SM's own LED, behind the panel, lights while the load
  governor is thinning grains.

## First bench session

1. **Boot and sound.** Audio in, DRY/WET up, DENSITY clockwise: grains.
2. **The knobs.** Each PLAY knob moves its parameter; flip the toggle and the TONE knobs
   catch rather than jump.
3. **Qualities.** Step through all four in SETTINGS. Vintage digital restarts the codec
   at 32 kHz. Sorrow has run a Patch SM at 32 kHz from boot, but switching rate while
   running is new here; listen for pitch and for a clean restart.
4. **Gates.** Triggers into GATE IN 1 seed grains, and the LED flashes. A gate on GATE
   IN 2 freezes. GATE OUT 1 pulses with the grains when it is on.
5. **V/OCT.** Calibrate, then check octaves with PITCH at noon.
6. **Load.** Digital stereo, DENSITY full clockwise, PITCH random up: no freeze, and the
   Patch SM's own LED lights while grains are thinned.
7. **Saving.** Change a setting and a TONE knob, wait five seconds, power-cycle.

## Licence

MIT, as Beads. Licence notices for everything compiled into the binary:
[`NOTICES.txt`](NOTICES.txt).
