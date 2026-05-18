' RUN_ARGS: --interp
' FLUSH #fnbr — after flush, the on-disk content must be readable by a
' second open against the same file. Interp-only because VM's file
' syscalls maintain a separate handle table that doesn't sync to
' FileTable[], so the bridged FLUSH can't locate the VM-opened handle.
OPEN "B:/flush.tmp" FOR OUTPUT AS #1
PRINT #1, "first-written"
FLUSH #1
OPEN "B:/flush.tmp" FOR INPUT AS #2
Dim a$
LINE INPUT #2, a$
CLOSE #2
CLOSE #1
IF a$ <> "first-written" THEN ERROR "FLUSH did not expose write: " + a$

KILL "B:/flush.tmp"
PRINT "flush ok"
