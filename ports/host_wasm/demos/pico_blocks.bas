' ==========================================
' PicoCalc Blocks Game (FASTGFX version)
' - Double-buffered with automatic scanline diffing
' - DMA transfers to SPI display on core1
' ==========================================
OPTION EXPLICIT

' ---- Tunables (calibrated for 50 FPS) ----
CONST PAD_ACCEL = 0.8
CONST PAD_DECAY = 0.95
CONST PAD_MAX   = 10
CONST BRADIUS   = 6
CONST BALL_SPEED_INIT = 1.2
CONST BALL_SPEED_ACCEL_PER_SEC = 0.012
CONST PADDLE_KICK = 0.2

' ---- Screen & colors ----
CONST W% = MM.HRES
CONST H% = MM.VRES
CONST HUDH% = 18

CONST COL_BG%     = RGB(BLACK)
CONST COL_TXT%    = RGB(WHITE)
CONST COL_BORDER% = RGB(MYRTLE)
CONST COL_PAD%    = RGB(GREEN)
CONST COL_BALL%   = RGB(RED)

' ---- Block layout ----
CONST LEVELS% = 10
CONST BLOCK_ROWS% = 5
CONST BLOCK_COLS% = 8
CONST BLOCK_W% = 35
CONST BLOCK_H% = 12
CONST BLOCK_GAP% = 4
CONST BLOCK_TOP% = 40

' ---- Block types ----
CONST BLOCK_RED% = 30
CONST BLOCK_ORANGE% = 20
CONST BLOCK_YELLOW_FULL% = 12
CONST BLOCK_YELLOW_DMG% = 11
CONST BLOCK_BLUE% = 99

' ---- Level DATA ----
' Level 1 - Easy warmup
DATA "0","0","0","0","0","0","0","0"
DATA "0","0","0","0","0","0","0","0"
DATA "0","0","R","R","R","R","0","0"
DATA "0","R","R","Y","Y","R","R","0"
DATA "R","R","R","R","R","R","R","R"
' Level 2
DATA "0","0","R","R","R","R","0","0"
DATA "0","O","O","O","O","O","O","0"
DATA "Y","Y","Y","Y","Y","Y","Y","Y"
DATA "Y","Y","Y","Y","Y","Y","Y","Y"
DATA "Y","Y","Y","Y","Y","Y","Y","Y"
' Level 3
DATA "R","R","O","O","O","O","R","R"
DATA "R","O","Y","Y","Y","Y","O","R"
DATA "O","Y","Y","Y","Y","Y","Y","O"
DATA "O","Y","Y","Y","Y","Y","Y","O"
DATA "Y","Y","Y","Y","Y","Y","Y","Y"
' Level 4
DATA "0","0","0","R","R","0","0","0"
DATA "0","0","O","O","O","O","0","0"
DATA "0","Y","Y","Y","Y","Y","Y","0"
DATA "Y","Y","Y","Y","Y","Y","Y","Y"
DATA "0","0","0","B","B","0","0","0"
' Level 5
DATA "R","R","R","R","R","R","R","R"
DATA "R","0","0","0","0","0","0","R"
DATA "R","0","B","B","B","B","0","R"
DATA "R","0","0","0","0","0","0","R"
DATA "R","R","R","R","R","R","R","R"
' Level 6
DATA "R","R","0","0","0","0","O","O"
DATA "R","Y","0","Y","Y","0","Y","O"
DATA "0","0","B","Y","Y","B","0","0"
DATA "Y","Y","0","B","B","0","Y","Y"
DATA "O","O","0","0","0","0","R","R"
' Level 7
DATA "R","0","0","Y","Y","0","0","R"
DATA "O","O","B","Y","Y","B","O","O"
DATA "Y","Y","Y","B","B","Y","Y","Y"
DATA "Y","Y","B","O","O","B","Y","Y"
DATA "0","0","0","R","R","0","0","0"
' Level 8
DATA "R","O","R","O","R","O","R","O"
DATA "Y","B","Y","B","Y","B","Y","B"
DATA "O","Y","O","Y","O","Y","O","Y"
DATA "B","Y","B","Y","B","Y","B","Y"
DATA "R","O","R","O","R","O","R","O"
' Level 9
DATA "0","0","R","R","R","R","0","0"
DATA "0","0","O","B","B","O","0","0"
DATA "Y","Y","Y","B","B","Y","Y","Y"
DATA "Y","Y","Y","B","B","Y","Y","Y"
DATA "0","0","O","O","O","O","0","0"
' Level 10
DATA "R","B","O","B","O","B","O","R"
DATA "B","Y","Y","0","0","Y","Y","B"
DATA "O","Y","B","Y","Y","B","Y","O"
DATA "B","Y","Y","0","0","Y","Y","B"
DATA "R","B","O","B","O","B","O","R"

' ---- State ----
DIM INTEGER currentLevel%=1
DIM FLOAT bx!, by!
DIM INTEGER br%
DIM FLOAT vx!, vy!
DIM INTEGER lastAccelTime%
DIM px!, py!, pw%, ph%, pvx!
DIM INTEGER score%, lives%
DIM INTEGER ballLaunched%
DIM INTEGER frames%, t0%
DIM fps$
DIM k$
DIM INTEGER blocks%(BLOCK_ROWS%-1, BLOCK_COLS%-1)
DIM INTEGER totalBlocks%, blocksLeft%
DIM FLOAT hitPos!, angle!
DIM INTEGER explosionActive%, explosionX%, explosionY%, explosionFrame%, explosionColor%
DIM INTEGER oldScore%, oldLives%
DIM INTEGER lastHitRow%, lastHitCol%, hitTimeout%

' ---- Previous position tracking (for erase) ----
DIM INTEGER prevBallX%, prevBallY%
DIM INTEGER prevPadX%
DIM INTEGER prevBallLaunched%
DIM INTEGER prevExpX%, prevExpY%, prevExpSize%, expCleanup%

' ---- Beeps ----
SUB BeepServe(): PLAY TONE 700,700 : PAUSE 40 : PLAY STOP : END SUB
SUB BeepPaddle(): PLAY TONE 800,800 : PAUSE 20 : PLAY STOP : END SUB
SUB BeepWall(): PLAY TONE 600,600 : PAUSE 20 : PLAY STOP : END SUB
SUB BeepBlock(): PLAY TONE 1200,1200 : PAUSE 25 : PLAY STOP : END SUB
SUB BeepMiss(): PLAY TONE 200,200 : PAUSE 80 : PLAY STOP : END SUB

SUB ResetBall()
  ballLaunched% = 0
  bx! = px! + pw%/2 - br%
  by! = py! - 2*br% - 2
  IF RND > 0.5 THEN vx! = BALL_SPEED_INIT ELSE vx! = -BALL_SPEED_INIT
  vy! = -BALL_SPEED_INIT
  lastAccelTime% = TIMER
  hitTimeout% = 0
END SUB

SUB Draw3DHighlight(x%, y%, w%, h%)
  LINE x%, y%, x%+w%-1, y%, , RGB(WHITE)
  LINE x%, y%, x%, y%+h%-1, , RGB(WHITE)
END SUB

SUB DrawHUD()
  LOCAL s$
  BOX 0, 0, W%, HUDH%, 0, , COL_BG%
  s$ = "L" + STR$(currentLevel%) + " Score " + STR$(score%) + " Lives " + STR$(lives%)
  TEXT 6, 3, s$, "LT", , , COL_TXT%, COL_BG%
  IF fps$ <> "" THEN
    TEXT W%-4, 3, fps$, "RT", , , COL_TXT%, COL_BG%
  END IF
END SUB

SUB DrawPaddleAt(x%, y%)
  BOX x%, y%, pw%, ph%, 0, , COL_PAD%
  Draw3DHighlight x%, y%, pw%, ph%
END SUB

SUB DrawBallAt(x%, y%)
  LOCAL cx%, cy%
  cx% = x% + br%
  cy% = y% + br%
  CIRCLE cx%, cy%, br%, 0, 1.0, , COL_BALL%
  CIRCLE cx%-2, cy%-2, 1, 0, 1.0, , RGB(WHITE)
END SUB

SUB DrawSingleBlock(r%, c%)
  LOCAL bx%, by%, blockType%
  blockType% = blocks%(r%, c%)
  IF blockType% > 0 THEN
    bx% = GetBlockX(c%)
    by% = GetBlockY(r%)
    BOX bx%, by%, BLOCK_W%, BLOCK_H%, 0, , GetBlockColor(blockType%)
    BOX bx%, by%, BLOCK_W%, BLOCK_H%, 1, COL_BORDER%
    Draw3DHighlight bx%+1, by%+1, BLOCK_W%-2, BLOCK_H%-1
  END IF
END SUB

SUB RedrawBlocksInRegion(rx%, ry%, rw%, rh%)
  LOCAL r%, c%, r1%, r2%, c1%, c2%, by%
  LOCAL bStep%
  bStep% = BLOCK_H% + BLOCK_GAP% + 3
  r1% = (ry% - BLOCK_TOP%) \ bStep%
  IF r1% < 0 THEN r1% = 0
  r2% = (ry% + rh% - BLOCK_TOP%) \ bStep%
  IF r2% >= BLOCK_ROWS% THEN r2% = BLOCK_ROWS% - 1
  c1% = (rx% - BLOCK_GAP%) \ (BLOCK_W% + BLOCK_GAP%)
  IF c1% < 0 THEN c1% = 0
  c2% = (rx% + rw%) \ (BLOCK_W% + BLOCK_GAP%)
  IF c2% >= BLOCK_COLS% THEN c2% = BLOCK_COLS% - 1
  FOR r% = r1% TO r2%
    FOR c% = c1% TO c2%
      IF blocks%(r%, c%) > 0 THEN DrawSingleBlock r%, c%
    NEXT
  NEXT
END SUB

SUB TriggerExplosion(x%, y%, w%, h%, blockColor%)
  explosionActive% = 1
  explosionX% = x% + w%/2
  explosionY% = y% + h%/2
  explosionFrame% = 0
  explosionColor% = blockColor%
  prevExpSize% = 0
END SUB

SUB DrawExplosion()
  LOCAL size%
  IF explosionActive% = 0 THEN EXIT SUB
  size% = (explosionFrame% + 1) * 4
  CIRCLE explosionX%, explosionY%, size%, 0, 1.0, , explosionColor%
  LINE explosionX%-size%, explosionY%, explosionX%+size%, explosionY%, , explosionColor%
  LINE explosionX%, explosionY%-size%, explosionX%, explosionY%+size%, , explosionColor%
  IF explosionFrame% > 0 THEN
    LOCAL offset%
    offset% = size% * 0.7
    LINE explosionX%-offset%, explosionY%-offset%, explosionX%+offset%, explosionY%+offset%, , explosionColor%
    LINE explosionX%-offset%, explosionY%+offset%, explosionX%+offset%, explosionY%-offset%, , explosionColor%
  END IF
  IF size% > prevExpSize% THEN prevExpSize% = size%
  explosionFrame% = explosionFrame% + 1
  IF explosionFrame% > 2 THEN
    explosionActive% = 0
    expCleanup% = 2
  END IF
END SUB

SUB DestroyBlock(r%, c%, points%)
  LOCAL bx%, by%
  bx% = GetBlockX(c%)
  by% = GetBlockY(r%)
  TriggerExplosion bx%, by%, BLOCK_W%, BLOCK_H%, GetBlockColor(blocks%(r%, c%))
  blocks%(r%, c%) = 0
  blocksLeft% = blocksLeft% - 1
  score% = score% + points%
  BOX bx%, by%, BLOCK_W%, BLOCK_H%, 0, , COL_BG%
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
  LOCAL r%, c%, blockChar$, skipRows%, i%
  totalBlocks% = 0
  RESTORE
  skipRows% = (currentLevel% - 1) * BLOCK_ROWS% * BLOCK_COLS%
  FOR i% = 1 TO skipRows%
    READ blockChar$
  NEXT
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
END SUB

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
    CASE BLOCK_ORANGE%
      GetBlockColor = RGB(RUST)
    CASE BLOCK_YELLOW_FULL%
      GetBlockColor = RGB(YELLOW)
    CASE BLOCK_YELLOW_DMG%
      GetBlockColor = RGB(BROWN)
    CASE BLOCK_BLUE%
      GetBlockColor = RGB(BLUE)
    CASE ELSE
      GetBlockColor = RGB(CYAN)
  END SELECT
END FUNCTION

FUNCTION CheckBlockCollision(ballX%, ballY%, ballR%) AS INTEGER
  LOCAL r%, c%, bx%, by%, bx2%, by2%, blockType%
  LOCAL ballLeft%, ballRight%, ballTop%, ballBot%
  LOCAL r1%, r2%, c1%, c2%, bStep%
  ballLeft% = ballX%
  ballRight% = ballX% + 2*ballR%
  ballTop% = ballY%
  ballBot% = ballY% + 2*ballR%
  IF hitTimeout% > 0 THEN hitTimeout% = hitTimeout% - 1
  bStep% = BLOCK_H% + BLOCK_GAP% + 3
  r1% = (ballTop% - BLOCK_TOP% - BLOCK_H%) \ bStep%
  IF r1% < 0 THEN r1% = 0
  r2% = (ballBot% - BLOCK_TOP%) \ bStep%
  IF r2% >= BLOCK_ROWS% THEN r2% = BLOCK_ROWS% - 1
  IF r1% > r2% THEN CheckBlockCollision = 0 : EXIT FUNCTION
  c1% = (ballLeft% - BLOCK_GAP% - BLOCK_W%) \ (BLOCK_W% + BLOCK_GAP%)
  IF c1% < 0 THEN c1% = 0
  c2% = (ballRight% - BLOCK_GAP%) \ (BLOCK_W% + BLOCK_GAP%)
  IF c2% >= BLOCK_COLS% THEN c2% = BLOCK_COLS% - 1
  FOR r% = r1% TO r2%
    FOR c% = c1% TO c2%
      blockType% = blocks%(r%, c%)
      IF blockType% > 0 THEN
        bx% = c% * (BLOCK_W% + BLOCK_GAP%) + BLOCK_GAP%
        by% = BLOCK_TOP% + r% * bStep%
        bx2% = bx% + BLOCK_W%
        by2% = by% + BLOCK_H%
        IF ballRight% >= bx% AND ballLeft% <= bx2% AND ballBot% >= by% AND ballTop% <= by2% THEN
          IF hitTimeout% > 0 AND r% = lastHitRow% AND c% = lastHitCol% THEN
            ' Still in timeout for this block
          ELSE
            lastHitRow% = r%
            lastHitCol% = c%
            hitTimeout% = 15
          IF blockType% = BLOCK_BLUE% THEN
            CheckBlockCollision = 1
            EXIT FUNCTION
          ELSE IF blockType% = BLOCK_YELLOW_FULL% THEN
            blocks%(r%, c%) = BLOCK_YELLOW_DMG%
            DrawSingleBlock r%, c%
          ELSE IF blockType% = BLOCK_YELLOW_DMG% THEN
            DestroyBlock r%, c%, 10
          ELSE IF blockType% = BLOCK_ORANGE% THEN
            DestroyBlock r%, c%, 20
          ELSE IF blockType% = BLOCK_RED% THEN
            DestroyBlock r%, c%, 30
          END IF
          CheckBlockCollision = 1
          EXIT FUNCTION
          END IF
        END IF
      END IF
    NEXT
  NEXT
  CheckBlockCollision = 0
END FUNCTION

' ---- Init ----
pw% = W% \ 6 : IF pw% < 30 THEN pw% = 30
ph% = 6
px! = (W% - pw%) / 2
py! = H% - (ph% + 6)
pvx! = 0
br% = BRADIUS
bx! = W% \ 2 : by! = H% \ 2
vx! = BALL_SPEED_INIT
vy! = -BALL_SPEED_INIT
score% = 0 : lives% = 3
oldScore% = 0 : oldLives% = 3
ballLaunched% = 0
explosionActive% = 0
hitTimeout% = 0
lastHitRow% = -1
lastHitCol% = -1
frames% = 0 : t0% = TIMER
fps$ = ""
lastAccelTime% = TIMER
prevExpSize% = 0 : expCleanup% = 0

InitBlocks

' ---- Setup FASTGFX ----
FASTGFX CREATE
FASTGFX FPS 50
CLS COL_BG%
DrawBlocks
DrawHUD
DrawPaddleAt INT(px!), INT(py!)
DrawBallAt INT(bx!), INT(by!)
TEXT W%\2, H%\2, "Hit SPACE to start", "CT", , , COL_TXT%, COL_BG%
FASTGFX SWAP
FASTGFX SYNC

prevBallX% = INT(bx!) : prevBallY% = INT(by!)
prevPadX% = INT(px!)
prevBallLaunched% = ballLaunched%
prevExpX% = 0 : prevExpY% = 0

BeepServe

' ---- Main loop ----
DO
  k$ = INKEY$
  IF k$ <> "" THEN
    SELECT CASE ASC(k$)
      CASE 130: pvx! = pvx! - PAD_ACCEL
      CASE 131: pvx! = pvx! + PAD_ACCEL
      CASE  32: IF ballLaunched% = 0 THEN ballLaunched% = 1 : BeepServe
      CASE  27: EXIT DO
      CASE  49 TO 56
        PLAY TONE 220 * (2 ^ ((ASC(k$)-48)/12.0)), 0 : PAUSE 60 : PLAY STOP
    END SELECT
  ENDIF

  ' ---- Erase old positions ----
  BOX prevBallX%-1, prevBallY%-1, br%*2+4, br%*2+4, 0, , COL_BG%
  IF prevBallY% < BLOCK_TOP% + BLOCK_ROWS% * (BLOCK_H% + BLOCK_GAP% + 3) THEN
    RedrawBlocksInRegion prevBallX%-1, prevBallY%-1, br%*2+4, br%*2+4
  END IF
  BOX prevPadX%-1, INT(py!)-1, pw%+2, ph%+2, 0, , COL_BG%
  IF explosionActive% OR expCleanup% > 0 THEN
    IF prevExpSize% > 0 THEN
      BOX prevExpX%-prevExpSize%-2, prevExpY%-prevExpSize%-2, prevExpSize%*2+4, prevExpSize%*2+4, 0, , COL_BG%
      RedrawBlocksInRegion prevExpX%-prevExpSize%-2, prevExpY%-prevExpSize%-2, prevExpSize%*2+4, prevExpSize%*2+4
    END IF
    IF explosionActive% = 0 THEN
      expCleanup% = expCleanup% - 1
      IF expCleanup% <= 0 THEN prevExpSize% = 0
    END IF
  END IF
  IF prevBallLaunched% = 0 THEN
    BOX 0, H%\2 - 10, W%, 20, 0, , COL_BG%
    RedrawBlocksInRegion 0, H%\2 - 10, W%, 20
  END IF

  ' ---- Paddle physics ----
  pvx! = pvx! * PAD_DECAY
  IF pvx! >  PAD_MAX THEN pvx! =  PAD_MAX
  IF pvx! < -PAD_MAX THEN pvx! = -PAD_MAX
  px! = px! + pvx!
  IF px! < 0 THEN px! = 0 : IF pvx! < 0 THEN pvx! = 0
  IF px! > (W% - pw%) THEN px! = W% - pw% : IF pvx! > 0 THEN pvx! = 0

  ' ---- Ball physics ----
  IF ballLaunched% = 0 THEN
    bx! = px! + pw%/2 - br%
    by! = py! - 2*br% - 2
  ELSE
    IF TIMER - lastAccelTime% >= 1000 THEN
      IF vy! > 0 THEN
        vy! = vy! + BALL_SPEED_ACCEL_PER_SEC
      ELSE
        vy! = vy! - BALL_SPEED_ACCEL_PER_SEC
      END IF
      lastAccelTime% = lastAccelTime% + 1000
    END IF
    bx! = bx! + vx!
    by! = by! + vy!
  END IF

  ' Wall bounces
  IF INT(bx!) < 0 THEN
    bx! = 0 : vx! = -vx! : hitTimeout% = 0 : BeepWall
  END IF
  IF INT(bx!) > W% - 2*br% THEN
    bx! = W% - 2*br% : vx! = -vx! : hitTimeout% = 0 : BeepWall
  END IF
  IF INT(by!) < HUDH% + 1 THEN
    by! = HUDH% + 1 : vy! = -vy! : hitTimeout% = 0 : BeepWall
  END IF

  ' Block collision
  IF ballLaunched% = 1 AND CheckBlockCollision(INT(bx!), INT(by!), br%) = 1 THEN
    vy! = -vy! : BeepBlock
  END IF

  ' Paddle collision
  IF ballLaunched% = 1 AND INT(by!)+2*br% >= INT(py!) AND INT(by!) <= INT(py!)+ph% AND INT(bx!)+2*br% >= INT(px!) AND INT(bx!) <= INT(px!)+pw% THEN
    by! = INT(py!) - 2*br%
    vy! = -vy!
    hitTimeout% = 0
    hitPos! = (bx! + br% - px!) / pw%
    IF hitPos! < 0 THEN hitPos! = 0
    IF hitPos! > 1 THEN hitPos! = 1
    angle! = (hitPos! - 0.5) * 2.0
    vx! = vx! + angle! + pvx! * PADDLE_KICK
    score% = score% + 1
    BeepPaddle
  ENDIF

  ' Miss
  IF ballLaunched% = 1 AND INT(by!) > H% - 2*br% THEN
    lives% = lives% - 1
    BeepMiss
    IF lives% > 0 THEN
      ResetBall
      BeepServe
    ELSE
      FASTGFX CLOSE
      CLS COL_BG%
      PRINT "GAME OVER!  Score="; score%
      END
    END IF
  ENDIF

  ' Level complete
  IF blocksLeft% = 0 THEN
    IF currentLevel% < LEVELS% THEN
      currentLevel% = currentLevel% + 1
      ResetBall
      CLS COL_BG%
      InitBlocks
      DrawBlocks
      DrawHUD
      oldScore% = score%
      oldLives% = lives%
      BeepServe
    ELSE
      FASTGFX CLOSE
      CLS COL_BG%
      PRINT "YOU WIN ALL LEVELS!  Score="; score%; "  Lives: "; lives%
      END
    END IF
  END IF

  ' HUD update on change
  IF score% <> oldScore% OR lives% <> oldLives% THEN
    oldScore% = score%
    oldLives% = lives%
    DrawHUD
  END IF

  ' FPS counter
  frames% = frames% + 1
  IF TIMER - t0% >= 1000 THEN
    fps$ = STR$(frames%) + " FPS"
    frames% = 0 : t0% = TIMER
    DrawHUD
  END IF

  ' ---- Draw new positions ----
  DrawPaddleAt INT(px!), INT(py!)
  DrawBallAt INT(bx!), INT(by!)
  IF explosionActive% THEN
    prevExpX% = explosionX% : prevExpY% = explosionY%
    expCleanup% = 2
  END IF
  DrawExplosion
  IF ballLaunched% = 0 THEN
    TEXT W%\2, H%\2, "Hit SPACE to start", "CT", , , COL_TXT%, COL_BG%
  END IF

  ' ---- Send frame ----
  FASTGFX SWAP
  FASTGFX SYNC

  ' Save positions for next frame
  prevBallX% = INT(bx!) : prevBallY% = INT(by!)
  prevPadX% = INT(px!)
  prevBallLaunched% = ballLaunched%
LOOP

FASTGFX CLOSE
CLS COL_BG%
PRINT "Thanks for playing!  Final Score: "; score%
END
