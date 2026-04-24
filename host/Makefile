# host/Makefile — delegator.
#
# The real native host build lives at ports/host_native/Makefile.
# This file stays as a thin delegator so run_tests.sh, buildall.sh,
# and every existing doc-referenced workflow keeps working unchanged
# while the retirement of host/ completes.
#
# Once run_tests.sh and docs have migrated to ports/host_native/,
# this file (and host/build.sh) can be deleted.

PORT_MAKEFILE := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../ports/host_native/Makefile)

.PHONY: all sim clean

all:
	$(MAKE) -f $(PORT_MAKEFILE) all

sim:
	$(MAKE) -f $(PORT_MAKEFILE) sim

clean:
	$(MAKE) -f $(PORT_MAKEFILE) clean
