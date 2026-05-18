' LONGSTRING APPEND / LLEN / LGETSTR$ / LGETBYTE / LINSTR / LCOMPARE
' These were no-op stubs on host before mm_misc_shared.c landed.
OPTION EXPLICIT
DIM INTEGER buf%(31)
DIM INTEGER other%(31)

LONGSTRING CLEAR buf%()
LONGSTRING APPEND buf%(), "Hello"
LONGSTRING APPEND buf%(), ", "
LONGSTRING APPEND buf%(), "world"
PRINT LLEN(buf%())
PRINT LGETSTR$(buf%(), 1, LLEN(buf%()))
PRINT LGETBYTE(buf%(), 1)
PRINT LINSTR(buf%(), "world")

LONGSTRING CLEAR other%()
LONGSTRING APPEND other%(), "Hello, world"
PRINT LCOMPARE(buf%(), other%())

LONGSTRING APPEND other%(), "!"
PRINT LCOMPARE(buf%(), other%())
