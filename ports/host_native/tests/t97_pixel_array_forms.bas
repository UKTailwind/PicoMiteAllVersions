OPTION EXPLICIT

DIM xs%(2), ys%(2), cs%(2)

xs%(0) = 20: ys%(0) = 15: cs%(0) = RGB(RED)
xs%(1) = 22: ys%(1) = 15: cs%(1) = RGB(GREEN)
xs%(2) = 24: ys%(2) = 15: cs%(2) = RGB(BLUE)

CLS RGB(BLACK)
PIXEL xs%(), ys%(), cs%()

PRINT "done"
END
