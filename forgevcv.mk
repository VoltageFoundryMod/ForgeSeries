# forgevcv.mk — build fragment for ForgeSeries VCV Rack plugins.
#
# A consuming plugin's Makefile pulls this in (usually via a git submodule):
#
#     FORGEVCV ?= ../ForgeSeries-VCVLib
#     include $(FORGEVCV)/forgevcv.mk
#
# It self-locates, so it adds the public include path and C++17 without the
# consumer needing to repeat paths. It also exports FORGEVCV_SHIM for the one
# translation unit that compiles the firmware against the Arduino shim.

# Absolute directory of this fragment (trailing slash), regardless of how the
# consumer referenced it.
FORGEVCV_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

# The firmware lib/ and shim require C++17 (Rack defaults to c++11).
# EXTRA_CXXFLAGS is appended after Rack's -std, so this wins.
EXTRA_CXXFLAGS += -std=c++17 -I$(FORGEVCV_DIR)include

# The Arduino shim. Scope this to the firmware engine TU only (see below) so its
# Adafruit_/Wire/EEPROM names never collide with rack.hpp in the UI sources:
#
#     build/src/engine/fw_engine.cpp.o: EXTRA_CXXFLAGS += -I$(FORGEVCV_SHIM) -I../lib
FORGEVCV_SHIM := $(FORGEVCV_DIR)shim
