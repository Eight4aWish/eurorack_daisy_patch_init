# DrumSeq MI - Daisy Patch SM firmware
TARGET = drumseq_mi

PROJECT_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
_P_DIR := $(patsubst %/,%,$(dir $(PROJECT_MAKEFILE)))
_P_UP1 := $(abspath $(_P_DIR)/..)
_P_UP2 := $(abspath $(_P_UP1)/..)
_P_UP3 := $(abspath $(_P_UP2)/..)
_P_UP4 := $(abspath $(_P_UP3)/..)
COMMON_MK := $(firstword \
	$(foreach d,$(_P_DIR) $(_P_UP1) $(_P_UP2) $(_P_UP3) $(_P_UP4),\
		$(if $(wildcard $(d)/make/common.mk),$(d)/make/common.mk,)))
ifeq ($(strip $(COMMON_MK)),)
$(error Could not find make/common.mk. Expected it in this repo; searched upwards from $(_P_DIR))
endif
include $(COMMON_MK)

# Board selection
USE_DAISY_PATCH_SM = 1

CPP_SOURCES = \
	src/main.cpp \
	src/grids_port.cpp \
	src/grids_nodes.cpp

C_INCLUDES += \
	-I$(_P_DIR)/include \
	-I$(LIBDAISY_DIR)/src \
	-I$(DAISYSP_DIR)/Source

OPT = -O2

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(LIBDAISY_DIR)/core/Makefile

# Ensure required static libraries exist
LIBDAISY_LIB ?= $(LIBDAISY_DIR)/build/libdaisy.a
DAISYSP_LIB  ?= $(DAISYSP_DIR)/build/libdaisysp.a

$(LIBDAISY_LIB):
	$(MAKE) -C $(LIBDAISY_DIR)

$(DAISYSP_LIB):
	$(MAKE) -C $(DAISYSP_DIR)


$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_LIB) $(DAISYSP_LIB)


# DFU flashing helpers
DFU_UTIL   ?= dfu-util
# Daisy Patch SM (STM32 DFU) is typically 0483:df11.
DFU_VIDPID ?= 0483:df11
# If you have more than one STM32 in DFU mode, set DFU_SERIAL to pick the right one.
DFU_SERIAL ?=
DFU_ALT    ?= 0
DFU_ADDR   ?= 0x08000000:leave

.PHONY: dfu-list dfu-check program-dfu-local flash

dfu-list:
	@$(DFU_UTIL) -l

dfu-check:
	@set -e; \
	serials=`$(DFU_UTIL) -l 2>/dev/null \
		| awk -F'serial="' '/Found DFU: \['"$(DFU_VIDPID)"'\]/{split($$2,a,"\""); print a[1]}' \
		| sort -u`; \
	count=`printf "%s\n" "$$serials" | sed '/^$$/d' | wc -l | tr -d ' '`; \
	if [ "$$count" -eq 0 ]; then \
		echo "No DFU device found for $(DFU_VIDPID). Put the Patch SM into DFU mode and try again."; \
		echo "Tip: run 'make dfu-list' to see all DFU devices."; \
		exit 2; \
	fi; \
	if [ "$$count" -gt 1 ] && [ -z "$(DFU_SERIAL)" ]; then \
		echo "More than one DFU device matched $(DFU_VIDPID)."; \
		echo "Re-run with: make flash DFU_SERIAL=<serial>"; \
		echo "Available serials:"; \
		printf "  %s\n" $$serials; \
		exit 2; \
	fi

program-dfu-local: build/$(TARGET).bin
	@set -e; \
	$(MAKE) dfu-check; \
	serial_to_use="$(DFU_SERIAL)"; \
	if [ -z "$$serial_to_use" ]; then \
		serial_to_use=`$(DFU_UTIL) -l 2>/dev/null \
			| awk -F'serial="' '/Found DFU: \['"$(DFU_VIDPID)"'\]/{split($$2,a,"\""); print a[1]}' \
			| sort -u | sed -n '1p'`; \
	fi; \
	if [ -z "$$serial_to_use" ]; then \
		echo "Could not determine DFU serial automatically. Use: make flash DFU_SERIAL=<serial>"; \
		exit 2; \
	fi; \
	out=`$(DFU_UTIL) -d $(DFU_VIDPID) -S $$serial_to_use -a $(DFU_ALT) -s $(DFU_ADDR) -D build/$(TARGET).bin 2>&1` || status=$$?; \
	status=$${status:-0}; \
	echo "$$out"; \
	if [ $$status -eq 74 ]; then \
		echo "$$out" | grep -Eq "Download[[:space:]]+\[|Download done|File downloaded successfully" && { \
			echo "dfu-util returned 74 on :leave; firmware was written OK. Tap RESET."; \
			exit 0; \
		}; \
	fi; \
	exit $$status

flash: program-dfu-local
