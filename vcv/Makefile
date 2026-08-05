# Voltage Foundry Modular — aggregate VCV Rack plugin.
#
# The VCV library publishes one plugin per repository, so this links every
# module into a single binary. Each module's own vcv-plugin/Makefile still
# builds it standalone for day-to-day work.
#
# Sources come straight from ../apps/<module>/vcv-plugin/. These used to be git
# submodules pinned to separate repositories; in the monorepo they are just
# directories, so there is nothing to check out and nothing to keep in sync.
#
#   make           # build plugin.{so,dylib,dll}
#   make dist      # package for the VCV library
#   make install   # install into the local Rack user dir

RACK_DIR ?= ../../Rack-SDK

# The modules bundled into this plugin. Adding one means: list it here and
# register its Model in src/plugin.hpp, src/plugin.cpp and plugin.json.
#
# These are directories under ../apps/. They used to be git submodules pinned to
# their own repositories; now they are simply the modules in this monorepo, so
# there is nothing to check out or keep in sync.
#
# Not every module belongs here: a hardware-only one (fully analog, no firmware
# to emulate) has no VCV build, so leave it out.
VFM_MODULES := clk dq scp gen att
VFM_DIRS := $(addprefix ../apps/,$(VFM_MODULES))

# The shared ForgeSeries VCV layer (shim + forgevcv/ headers + forgevcv.mk).
# Override FORGEVCV to point at a different checkout.
# The fragment adds C++17, the forgevcv include path, and exports FORGEVCV_SHIM.
FORGEVCV ?= ../vcvlib

ifeq ($(wildcard $(FORGEVCV)/forgevcv.mk),)
$(error ForgeSeries VCV layer not found in $(FORGEVCV))
endif

include $(FORGEVCV)/forgevcv.mk

# ── Sources ──────────────────────────────────────────────────────────────────
# This repo supplies the plugin entry point, since it registers every Model.
# Each submodule's own single-module plugin.cpp is filtered out; it stays in
# place for the standalone builds.
SOURCES += src/plugin.cpp
SOURCES += $(filter-out %/plugin.cpp,$(wildcard $(addsuffix /vcv-plugin/src/*.cpp,$(VFM_DIRS))))
SOURCES += $(wildcard $(addsuffix /vcv-plugin/src/engine/*.cpp,$(VFM_DIRS)))

# ── Modules whose VCV port wraps RP2040 firmware via the Arduino shim ─────────
# Detected rather than listed, so this tracks VFM_MODULES on its own: a module
# that ships plain C++ DSP instead of shim-compiled firmware simply has no
# engine/fw_engine.cpp and drops out here.
SHIM_MODULES := $(foreach m,$(VFM_MODULES),\
  $(if $(wildcard ../apps/$(m)/vcv-plugin/src/engine/fw_engine.cpp),$(m)))

# A module's firmware engine translation unit is the ONLY file that sees the
# Arduino shim and that module's hardware lib/. Scope those include paths per
# module so they leak neither into the rack-facing sources (which use rack.hpp
# types of the same names) nor into a sibling module's firmware — every module's
# lib/ has its own divergent boardIO.hpp, utils.hpp, pinouts.hpp and friends.
#
# The matching source-side half of this is the anonymous namespace wrapping each
# fw_engine.cpp: it gives the firmware internal linkage so the modules'
# identically-named globals (micros(), Serial, display, MENU_ITEMS, ...) can
# coexist in one binary.
define vfm_engine_flags
build/../apps/$(1)/vcv-plugin/src/engine/fw_engine.cpp.o: \
	EXTRA_CXXFLAGS += -I$$(FORGEVCV_SHIM) -I../apps/$(1)/lib -I../core \
	                  -Wno-unused-function -Wno-unused-variable
endef
$(foreach m,$(SHIM_MODULES),$(eval $(call vfm_engine_flags,$(m))))

# ── Panel assets ─────────────────────────────────────────────────────────────
# Rack resolves asset::plugin(pluginInstance, "res/X.svg") against the plugin
# directory, so each module's res/ is staged into one top-level res/. That
# directory is generated — it is gitignored.
VFM_RES_SRC := $(foreach d,$(VFM_DIRS),$(wildcard $(d)/vcv-plugin/res/*))
VFM_RES := $(addprefix res/,$(notdir $(VFM_RES_SRC)))

define vfm_res_rule
res/$(notdir $(1)): $(1)
	@mkdir -p $$(@D)
	cp -r $$< $$@
endef
$(foreach f,$(VFM_RES_SRC),$(eval $(call vfm_res_rule,$(f))))

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk

# Stage the panels before linking. Order-only, so refreshing an SVG does not
# force a relink.
$(TARGET): | $(VFM_RES)

stage-res: $(VFM_RES)

clean: clean-res
clean-res:
	rm -rf $(VFM_RES)

.PHONY: stage-res clean-res

# ── Stray objects ────────────────────────────────────────────────────────────
# Rack derives object paths as build/<source>.o (compile.mk). Our sources live
# outside this directory, so build/../apps/... cancels down to ./apps/... and
# the objects land beside the modules instead of under build/.
#
# plugin.mk's clean only removes build, dist and the target, so those objects
# would survive it and be silently reused — a "clean" build that is not one.
# Adding a prerequisite rather than redefining clean keeps Rack's recipe.
clean: clean-stray

.PHONY: clean-stray
clean-stray:
	rm -rf apps

# Where Rack loads user plugins from. plugin.mk works this out per platform
# (LOCALAPPDATA on Windows, XDG on Linux, Application Support on macOS) and
# appends the arch; the root Makefile asks for it rather than guessing.
.PHONY: print-plugins-dir
print-plugins-dir:
	@echo "$(PLUGINS_DIR)"
