OPTION EXPLICIT

CONST COL_BG% = RGB(BLACK)

DIM INTEGER explosionColor%

SUB TriggerExplosion(blockColor%)
  explosionColor% = blockColor%
  BOX 0, 0, 10, 10, 0, , explosionColor%
END SUB

FUNCTION GetBlockColor(blockType%) AS INTEGER
  SELECT CASE blockType%
    CASE 30
      GetBlockColor = RGB(RED)
    CASE ELSE
      GetBlockColor = RGB(CYAN)
  END SELECT
END FUNCTION

FASTGFX CREATE
CLS COL_BG%
TriggerExplosion GetBlockColor(30)
PRINT explosionColor%
