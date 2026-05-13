# Pico SDK Compatibility Headers

This directory holds legacy Pico SDK header shims used by non-Pico ports while
shared code is migrated behind HAL and port-owned boundaries.

These headers do not define a port implementation. They are compile-time
compatibility only, and should stay behavior-neutral.
