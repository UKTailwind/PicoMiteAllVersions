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

PRINT "RED(30)="; GetBlockColor(30)
PRINT "YEL(12)="; GetBlockColor(12)
PRINT "OTHER(0)="; GetBlockColor(0)
PRINT "OTHER(99)="; GetBlockColor(99)
END
