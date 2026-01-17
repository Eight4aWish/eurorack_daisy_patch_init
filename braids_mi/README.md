# braids_mi

A "genuine" Braids-style oscillator app for Daisy Patch SM, built from the original Mutable Instruments Braids macro-oscillator DSP (`braids/macro_oscillator.*` + dependencies) in the shared `deps/mutable/eurorack` tree.

## Controls (Patch SM)

- `CV_4` (knob): **Model** select (within the current bank).
- `B8` (toggle): **Shift/page**.
- `B7` (button): **Model bank** select (cycles through 4 banks).
- `GATE_IN_1`: **Strike** (excite many models).
- `GATE_IN_2`: **Sync** (edge into the macro oscillator sync buffer).

### Model banks

This project has two build variants, which affects how many Braids models are available.

- **`BRAIDS_VARIANT=lite` (DFU/internal flash)**: reduced model set (fits the STM32H750's 128KB internal flash).
- **`BRAIDS_VARIANT=full` (bootloader/QSPI recommended)**: full upstream Braids model set.

- **Lite included models**: 13 (analog-style subset)
- **Full included models**: 48 (the original Braids `MacroOscillatorShape` list)
- **Banks**: 4 banks (`B7` cycles banks)

- **Change bank**: press `B7` (each press advances to the next bank)
- **LED feedback**: the front-panel LED is driven via `CV_OUT_2` and blinks `1..4` times on bank change (bank 1..4). When idle it is **off**.

#### Lite (DFU) bank split (13 models)

Bank 1:
- CSAW
- MORPH
- SAW_SQUARE
- SINE_TRIANGLE

Bank 2:
- BUZZ
- SQUARE_SUB
- SAW_SUB
- SQUARE_SYNC

Bank 3:
- SAW_SYNC
- TRIPLE_SAW
- TRIPLE_SQUARE
- TRIPLE_TRIANGLE

Bank 4:
- TRIPLE_SINE

### Page A (B8 OFF)
- `CV_1`: Timbre (parameter 1)
- `CV_2`: Color (parameter 2)
- `CV_3`: Output level (VCA)

### Page B (B8 ON)

#### Internal envelope
- `CV_1`: Attack time
- `CV_2`: Decay time
- `CV_3`: Output level (VCA)

The envelope is controlled by `GATE_IN_1`: attack on rising edge, sustain while held, decay on release. It is applied to the output level.

### CV Modulation
- `CV_5`: V/Oct pitch (Patch SM bipolar input normalized ~-1..+1; **0V ≈ 0.0**). Mapped to ±60 semitones around 0V. Default 0V base note is **C3 (MIDI 48)**.
- `CV_6`: Timbre modulation
- `CV_7`: Color modulation

#### Tuning 0V reference
If you want **0V = C2** (or any other reference), override at build time:

- `make ... VOCT_BASE_MIDI=36` (0V = C2)
- `make ... VOCT_BASE_MIDI=48` (0V = C3)

If your hardware reads 0V slightly off-center (not ~0.5), you can trim the center:

- `make ... VOCT_CENTER_NORM=-0.01f`

## Model list (lite / DFU build)

- CSAW
- MORPH
- SAW_SQUARE
- SINE_TRIANGLE
- BUZZ
- SQUARE_SUB
- SAW_SUB
- SQUARE_SYNC
- SAW_SYNC
- TRIPLE_SAW
- TRIPLE_SQUARE
- TRIPLE_TRIANGLE
- TRIPLE_SINE

## Memory constraints (why this DFU build is trimmed)

On Daisy Patch SM (STM32H750), there are multiple memories involved:

- **Internal FLASH** (on the STM32H750): this is where code normally runs from after reset.
	- In libDaisy, the linker script used by this project for DFU is `STM32H750IB_flash.lds`, which defines **128KB** at address `0x08000000`.

## Building

This app is typically built as a Daisy bootloader QSPI app, using the full Braids oscillator set.

- Default build (BOOT_QSPI + full):
	- `make -j DAISY_ROOT=../../deps/daisy`
- DFU/internal flash build (BOOT_NONE), using the lite subset:
	- `make -j DAISY_ROOT=../../deps/daisy APP_TYPE=BOOT_NONE BRAIDS_VARIANT=lite`
	- When you flash over **DFU**, you are programming this internal flash.

- **QSPI flash** (external memory on the board): much larger (commonly **8MB**) and mapped at `0x90000000`.
	- A QSPI-linked build places `.text` (code) and/or big resources into that address range.
	- Writing QSPI usually requires an ST-Link/OpenOCD workflow (or a dedicated QSPI programmer flow). It is **not** covered by the simple `dfu-util` internal-flash write.

- **RAM** (SRAM/DTCM/etc): used for audio buffers/state at runtime. You typically have plenty compared to internal flash; the hard limit we hit was **code+const data size in internal flash**.

### Why “full Braids” didn’t fit DFU/internal flash

The original Braids macro-oscillator supports many models and pulls in large lookup tables/resources (wave tables, window tables, etc.). That combination tends to exceed the **128KB internal flash** budget.

So there are two workable configurations:

1. **QSPI build (fuller feature set)**
	 - Link for QSPI (`STM32H750IB_qspi.lds`), code/resources live around `0x900...`.
	 - Needs a way to program QSPI (typically ST-Link).

2. **DFU build (this one)**
	 - Link for internal flash (`STM32H750IB_flash.lds`) so DFU works.
	 - **Trimmed model set** (13 analog-style models) and a smaller macro-oscillator implementation so everything fits.

You can see the current build’s usage in the link step output (example from this repo): about **~97KB / 128KB FLASH** used.

## Original Braids model list (reference)

This is the full original Braids `MacroOscillatorShape` list from Mutable Instruments. It is **not** what this DFU/internal-flash build contains.

Bank 1:
- CSAW
- MORPH
- SAW_SQUARE
- SINE_TRIANGLE
- BUZZ
- SQUARE_SUB
- SAW_SUB
- SQUARE_SYNC
- SAW_SYNC
- TRIPLE_SAW
- TRIPLE_SQUARE
- TRIPLE_TRIANGLE

Bank 2:
- TRIPLE_SINE
- TRIPLE_RING_MOD
- SAW_SWARM
- SAW_COMB
- TOY
- DIGITAL_FILTER_LP
- DIGITAL_FILTER_PK
- DIGITAL_FILTER_BP
- DIGITAL_FILTER_HP
- VOSIM
- VOWEL
- VOWEL_FOF

Bank 3:
- HARMONICS
- FM
- FEEDBACK_FM
- CHAOTIC_FEEDBACK_FM
- PLUCKED
- BOWED
- BLOWN
- FLUTED
- STRUCK_BELL
- STRUCK_DRUM
- KICK
- CYMBAL

Bank 4:
- SNARE
- WAVETABLES
- WAVE_MAP
- WAVE_LINE
- WAVE_PARAPHONIC
- FILTERED_NOISE
- TWIN_PEAKS_NOISE
- CLOCKED_NOISE
- GRANULAR_CLOUD
- PARTICLE_NOISE
- DIGITAL_MODULATION
- QUESTION_MARK

## Build

From `eurorack_libdaisy/braids_mi`:

- `make`

If you keep Daisy deps elsewhere, you can override:

- `make DAISY_ROOT=/path/to/daisy`  (must contain `libDaisy/` and `DaisySP/`)

By default this project builds the **lite** (DFU-friendly) Braids subset.

## Bootloader + SD card (recommended for full-size builds)

If you want to run a larger build (e.g. full Braids models/resources) without an ST-Link, the practical path is:

1. Flash the **Daisy bootloader** once via DFU (internal flash).
2. Build your app for **BOOT_QSPI**.
3. Copy the generated `.bin` to the SD card root; the bootloader will flash it into QSPI and run it.

### Full Braids build

This repo supports two build variants:

- `BRAIDS_VARIANT=lite` (default): trimmed analog-only subset (fits internal flash).
- `BRAIDS_VARIANT=full`: uses upstream Braids `macro_oscillator` + `digital_oscillator` (recommended with bootloader/QSPI).

To build the full variant for the bootloader:

- `make clean && make BRAIDS_VARIANT=full APP_TYPE=BOOT_QSPI`

Then copy `build/braids_mi.bin` to the SD card root and reboot once to flash QSPI.

### Format the SD card on macOS

The Daisy bootloader expects a normal FAT filesystem on an SD card (not exFAT). A simple, reliable setup is **MBR + FAT32**.

If you have a large SD card (e.g. 128GB/256GB/512GB) and macOS refuses to mount a freshly-formatted FAT32 volume, a reliable workaround is:

- Make the **first** partition a **32GB FAT32** volume (MBR), and put anything else in a second partition.

Example:

- `diskutil partitionDisk /dev/diskN MBR FAT32 DAISY 32G ExFAT EXTRA R`

1. List disks and find your SD card (be careful to pick the right one):

 - `diskutil list`

2. Erase/format as MBR + FAT32 (this will wipe the card):

 - `diskutil eraseDisk FAT32 DAISY MBRFormat /dev/diskN`

Replace `diskN` with your SD card device from `diskutil list`.

### Flash the Daisy bootloader (one time)

Put the Daisy into **STM32 system DFU mode** (this is *not* the Daisy bootloader yet):

1. Connect USB
2. Hold **BOOT**
3. Press and release **RESET**
4. Release **BOOT**

Optional sanity check (Mac): `dfu-util -l` should show an STM32 DFU device.

Then from this project directory, with the device in DFU mode:

- `make program-boot`

### Build for QSPI + copy to SD

- `make clean && make APP_TYPE=BOOT_QSPI`

Then copy `build/braids_mi.bin` to the **root** of the SD card.

Notes:
- Keep the SD card root free of other `.bin` files; the bootloader stops at the first `.bin` it finds.
- The bootloader stores programs in QSPI starting at `0x90040000` (so the BOOT_QSPI linker uses that origin).

