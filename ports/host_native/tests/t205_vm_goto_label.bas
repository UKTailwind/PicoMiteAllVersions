' VM compiler should accept GOTO <label>, like the interpreter.
OPTION EXPLICIT
DIM INTEGER hit = 0

GOTO mark
PRINT "skipped"
hit = 1
mark:
PRINT "ok"
PRINT hit
END
