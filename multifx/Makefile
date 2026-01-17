# Minimal Makefile for Daisy Patch SM firmware
# Adjust DAISY_PATH in config.mk if not detected

TARGET = multifx

# Include user config if present
-include config.mk

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

# Shared-deps root (preferred): contains libDaisy/ and DaisySP/
# (DAISY_ROOT default comes from make/common.mk)

# Optional legacy DaisyToolchain layout (only used if explicitly set):
# contains libraries/LibDaisy and libraries/DaisySP
DAISY_PATH ?=

ifeq ($(strip $(DAISY_ROOT)),)
ifeq ($(strip $(DAISY_PATH)),)
$(error Could not locate Daisy deps. Set DAISY_ROOT (parent containing libDaisy/ and DaisySP/), or set LIBDAISY/DAISYSP explicitly, or set DAISY_PATH for legacy DaisyToolchain layout.)
endif
LIBDAISY ?= $(DAISY_PATH)/libraries/LibDaisy
DAISYSP  ?= $(DAISY_PATH)/libraries/DaisySP
else
LIBDAISY ?= $(DAISY_ROOT)/libDaisy
DAISYSP  ?= $(DAISY_ROOT)/DaisySP
endif

DAISYSP_LGPL     ?= $(DAISYSP)/DaisySP-LGPL
DAISYSP_LGPL_LIB ?= $(DAISYSP_LGPL)/build/libdaisysp-lgpl.a

CPP_SOURCES = \
	src/main.cpp

C_INCLUDES = \
	-I$(LIBDAISY) \
	-I$(LIBDAISY)/src \
	-I$(LIBDAISY)/src/sys \
	-I$(LIBDAISY)/src/usbh \
	-I$(LIBDAISY)/src/usbd \
	-I$(LIBDAISY)/src/util \
	-I$(LIBDAISY)/core \
	-I$(LIBDAISY)/Drivers/STM32H7xx_HAL_Driver/Inc \
	-I$(LIBDAISY)/Drivers/CMSIS-Device/ST/STM32H7xx/Include \
	-I$(LIBDAISY)/Drivers/CMSIS_5/CMSIS/Core/Include \
	-I$(LIBDAISY)/core/STM32H7xx_HAL \
	-I$(LIBDAISY)/build \
	-I$(DAISYSP) \
	-I$(DAISYSP)/Source \
	-I$(DAISYSP)/DaisySP-LGPL/Source \
	-Iinclude

# FatFs includes
C_INCLUDES += \
	-I$(LIBDAISY)/Middlewares/Third_Party/FatFs/src

# USB middleware includes (match LibDaisy build expectations)
C_INCLUDES += \
	-I$(LIBDAISY)/Middlewares/ST/STM32_USB_Device_Library/Core/Inc \
	-I$(LIBDAISY)/Middlewares/Patched/ST/STM32_USB_Device_Library/Class/CDC/Inc \
	-I$(LIBDAISY)/Middlewares/ST/STM32_USB_Host_Library/Core/Inc \
	-I$(LIBDAISY)/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc \
	-I$(LIBDAISY)/Middlewares/ST/STM32_USB_Host_Library/Class/MIDI/Inc

# Toolchain (assumes arm-none-eabi is installed)
PREFIX    ?= arm-none-eabi
CC        = $(PREFIX)-gcc
CXX       = $(PREFIX)-g++
OBJCOPY   = $(PREFIX)-objcopy

# MCU & flags for H750 (Daisy Patch SM)
MCU_FLAGS = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
OPT_FLAGS = -O3 -ffast-math
CXXDEFS   = -DCORE_CM7 -DSTM32H750xx -DSTM32H750IB -DARM_MATH_CM7 -DHSE_VALUE=16000000 -DUSE_HAL_DRIVER -DUSE_FULL_LL_DRIVER -DDATA_IN_D2_SRAM -DFILEIO_ENABLE_FATFS_READER -DNDEBUG=1 -DRELEASE=1
CXXFLAGS  = $(MCU_FLAGS) $(OPT_FLAGS) -fno-exceptions -fno-rtti -Wall -Wextra $(C_INCLUDES) $(CXXDEFS)
LDFLAGS   = $(MCU_FLAGS) -Wl,--gc-sections -Wl,-Map=$(TARGET).map -specs=nosys.specs

LD_SCRIPT = $(LIBDAISY)/core/STM32H750IB_flash.lds
LIBS      = -L$(LIBDAISY)/build -ldaisy -L$(DAISYSP)/build -ldaisysp -L$(DAISYSP)/DaisySP-LGPL/build -ldaisysp-lgpl -lm -lc -lnosys

# Build rules
OBJ_DIR    = build
OBJS       = $(CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o)

all: $(TARGET).bin

$(TARGET).elf: $(OBJS) $(DAISYSP_LGPL_LIB)
	$(CXX) $(LDFLAGS) -T$(LD_SCRIPT) -o $@ $^ $(LIBS)

$(DAISYSP_LGPL_LIB):
	$(MAKE) -C $(DAISYSP_LGPL)

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)/src

clean:
	rm -rf $(OBJ_DIR) $(TARGET).elf $(TARGET).bin $(TARGET).map

flash: $(TARGET).bin
	# Requires dfu-util and Patch SM in DFU mode
	# If you have multiple DFU devices connected, pass SERIAL=... (shown by `dfu-util --list`)
	# Example: make flash SERIAL=200364500000
	dfu-util -a 0 -d 0483:df11 $(if $(SERIAL),-S $(SERIAL),) -s 0x08000000:leave -D $(TARGET).bin || true

.PHONY: all clean flash
