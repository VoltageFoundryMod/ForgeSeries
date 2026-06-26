# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.
LDFLAGS +=

# The firmware lib/ and shim require C++17 (Rack defaults to c++11).
# EXTRA_CXXFLAGS is appended after Rack's -std, so this wins.
EXTRA_CXXFLAGS += -std=c++17

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/engine/*.cpp)

# The firmware engine translation unit is the ONLY file that sees the Arduino
# shim and the hardware lib/. Scope those include paths to it so they never
# leak into the rack-facing sources (which use rack.hpp types of the same names).
build/src/engine/fw_engine.cpp.o: EXTRA_CXXFLAGS += -Isrc/shim -I../lib

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk
