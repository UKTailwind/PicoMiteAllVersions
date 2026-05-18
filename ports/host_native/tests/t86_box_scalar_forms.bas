OPTION EXPLICIT

CLS RGB(BLACK)

' Required args only: default width/color.
BOX 5, 5, 6, 6

' Explicit width only.
BOX 17, 5, 6, 6, 2

' Skipped width, explicit color.
BOX 29, 5, 6, 6, , RGB(RED)

' Explicit width and color.
BOX 41, 5, 6, 6, 1, RGB(GREEN)

' Explicit width with skipped color and fill.
BOX 53, 5, 6, 6, 0, , RGB(BLUE)

' Skipped width with explicit color and fill.
BOX 65, 5, 6, 6, , RGB(WHITE), RGB(RED)

' Skipped width and color with fill.
BOX 77, 5, 6, 6, , , RGB(YELLOW)

' Explicit width, color, and fill.
BOX 89, 5, 6, 6, 1, RGB(CYAN), RGB(MAGENTA)

' Negative width/height draw up and left.
BOX 101, 11, -6, -6, 1, RGB(YELLOW)

' Zero width is a no-op.
BOX 113, 5, 0, 6, 1, RGB(CYAN)

PRINT "done"
END
