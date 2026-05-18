OPTION EXPLICIT

DIM INTEGER x1%(2), y1%(2), x2%(2), y2%(2), w%(2), c%(2)
DIM INTEGER i%

x1%(0) = 10 : x1%(1) = 30 : x1%(2) = 45
y1%(0) = 40 : y1%(1) = 40 : y1%(2) = 40
x2%(0) = 20 : x2%(1) = 30 : x2%(2) = 55
y2%(0) = 40 : y2%(1) = 50 : y2%(2) = 50
w%(0) = 1   : w%(1) = 3   : w%(2) = -3
c%(0) = RGB(RED) : c%(1) = RGB(GREEN) : c%(2) = RGB(BLUE)

CLS RGB(BLACK)
FOR i% = 0 TO 2
  LINE x1%(i%), y1%(i%), x2%(i%), y2%(i%), w%(i%), c%(i%)
NEXT i%

PRINT "done"
END
