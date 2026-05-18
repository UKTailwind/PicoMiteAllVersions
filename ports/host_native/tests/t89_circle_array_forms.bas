OPTION EXPLICIT

DIM INTEGER x%(2), y%(2), r%(2), w%(2), c%(2), f%(2)
DIM a!(2)
DIM INTEGER i%

x%(0) = 20 : x%(1) = 40 : x%(2) = 60
y%(0) = 50 : y%(1) = 50 : y%(2) = 50
r%(0) = 5  : r%(1) = 5  : r%(2) = 5
w%(0) = 0  : w%(1) = 1  : w%(2) = 0
a!(0) = 1.0 : a!(1) = 1.0 : a!(2) = 2.0
c%(0) = RGB(WHITE) : c%(1) = RGB(GREEN) : c%(2) = RGB(WHITE)
f%(0) = RGB(RED)   : f%(1) = RGB(BLUE)  : f%(2) = RGB(YELLOW)

CLS RGB(BLACK)
FOR i% = 0 TO 2
  CIRCLE x%(i%), y%(i%), r%(i%), w%(i%), a!(i%), c%(i%), f%(i%)
NEXT i%

PRINT "done"
END
