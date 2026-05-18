' VM native call regression:
' A native command should be able to read a top-level global array expression
' without emitting a recoverable interpreter error.

DIM INTEGER a%(1,1)
a%(0,0) = 7
LINE a%(0,0), 0, a%(0,0), 0
PRINT "ok"
