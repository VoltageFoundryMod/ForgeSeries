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
