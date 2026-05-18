' Test that bridged commands can read VM-dimensioned string arrays.
' Picovaders uses GUI BITMAP alien$(i,j) — same pattern.
' The bridge needs to see VM-side string-array storage.

Dim names$(2) = ("alpha", "beta", "gamma")

' 2D string array, populated by READ (Picovaders-style).
Dim grid$(2, 1)
grid$(0,0) = "00"
grid$(0,1) = "01"
grid$(1,0) = "10"
grid$(1,1) = "11"
grid$(2,0) = "20"
grid$(2,1) = "21"

' Native print (sanity).
Print "native: "; names$(0); " "; names$(1); " "; names$(2)
Print "grid native: "; grid$(0,0); " "; grid$(2,1)

' UCASE$ — bridged.  Hits findvar -> g_vartbl path that breaks if the
' VM stored the array in its own private layout.
Print "ucase: "; UCase$(names$(0))
Print "ucase grid: "; UCase$(grid$(1,1))

' Concatenation as a final cross-check.
Print "concat: "; names$(0) + "-" + names$(2)
