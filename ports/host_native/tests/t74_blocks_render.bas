CONST BLOCK_NONE% = 0
CONST BLOCK_RED% = 30
CONST BLOCK_YELLOW_FULL% = 12
CONST BLOCK_WHITE% = 40

FUNCTION GetBlockColor(blockType%) AS INTEGER
  SELECT CASE blockType%
    CASE BLOCK_RED%
      GetBlockColor = RGB(RED)
    CASE BLOCK_YELLOW_FULL%
      GetBlockColor = RGB(YELLOW)
    CASE BLOCK_WHITE%
      GetBlockColor = RGB(WHITE)
    CASE ELSE
      GetBlockColor = RGB(CYAN)
  END SELECT
END FUNCTION

CLS RGB(BLACK)
DIM INTEGER blocks%(4, 9)
blocks%(0, 0) = BLOCK_RED%
blocks%(0, 1) = BLOCK_YELLOW_FULL%
blocks%(0, 2) = BLOCK_WHITE%
blocks%(0, 3) = BLOCK_RED%
blocks%(0, 4) = BLOCK_NONE%
blocks%(1, 0) = BLOCK_YELLOW_FULL%
blocks%(1, 1) = BLOCK_RED%
blocks%(1, 2) = BLOCK_RED%
blocks%(1, 3) = BLOCK_WHITE%
blocks%(1, 4) = BLOCK_YELLOW_FULL%

DIM r%, c%, bx%, by%, col_c%
FOR r% = 0 TO 1
  FOR c% = 0 TO 4
    IF blocks%(r%, c%) <> BLOCK_NONE% THEN
      bx% = c% * 30 + 4
      by% = r% * 15 + 10
      col_c% = GetBlockColor(blocks%(r%, c%))
      BOX bx%, by%, 28, 12, 0, , col_c%
    END IF
  NEXT
NEXT
PRINT "done"
END
