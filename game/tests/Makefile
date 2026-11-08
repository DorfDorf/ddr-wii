# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host-side tests for game/source/core (M4a). Builds with any C++17 g++/clang++
# (MSYS2 g++ on Windows) -- no devkitPro, no libogc.
#
#   make -C game/tests          # build and run
#   make -C game/tests build    # build only
#   make -C game/tests oracle   # regenerate expected/ with tools/simfile-oracle.py
#   make -C game/tests clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -pedantic
PYTHON   ?= py

CORE_SRCS := $(wildcard ../source/core/chart/*.cpp) $(wildcard ../source/core/timing/*.cpp)
SRCS      := test_main.cpp $(CORE_SRCS)
HDRS      := $(wildcard ../source/core/chart/*.h) $(wildcard ../source/core/timing/*.h)
BIN       := build/ddr_tests

.PHONY: all run build oracle clean
all: run

build: $(BIN)

$(BIN): $(SRCS) $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

run: $(BIN)
	./$(BIN) .

oracle:
	$(PYTHON) ../../tools/simfile-oracle.py --regen

clean:
	rm -rf build
