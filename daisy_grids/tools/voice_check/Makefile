# Host-side checks for Sorrow's drum voice pool. Nothing here is built into
# firmware or flashed; it is a PC compiler driving the real drum_voices.cpp and
# the real DaisySP models, so a voice bug is caught before it costs a bench
# cycle. See the header comment in voice_check.cpp for what each check exists
# to catch - every one of them found a real bug.
#
#   make check

CXX      ?= c++
CXXFLAGS ?= -O2 -std=gnu++14 -Wall

REPO_ROOT := $(abspath ../../..)
DSP       := $(REPO_ROOT)/deps/daisy/DaisySP
INCS       = -I../../include -I$(DSP)/Source -I$(DSP)/Source/Utility
DSP_SRCS  := $(wildcard $(DSP)/Source/*/*.cpp)

all: voice_check

voice_check: voice_check.cpp ../../src/drum_voices.cpp ../../include/drum_voices.h
	$(CXX) $(CXXFLAGS) $(INCS) -o $@ voice_check.cpp ../../src/drum_voices.cpp $(DSP_SRCS)

check: voice_check
	./voice_check

clean:
	rm -f voice_check

.PHONY: all check clean
