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

# Which make is running decides only ONE thing here: the form the PATH below has
# to be written in. Both makes build everything in this repo.
#
# msys2's make is itself an msys2 program, so the PATH it inherits has already
# been converted to POSIX for it ("/c/Windows/system32:/c/Program Files/...").
# A native Windows make (MAKE_HOST "Windows32", what WinGet and Chocolatey
# install) gets the raw ";"-separated Windows PATH — and, usefully, its Windows
# port accepts ":" as a separator too, which is why the original single-form
# line here worked there for years.
#
# ⚠ The two forms are NOT interchangeable, and getting it wrong does not fail
# where you are looking. A native make locates SHELL by searching PATH for
# sh.exe; hand it POSIX paths it cannot search and sh.exe goes invisible, SHELL
# falls back to cmd.exe, and the first thing to notice is Rack's arch.mk, two
# levels down:
#
#     process_begin: CreateProcess(NULL, cc -dumpmachine, ...) failed.
#     arch.mk:17: *** Could not determine CPU architecture of .  Stop.
#
# which names the compiler for a problem that is entirely about the shell.
MSYS_MAKE := $(if $(findstring msys,$(MAKE_HOST))$(findstring cygwin,$(MAKE_HOST)),1)

# Empty, and the only way to indent a $(info) line: make strips the leading
# whitespace off a function argument, but only before the first token. Opening
# with a variable that expands to nothing gives the spaces after it something to
# follow, and they survive. Used by the diagnostics below, where the difference
# is between a command the reader can pick out and a wall of flush-left prose.
E :=

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

    # ⚠ Prepend msys2 in the form and with the separator the running make's own
    # PATH already uses — see MSYS_MAKE above for why the two makes differ.
    #
    # Getting this wrong does not degrade gracefully, it fails totally and in a
    # way that names the wrong culprit: what survives is every Windows directory
    # and no POSIX tools. The recipes here lose coreutils ("mkdir: command not
    # found") and, one level down, Rack's plugin.mk loses the tool it reads the
    # manifest with — reported as "SLUG could not be found in manifest". The
    # block below exists to keep anyone from ever seeing that message, and this
    # line used to generate it.
    ifdef MSYS_MAKE
      # POSIX list. Splicing a native "C:/msys64/usr/bin" in here would split it
      # at the drive colon into "C" and "/msys64/usr/bin" — neither exists. The
      # colon is what has to go, so it goes: C:/msys64 -> /C/msys64, which is
      # the msys2 mount form and case-insensitive in its drive letter.
      MSYS_POSIX := /$(subst :,,$(MSYS))
      export PATH := $(MSYS_POSIX)/usr/bin:$(MSYS_POSIX)/mingw64/bin:$(PATH)
    else
      # Windows list, ";"-separated: native paths a Win32 search can resolve,
      # which is what this make needs to find sh.exe and adopt it as SHELL.
      export PATH := $(MSYS)/usr/bin;$(MSYS)/mingw64/bin;$(PATH)
    endif
  endif
endif

APPS  := clk dq gen scp att wea

# Every goal that ends up inside a vcv-plugin/. Named here, above the guard that
# uses it, because getting this list wrong is invisible until someone runs the
# one target that was left out — which is how `make vcv-install` came to answer
# a wrong-toolchain problem with "Could not determine CPU architecture of ."
# rather than with the message below. `$(APPS)` is in it because a bare
# `make clk` builds and installs that module's standalone plugin.
WIN_PLUGIN_GOALS := plugins plugin-% everything vcv vcv-% $(APPS)

# The firmware needs none of the above — only PlatformIO — so the check fires
# just for the plugin goals. Doing it at parse time means one clear message
# instead of plugin.mk's "SLUG could not be found in manifest", which names
# neither the missing tool nor the wrong shell.
ifdef WINDOWS
ifneq ($(filter $(WIN_PLUGIN_GOALS),$(or $(MAKECMDGOALS),all)),)
  # ⚠ Deliberately NOT a check on which make is running. A native Windows make
  # (MAKE_HOST "Windows32", what WinGet and Chocolatey install) builds these
  # plugins perfectly well: it accepts ":" as a PATH separator alongside ";",
  # so it finds msys2's sh.exe by searching PATH and adopts it as SHELL, and
  # its sub-makes do the same. Rejecting it would refuse a working setup.
  #
  # What it cannot survive is being handed a PATH it cannot search — see the
  # MSYS_MAKE branch at the top. Get the form wrong and sh.exe becomes
  # invisible, SHELL silently falls back to cmd.exe, and the failure surfaces
  # two levels down as "Could not determine CPU architecture of ." out of
  # Rack's arch.mk. That is a bug in this file's PATH line, not in the user's
  # choice of make, and it belongs fixed there rather than papered over here.
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

PIO   ?= pio
ENV   ?= xiao_rp2040

# Scratch space for intermediates that are neither firmware nor plugin output —
# the prepared panel SVGs handed to Inkscape. Gitignored.
BUILD_TMP ?= .build

# Both spellings of home, because which one is exported depends on the shell:
# USERPROFILE from PowerShell, HOME from Git Bash. Same reasoning as PIO_EXE
# above, which does this inline.
HOMEDIRS := $(subst \,/,$(USERPROFILE)) $(HOME)

# The panel pipeline is Python; msys2 ships one and PlatformIO's venv has
# another, so a machine set up to build this repo at all already has it.
PYTHON ?= $(if $(shell command -v python 2>/dev/null),python,\
            $(firstword $(wildcard \
              $(addsuffix /.platformio/penv/Scripts/python.exe,$(HOMEDIRS)))))

# A module arrives in pieces — the pure lib/ and its host tests first, the
# firmware TU and the Rack port later — so the aggregate targets ask the
# filesystem what exists rather than assuming every app has everything. Same
# reasoning as TEST_APPS below, which has always worked this way: `make
# everything` should build what is there, not fail on what is not yet.
#
# The per-app targets stay on APPS, so `make fw-wea` before wea has a firmware
# still runs and fails with PlatformIO naming the missing environment.
FW_APPS     := $(foreach a,$(APPS),$(if $(wildcard apps/$(a)/src/$(a)_app.cpp),$(a)))
PLUGIN_APPS := $(foreach a,$(APPS),$(if $(wildcard apps/$(a)/vcv-plugin/Makefile),$(a)))

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

# ── OLED screenshots ─────────────────────────────────────────────────────────
# Dump a module's 128x64 screen to the terminal as ASCII, on the host compiler.
# The alternative is flashing a board or opening Rack and squinting at a photo,
# which is not a way to check that a label lines up with the cell it names.
#
#   make screen-wea
#   make screen-wea ARGS="--ms 2100 --turn 3 --click 1"
#
# Generic over forgevcv::IEngine, so it works for any app with a vcv-plugin;
# `--` keeps ARGS out of the script's own argument parsing.
.PHONY: $(addprefix screen-,$(APPS))
$(addprefix screen-,$(APPS)): screen-%:
	bash vcvlib/test/build_screenshot.sh $* -- $(ARGS)

# ── Panels ───────────────────────────────────────────────────────────────────
# Rack's SVG parser (nanosvg) renders `svg g path rect circle ellipse line
# polyline polygon` plus the gradient elements — that is the whole list. It does
# not understand <text>, so a label typed in Inkscape simply does not appear,
# and it does not understand <use>, so a clone draws nothing at all. Fixing both
# by hand before every release is exactly the step that gets forgotten: the
# ClockForge panel shipped for months with five <text> labels that Rack never
# drew.
#
# So: edit `panel-src/<Name>.svg` in Inkscape and leave text as text. The build
# converts it into `apps/<app>/vcv-plugin/res/<Name>.svg`, which is what the
# plugin loads and what gets committed. Both files are tracked — the source
# because it is the source, the output because a machine without Inkscape still
# has to be able to build. `vcv/res/` is a further copy of the outputs, staged
# by vcv/Makefile and gitignored; nothing here touches it.
#
# ⚠ Never widen the selection to `select-all:all`. The docs make `:all` ("every
# object including groups") sound safer, but `object-to-path` then recurses into
# every group in the document and on a panel this size it does not finish. The
# selection is narrower still — see PANEL_ACTIONS, which asks for text and
# nothing else, because text is the only thing here that needs Inkscape at all.
#
# `export-plain-svg` drops the inkscape:/sodipodi: namespaces, which nanosvg
# ignores anyway.
#
# ⚠ `--batch-process` is not optional. Without it Inkscape writes the export and
# then keeps running with its GUI event loop alive, so make blocks forever on a
# job that already finished. It is intermittent enough to look like a slow
# conversion rather than a hang: small documents happen to exit on their own,
# large ones do not.
#
# ⚠ And `>/dev/null 2>&1 </dev/null` — all three, not just stdout. Even with
# --batch-process, Inkscape leaves a process behind that inherits whatever file
# descriptors the recipe had. It writes GTK warnings to *stderr*, so redirecting
# only stdout leaves that orphan holding the write end of the build's output
# pipe — and a reader on that pipe blocks until every writer closes. The visible
# symptom is `make vcv` freezing *after* the panels are already converted, while
# `make panels` on its own appears fine because nothing was reading a pipe.
#
# Probed with a shell loop, not $(wildcard)/$(firstword) like the other tools in
# this file: the default install path is "C:/Program Files/Inkscape/...", and
# both of those functions split their arguments on whitespace — so the space
# turns one path into two and neither exists. Every use site quotes $(INKSCAPE)
# for the same reason. `.com` before `.exe`: on Windows the `.com` stub is the
# console-subsystem build, and it is the one that honours the redirections
# above.
INKSCAPE ?= $(shell for p in \
      "$$(command -v inkscape 2>/dev/null)" \
      "C:/Program Files/Inkscape/bin/inkscape.com" \
      "C:/Program Files (x86)/Inkscape/bin/inkscape.com" \
      $(foreach h,$(HOMEDIRS),"$(h)/AppData/Local/Programs/Inkscape/bin/inkscape.com") \
      ; do [ -n "$$p" ] && [ -x "$$p" ] && printf '%s' "$$p" && break; done)

# Guide layers stripped before conversion. `components` holds a shape per jack,
# encoder and screen so the mm coordinates in each module's <Name>.cpp can be
# read off the drawing (see panel-coords below), and `Drill` is fabrication
# geometry — Rack would happily render both on top of the finished panel. Keyed
# on the Inkscape layer label, which is the name you chose and will keep, rather
# than on the generated id.
PANEL_HIDE_LAYERS ?= components Drill

# The action list, overridable so a variant can be tried without editing this
# file: make panels PANEL_ACTIONS="..."
#
# Text only. Inkscape is here for one thing nothing else can do — turning glyphs
# into outlines needs the font metrics — and every other conversion it was being
# asked for was either unnecessary or ruinous:
#
#   rect/circle/ellipse/line/polyline/polygon  nanosvg draws these natively.
#       Converting them to paths is work with no effect on what Rack renders.
#   <use>  nanosvg cannot draw it, so it does have to go — but `object-to-path`
#       unlinks one clone per document rebuild, and the 815-tile `texture` layer
#       on these panels blows straight through any sane wall clock. It is a deep
#       copy per clone, so prep_panel.py does it in well under a second.
#
# ⚠ Recursively expanded on purpose (`?=`, not `:=`): `$@` has to resolve in the
# recipe, not here, where it is empty.
#
# `select-by-element` needs Inkscape 1.1+. On an older build, fall back with
#   make panels PANEL_ACTIONS="select-all; object-to-path; export-plain-svg; export-filename:$@; export-do"
PANEL_ACTIONS ?= select-by-element:text; object-to-path; export-plain-svg; export-filename:$@; export-do

# ⚠ Give the build its own Inkscape application ID. This is what makes the
# panel step safe to run while you have Inkscape open, and it is not optional.
#
# Inkscape is a GApplication with a fixed ID, `org.inkscape.Inkscape`. The first
# process to claim that ID becomes the primary; every later `inkscape` command
# becomes a *remote* instance that forwards its arguments to the primary and
# then waits for it. So without a tag, `make panels` does not run Inkscape — it
# asks whatever Inkscape already exists to do the work, and blocks. If that is
# your editor, the actions run in your editing session; if it is a wedged batch
# process from an earlier build, nothing happens at all and make waits forever.
#
# It gets worse: `--batch-process` is documented as "Close GUI after executing
# all actions". Forwarded into your open editor, that is an instruction to run
# object-to-path on whatever you have open and then close it.
#
# `--app-id-tag=TAG` gives this invocation the ID `org.inkscape.Inkscape.TAG`,
# so it always starts its own private instance, never attaches to the GUI, and
# cannot be held up by an orphan of the untagged one.
PANEL_APP_TAG ?= forgebuild

# ⚠ Hard wall-clock limit on Inkscape, because every other guard here stops a
# *known* hang and this one stops the unknown next one.
#
# Inkscape on Windows is single-instance: a second invocation hands its work to
# the first and waits. So one wedged batch process does not just fail its own
# build, it wedges every build after it — make prints "panel: ..." and sits
# there while an orphan from twenty minutes earlier spins at 8% CPU holding the
# lock. Nothing times out, because make is waiting on a process that is
# technically alive.
#
# 180 s is generous: these panels convert in under ten seconds each. Exceeding
# it means something is wrong, and a failed build that says so beats a hung one.
#
# Resolved with $(wildcard), not a shell loop. A `case` statement cannot live
# inside $(shell ...) — its `)` closes the make function call, and the result is
# a syntax error from sh plus an empty variable. And $(wildcard)'s whitespace
# splitting, which rules it out for the Inkscape probe above ("C:/Program
# Files/..."), is harmless here because none of these paths contain a space.
#
# ⚠ PATH is deliberately not consulted: `timeout` there resolves to
# C:/Windows/system32/timeout.exe, an unrelated tool that waits for a keypress
# and rejects these arguments outright.
PANEL_TIMEOUT_S ?= 180
PANEL_TIMEOUT ?= $(firstword $(wildcard \
      $(MSYS)/usr/bin/timeout.exe \
      C:/msys64/usr/bin/timeout.exe \
      /usr/bin/timeout \
      /opt/homebrew/bin/gtimeout))

# Sources live OUTSIDE any vcv-plugin/res/ on purpose. Each module's
# vcv-plugin/Makefile ships the whole of res/ via `DISTRIBUTABLES += res`, so an
# editable source sitting there would be packaged into every .vcvplugin — a
# megabyte of Inkscape working file per panel, in a release. Keeping them here
# means `make vcv-dist` cannot pick them up by accident rather than because
# someone remembered to exclude them.
PANEL_SRCDIR := panel-src
PANEL_TMP    := $(BUILD_TMP)/panels

# Which panels belong to which app. Listed rather than derived: the app codes
# are three-letter directory names and the panels are product names, and there
# is no rule connecting `dq` to NoteForge that a reader could check. A new
# module adds its line here and nothing else.
# A LIST per app, not a single name: a module can ship more than one panel.
# ClockForge has two — its own, and its expander's.
PANELS_att := ChaosForge
PANELS_clk := ClockForge ClockForge_Exp1
PANELS_dq  := NoteForge
PANELS_gen := GravityForge
PANELS_scp := ForgeView
PANELS_wea := WeaveForge

# Ask the filesystem which panels actually exist, for the same reason FW_APPS
# and PLUGIN_APPS do: a module arrives in pieces, and `make panels` should
# convert what is there rather than fail on what is not drawn yet.
# An app counts as having panels if ANY of its listed drawings exists.
PANEL_APPS   := $(foreach a,$(APPS),$(if $(wildcard $(addsuffix .svg,$(addprefix $(PANEL_SRCDIR)/,$(PANELS_$(a))))),$(a)))
NOPANEL_APPS := $(filter-out $(PANEL_APPS),$(APPS))
PANEL_OUT    := $(foreach a,$(PANEL_APPS),$(foreach p,$(PANELS_$(a)),apps/$(a)/vcv-plugin/res/$(p).svg))

.PHONY: panels panels-force panel-coords \
        $(addprefix panels-,$(APPS)) $(addprefix panel-coords-,$(APPS))

panels: $(PANEL_OUT)

# Rebuild every panel regardless of timestamps — for when Inkscape's output
# changed under you (a version bump) rather than the source.
panels-force:
	rm -f $(PANEL_OUT)
	@$(MAKE) --no-print-directory panels

# One recipe, two forms, chosen once at parse time. Missing Inkscape is not an
# error: the converted SVG is committed, so only someone actually editing a
# panel needs the tool. Warn and use what is there.
#
# `$$` throughout, because this text is expanded once more on its way through
# $(call panel_rule,...) below and $@/$< must survive that to reach the recipe.
ifeq ($(INKSCAPE),)
define panel_recipe
	@echo "  Inkscape not found - keeping the committed $$@."
	@echo "  Install Inkscape, or set INKSCAPE=/path/to/inkscape.com, to rebuild"
	@echo "  it from $$<. Text in the source will not render in Rack until you do."
	@touch $$@
endef
else
define panel_recipe
	@echo "panel: $$< -> $$@"
	@mkdir -p $(PANEL_TMP) $$(@D)
	@$(PYTHON) tools/prep_panel.py $$< $(PANEL_TMP)/$$(notdir $$<) $(PANEL_HIDE_LAYERS)
	@$(if $(PANEL_TIMEOUT),"$(PANEL_TIMEOUT)" -k 5 $(PANEL_TIMEOUT_S),) \
	  "$(INKSCAPE)" --app-id-tag=$(PANEL_APP_TAG) \
	  --batch-process $(PANEL_TMP)/$$(notdir $$<) \
	  --actions="$$(PANEL_ACTIONS)" \
	  >/dev/null 2>&1 </dev/null \
	  || { echo "  ERROR: Inkscape failed or exceeded $(PANEL_TIMEOUT_S)s."; \
	       echo "  The build runs Inkscape under its own application ID"; \
	       echo "  (org.inkscape.Inkscape.$(PANEL_APP_TAG)), so your open editor is"; \
	       echo "  not the cause. Look for a stray process with that tag:"; \
	       echo "      Get-Process inkscape | Stop-Process -Force"; \
	       exit 1; }
	@$(PYTHON) tools/prep_panel.py --check $$@
endef
endif

# Generated per app rather than written as one pattern rule, because the source
# directory is flat and the outputs are not: `panel-src/X.svg` has to land in
# `apps/<app>/vcv-plugin/res/X.svg`, and no single `%` relates the two. Same
# $(eval) shape vcv/Makefile uses to stage those same files one level further on.
define panel_rule
apps/$(1)/vcv-plugin/res/$(2).svg: $(PANEL_SRCDIR)/$(2).svg
$(panel_recipe)

panels-$(1): apps/$(1)/vcv-plugin/res/$(2).svg
endef
$(foreach a,$(PANEL_APPS),$(foreach p,$(PANELS_$(a)),$(eval $(call panel_rule,$(a),$(p)))))

# An app whose panel is not drawn yet still needs its `panels-<app>` target: the
# plugin rules below depend on it unconditionally, and a missing prerequisite
# would be an error rather than the no-op it should be.
ifneq ($(NOPANEL_APPS),)
$(addprefix panels-,$(NOPANEL_APPS)): panels-%:
	@echo "  no panel for $* in $(PANEL_SRCDIR)/ - nothing to convert."
endif

# Print the true component positions from a panel's `components` layer, ready to
# paste into the module's <Name>.cpp. Honours the ancestor transforms and the
# viewBox scale (1 uu = 0.01 mm on these drawings), which reading cx/cy out of
# the XML does not — that is how widgets end up plausibly but wrongly placed.
#
#   make panel-coords-clk
#   make panel-coords          # every panel
panel-coords: $(addprefix panel-coords-,$(PANEL_APPS))

$(addprefix panel-coords-,$(APPS)): panel-coords-%:
	@$(foreach p,$(PANELS_$*),$(PYTHON) tools/panel_coords.py $(PANEL_SRCDIR)/$(p).svg \
	  $(firstword $(PANEL_HIDE_LAYERS));)

# ── VCV Rack plugins ─────────────────────────────────────────────────────────
# PlatformIO does NOT compile vcv-plugin/, so `make all` passing says nothing
# about the Rack ports. Any change to a firmware global has to be checked here
# too — engine_state.def references those globals by name, and a rename that
# compiles fine on hardware will fail (or worse, silently share state between
# Rack instances) on this side.
#
# Needs a Rack-SDK checkout; override RACK_DIR if it is not beside the repo.
.PHONY: plugins $(addprefix plugin-,$(APPS))

plugins: $(addprefix plugin-,$(PLUGIN_APPS))

# Each plugin converts its own panel first — see the Panels section. The
# conversion is timestamp-driven, so this costs nothing on a build where the
# drawing has not changed.
$(addprefix plugin-,$(APPS)): plugin-%: panels-%
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

$(APPS): %: panels-%
	$(MAKE) -C apps/$*/vcv-plugin install $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

# `panels` before each of these, not just `vcv`: vcv/Makefile stages every
# module's res/ into vcv/res/ and it compares timestamps, so a panel refreshed
# after the staging copy would otherwise sit in apps/ and never reach the
# consolidated plugin.
vcv: panels
	$(MAKE) -C vcv $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

vcv-install: panels
	$(MAKE) -C vcv install $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

# The .vcvplugin package for the VCV library. Goes through here rather than
# `make -C vcv dist` so it also works on Windows, where only this Makefile knows
# where the msys2 toolchain is.
vcv-dist: panels
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

modules: $(addprefix fw-,$(FW_APPS))

$(addprefix fw-,$(APPS)): fw-%:
	$(PIO) run -e xiao_$*

$(addprefix fw-upload-,$(APPS)): fw-upload-%:
	$(PIO) run -e xiao_$* -t upload
