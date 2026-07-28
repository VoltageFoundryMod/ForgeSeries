# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../../Rack-SDK

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.
LDFLAGS +=

# The shared ForgeSeries VCV layer (shim + forgevcv/ headers + forgevcv.mk).
# Override FORGEVCV to point at a different checkout/submodule path. The fragment
# adds C++17, the forgevcv include path, and exports FORGEVCV_SHIM.
FORGEVCV ?= ../../ForgeSeries-VCVLib
include $(FORGEVCV)/forgevcv.mk

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/engine/*.cpp)

# The firmware engine translation unit is the ONLY file that sees the Arduino
# shim and the hardware lib/. Scope those include paths to it so they never
# leak into the rack-facing sources (which use rack.hpp types of the same names).
#
# The engine TU also wraps the whole firmware in an anonymous namespace so that
# several Forge modules can be linked into one plugin (see the ForgeSeries-VCV
# meta-repo). That gives internal linkage to firmware entry points this build
# never calls, so unused-function/variable warnings are expected here.
build/src/engine/fw_engine.cpp.o: EXTRA_CXXFLAGS += -I$(FORGEVCV_SHIM) -I../lib \
                                                    -Wno-unused-function -Wno-unused-variable

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk
