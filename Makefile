# ForgeSeries build wrapper.
#
# One firmware is built for the board: unified/, the shell plus every module.
# apps/<app>/ are now native-test projects only — their hardware environments
# are gone, and src/main.cpp is kept for reference until the unified image has
# been verified on hardware for every module.

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

APPS  := clk dq gen scp
PIO   ?= pio
ENV   ?= xiao_rp2040

.PHONY: all list
.DEFAULT_GOAL := all

list:
	@echo "apps: $(APPS)"

# `all` is the firmware you flash.
all: unified

# Native unit tests (env:native, googletest + ArduinoFake).
.PHONY: test
test:
	@for a in $(APPS); do \
	  echo "== test $$a =="; \
	  $(PIO) test -d apps/$$a -e native || exit 1; \
	done

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

# Everything: firmware for all apps, then every Rack plugin.
.PHONY: everything
everything: all plugins

# NOTE: Rack's plugin.mk shells out to `jq` to read SLUG from plugin.json, and
# the compiler must be mingw64 g++ (not the arm-none-eabi one PlatformIO uses).
# An msys2 *shell* is not needed — only its tools on PATH; make finds sh.exe
# itself. On Windows:
#     PowerShell:  . .\tools\env.ps1
#     Git Bash:    export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"

# ── Unified firmware ─────────────────────────────────────────────────────────
# One image hosting the shell plus every app. Separate PlatformIO project (see
# unified/platformio.ini for why), so it is not part of `make all`.
.PHONY: unified upload clean upload-monitor
unified:
	$(PIO) run -d unified -e $(ENV)
upload:
	$(PIO) run -d unified -e $(ENV) -t upload
clean:
	$(PIO) run -d unified -e $(ENV) -t clean
upload-monitor:
	$(PIO) run -d unified -e $(ENV) -t upload -t monitor
