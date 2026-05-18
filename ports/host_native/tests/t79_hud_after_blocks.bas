OPTION EXPLICIT

CONST BG% = RGB(BLACK)
CONST FG% = RGB(WHITE)
CONST BORDER% = RGB(MYRTLE)
CONST BLOCK_W% = 35
CONST BLOCK_H% = 12
CONST BLOCK_GAP% = 4
CONST BLOCK_TOP% = 40

DIM fps$
DIM INTEGER blocks%(4, 7)

FUNCTION GetBlockX%(c%)
  GetBlockX% = BLOCK_GAP% + c% * (BLOCK_W% + BLOCK_GAP%)
END FUNCTION

FUNCTION GetBlockY%(r%)
  GetBlockY% = BLOCK_TOP% + r% * (BLOCK_H% + BLOCK_GAP% + 3)
END FUNCTION

FUNCTION GetBlockColor%(blockType%)
  SELECT CASE blockType%
    CASE 1
      GetBlockColor% = RGB(RED)
    CASE 2
      GetBlockColor% = RGB(YELLOW)
    CASE ELSE
      GetBlockColor% = RGB(BLUE)
  END SELECT
END FUNCTION

SUB DrawSingleBlock(r%, c%)
  LOCAL bx%, by%, blockType%
  blockType% = blocks%(r%, c%)
  IF blockType% > 0 THEN
    bx% = GetBlockX%(c%)
    by% = GetBlockY%(r%)
    BOX bx%, by%, BLOCK_W%, BLOCK_H%, 0, , GetBlockColor%(blockType%)
    BOX bx%, by%, BLOCK_W%, BLOCK_H%, 1, BORDER%
  END IF
END SUB

SUB DrawBlocks()
  LOCAL r%, c%
  FOR r% = 0 TO 4
    FOR c% = 0 TO 7
      DrawSingleBlock r%, c%
    NEXT
  NEXT
END SUB

SUB DrawHUD()
  LOCAL s$
  BOX 0, 0, MM.HRES, 18, 0, , BG%
  s$ = "L1 SCORE 0 LIVES 3"
  PRINT "before-if"; LEN(fps$); ":"; fps$
  TEXT 6, 3, s$, "LT", , , FG%, BG%
  IF fps$ <> "" THEN
    PRINT "right"; LEN(fps$); ":"; fps$
    TEXT MM.HRES-4, 3, fps$, "RT", , , FG%, BG%
  END IF
END SUB

blocks%(2, 2) = 1
blocks%(2, 3) = 1
blocks%(2, 4) = 1
blocks%(2, 5) = 1
blocks%(3, 1) = 1
blocks%(3, 2) = 1
blocks%(3, 3) = 2
blocks%(3, 4) = 2
blocks%(3, 5) = 1
blocks%(3, 6) = 1
blocks%(4, 0) = 1
blocks%(4, 1) = 1
blocks%(4, 2) = 1
blocks%(4, 3) = 1
blocks%(4, 4) = 1
blocks%(4, 5) = 1
blocks%(4, 6) = 1
blocks%(4, 7) = 1

fps$ = ""
CLS BG%
DrawBlocks
DrawHUD
PRINT "after"; LEN(fps$); ":"; fps$
