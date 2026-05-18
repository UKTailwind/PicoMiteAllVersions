' TRIM$(s$ [, mask$ [, "L"|"R"|"B"]])
' Exercises the upstream-ported fun_trim.
'
' Default behaviour (mask=" ", where="L"):
PRINT "[" + TRIM$("   hello") + "]"
PRINT "[" + TRIM$("hello   ") + "]"

' Explicit L / R / B with default space mask:
PRINT "[" + TRIM$("   hello   ", " ", "L") + "]"
PRINT "[" + TRIM$("   hello   ", " ", "R") + "]"
PRINT "[" + TRIM$("   hello   ", " ", "B") + "]"

' Custom mask — multiple chars:
PRINT "[" + TRIM$("xyxyHELLOyxyx", "xy", "B") + "]"

' Mask with characters not in the string:
PRINT "[" + TRIM$("hello", "z", "B") + "]"

' Empty input string:
PRINT "[" + TRIM$("", " ", "B") + "]"

' Entirely-mask input (everything trimmed):
PRINT "[" + TRIM$("     ", " ", "B") + "]"

' One-character leftover:
PRINT "[" + TRIM$("****A****", "*", "B") + "]"

' Right-trim only:
PRINT "[" + TRIM$("abc...", ".", "R") + "]"

' Left-trim only:
PRINT "[" + TRIM$("...abc", ".", "L") + "]"
