# daisy_chaos — Secret, for the Hermetic Modular Alchemy Lab (V2)
#
#   make libdaisy   build the SDK's pinned libDaisy once, after cloning
#   make            build firmware -> build/secret.bin
#   make program-dfu   flash over USB (module in DFU mode: hold B3 at power-on)
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
