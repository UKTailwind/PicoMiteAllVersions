# ports/host_native/Makefile — native host build.
#
# Transitional: delegates to host/Makefile while host/ is being
# retired. Once host/Makefile is inlined here (final Phase 12 step),
# this becomes the real build file.
#
# Usage:
#   make            — builds mmbasic_test in host/
#   make sim        — builds mmbasic_sim in host/
#   make clean      — cleans host/ build artefacts
#
# Output binaries land in host/ for now. The move into
# ports/host_native/build/ ships with the host/ retirement commit.

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

.PHONY: all sim clean

all:
	$(MAKE) -C $(REPO_ROOT)/host

sim:
	$(MAKE) -C $(REPO_ROOT)/host sim

clean:
	$(MAKE) -C $(REPO_ROOT)/host clean
