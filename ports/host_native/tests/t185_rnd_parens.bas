' t185_rnd_parens.bas -- RND accepts bare, empty parens, or ignored arg
'
' Regression for VM source compiler: `Rnd()` and `Rnd(1)` were rejected
' with "Unsupported source syntax" because the VM only handled bare
' `Rnd`. The interpreter's fun_rnd in Functions.c returns a random float
' in [0,1) and ignores any argument — the VM must do the same so
' programs that use either syntax compile under FRUN.

Option Explicit
Dim X!

X! = Rnd
If X! < 0 Or X! >= 1 Then Error "bare Rnd out of range"

X! = Rnd()
If X! < 0 Or X! >= 1 Then Error "Rnd() out of range"

X! = Rnd(1)
If X! < 0 Or X! >= 1 Then Error "Rnd(1) out of range"

X! = Rnd(0)
If X! < 0 Or X! >= 1 Then Error "Rnd(0) out of range"

X! = Rnd(-5)
If X! < 0 Or X! >= 1 Then Error "Rnd(-5) out of range"

Print "ok"
