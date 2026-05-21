' smokes/vm.bas — bytecode VM (FRUN) exercise.
'
' Writes a small BASIC program to disk, FRUNs it, and verifies the
' compiled+executed output. FRUN tokenises + compiles + runs via the VM
' rather than the interpreter, so this catches VM-specific regressions.

CHDIR "C:/"

' Build a tiny program on disk. MMBasic strings don't support `\"`
' escapes — build the embedded quotes from CHR$(34).
DIM q$ = CHR$(34)

OPEN "C:/vmtest.bas" FOR OUTPUT AS #1
PRINT #1, "DIM s% = 0"
PRINT #1, "DIM i%"
PRINT #1, "FOR i% = 1 TO 100 : s% = s% + i% : NEXT i%"
PRINT #1, "IF s% = 5050 THEN PRINT " + q$ + "OK_vm_sum" + q$ + " ELSE PRINT " + q$ + "FAIL_vm_sum" + q$
PRINT #1, "DIM x! = SIN(0)"
PRINT #1, "IF ABS(x!) < 0.0001 THEN PRINT " + q$ + "OK_vm_math" + q$ + " ELSE PRINT " + q$ + "FAIL_vm_math" + q$
PRINT #1, "DIM t$ = " + q$ + "hello" + q$
PRINT #1, "IF LEN(t$) = 5 THEN PRINT " + q$ + "OK_vm_string" + q$ + " ELSE PRINT " + q$ + "FAIL_vm_string" + q$
CLOSE #1

' Run it via the bytecode VM.
FRUN "C:/vmtest.bas"

KILL "C:/vmtest.bas"

PRINT "SMOKE_DONE"
END
