# daisy_chaos — Secret, for the Hermetic Modular Alchemy Lab (V2)
#
#   make libdaisy       build the SDK's pinned libDaisy once, after cloning
#   make                build firmware -> build/secret.bin
#   make program-live   Secret already running: reboot it into DFU over USB and
#                       flash, no buttons (needs node, dfu-util >= 0.11)
#   make flash          module already in DFU mode: hold B3 at power-on
#   make dfu-list       show every DFU device attached
#
# Laid out after hermetic-modular/alchemy-template, the SDK's recommended
# starting point: the standard Daisy Makefile, the SDK compiled straight from
# its submodule. Two differences from the rest of this repo:
#
#   - libDaisy is the SDK's own pinned copy (deps/alchemy-sdk/vendor/libDaisy),
#     not deps/daisy/libDaisy. It is the commit the SDK is tested against, and
#     the two differ. `git submodule update --init --recursive deps/alchemy-sdk`
#   - BOOT_SRAM with the SDK's linker script: the Alchemy Lab runs its own Daisy
#     bootloader, which serves DFU over the front USB-C port.

TARGET = secret

BOARD ?= v2
ifneq ($(BOARD),v2)
$(error Secret is written for the Alchemy Lab V2 only (got BOARD=$(BOARD)))
endif

PROJECT_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
_P_DIR := $(patsubst %/,%,$(dir $(PROJECT_MAKEFILE)))
_P_UP1 := $(abspath $(_P_DIR)/..)

ALCHEMY_DIR  := $(_P_UP1)/deps/alchemy-sdk
CHAOS_CORE   := $(_P_UP1)/common/chaos_core
# Set before common.mk, whose own LIBDAISY_DIR is a ?= default.
LIBDAISY_DIR := $(ALCHEMY_DIR)/vendor/libDaisy

ifeq ($(wildcard $(LIBDAISY_DIR)/core/Makefile),)
$(error $(LIBDAISY_DIR) is empty. Run: git submodule update --init --recursive deps/alchemy-sdk)
endif

# Toolchain auto-detection, shared with the other apps. It only sets paths.
include $(_P_UP1)/make/common.mk

# No DaisySP here, and libDaisy's core Makefile links -ldaisysp whenever
# DAISYSP_DIR is set -- which common.mk always does. Emptied rather than
# `undefine`d: `ifdef` is false for an empty value, and macOS ships GNU make
# 3.81, which has no `undefine` and stops with "missing separator".
DAISYSP_DIR :=

# ── App ──────────────────────────────────────────────────────────────────────
CPP_SOURCES = \
    src/secret.cpp \
    $(CHAOS_CORE)/src/Registry.cpp

C_INCLUDES += \
    -Isrc \
    -I$(CHAOS_CORE)/include

# ── Alchemy SDK, compiled from the submodule ─────────────────────────────────
CPP_SOURCES += $(sort $(shell find $(ALCHEMY_DIR)/framework/src -name '*.cpp'))
CPP_SOURCES += $(sort $(wildcard $(ALCHEMY_DIR)/hardware/alchemy-lab/$(BOARD)/src/*.cpp))

C_INCLUDES += \
    -I$(ALCHEMY_DIR)/framework/include \
    -I$(ALCHEMY_DIR)/hardware/include \
    -I$(ALCHEMY_DIR)/hardware/alchemy-lab/$(BOARD)/include

C_DEFS += -DALCHEMY_BOARD_V2

# Reported over HostLink and in the boot log line, so hostlink-cli and the web
# programmer can tell which build is on the module.
GIT_HASH := $(shell git -C $(_P_DIR) rev-parse --short HEAD 2>/dev/null || echo unknown)
C_DEFS += -DSECRET_GIT_HASH=\"$(GIT_HASH)\"

# The USB log formats numbers with %f, and newlib-nano's printf leaves floats
# out unless asked for: without this every value in a click report is blank.
LDFLAGS += -u _printf_float

# ── Daisy bootloader build ───────────────────────────────────────────────────
APP_TYPE = BOOT_SRAM
LDSCRIPT = $(ALCHEMY_DIR)/cmake/linkers/alchemy_stm32h750ib_sram.lds

# The SDK requires C++17 (libDaisy's default is gnu++14).
CPP_STANDARD = -std=gnu++17

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

.PHONY: libdaisy
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

# ── Flashing ─────────────────────────────────────────────────────────────────
# Both routes write QSPI at 0x90040000 through the Alchemy bootloader's own DFU,
# which is only on the front-panel USB-C.
#
# `flash` refuses any DFU device that doesn't offer 0x90040000. The one that
# doesn't is the STM32 ROM DFU ('@Internal Flash /0x08000000'), reachable only
# through the Seed2 DFM's onboard micro-USB, and there for installing the
# bootloader itself. Seen on the first bench session; this makes it a clear
# message rather than a puzzling dfu-util error.
#
# `program-live` reboots the running firmware into the bootloader over HostLink
# and has dfu-util wait for it (-w) in the same breath. HostLink's reboot uses
# the bootloader's timed mode, which only listens for about 2 s before starting
# the app again: a separate "is it there yet?" check followed by a fresh make
# spends that window and misses it.
#
# The port is given explicitly because hostlink-cli otherwise picks the first
# /dev/tty.usbmodem*, and on macOS opening a tty.* node blocks waiting for
# carrier detect, so the CLI hangs before it sends anything. The cu.* node for
# the same device does not.
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
		echo "No /dev/cu.usbmodem* found. Is Secret running, on the front USB-C? Otherwise use 'make flash'."; \
		exit 2; }
	node $(ALCHEMY_DIR)/tools/hostlink-cli/hostlink.mjs -p $(HOSTLINK_PORT) reboot bootloader
	$(DFU_UTIL) -w -a 0 -s $(DFU_ADDR) -D $(BUILD_DIR)/$(TARGET).bin -d ,$(DFU_VIDPID)
