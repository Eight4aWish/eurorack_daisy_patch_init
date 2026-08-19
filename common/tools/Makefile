# Host-side panel checks. Nothing here is built into firmware or flashed; it is
# a PC compiler driving the real SSD1306 driver and the real legend layout, so
# a panel bug is caught before it costs a bench cycle.
CXX      ?= c++
CXXFLAGS ?= -O2 -std=gnu++17 -Wall -Wextra
INCS      = -Istub -I.. -I../..

all: panel_check

panel_check: panel_check.cpp ../oled_soft_i2c.cpp ../multiosc_core/legend_layout.h
	$(CXX) $(CXXFLAGS) $(INCS) -o $@ panel_check.cpp ../oled_soft_i2c.cpp

check: panel_check
	./panel_check

clean:
	rm -f panel_check

.PHONY: all check clean
