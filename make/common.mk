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
