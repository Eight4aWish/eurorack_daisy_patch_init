# daisy_chaos: Secret

> **Released: v1.0.0, 2026-10-02.** Tag `secret-v1.0.0` on `a29bb01`, asset
> `secret-v1.0.0.bin`; the page is at <https://eight4awish.com/modules/secret/>.
> Released with steps 3, 4 and 6 below still open. The four low-topping models ship
> as they are, listed as a known limit.
>
> **Running on the Alchemy Lab since 2026-09-30.** First bench session:
> sound and the gated envelope work. GATE moved from J6 to J4, and Drone now
> ignores GATE, because its re-seed clicked on every sequenced note. The TAME
> listening tests, V/Oct accuracy and the scope check (steps 3, 4 and 6 below)
> are still to do. The plan and the measurements behind it are in
> [`docs/SECRET.md`](../docs/SECRET.md).
>
> **2026-10-01, second session, with the USB log.** The occasional freeze, mostly
> on Chua, was the load governor living in the control loop: Chua's audio blocks
> reached 94% of their time, and one past 100% starved the loop that should have
> throttled it. The governor now runs in the audio callback, and no freeze has
> been seen since. Chua at its step cap (~1.3 kHz) is throttled to about half
> speed, so its Teensy-era `maxStepsPerSecond` is too high for this 400 MHz
> board. The remaining logged clicks are Chua's documented stutter corner (guard
> re-seeds at CHAOS a 9.25–10.9, CHAR b 12–13.7), a model change, and Duffing's
> near-vertical well switches above ~650 Hz, which is aliasing rather than a
> fault. The Drone/Gated switch no longer clicks.
>
> **Twelve models (2026-10-01).** Bank 2 was chosen by ear from nine measured
> candidates (`docs/SECRET.md`, section 3) and added after bank 1 as one list of
> twelve on B1. A bank selector waits for a third bank. Every model's step cap
> is measured on the board with `make BENCH=1`: at its cap with TAME on, a block
> takes 64–71% of its time, so nothing can overrun. On this chip four models top out
> inside the playing range, and a note above the top plateaus: Hindmarsh–Rose
> ~200 Hz, Chua ~410 Hz, Colpitts ~570 Hz, Moore–Spiegel ~730 Hz, at mid settings.
> A bigger integration step would lift them 3–4×, at some change in sound;
> undecided (`docs/SECRET.md`).

A chaotic oscillator for the **Hermetic Modular Alchemy Lab (V2)**. It runs
continuous strange attractors as audio, with V/Oct, and a **TAME** control that
goes from free chaos to a locked note without losing the grit in between.

*Seven for a secret, never to be told.* **Secret** is the release name. It
succeeds the Teensy 4.1 **Chaos** module in
[`eurorack_modules`](https://github.com/Eight4aWish/eurorack_modules), whose
firmware is frozen and whose hardware will be repurposed.

## Panel

| Control | Job |
| --- | --- |
| P1 **TUNE** | 27.5–880 Hz, exponential; plus V/OCT on J3 |
| P2 **CHAOS** | the bifurcation parameter; plus CV on J5 |
| P3 **CHAR** | the secondary parameter |
| P4 **TAME** | free chaos (0) to a locked note (1); plus CV on J6 |
| P5 **AD** | envelope attack + decay |
| P6 **SR** | envelope sustain + release |
| B1 | model, twelve in one list (see below) |
| B2 | TAME mode: Auto, Force, Sync (Auto is the model's own choice) |
| B3 | envelope: Drone (VCA open) or Gated by J4 |

Each button sits between the knobs it belongs with: B2 beside TAME, B3 between AD and SR
(swapped 2026-10-02; until then B2 was the envelope and B3 the TAME mode).

| Jack | Job |
| --- | --- |
| J3 | V/OCT in |
| J4 | GATE in, above +1.2 V. In Gated mode a rising edge opens the envelope and re-seeds the attractor, as on the Teensy. Drone ignores it: a re-seed with the VCA open clicks |
| J5 | CHAOS CV in |
| J6 | TAME CV in |
| J7 / J8 | X / Y CV out: the raw attractor, for a scope (Tiliqua `xbeam`) |
| J9 / J10 | audio L (X) / R (Y) |

**B1's twelve models**, in order, by the colour both of its LEDs show:

| | Bank 1 | | Bank 2 |
| --- | --- | --- | --- |
| orange | Rössler | red | Driven pendulum |
| yellow | Van der Pol | cyan | Lorenz–Lü–Chen |
| blue | Lorenz | violet | Moore–Spiegel |
| magenta | Chua | lime | Forced Brusselator |
| green | Duffing | pink | Chaotic Colpitts |
| white | Coupled Rössler | teal | Hindmarsh–Rose |

Bank 2's colours sit in the hue gaps between bank 1's. What each one is, and why
it was chosen, is in `docs/SECRET.md`, section 3.

CV into CHAOS and TAME: ±5 V sweeps the knob from its centre to either end. The
rings show knob plus CV. Each button's LED shows its current choice by colour.

Boot gestures belong to the board: **B3** held at power-on enters DFU for
flashing, and **B1 + B2** runs the factory CV calibration.

Not in this first build: presets, EXT DRIVE (J1), SYNC in (J2), FREEZE, and a
V/Oct calibration of its own (see below).

### Faceplate

`panel/` holds a Secret faceplate to have made. It is Hermetic's own front-panel template
from the Alchemy SDK (`deps/alchemy-sdk/panel/`, MIT), with the placeholder text replaced
by Secret's labels. Every hole, LED window and plating note is still Hermetic's. Lettering
is bare ENIG copper through a solder-mask opening, the same way Hermetic makes the stock
panel. J1/J2 are left unlabelled because Secret does not use them.

```sh
python3 panel/make_panel.py --gerbers   # secret_panel.kicad_pcb, the JLCPCB zip, a preview
```

The script needs KiCad 9's `kicad-cli`. To order, upload `secret_panel_gerbers.zip` to
JLCPCB with the options from Hermetic's `panel/README.md`: ENIG, edge plating, black
solder mask, and its order comment ("Please plate all holes and all drills. Plate all
edges as indicated by F.cu boxes. Four areas are left for tooling handles."). The
website's panel image is a different thing: a render in the series style, made with
`build123d/panels/alchemy_secret.py`.

## Build

Like the other apps here, a standard Daisy Makefile, laid out after Hermetic's
[`alchemy-template`](https://github.com/hermetic-modular/alchemy-template). The
difference: it builds against the **SDK's own pinned libDaisy**
(`deps/alchemy-sdk/vendor/libDaisy`), not `deps/daisy/libDaisy`. They are
different commits, and this is the one the SDK is tested against.

```sh
git submodule update --init --recursive deps/alchemy-sdk
cd daisy_chaos
make libdaisy      # once
make               # build/secret.bin
```

Needs `arm-none-eabi-gcc` 12 or later. The newlib "`_close` is not implemented"
link warnings are normal: the SDK's own template gives the same eight.

**Measuring step caps:** `make clean && make BENCH=1`, then flash. At power-on,
before audio starts, it runs every model flat out at its step cap, at TAME 0 and
TAME 1, and logs the block load and cycles per step over USB (`logs`, below). It
runs in the control loop, so a model that costs too much can't freeze anything.
It is for setting `maxStepsPerSecond` from this chip, not for playing:
`make clean && make` again afterwards.

## Flash

**With Secret already running**, no buttons:

```sh
make program-live    # needs node, and dfu-util >= 0.11
```

It asks the module over USB (HostLink) to reboot into its bootloader, and has
`dfu-util` wait for it in the same step. The reboot only opens the bootloader's
~2 s window, so a separate check-then-flash misses it.

**From anything else** (another firmware, or a first install):

1. Connect the **front-panel** USB-C.
2. Power on while holding **B3**. The rings spin a warm-white comet, then breathe
   slowly: the module is in DFU mode.
3. `make flash`, or load `build/secret.bin` in the
   [Hermetic Modular Web Programmer](https://hermeticmodular.com/program).

`make flash` refuses any DFU device that doesn't offer QSPI at `0x90040000`. The
one that doesn't is the STM32 ROM DFU (`@Internal Flash /0x08000000`), reachable
only through the Seed2 DFM's onboard micro-USB and meant for installing the
bootloader itself. `make dfu-list` shows what's attached.

## Watching it over USB

Secret runs HostLink's diagnostics on the front USB-C:

```sh
HL=../deps/alchemy-sdk/tools/hostlink-cli/hostlink.mjs
node $HL -p /dev/cu.usbmodem* logs --follow   # boot line, panel changes, clicks
node $HL -p /dev/cu.usbmodem* watch           # live gauges
```

**Use the `cu.*` port on macOS.** With no `-p` the CLI opens the first
`/dev/tty.usbmodem*`, which blocks waiting for carrier detect, so the CLI hangs
before sending anything. A real timeout from the module takes 3 s, so a longer
hang is the port, not the firmware.

| Gauge | Meaning |
| --- | --- |
| `cpu.avg`, `cpu.max` | audio callback load, %. The peak resets on each model change |
| `gov.scale` | load governor: 1 = not throttling |
| `steps` | RK4 steps per sample the current pitch asks for, which sets the cost |
| `model`, `hz`, `chaos`, `char`, `tame`, `gate`, `env` | what the DSP is getting |
| `guard` | divergence-guard re-seeds on the current model |
| `jumps` | output discontinuities seen (see below) |
| `cal` | whether the board's CV calibration record was found |

**The click log.** The audio callback watches every sample-to-sample step. One
over 0.5 of full scale is a discontinuity, not a waveform: a full-scale sine at
880 Hz moves at most 0.12 per sample. For each one it logs the model, CHAOS, CHAR,
pitch, TAME and its resolved mode, Drone or Gated, and what else happened in that
block: a `guard` re-seed, a `sync-pull`, a new SYNC `snapshot`, a `gate` re-seed,
a `model` change, or the `governor` holding the pitch back. At most four lines a
second; `+N more` counts the ones in between. Panel changes are logged too, so
the clicks can be read against what was being played.

## First time on the bench

In order, so a failure points at one thing:

1. **It boots.** The rings show the six knob positions, and B1's LED is orange
   (Rössler).
2. **Sound.** J9/J10 into the mixer, TUNE at noon (~155 Hz), TAME at 0: Rössler's
   rough, pitched drone.
3. **TAME.** Turn it up: free, then locked but still gritty, then a clean
   periodic tone at the top. Try each model on B1, and B2 to compare Force and
   Sync. These are the listening tests from `SECRET.md` section 2.
4. **V/Oct.** With TAME at 1 (strictly periodic, so a tuner can read it), feed J3
   from a quantiser (Scales) and check octaves. TUNE trims the offset. If
   octaves come out consistently stretched or squeezed, that's the input gain,
   and the reason for the calibration below.
5. **Gate.** B3 to Gated, a clock or gate into J4, AD and SR to taste.
6. **Scope.** J7/J8 into Tiliqua's `xbeam` as X/Y.

The load governor runs inside the audio callback: if a block takes more than 75%
of its time, the next one runs fewer integration steps, and the pitch goes flat
rather than the module freezing. It lights the Seed's own LED, which may not be
visible behind the panel; the `gov.scale` gauge shows it too. The first build ran
the governor from the control loop, where an overrunning callback starves it, so
it could never act. If a high note sounds flat on one model, that's the governor,
and that model's `maxStepsPerSecond` needs retuning for this board.

## Known limits of this build

- **V/Oct accuracy.** The board's factory calibration measures each jack's zero
  point, but its gain is a design constant (`CvInput::SetCalibration`), so the
  scale is only as good as the resistors. A two-point calibration of our own,
  like Joy's `common/voct_cal.h`, comes next if step 4 shows it's needed.
- **CV is read at 1 kHz** through the board's smoothed `AnalogControl`, not at
  audio rate. That's fine for notes and modulation, but not for audio-rate FM.
- **X/Y CV updates once per audio block** (2 kHz), not per sample.
- **No presets.** The knobs are physical; the three button choices reset at power-on.
