CONST BLOCK_ROWS% = 5
CONST BLOCK_COLS% = 8
CONST BLOCK_RED% = 30
CONST BLOCK_ORANGE% = 20
CONST BLOCK_YELLOW_FULL% = 12
CONST BLOCK_BLUE% = 99

' Level 1 data
DATA "0","0","0","0","0","0","0","0"
DATA "0","0","0","0","0","0","0","0"
DATA "0","0","R","R","R","R","0","0"
DATA "0","R","R","Y","Y","R","R","0"
DATA "R","R","R","R","R","R","R","R"

DIM INTEGER blocks%(BLOCK_ROWS%-1, BLOCK_COLS%-1)
DIM INTEGER totalBlocks%, blocksLeft%
DIM r%, c%
DIM blockChar$

totalBlocks% = 0
RESTORE
FOR r% = 0 TO BLOCK_ROWS%-1
  FOR c% = 0 TO BLOCK_COLS%-1
    READ blockChar$
    SELECT CASE blockChar$
      CASE "R"
        blocks%(r%, c%) = BLOCK_RED%
        totalBlocks% = totalBlocks% + 1
      CASE "O"
        blocks%(r%, c%) = BLOCK_ORANGE%
        totalBlocks% = totalBlocks% + 1
      CASE "Y"
        blocks%(r%, c%) = BLOCK_YELLOW_FULL%
        totalBlocks% = totalBlocks% + 1
      CASE "B"
        blocks%(r%, c%) = BLOCK_BLUE%
      CASE ELSE
        blocks%(r%, c%) = 0
    END SELECT
  NEXT
NEXT
blocksLeft% = totalBlocks%

PRINT "total="; totalBlocks%; " left="; blocksLeft%
FOR r% = 0 TO BLOCK_ROWS%-1
  FOR c% = 0 TO BLOCK_COLS%-1
    IF blocks%(r%, c%) > 0 THEN
      PRINT r%; ","; c%; "="; blocks%(r%, c%)
    END IF
  NEXT
NEXT
PRINT "done"
END
