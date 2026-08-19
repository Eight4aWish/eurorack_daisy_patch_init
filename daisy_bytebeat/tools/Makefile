# Host-side checks for the BEAT engine. Nothing here is built into firmware or
# flashed; it is a PC compiler driving the same sources the module runs, in the
# spirit of Ogham's own firmware/tools.
#
#   make && ./bank_check && ./voice_check
#
# bank_check  — the vendored engine alone: bank boundaries, levels, the A440
#               reference, and A/B grid interpolation.
# voice_check — the multiosc wrapper's control mapping, driven through the
#               stub hardware in stub/ (a few dozen lines standing in for the
#               slice of libDaisy the engine touches).
CXX      ?= g++
CXXFLAGS ?= -O2 -std=gnu++17 -Wall -Wextra
INC      := -Istub -I.. -I../../common
ENGINE   := ../bytebeat_engine.cpp ../formulas.cpp
VOICE    := ../bytebeat_voice.cpp $(ENGINE)

all: bank_check voice_check

bank_check: bank_check.cpp $(ENGINE)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $^

voice_check: voice_check.cpp $(VOICE)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $^

clean:
	rm -f bank_check voice_check

.PHONY: all clean
