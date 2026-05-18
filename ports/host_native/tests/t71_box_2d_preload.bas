CONST A% = 30
CONST B% = 12

FUNCTION F(x%) AS INTEGER
  IF x% = A% THEN
    F = RGB(RED)
  ELSE IF x% = B% THEN
    F = RGB(YELLOW)
  ELSE
    F = RGB(CYAN)
  END IF
END FUNCTION

CLS RGB(BLACK)
DIM INTEGER arr%(4, 7)
arr%(0, 0) = 30
arr%(0, 1) = 12
arr%(0, 2) = 30

DIM c%, v%
FOR c% = 0 TO 2
  v% = arr%(0, c%)
  BOX c%*40+4, 10, 35, 12, 0, , F(v%)
NEXT
PRINT "done"
END
