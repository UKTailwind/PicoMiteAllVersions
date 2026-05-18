CONST BLOCK_RED% = 30
CONST BLOCK_YELLOW_FULL% = 12

FUNCTION GetBlockColor(blockType%) AS INTEGER
  SELECT CASE blockType%
    CASE BLOCK_RED%
      GetBlockColor = RGB(RED)
    CASE BLOCK_YELLOW_FULL%
      GetBlockColor = RGB(YELLOW)
    CASE ELSE
      GetBlockColor = RGB(CYAN)
  END SELECT
END FUNCTION

DIM INTEGER blocks%(4, 7)
blocks%(0, 0) = BLOCK_RED%
blocks%(0, 1) = BLOCK_YELLOW_FULL%
blocks%(0, 2) = 0
blocks%(0, 3) = BLOCK_RED%

DIM r%, c%, bx%, by%
CLS RGB(BLACK)
FOR c% = 0 TO 3
  IF blocks%(0, c%) > 0 THEN
    bx% = c% * 40 + 4
    by% = 40
    BOX bx%, by%, 35, 12, 0, , GetBlockColor(blocks%(0, c%))
  END IF
NEXT
PRINT "done"
END
