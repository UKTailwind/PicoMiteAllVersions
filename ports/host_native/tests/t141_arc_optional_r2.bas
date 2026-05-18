CLS RGB(BLACK)

' Omitted r2 means 1-pixel-wide arc
ARC 30, 30, 10, , 0, 180, RGB(YELLOW)

' Angle normalization should match legacy
ARC 70, 30, 8, , -90, 45, RGB(CYAN)

' Another omitted-r2 form with explicit colour
ARC 110, 30, 9, , 180, 315, RGB(MAGENTA)

PRINT "done"
