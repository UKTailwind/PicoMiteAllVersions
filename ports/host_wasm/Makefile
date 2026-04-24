# ports/host_wasm/Makefile — WASM host build.
#
# Transitional: delegates to host/Makefile.wasm while host/ is being
# retired. Once host/Makefile.wasm is inlined here (final Phase 12
# step), this becomes the real build file.
#
# Output: host/web/picomite.{mjs,wasm,data}.

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

.PHONY: all clean

all:
	$(MAKE) -C $(REPO_ROOT)/host -f Makefile.wasm

clean:
	$(MAKE) -C $(REPO_ROOT)/host -f Makefile.wasm clean
