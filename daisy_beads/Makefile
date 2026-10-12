# daisy_beads — Wish, for the Hermetic Modular Alchemy Lab (V2), based on Mutable
# Instruments Beads (Emilie Gillet, MIT)
#
#   make libdaisy       build the SDK's pinned libDaisy once, after cloning
#   make                build firmware -> build/wish.bin
#   make program-live   Wish already running: reboot it into DFU over USB and
#                       flash, no buttons (needs node, dfu-util >= 0.11)
#   make flash          module already in DFU mode: hold B3 at power-on
#   make dfu-list       show every DFU device attached
#
# Built like daisy_chaos (Secret), the repo's other Alchemy Lab firmware: the
# standard Daisy Makefile, the Alchemy SDK compiled straight from its submodule,
# the SDK's own pinned libDaisy, BOOT_SRAM with the SDK's linker script.
#
# The DSP is Beads' own, compiled from deps/mutable/eurorack/beads. Two of its
# headers are shadowed by src/upstream/ (searched first, see C_INCLUDES):
#
#   beads/dsp/processor.h      upstream's, with ENABLE_HARDWARE_24K off: libDaisy
#                              has no 24 kHz codec rate, so dirty tape takes
#                              upstream's 48 kHz path instead
#   beads/drivers/codec.h      Codec::Frame only, for gain_control.cc; the rest of
#                              Beads' codec driver is ST HAL and is not built

TARGET = wish

BOARD ?= v2
ifneq ($(BOARD),v2)
$(error Wish is written for the Alchemy Lab V2 only (got BOARD=$(BOARD)))
endif

PROJECT_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
_P_DIR := $(patsubst %/,%,$(dir $(PROJECT_MAKEFILE)))
_P_UP1 := $(abspath $(_P_DIR)/..)

ALCHEMY_DIR  := $(_P_UP1)/deps/alchemy-sdk
# Set before common.mk, whose own LIBDAISY_DIR is a ?= default.
LIBDAISY_DIR := $(ALCHEMY_DIR)/vendor/libDaisy

ifeq ($(wildcard $(LIBDAISY_DIR)/core/Makefile),)
$(error $(LIBDAISY_DIR) is empty. Run: git submodule update --init --recursive deps/alchemy-sdk)
endif

# Toolchain auto-detection, shared with the other apps. It only sets paths.
include $(_P_UP1)/make/common.mk

ifeq ($(wildcard $(MUTABLE_EURORACK)/beads/dsp/processor.cc),)
$(error $(MUTABLE_EURORACK)/beads is missing. Beads needs pichenettes/eurorack d8f4739 or later: git submodule update --init deps/mutable/eurorack)
endif

# No DaisySP here, and libDaisy's core Makefile links -ldaisysp whenever
# DAISYSP_DIR is set -- which common.mk always does. Emptied rather than
# `undefine`d: macOS ships GNU make 3.81, which has no `undefine`.
DAISYSP_DIR :=

BEADS_DIR := $(MUTABLE_EURORACK)/beads
STMLIB_DIR := $(MUTABLE_EURORACK)/stmlib

# ── App ──────────────────────────────────────────────────────────────────────
CPP_SOURCES = \
    src/wish.cpp

# Beads: the portable part. Everything under beads/drivers, the UI, the
# calibrator, the settings store, the factory test and the frozen-buffer backup
# are hardware and are replaced by src/wish.cpp.
CC_SOURCES = \
    $(BEADS_DIR)/dsp/processor.cc \
    $(BEADS_DIR)/dsp/synth.cc \
    $(BEADS_DIR)/dsp/delay_clock.cc \
    $(BEADS_DIR)/dsp/delay_controller.cc \
    $(BEADS_DIR)/dsp/mu_law.cc \
    $(BEADS_DIR)/dsp/trigger_generator.cc \
    $(BEADS_DIR)/gain_control.cc \
    $(BEADS_DIR)/resources.cc \
    $(STMLIB_DIR)/dsp/units.cc \
    $(STMLIB_DIR)/utils/random.cc

# src/upstream first: its two headers shadow Beads' own (see the top).
C_INCLUDES += \
    -Isrc \
    -Isrc/upstream \
    -I$(MUTABLE_EURORACK)

# ── Alchemy SDK, compiled from the submodule ─────────────────────────────────
CPP_SOURCES += $(sort $(shell find $(ALCHEMY_DIR)/framework/src -name '*.cpp'))
CPP_SOURCES += $(sort $(wildcard $(ALCHEMY_DIR)/hardware/alchemy-lab/$(BOARD)/src/*.cpp))

C_INCLUDES += \
    -I$(ALCHEMY_DIR)/framework/include \
    -I$(ALCHEMY_DIR)/hardware/include \
    -I$(ALCHEMY_DIR)/hardware/alchemy-lab/$(BOARD)/include

C_DEFS += -DALCHEMY_BOARD_V2

# Reported over HostLink and in the boot log line.
GIT_HASH := $(shell git -C $(_P_DIR) rev-parse --short HEAD 2>/dev/null || echo unknown)
C_DEFS += -DWISH_GIT_HASH=\"$(GIT_HASH)\"

# The USB log formats numbers with %f; newlib-nano leaves floats out unless asked.
LDFLAGS += -u _printf_float

# 480 MHz rather than libDaisy's default 400, as Secret does (daisy_chaos/Makefile
# has the full story): the link wraps the SDK's seed.Init() and passes boost on.
# Beads itself ran at 400 MHz on an H743; the extra fifth is headroom for its
# delay buffers living in SDRAM here. `make CLOCK400=1` builds without it.
ifneq ($(CLOCK400),1)
LDFLAGS += -Wl,--wrap=_ZN5daisy9DaisySeed4InitEb
C_DEFS  += -DWISH_BOOST
endif

# ── Daisy bootloader build ───────────────────────────────────────────────────
APP_TYPE = BOOT_SRAM
LDSCRIPT = $(ALCHEMY_DIR)/cmake/linkers/alchemy_stm32h750ib_sram.lds

# The SDK requires C++17 (libDaisy's default is gnu++14).
CPP_STANDARD = -std=gnu++17

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# stmlib's makefile.inc builds Mutable's code with these two off; its
# STATIC_ASSERT and type-punning trip them on every file otherwise. Also after
# the include, which sets CPPFLAGS outright.
CPPFLAGS += -Wno-unused-local-typedefs -Wno-strict-aliasing

# libDaisy's core Makefile compiles .cpp only; Mutable's sources are .cc. As
# daisy_braids_oled/Makefile: add their objects and a rule for them.
CC_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
OBJECTS += $(CC_OBJECTS)
vpath %.cc $(sort $(dir $(CC_SOURCES)))

$(BUILD_DIR)/$(TARGET).elf: $(CC_OBJECTS)

$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst)) $< -o $@

.PHONY: libdaisy
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

# ── Host check (tools/host_render.cpp) ───────────────────────────────────────
# Beads' DSP as built here, rendered on the Mac at each media's rate: every case
# finite, within full scale, and sounding. WAVs land in build/host/.
HOST_CXX     ?= c++
HOST_SOURCES := tools/host_render.cpp $(CC_SOURCES)

.PHONY: host-test
host-test:
	@mkdir -p build/host
	$(HOST_CXX) -std=c++17 -O2 -DTEST -Wno-vla-cxx-extension -Wno-unused-local-typedef \
		-Isrc -Isrc/upstream -I$(MUTABLE_EURORACK) $(HOST_SOURCES) -o build/host/host_render
	./build/host/host_render build/host

# ── Flashing ─────────────────────────────────────────────────────────────────
# As daisy_chaos/Makefile, which explains both routes: QSPI at 0x90040000
# through the Alchemy bootloader's DFU on the front-panel USB-C.
DFU_UTIL      ?= dfu-util
DFU_VIDPID    ?= 0483:df11
DFU_ADDR      ?= $(FLASH_ADDRESS):leave
HOSTLINK_PORT ?= $(firstword $(wildcard /dev/cu.usbmodem*))

.PHONY: flash program-live dfu-list

dfu-list:
	@$(DFU_UTIL) -l

flash: $(BUILD_DIR)/$(TARGET).bin
	@$(DFU_UTIL) -l 2>/dev/null | grep -q "$(DFU_VIDPID)" || { \
		echo "No DFU device. Front-panel USB-C, then hold B3 through power-on until the rings breathe."; \
		exit 2; }
	@$(DFU_UTIL) -l 2>/dev/null | grep -q "0x90040000" || { \
		echo "A DFU device is present, but none offers QSPI at 0x90040000."; \
		echo "That is the STM32 ROM DFU, on the Seed2 DFM's onboard micro-USB: it is for"; \
		echo "installing the bootloader. Use the FRONT-PANEL USB-C and hold B3 through power-on."; \
		exit 2; }
	$(DFU_UTIL) -a 0 -s $(DFU_ADDR) -D $(BUILD_DIR)/$(TARGET).bin -d ,$(DFU_VIDPID)

program-live: $(BUILD_DIR)/$(TARGET).bin
	@test -n "$(HOSTLINK_PORT)" || { \
		echo "No /dev/cu.usbmodem* found. Is Wish running, on the front USB-C? Otherwise use 'make flash'."; \
		exit 2; }
	node $(ALCHEMY_DIR)/tools/hostlink-cli/hostlink.mjs -p $(HOSTLINK_PORT) reboot bootloader
	$(DFU_UTIL) -w -a 0 -s $(DFU_ADDR) -D $(BUILD_DIR)/$(TARGET).bin -d ,$(DFU_VIDPID)
