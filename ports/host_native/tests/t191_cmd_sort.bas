' Exercise SORT on an integer array now that mm_misc_shared.c provides
' the real cmd_sort implementation (previously a host-only no-op stub).
' SORT has no native VM compile path, so under FRUN it falls through to
' OP_BRIDGE_CMD → commandtbl[cmdSORT].fptr() — interp and VM share one
' implementation.
DIM a%(4)
a%(0) = 50
a%(1) = 30
a%(2) = 10
a%(3) = 40
a%(4) = 20
SORT a%()
PRINT a%(0); a%(1); a%(2); a%(3); a%(4)
