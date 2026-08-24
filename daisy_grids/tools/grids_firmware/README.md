# A new bank for a real Grids

Sorrow's banks and Mutable Grids' own map are the same data structure: 25 nodes
of 96 bytes (3 instruments x 32 steps), bilinearly interpolated between the four
corners of a 5x5 grid, thresholded by density, accented above 192. So a bank
derived in [../groove_nodes/](../groove_nodes/) is not merely *portable* to real
Grids hardware - it is a **byte-for-byte data substitution**. Same size, no code
change, nothing to fit in the flash budget.

That is the whole idea here: give a Grids owner a different instrument by
replacing 2,400 bytes of lookup table.

**Untested on hardware.** Nobody involved owns a Grids. Everything that can be
checked without one has been (see *What is verified*), but no one has yet run
this on a real module.

## The three banks

Measured with `som.py`'s coherence metric - how much more alike map neighbours
are than random pairs. It is a ratio, and reading it as "how smoothly X/Y morphs"
is wrong: that is the numerator alone, and every bank here sits within a few per
cent of every other on it. A high ratio mostly means a *large denominator* - the
far corners of the map are far apart.

| bank | corpus | patterns | coherence |
| --- | --- | ---: | ---: |
| `nodes_jazzlatin.cc` | Groove MIDI, jazz/latin/afro/NOLA/reggae/highlife | 4,793 | **41.9%** |
| `nodes_groove.cc` | Groove MIDI, all styles - human drummers | 11,155 | 38.8% |
| `nodes_club.cc` | Lakh, by rhythmic signature - four-to-the-floor, breaks, half-time | 60,000 | 34.9% |
The jazz/latin map scores highest, but not for the reason first written here.
The claim was that 25 nodes cover one family better than eight, which predicts a
*small* spread between distant cells - and Latin's is the largest of the three
(59). What its score actually says is that the SOM ordered a wide corpus well:
neighbours 34 apart inside a map whose far cells are 59 apart.

Which is the thing to keep hold of when reading these numbers at all. A high
ratio can come from close neighbours OR from distant far-cells, and it is mostly
the second. It is a check that the SOM converged, not a quality score, and
certainly not a league table - a map of 25 identical patterns would score near
100% and be useless.

Its nodes come out recognisably single-style - 76% bossa, 67% afrobeat, 55%
songo, 51% jazz - where the all-styles map is mixed everywhere and spends about
half its nodes on rock, which is only a quarter of that corpus.

## Generating a table

    python3 emit_resources.py jazzlatin > nodes_jazzlatin.cc

Paste the output over the `node_0` ... `node_24` block in `grids/resources.cc`.

The emitter permutes the nodes, and that matters: Grids' `drum_map[5][5]` does
not list its nodes in order. Emilie arranged the 25 patterns by ear so that
neighbours are musically related, and it measures - her arrangement scores 14.8%
against 5.2% for the same patterns in file order. Our banks come off a
self-organising map, which does that arranging automatically, so they are
already row-major correct; the emitter inverts her table so Grids' source needs
no other edit.

**Self-test.** `emit_resources.py original` re-emits Grids' own data unpermuted,
and the output is byte-identical to the node block in `grids/resources.cc`. If
that ever stops being true, the formatting has drifted.

## Building and packaging

Needs an AVR toolchain. On macOS: `brew tap osx-cross/avr && brew install
avr-gcc@14`. Note that `avr-objcopy` and `avr-size` come from `avr-binutils`, a
different keg, while the makefile expects one `AVRLIB_TOOLS_PATH` for everything
- so symlink both kegs' `bin` into a single directory and point at that.

Build against the Mutable submodule with the node block in `grids/resources.cc`
replaced by one of the tables here. The 2012 source compiles clean on avr-gcc 14:
the makefile already passes `-D__PROG_TYPES_COMPAT__`, which is what keeps the
deprecated `prog_uint8_t` alive.

    make -f grids/makefile AVRLIB_TOOLS_PATH=<combined-bin>/
    avr-objcopy -I ihex -O binary build/grids/grids.hex grids.bin
    python3 avr_audio_bootloader/fsk/encoder.py \
        --packet_size=128 --page_size=128 --sample_rate=40000 \
        --zero_period=4 --one_period=8 --pause_period=16 \
        -o grids_<bank>.wav grids.bin

**None of the period flags are optional either, and the encoder's defaults are
wrong for this bootloader.** `fsk/decoder.h` fixes the symbol durations the
module listens for:

    const uint16_t kPause = 16;  const uint16_t kOne = 8;  const uint16_t kZero = 4;

and classifies by threshold - `>= 12` is a pause, `>= 6` is a one, else a zero.
The encoder defaults to 8/16/64, so every zero arrives as a one and every one as
a pause: nothing ever CRCs and nothing is ever written. Mutable's own
`grids_1.0.wav` measures 4/8/16, which is how this was pinned down.

**`--packet_size=128` is not optional.** The bootloader reads one FSK packet into
`rx_buffer[SPM_PAGESIZE + 4]` and flashes it as one page, and `SPM_PAGESIZE` on
the ATmega328P is 128. The encoder's default of 256 would overrun the buffer.
Do not reuse the `--page_size=64` from the makefile's `SYSEX_FLAGS`: that is the
MIDI SysEx route, and at 64 the encoder never emits the inter-page blank the
flash write needs.

Mutable's Python tooling is Python 2 - `file()`, `map(ord, ...)`, str-vs-bytes
and a couple of `/` that must floor. `mutable-python3.patch` here is the minimal
diff against the submodule; apply it to a scratch copy rather than the submodule
itself.

## What is verified

- **The code is untouched.** Stock and all three bank builds come out at exactly
  12,122 bytes of text and 58 of data. Diffing stock against a bank build, every
  differing byte falls inside a 2,399-byte span - the drum map and nothing else.
- **The emitter is exact.** `emit_resources.py original` reproduces the node block
  in `grids/resources.cc` byte for byte, whitespace included.
- **The audio is correct.** `verify_wav.py` decodes each WAV the way the bootloader
  does - 96 packets of 128 bytes plus a big-endian CRC32 - and confirms every
  checksum passes and the payload matches the compiled firmware.

        python3 verify_wav.py

- **The format is pinned to a known-good reference.** The same decoder reads
  Mutable's own [grids_1.0.wav](https://pichenettes.github.io/mutable-instruments-documentation/modules/grids/downloads/grids_1.0.wav)
  - the file they publish for reverting a module to stock - and recovers 99 valid
  packets, 12,672 bytes, zero rejected. That is what makes the encoding claim
  worth anything: it is checked against a file known to flash real hardware,
  rather than against our own assumptions.

## How it gets onto the module

Grids' bootloader takes firmware as **FSK-encoded audio through the CLOCK
input** - `grids/bootloader/bootloader.cc` samples that jack and decodes it.
This is Mutable's own official update path, so no programmer, no opening the
case, no soldering:

1. Hold **RESET** while powering the module on. The LEDs flash to confirm the
   bootloader is listening.
2. Patch an audio output - phone, laptop, anything - into **CLOCK IN**.
3. Play the `.wav` at full volume, no EQ or effects.
4. The LEDs march while it loads, then flash and the module boots.

**It is reversible.** Mutable's stock firmware `.wav` re-flashes by exactly the
same route, so anyone trying this can put their module back the way it was.

## Licence

Grids is GPL-3.0-or-later. A modified binary may only be distributed with the
complete corresponding source - which is why the tables, the emitter and the
derivation tooling are all in this repo.
