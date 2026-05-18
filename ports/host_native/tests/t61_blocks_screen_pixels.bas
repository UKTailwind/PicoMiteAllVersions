OPTION EXPLICIT

CONST W% = MM.HRES
CONST H% = MM.VRES
CONST BG% = RGB(BLACK)
CONST TXT% = RGB(WHITE)
CONST BORDER% = RGB(MYRTLE)
CONST PAD% = RGB(GREEN)
CONST BALL% = RGB(RED)
CONST BLOCK_W% = 35
CONST BLOCK_H% = 12
CONST BLOCK_GAP% = 4
CONST BLOCK_TOP% = 40

DIM INTEGER blocks%(4, 7)

FUNCTION GetBlockX%(c%)
  GetBlockX% = BLOCK_GAP% + c% * (BLOCK_W% + BLOCK_GAP%)
END FUNCTION

FUNCTION GetBlockY%(r%)
  GetBlockY% = BLOCK_TOP% + r% * (BLOCK_H% + BLOCK_GAP%)
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

SUB DrawHUD()
  LOCAL s$
  s$ = "L1 SCORE 0 LIVES 3"
  TEXT 6, 3, s$, "LT", , , TXT%, BG%
END SUB

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

SUB DrawBallAt(x%, y%)
  LOCAL cx%, cy%
  cx% = x% + 6
  cy% = y% + 6
  CIRCLE cx%, cy%, 6, 0, 1.0, , BALL%
  CIRCLE cx%-2, cy%-2, 1, 0, 1.0, , RGB(WHITE)
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

FASTGFX CREATE
CLS BG%
DrawHUD
DrawBlocks
TEXT W%\2, H%\2, "HIT SPACE TO START", "CT", , , TXT%, BG%
DrawBallAt 154, 185
BOX 131, 221, 58, 7, 0, , PAD%
FASTGFX SWAP
FASTGFX CLOSE
