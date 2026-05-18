' VM compiler should accept a variable as the file number in
' OPEN / PRINT # / CLOSE — same as the interpreter. Compare-mode test.
OPTION EXPLICIT

DIM INTEGER fn = 1
OPEN "B:/varfn.tmp" FOR OUTPUT AS #fn
PRINT #fn, "hello"
PRINT #fn, "world"
CLOSE #fn

OPEN "B:/varfn.tmp" FOR INPUT AS #fn
DIM a$, b$
LINE INPUT #fn, a$
LINE INPUT #fn, b$
CLOSE #fn

PRINT a$
PRINT b$
END
