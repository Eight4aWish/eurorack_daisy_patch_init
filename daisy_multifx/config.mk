## Optional local overrides for Daisy dependencies.
##
## Preferred (shared-deps layout):
##   DAISY_ROOT = /path/to/deps/daisy
##   # must contain: libDaisy/ and DaisySP/
##
## Alternate (explicit):
##   LIBDAISY = /path/to/libDaisy
##   DAISYSP  = /path/to/DaisySP
##
## If you leave this file empty, the Makefile falls back to a legacy
## DaisyToolchain-style layout under $(HOME)/Documents/Daisy.

#DAISY_ROOT = $(HOME)/Documents/eurorack/deps/daisy
#LIBDAISY = $(DAISY_ROOT)/libDaisy
#DAISYSP  = $(DAISY_ROOT)/DaisySP
