# ForgeSeries build wrapper.
#
# All firmware comes from the one PlatformIO project at the repository root
# (platformio.ini + src/): the shell plus the modules. `make` builds the unified
# image with every module in it; `make modules` builds the four single-module
# images from the same sources — see the "Single-module firmwares" section
# below. apps/<app>/ hold module sources and native tests, and have no
# PlatformIO project of their own.

# ── Windows: find the toolchain ourselves ────────────────────────────────────
# Rack's plugin.mk is POSIX, and the usual Windows trap is Chocolatey's make
# arriving first on PATH: it drives cmd.exe as SHELL and cannot run those
# recipes, failing with an opaque "SLUG could not be found in manifest".
#
# We cannot repair the PATH of the shell that invoked us, but we can pick the
# shell and PATH our own recipes use, which is what actually matters. Override
# MSYS if msys2 lives elsewhere.
#
# Deliberately NOT keyed on $(OS): Git Bash sets OS and USERPROFILE as shell
# variables without exporting them, so make sees neither and the whole block was
# silently skipped there — it worked from PowerShell, where they are exported.
# Probing for msys2 itself is the one signal available in every shell.
MSYS ?= C:/msys64

WIN_SH  := $(wildcard $(MSYS)/usr/bin/sh.exe)
WIN_GXX := $(wildcard $(MSYS)/mingw64/bin/g++.exe)
WIN_JQ  := $(wildcard $(MSYS)/mingw64/bin/jq.exe)

ifneq ($(WIN_SH)$(WIN_GXX)$(WIN_JQ),)
  WINDOWS := 1
endif

ifdef WINDOWS
  # PlatformIO: point PIO at the executable rather than putting its directory on
  # PATH. A native path's backslashes and drive colon corrupt a PATH that make
  # splits on ":" under msys. An absolute path to the exe needs no PATH entry.
  # Both spellings of home are tried, since which one is exported depends on the
  # shell: USERPROFILE from PowerShell, HOME from Git Bash.
  PIO_EXE := $(firstword $(wildcard \
      $(subst \,/,$(USERPROFILE))/.platformio/penv/Scripts/pio.exe \
      $(HOME)/.platformio/penv/Scripts/pio.exe))
  #
  # Neither is found under Git Bash: it exports neither USERPROFILE nor a
  # Windows-shaped HOME (msys2 sets HOME to /home/<user>). PIO then falls back
  # to plain `pio`, so put PlatformIO on PATH there:
  #     export PATH="$USERPROFILE/.platformio/penv/Scripts:$PATH"
  ifneq ($(PIO_EXE),)
    PIO := $(PIO_EXE)
  endif

  # msys2 is only for the Rack plugins. When it is present we also take its sh
  # as SHELL, which is what lets even Chocolatey's make run plugin.mk.
  ifneq ($(WIN_SH),)
    SHELL := $(WIN_SH)
    .SHELLFLAGS := -c
    export PATH := $(MSYS)/usr/bin:$(MSYS)/mingw64/bin:$(PATH)
  endif
endif

# The firmware needs none of the above — only PlatformIO — so the check fires
# just for the plugin goals. Doing it at parse time means one clear message
# instead of plugin.mk's "SLUG could not be found in manifest", which names
# neither the missing tool nor the wrong shell.
ifdef WINDOWS
ifneq ($(filter plugins plugin-% everything,$(or $(MAKECMDGOALS),all)),)
  WIN_MISSING :=
  ifeq ($(WIN_SH),)
    WIN_MISSING += msys2($(MSYS)/usr/bin/sh.exe)
  endif
  ifeq ($(WIN_GXX),)
    WIN_MISSING += mingw-w64-g++($(MSYS)/mingw64/bin/g++.exe)
  endif
  ifeq ($(WIN_JQ),)
    WIN_MISSING += jq($(MSYS)/mingw64/bin/jq.exe)
  endif
  ifneq ($(WIN_MISSING),)
    $(info )
    $(info Building the VCV Rack plugins on Windows needs msys2, and it is)
    $(info incomplete or missing. Not found:)
    $(info )
    $(foreach m,$(WIN_MISSING),$(info   - $(m)))
    $(info )
    $(info Check install instructions on https://vcvrack.com/manual/Building)
    $(info )
    $(info If msys2 lives elsewhere: make MSYS=D:/msys64 plugins)
    $(info )
    $(error missing Windows toolchain)
  endif
endif
endif

APPS  := clk dq gen scp att
PIO   ?= pio
ENV   ?= xiao_rp2040

.PHONY: all list
.DEFAULT_GOAL := all

list:
	@echo "apps: $(APPS)"

# `all` is the firmware you flash.
all: unified

# Native unit tests (googletest + ArduinoFake), one env:native_<app> each.
#
# These moved into the root project when the per-app platformio.ini files were
# removed: the suites still live in apps/<app>/test/test_native, but the project
# that builds them is this one (test_dir = apps).
#
# Only the apps that actually have a suite: `pio test` fails outright ("Nothing
# to build. Please put your test suites to the 'test' folder") on an app whose
# test/ holds just a README, which is scp today. Asking the filesystem keeps this
# honest if a suite is added or removed — though a new suite now also needs its
# env:native_<app> in platformio.ini, since each one may only see its own lib/.
TEST_APPS := $(foreach a,$(APPS),$(if $(wildcard apps/$(a)/test/test_native),$(a)))

.PHONY: test $(addprefix test-,$(APPS))
test: $(addprefix test-,$(TEST_APPS))

$(addprefix test-,$(APPS)): test-%:
	$(PIO) test -e native_$*

# ── VCV engine isolation tests ───────────────────────────────────────────────
# Each module's fw_engine.cpp is compiled twice into one binary to prove two
# Rack instances of the same module keep their firmware state apart. Host
# compiler only — no Rack SDK, since fw_engine.cpp never includes rack.hpp.
ISOLATION_APPS := $(foreach a,$(APPS),\
  $(if $(wildcard apps/$(a)/vcv-plugin/test/build_isolation_test.sh),$(a)))

.PHONY: isolation $(addprefix isolation-,$(APPS))
isolation: $(addprefix isolation-,$(ISOLATION_APPS))

# The script cd's to its own vcv-plugin/, so it runs correctly from here.
$(addprefix isolation-,$(APPS)): isolation-%:
	bash apps/$*/vcv-plugin/test/build_isolation_test.sh

# ── VCV Rack plugins ─────────────────────────────────────────────────────────
# PlatformIO does NOT compile vcv-plugin/, so `make all` passing says nothing
# about the Rack ports. Any change to a firmware global has to be checked here
# too — engine_state.def references those globals by name, and a rename that
# compiles fine on hardware will fail (or worse, silently share state between
# Rack instances) on this side.
#
# Needs a Rack-SDK checkout; override RACK_DIR if it is not beside the repo.
.PHONY: plugins $(addprefix plugin-,$(APPS))

plugins: $(addprefix plugin-,$(APPS))

$(addprefix plugin-,$(APPS)): plugin-%:
	$(MAKE) -C apps/$*/vcv-plugin $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

# Everything: every firmware image, then every Rack plugin.
#
# `modules` is in here because the single-module images are the one target that
# a change to src/main.cpp's registry can break while the unified image still
# builds — which is exactly the change made when an app is added.
.PHONY: everything
everything: all modules plugins

# NOTE: Rack's plugin.mk shells out to `jq` to read SLUG from plugin.json, and
# the compiler must be mingw64 g++ (not the arm-none-eabi one PlatformIO uses).
# An msys2 *shell* is not needed — only its tools on PATH; make finds sh.exe
# itself. On Windows:
#     PowerShell:  . .\tools\env.ps1
#     Git Bash:    export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"

# ── Rack plugins: build and install ──────────────────────────────────────────
# `make clk` builds one module's STANDALONE plugin and installs it into Rack's
# user plugin directory. Handy while working on a single module.
#
# `make vcv` builds the consolidated plugin — every module in one binary, which
# is what ships. `make vcv-install` installs that instead.
#
# CAUTION: do not leave both installed. The standalone plugins have their own
# slugs (ClockForge, NoteForge, ...) and the consolidated one is
# VoltageFoundryMod, so Rack loads them all happily — and every module then
# appears twice in the browser, from two different plugins. Delete the
# standalone ones from Rack's plugin directory before installing the
# consolidated build - see the note below it.
.PHONY: $(APPS) vcv vcv-install vcv-dist

$(APPS): %:
	$(MAKE) -C apps/$*/vcv-plugin install $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

vcv:
	$(MAKE) -C vcv $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

vcv-install:
	$(MAKE) -C vcv install $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

# The .vcvplugin package for the VCV library. Goes through here rather than
# `make -C vcv dist` so it also works on Windows, where only this Makefile knows
# where the msys2 toolchain is.
vcv-dist:
	$(MAKE) -C vcv dist $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

# Remove the standalone plugins from Rack's user directory, leaving the
# consolidated one alone.
# Removing the standalone plugins again is a manual step: Rack's plugin
# directory is platform- and arch-specific (LOCALAPPDATA/Rack2/plugins-<os>-<cpu>
# on Windows), and resolving it needs a working compiler, so auto-detecting it
# to drive an rm -rf is not worth it. `make -C vcv print-plugins-dir` prints the
# path when the toolchain is available.
#
#   ClockForge  NoteForge  GravityForge  ForgeView   <- the standalone slugs
#   VoltageFoundryMod                                <- the consolidated one

# ── Unified firmware ─────────────────────────────────────────────────────────
# One image hosting the shell plus every app, and the only thing `make all`
# builds. Its PlatformIO project is the repository root — see ./platformio.ini
# for why it cannot just be another environment under apps/<app>/.
.PHONY: unified upload clean upload-monitor
unified:
	$(PIO) run -e $(ENV)
upload:
	$(PIO) run -e $(ENV) -t upload
clean:
	$(PIO) run -e $(ENV) -t clean
upload-monitor:
	$(PIO) run -e $(ENV) -t upload -t monitor

# ── Single-module firmwares ──────────────────────────────────────────────────
# The same shell with one module linked in — env:xiao_<app>, see platformio.ini.
# For hardware that is only ever going to be one module: it boots straight into
# it, and its boot menu lists that module and CALIBRATE.
#
# Named fw-<app> rather than <app>: the bare app names are already taken above,
# by the targets that build and install that module's standalone Rack plugin.
#
#   make modules          all four images
#   make fw-clk           just ClockForge
#   make fw-upload-clk    build + flash it
.PHONY: modules $(addprefix fw-,$(APPS)) $(addprefix fw-upload-,$(APPS))

modules: $(addprefix fw-,$(APPS))

$(addprefix fw-,$(APPS)): fw-%:
	$(PIO) run -e xiao_$*

$(addprefix fw-upload-,$(APPS)): fw-upload-%:
	$(PIO) run -e xiao_$* -t upload
