CLS RGB(BLACK)
DIM INTEGER arr%(4, 7)
arr%(0, 0) = RGB(RED)
arr%(0, 1) = RGB(YELLOW)
arr%(0, 2) = RGB(RED)

DIM c%
FOR c% = 0 TO 2
  BOX c%*40+4, 10, 35, 12, 0, , arr%(0, c%)
NEXT
PRINT "done"
END
