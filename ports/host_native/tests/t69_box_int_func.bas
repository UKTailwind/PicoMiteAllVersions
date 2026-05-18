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
DIM INTEGER arr%(3)
arr%(0) = 30
arr%(1) = 12
arr%(2) = 30

DIM c%
FOR c% = 0 TO 2
  BOX c%*40+4, 10, 35, 12, 0, , F(arr%(c%))
NEXT
PRINT "done"
END
