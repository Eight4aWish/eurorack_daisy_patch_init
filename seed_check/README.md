# seed_check: is a Daisy Seed alive after it comes off a board?

A bench check for a Daisy Seed that has been desoldered, run on USB alone with nothing on its
pins. Written on 2026-11-12 for the Seed taken off the retired Seed MultiFX; that Seed (a 1.2)
passes everything.

## Run it

1. Put the Seed in its ROM bootloader: hold **BOOT**, tap **RESET**, let go of BOOT.
   `dfu-util -l` shows `[0483:df11]`, which on its own says the chip and its USB work.
2. Build and flash:

   ```sh
   mkdir -p build && make BUILD_DIR=build
   dfu-util -a 0 -s 0x08000000:leave -D build/seed_check.bin -d ,0483:df11
   ```

   "Error during download get_status" after "File downloaded successfully" is normal: the
   bootloader has already handed over.
3. Open the USB serial port (`/dev/cu.usbmodem*`). The report repeats every 3 s, and the user
   LED blinks twice a second while it runs. Sending any byte to the port reboots the Seed into
   the ROM bootloader, ready for the next flash without the buttons.

## What it checks

| Line | How | A fail means |
| --- | --- | --- |
| Board, clock | `CheckBoardVersion()`, `GetSysClkFreq()` | the wrong Seed, or the clock tree not up |
| SDRAM | all 64 MB written and read back twice, an address-unique pattern and its inverse, the cache flushed between | a dead chip, a stuck data line or a broken address line |
| QSPI | two 4 KB sectors near the end, each erased, written, read back and erased again, five runs | the flash chip or its lines |
| AUDIO | the audio callback counted over 1 s at 48 kHz / 48 samples (1000 blocks) | the codec or its clocks |
| PINS | D0–D30 read with pull-ups, then pull-downs; then each driven low in turn | low with pull-up: short to ground. High with pull-down: short to 3V3. A pin following its neighbour: a solder bridge |

**What it can't see:** a lifted pad or a cracked joint on a header pin. With nothing attached,
an open pin reads exactly like a good one. Look at the pads under a loupe, and check continuity
once it's on its next board.

**The QSPI test stays off the chip's last sector.** Through the memory-mapped window, the very
last byte of the 8 MB chip read back as the byte 32 before it (the controller's FIFO depth) on
every run, while the byte before it and an erase read fine, and the next sector down passed
every run. That is a read quirk at the end of the controller's address range, not the chip.

The firmware writes only to internal flash and to those two QSPI sectors, and erases the
sectors again before it finishes.
