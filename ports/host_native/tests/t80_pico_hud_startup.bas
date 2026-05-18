OPTION EXPLICIT

CONST W% = MM.HRES
CONST H% = MM.VRES
CONST HUDH% = 18
CONST COL_BG%     = RGB(BLACK)
CONST COL_TXT%    = RGB(WHITE)
CONST COL_BORDER% = RGB(MYRTLE)
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

DIM INTEGER currentLevel%=1
DIM INTEGER score%, lives%
DIM INTEGER frames%, t0%
DIM fps$
DIM INTEGER blocks%(BLOCK_ROWS%-1, BLOCK_COLS%-1)

SUB DrawHUD()
  LOCAL s$
  BOX 0, 0, W%, HUDH%, 0, , COL_BG%
  s$ = "L" + STR$(currentLevel%) + " Score " + STR$(score%) + " Lives " + STR$(lives%)
  PRINT "before-if"; LEN(fps$); ":"; fps$
  TEXT 6, 3, s$, "LT", , , COL_TXT%, COL_BG%
  IF fps$ <> "" THEN
    PRINT "right"; LEN(fps$); ":"; fps$
    TEXT W%-4, 3, fps$, "RT", , , COL_TXT%, COL_BG%
  END IF
END SUB

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

FUNCTION GetBlockX(c%) AS INTEGER
  GetBlockX = c% * (BLOCK_W% + BLOCK_GAP%) + BLOCK_GAP%
END FUNCTION

FUNCTION GetBlockY(r%) AS INTEGER
  GetBlockY = BLOCK_TOP% + r% * (BLOCK_H% + BLOCK_GAP% + 3)
END FUNCTION

SUB DrawSingleBlock(r%, c%)
  LOCAL bx%, by%, blockType%
  blockType% = blocks%(r%, c%)
  IF blockType% > 0 THEN
    bx% = GetBlockX(c%)
    by% = GetBlockY(r%)
    BOX bx%, by%, BLOCK_W%, BLOCK_H%, 0, , GetBlockColor(blockType%)
    BOX bx%, by%, BLOCK_W%, BLOCK_H%, 1, COL_BORDER%
  END IF
END SUB

SUB DrawBlocks()
  LOCAL r%, c%
  FOR r% = 0 TO BLOCK_ROWS%-1
    FOR c% = 0 TO BLOCK_COLS%-1
      IF blocks%(r%, c%) > 0 THEN DrawSingleBlock r%, c%
    NEXT
  NEXT
END SUB

SUB InitBlocks()
  LOCAL r%, c%, blockChar$
  RESTORE
  FOR r% = 0 TO BLOCK_ROWS%-1
    FOR c% = 0 TO BLOCK_COLS%-1
      READ blockChar$
      SELECT CASE blockChar$
        CASE "R"
          blocks%(r%, c%) = BLOCK_RED%
        CASE "Y"
          blocks%(r%, c%) = BLOCK_YELLOW_FULL%
        CASE ELSE
          blocks%(r%, c%) = 0
      END SELECT
    NEXT
  NEXT
END SUB

score% = 0
lives% = 3
frames% = 0
t0% = TIMER
fps$ = ""

InitBlocks
FASTGFX CREATE
FASTGFX FPS 50
CLS COL_BG%
DrawBlocks
DrawHUD
PRINT "after"; LEN(fps$); ":"; fps$
