OPTION EXPLICIT

CLS RGB(BLACK)

' Default outline using gui_fcolour.
CIRCLE 20, 20, 4

' Filled circle with skipped colour.
CIRCLE 40, 20, 4, 0, 1.0, , RGB(RED)

' Explicit outline and fill colours.
CIRCLE 60, 20, 4, 1, 1.0, RGB(GREEN), RGB(BLUE)

' Filled ellipse via aspect.
CIRCLE 80, 20, 4, 0, 2.0, , RGB(YELLOW)

PRINT "done"
END
