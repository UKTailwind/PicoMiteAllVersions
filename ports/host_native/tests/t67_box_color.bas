CONST BLOCK_RED% = 30
CONST BLOCK_YELLOW_FULL% = 12

FUNCTION GetColor(bt%) AS INTEGER
  IF bt% = BLOCK_RED% THEN
    GetColor = RGB(RED)
  ELSE IF bt% = BLOCK_YELLOW_FULL% THEN
    GetColor = RGB(YELLOW)
  ELSE
    GetColor = RGB(CYAN)
  END IF
END FUNCTION

DIM INTEGER blocks%(4, 7)
blocks%(0, 0) = BLOCK_RED%
blocks%(0, 1) = BLOCK_YELLOW_FULL%
blocks%(0, 2) = BLOCK_RED%

CLS RGB(BLACK)
DIM c%
FOR c% = 0 TO 2
  BOX c%*40+4, 10, 35, 12, 0, , GetColor(blocks%(0, c%))
NEXT
PRINT "done"
END
