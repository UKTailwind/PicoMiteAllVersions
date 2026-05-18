' FORMAT$ — was a no-op stub on host before mm_misc_shared.c.
' MMBasic FORMAT$ only accepts float format specifiers (g/G/f/e/E/l),
' not %d — see fun_format in mm_misc_shared.c.
OPTION EXPLICIT
PRINT FORMAT$(3.14159, "%7.3f")
PRINT FORMAT$(42, "%8.1f")
PRINT FORMAT$(-17.5, "%+8.2f")
PRINT FORMAT$(1000000, "%e")
PRINT FORMAT$(0.5)
