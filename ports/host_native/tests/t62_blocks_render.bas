CONST BLOCK_ROWS% = 5
CONST BLOCK_COLS% = 8
CONST BLOCK_W% = 35
CONST BLOCK_H% = 12
CONST BLOCK_GAP% = 4
CONST BLOCK_TOP% = 40
CONST BLOCK_RED% = 30
CONST BLOCK_YELLOW_FULL% = 12
CONST COL_BG% = RGB(BLACK)
CONST COL_BORDER% = RGB(MYRTLE)

DATA "0","0","0","0","0","0","0","0"
DATA "0","0","0","0","0","0","0","0"
DATA "0","0","R","R","R","R","0","0"
DATA "0","R","R","Y","Y","R","R","0"
DATA "R","R","R","R","R","R","R","R"

DIM INTEGER blocks%(BLOCK_ROWS%-1, BLOCK_COLS%-1)
DIM r%, c%
DIM blockChar$

FUNCTION GetBlockX(c%) AS INTEGER
  GetBlockX = c% * (BLOCK_W% + BLOCK_GAP%) + BLOCK_GAP%
END FUNCTION

FUNCTION GetBlockY(r%) AS INTEGER
  GetBlockY = BLOCK_TOP% + r% * (BLOCK_H% + BLOCK_GAP% + 3)
END FUNCTION

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

SUB Draw3DHighlight(x%, y%, w%, h%)
  LINE x%, y%, x%+w%-1, y%, , RGB(WHITE)
  LINE x%, y%, x%, y%+h%-1, , RGB(WHITE)
END SUB

RESTORE
FOR r% = 0 TO BLOCK_ROWS%-1
  FOR c% = 0 TO BLOCK_COLS%-1
    READ blockChar$
    SELECT CASE blockChar$
      CASE "R": blocks%(r%, c%) = BLOCK_RED%
      CASE "Y": blocks%(r%, c%) = BLOCK_YELLOW_FULL%
      CASE ELSE: blocks%(r%, c%) = 0
    END SELECT
  NEXT
NEXT

DIM bx%, by%
CLS COL_BG%
FOR r% = 0 TO BLOCK_ROWS%-1
  FOR c% = 0 TO BLOCK_COLS%-1
    IF blocks%(r%, c%) > 0 THEN
      bx% = GetBlockX(c%)
      by% = GetBlockY(r%)
      BOX bx%, by%, BLOCK_W%, BLOCK_H%, 0, , GetBlockColor(blocks%(r%, c%))
      BOX bx%, by%, BLOCK_W%, BLOCK_H%, 1, COL_BORDER%
      Draw3DHighlight bx%+1, by%+1, BLOCK_W%-2, BLOCK_H%-1
    END IF
  NEXT
NEXT
PRINT "done"
END
