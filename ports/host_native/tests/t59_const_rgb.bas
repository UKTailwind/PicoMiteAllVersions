' VM regression:
' expression-valued integer CONSTs that call RGB() must preserve the
' interpreter result on the VM path.

OPTION EXPLICIT
CONST COL_BG% = RGB(BLACK)
CONST COL_BORDER% = RGB(MYRTLE)

PRINT COL_BG%
PRINT COL_BORDER%
