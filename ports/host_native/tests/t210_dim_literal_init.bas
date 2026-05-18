' Test Dim arr(n) = (literal-list) initializer.
' Used by Picovaders.bas line 19: Dim snd%(4) = (100,90,85,80,70)
'
' Interpreter accepts; VM source frontend must too.

' --- Integer array (suffix-typed) ---
Dim snd%(4) = (100,90,85,80,70)
Print snd%(0)
Print snd%(1)
Print snd%(2)
Print snd%(3)
Print snd%(4)

' --- Float array (suffix-typed) ---
Dim ratio!(2) = (0.5, 1.5, 2.5)
Print ratio!(0)
Print ratio!(1)
Print ratio!(2)

' --- String array (suffix-typed) ---
Dim names$(2) = ("alpha", "beta", "gamma")
Print names$(0)
Print names$(1)
Print names$(2)

' --- Forced-type DIM keyword (no suffix) ---
Dim INTEGER fi(3) = (10, 20, 30, 40)
Print fi(0)
Print fi(1)
Print fi(2)
Print fi(3)

Dim FLOAT ff(2) = (1.25, 2.5, 3.75)
Print ff(0)
Print ff(1)
Print ff(2)

Dim STRING fs(1) = ("one", "two")
Print fs(0)
Print fs(1)

' --- Type coercion: int literals stored in float array ---
Dim mixf!(3) = (1, 2, 3, 4)
Print mixf!(0)
Print mixf!(1)
Print mixf!(2)
Print mixf!(3)

' --- Type coercion: float literals truncated to int ---
Dim mixi%(2) = (1.7, 2.3, 3.9)
Print mixi%(0)
Print mixi%(1)
Print mixi%(2)

' --- Expressions in initializer list ---
Dim exprs%(3) = (1+2, 3*4, 100-50, (5+5)*2)
Print exprs%(0)
Print exprs%(1)
Print exprs%(2)
Print exprs%(3)

' --- String expressions ---
Dim s1$ = "hello"
Dim joined$(1) = (s1$ + " world", "lit")
Print joined$(0)
Print joined$(1)

' --- Two-element single-pair array (smallest valid) ---
Dim pair%(1) = (42, 99)
Print pair%(0)
Print pair%(1)
