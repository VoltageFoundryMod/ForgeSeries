# ForgeSeries — convenience wrapper around the per-app PlatformIO projects.
#
# Each app under apps/ is its own PlatformIO project (PlatformIO scopes
# src_dir/lib_dir per project, not per environment), so everything here is just
# `pio run -d apps/<app>`.

APPS  := clk dq gen scp
PIO   ?= pio
ENV   ?= xiao_rp2040

.PHONY: all clean list $(APPS) $(addprefix upload-,$(APPS)) $(addprefix clean-,$(APPS))

all: $(APPS)

list:
	@echo "apps: $(APPS)"

$(APPS):
	$(PIO) run -d apps/$@ -e $(ENV)

$(addprefix upload-,$(APPS)): upload-%:
	$(PIO) run -d apps/$* -e $(ENV) -t upload

$(addprefix clean-,$(APPS)): clean-%:
	$(PIO) run -d apps/$* -e $(ENV) -t clean

clean: $(addprefix clean-,$(APPS))

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
# On Windows both live in msys64:
#     export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
