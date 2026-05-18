CONST BLOCK_ROWS% = 5
CONST BLOCK_COLS% = 8
CONST BLOCK_W% = 35
CONST BLOCK_H% = 12
CONST BLOCK_GAP% = 4
CONST BLOCK_TOP% = 40
CONST BLOCK_RED% = 30
CONST BLOCK_YELLOW_FULL% = 12

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

FOR r% = 0 TO BLOCK_ROWS%-1
  FOR c% = 0 TO BLOCK_COLS%-1
    IF blocks%(r%, c%) > 0 THEN
      PRINT "block("; r%; ","; c%; ") at "; GetBlockX(c%); ","; GetBlockY(r%); " val="; blocks%(r%, c%)
    END IF
  NEXT
NEXT
PRINT "done"
END
