OPTION EXPLICIT

DIM INTEGER x%(2), y%(2), w%(2), h%(2), lw%(2), c%(2), f%(2)
DIM INTEGER i%

x%(0) = 10 : y%(0) = 10 : w%(0) = 6 : h%(0) = 6
x%(1) = 24 : y%(1) = 10 : w%(1) = 6 : h%(1) = 6
x%(2) = 38 : y%(2) = 10 : w%(2) = 6 : h%(2) = 6

lw%(0) = 0 : lw%(1) = 1 : lw%(2) = 0
c%(0) = RGB(WHITE) : c%(1) = RGB(GREEN) : c%(2) = RGB(WHITE)
f%(0) = RGB(RED)   : f%(1) = RGB(BLACK)
f%(2) = RGB(BLUE)

CLS RGB(BLACK)
FOR i% = 0 TO 2
  BOX x%(i%), y%(i%), w%(i%), h%(i%), lw%(i%), c%(i%), f%(i%)
NEXT i%

PRINT "done"
END
