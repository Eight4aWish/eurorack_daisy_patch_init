# Shared Makefile helpers for eurorack_daisy_patch_init projects.
#
# Goal: avoid hard-coded dependency paths by locating repo-local deps relative
# to the including project Makefile, while still allowing explicit overrides.

# The including Makefile should set PROJECT_MAKEFILE before including us.
ifeq ($(strip $(PROJECT_MAKEFILE)),)
$(error PROJECT_MAKEFILE not set before including make/common.mk)
endif

PROJECT_DIR := $(patsubst %/,%,$(dir $(abspath $(PROJECT_MAKEFILE))))

UP_1 := $(abspath $(PROJECT_DIR)/..)
UP_2 := $(abspath $(UP_1)/..)
UP_3 := $(abspath $(UP_2)/..)
UP_4 := $(abspath $(UP_3)/..)

# Best-effort repo root (parent of project dir in this mono-repo layout).
REPO_ROOT ?= $(UP_1)

# Auto-detect deps root by searching upwards for deps/daisy/libDaisy/core/Makefile.
DEPS_ROOT ?= $(firstword \
	$(foreach d,$(PROJECT_DIR)/deps $(UP_1)/deps $(UP_2)/deps $(UP_3)/deps $(UP_4)/deps,\
		$(if $(wildcard $(d)/daisy/libDaisy/core/Makefile),$(d),)))

# Daisy deps root (contains libDaisy/ and DaisySP/).
DAISY_ROOT ?= $(firstword \
	$(foreach d,$(DEPS_ROOT)/daisy,\
		$(if $(wildcard $(d)/libDaisy/core/Makefile),$(d),)))

LIBDAISY_DIR ?= $(DAISY_ROOT)/libDaisy
DAISYSP_DIR  ?= $(DAISY_ROOT)/DaisySP

# Mutable Instruments eurorack deps root.
MUTABLE_EURORACK ?= $(DEPS_ROOT)/mutable/eurorack

# ----------------------------------------------------------------------------
# Toolchain auto-detection
#
# libDaisy's core Makefile uses arm-none-eabi-gcc from PATH unless GCC_PATH is
# set. That breaks when the toolchain on PATH cannot run on this host — an
# x86-64 arm-none-eabi-gcc on an Apple Silicon Mac without Rosetta fails with
# "Bad CPU type in executable", which is a confusing way to be told to install
# something.
#
# So: try the one on PATH first, and only if it cannot even report its version
# do we go looking. An explicit GCC_PATH= on the command line still wins, and
# when PATH is healthy this costs one `-dumpversion` and changes nothing.
ifeq ($(strip $(GCC_PATH)),)
  _TC_PATH_OK := $(shell arm-none-eabi-gcc -dumpversion 2>/dev/null)
  ifeq ($(strip $(_TC_PATH_OK)),)
    # Candidates, in preference order. Add to this list rather than hard-coding
    # a path in a project Makefile.
    _TC_CANDIDATES := \
      $(HOME)/.platformio/packages/toolchain-gccarmnoneeabi/bin \
      /opt/homebrew/bin \
      /usr/local/bin
    GCC_PATH := $(firstword \
      $(foreach d,$(_TC_CANDIDATES),\
        $(if $(shell $(d)/arm-none-eabi-gcc -dumpversion 2>/dev/null),$(d),)))
    ifeq ($(strip $(GCC_PATH)),)
      $(error No runnable arm-none-eabi-gcc found. The one on PATH will not run on \
this host, and none of $(_TC_CANDIDATES) works either. Install a native toolchain \
(brew install arm-none-eabi-gcc) or pass GCC_PATH=/path/to/bin)
    endif
    export GCC_PATH
    $(info Toolchain: arm-none-eabi-gcc on PATH will not run here; using $(GCC_PATH))
  endif
endif
