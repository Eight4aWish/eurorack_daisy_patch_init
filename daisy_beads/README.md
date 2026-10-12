# daisy_beads: Wish

> **In development (0.1.0-dev, unreleased; on David's module since 2026-10-12).**
> Émilie Gillet published the Beads source on 2026-10-11 (pichenettes/eurorack
> `d8f4739`); this port was built and first flashed the next day. It boots at 480 MHz,
> plays, and runs Digital mono at about 40% CPU on average with grains firing, 65% at
> peak. The four qualities, the inputs and the jacks are still to be checked one by one
> on the bench. J3 and J6 are coded as working jacks but can't be tested on David's
> board until the replacement arrives.

A granular texture synthesizer for the **Hermetic Modular Alchemy Lab (V2)**, based on
the **Beads** texture synthesizer by Émilie Gillet (Mutable Instruments), MIT. Beads
followed Clouds: a granular processor with four recording qualities, a delay mode, a
reverb, and a wavetable synth for when nothing is patched in. Wish runs Beads' own DSP,
compiled from Mutable's source, behind the Alchemy Lab's six knobs, three buttons and
LED rings.

The name is from the rhyme: *eight for a wish*.

## Panel

```
P1 TIME            [B1 FREEZE]    P2 SIZE
P3 SHAPE           [B2 SEED]      P4 DENSITY
P5 PITCH           [B3 QUALITY]   P6 DRY/WET
```

**Hold B3** for the second layer. Each randomiser sits on its own parameter's pot:

```
P1 TIME random                    P2 SIZE random
P3 SHAPE random                   P4 FEEDBACK
P5 PITCH random                   P6 REVERB
```

| Control | Does |
| --- | --- |
| **TIME** | where in the recording grains are read from: fully anticlockwise is the most recent audio, clockwise reaches further back. In delay mode, the delay time |
| **SIZE** | grain length; fully clockwise is Beads' delay mode |
| **SHAPE** | grain envelope |
| **DENSITY** | noon is no grains; clockwise of noon, random grains, denser as it turns; anticlockwise, regular ones. With a clock on J4 it divides or thins the clock instead |
| **PITCH** | noon is the input's pitch; it snaps through 0, 1, 3, 4, 7, 12 and 24 semitones each way. Add V/OCT on J3 |
| **DRY/WET** | fully clockwise is grains only |
| **randomisers** | Beads' attenuverters with nothing patched: noon is none, clockwise a smooth random drift, anticlockwise a fresh random value per grain |
| **FEEDBACK**, **REVERB** | as on Beads |
| **B1** | FREEZE on/off; J5 high freezes too |
| **B2** | SEED. While its LED breathes, the internal clock (DENSITY) makes the grains. A tap switches to manual: one grain per tap, held for a stream. Hold B2 for 4 s, or hold B2 and press B1, to return to the clock |
| **B3** tap | quality, cycled in Beads' order: Digital (grey), Vintage digital (teal), Clean tape (orange), Dirty tape (dark red). It waits while frozen, as on Beads, and blinks until it applies |
| **B3** hold, let go after 1.5 s without turning anything | restart the input level learning (AGC) |
| **B3 + B1** | input: **Mono** (J1 only, the default) / **Stereo** / **Synth** (no input: Beads' wavetable synth). B1's LEDs show it: white, blue, magenta |
| **B3 + B2** | gate output: J10 carries Beads' grain gates instead of the right channel, and J9 a mono mix |

LEDs: B1 lights when frozen. B2 breathes while the clock is latched and flashes on each
seed. B3's upper LED is the quality colour. As on Beads, it dips as the recording since
the last freeze is about to wrap round and overwrite itself (Beads' overwrite reserve).
Its lower LED shows the input level, blinking while the AGC learns.
The rings show the knob values.

## Jacks

| Jack | Is |
| --- | --- |
| J1, J2 | audio in L, R (Mono uses J1 only) |
| J3 | V/OCT, added to PITCH |
| J4 | SEED trigger (delayed 2.5 ms, as Beads does, so a sequencer's pitch has settled) |
| J5 | FREEZE gate |
| J6 | TIME CV |
| J7 | SIZE CV |
| J8 | DENSITY CV |
| J9, J10 | audio out L, R |

CV is added at **full depth**, at Beads' own scaling (TIME and SIZE ±1.6 over ±5 V,
DENSITY ±1, PITCH 1 V/oct). Attenuate off-module. SHAPE CV and Beads' ASSIGN CV have no
jacks; there are six, against Beads' eight.

## What differs from Beads, and why

- **Twelve pots become six, on two layers.** The randomisers replace the
  attenuverters. Because CV is always added at full depth, a randomiser stays a
  randomiser whether or not its jack is patched, and V/OCT tracks without a knob at
  full. What is lost is Beads' patched-and-anticlockwise case, where CV sets the
  amount of randomness.
- **No normalisation probe.** Beads can tell a patched jack from an empty one; the
  Alchemy Lab can't. So the audio input mode is a setting (B3 + B1) rather than
  automatic. SEED counts as patched while triggers arrive, and for 4 s after (or four
  of their own periods, if longer, up to 30 s); then the internal clock takes over
  again, where Beads would wait silently for the next trigger.
- **Dirty tape at 48 kHz.** Beads runs its codec at 24 kHz for dirty tape. libDaisy
  can't, so Wish uses Beads' own 48 kHz path, in which the wet signal is
  downsampled in software. It's the one line changed in Beads' code: see
  [`src/upstream/beads/dsp/processor.h`](src/upstream/beads/dsp/processor.h).
- **Memory.** The 768 KB of delay lines are in SDRAM, the 64 KB reverb in on-chip D2
  RAM. On Beads it was all on-chip. The CPU gauges are how to tell whether that
  costs anything.
- **Left for later:** manual input gain (the AGC only, for now), Beads' saving of a
  frozen buffer across power-off, and calibration of its own (the Alchemy Lab's
  factory CV calibration is used).

Settings (quality, input mode, gate output) and the second layer's knob values are
saved three seconds after a change, and restored at power-on.

## Build and flash

```sh
make libdaisy        # once: the Alchemy SDK's pinned libDaisy
make                 # build/wish.bin
make program-live    # Wish (or Secret) running: reboot into DFU over USB and flash
make flash           # module already in DFU: hold B3 at power-on
make host-test       # Beads' DSP as built here, rendered on the Mac (see below)
```

Built like [`daisy_chaos`](../daisy_chaos/) (Secret): the Alchemy SDK from
`deps/alchemy-sdk` with its own libDaisy, `BOOT_SRAM`, 480 MHz. Beads' sources come
from `deps/mutable/eurorack/beads`. Two of its headers are shadowed from
[`src/upstream/`](src/upstream/), and the Makefile says which and why. After bumping
the submodule, diff the shadowed `processor.h` against upstream's.

`make host-test` builds [`tools/host_render.cpp`](tools/host_render.cpp) natively. It
renders every quality in mono and stereo, plus delay mode, freeze, the randomisers at
their ends, an external SEED clock, manual seeding, gate output, and synth mode at
every quality. Each case must stay finite, keep its limiter under 1.25 before the
output clamp, and make sound. WAVs land in `build/host/`.

While Wish runs, the front USB-C carries its log and gauges (CPU, sample rate,
quality, input, freeze, SEED state):

```sh
node ../deps/alchemy-sdk/tools/hostlink-cli/hostlink.mjs -p /dev/cu.usbmodem<serial> watch
node ../deps/alchemy-sdk/tools/hostlink-cli/hostlink.mjs -p /dev/cu.usbmodem<serial> logs --follow
```

To go back to Secret: `make -C ../daisy_chaos program-live`.

## Files

- [`src/wish.cpp`](src/wish.cpp): the platform layer (panel, jacks, audio callback,
  codec rate, saved state, LEDs, HostLink)
- [`src/pot_laws.h`](src/pot_laws.h): Beads' pot laws and filters
- [`src/upstream/`](src/upstream/): the two shadowed Beads headers
- [`tools/host_render.cpp`](tools/host_render.cpp): the Mac render check
- [`NOTICES.txt`](NOTICES.txt): licence notices for `wish.bin`, from
  `python3 common/tools/notices.py wish`

## Credits

Beads' DSP and the parts of stmlib it uses are © 2012–2021 Émilie Gillet (Mutable
Instruments), MIT, each file under its own notice. Files here that carry her code, or
follow it closely, carry her notice too: [`src/pot_laws.h`](src/pot_laws.h),
[`src/wish.cpp`](src/wish.cpp), [`tools/host_render.cpp`](tools/host_render.cpp) and
the two in [`src/upstream/`](src/upstream/). The stand-in `codec.h` declares one
struct as Beads' own `codec.h` does, which is © 2018 Fred's Lab and Mutable
Instruments, MIT. The notices that must go with `wish.bin` are in
[`NOTICES.txt`](NOTICES.txt).

Following Mutable's guidelines for derivative works, the firmware has a name of its own
and does not carry the Mutable Instruments name. "Beads" and "Mutable Instruments" are
used only to say where it comes from. Not affiliated with, or endorsed by, Mutable
Instruments or Hermetic Modular.
