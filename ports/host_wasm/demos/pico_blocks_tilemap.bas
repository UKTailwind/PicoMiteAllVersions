' ==========================================
' PicoCalc Blocks Game (FASTGFX + TILEMAP SPRITE version)
' - Double-buffered with automatic scanline diffing
' - DMA transfers to SPI display on core1
' - Ball, explosion, and paddle rendered as TILEMAP SPRITEs
'   from two baked atlases:
'     atlas.bmp   -> 64x16 (4 tiles), flash slot 1, tilemap 1
'                    tile 1 = ball, 2/3/4 = explosion frames
'     paddle.bmp  -> pw% x ph% (1 tile), flash slot 2, tilemap 2
'   Both atlases authored at boot from BASIC; one TILEMAP SPRITE
'   DRAW per frame paints all three sprites (ball, paddle, explosion).
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

' ---- TILEMAP SPRITE atlases ----
' Atlas A (atlas.bmp -> flash slot 1, tilemap 1):
'   112x16 BMP, seven 16x16 tiles laid out left-to-right.
'     tile 1 = ball (red disk + white highlight)
'     tiles 2..7 = explosion frames 0..5 — white core + yellow flash
'                  -> orange burst -> red fade -> sparse red sparks.
'                  Each frame uses a per-pixel hash wobble for ragged,
'                  irregular borders rather than a clean disk.
' Atlas B (paddle.bmp -> flash slot 2, tilemap 2):
'   pw% x ph% BMP, one tile = solid green paddle with white
'   top + left highlight stroke baked in (matches the original
'   Draw3DHighlight look).
'
' Sprites:
'   1 = ball      (tilemap 1, tile 1)
'   2 = paddle    (tilemap 2, tile 1)
'   3 = explosion (tilemap 1, tile 2..7 cycled per frame)
CONST TM_BALL_TILE% = 1
CONST TM_EXP_FIRST_TILE% = 2
CONST TM_EXP_FRAMES% = 6
CONST TM_BALL_SPRITE% = 1
CONST TM_PAD_SPRITE% = 2
CONST TM_EXP_SPRITE% = 3

SUB CreateAtlas()
  ' 112x16 atlas: tile 1 = ball, tiles 2..7 = 6 explosion frames.
  ' BMP rows are bottom-up; BMP pixels are BGR. Each per-pixel cell
  ' picks a colour from {transparent / red / orange / yellow / white}
  ' based on a frame-specific radius band, perturbed by a cheap
  ' integer hash wobble that makes the border ragged instead of a
  ' clean disk. Last two frames switch to sparse spark patterns so
  ' the explosion fades out instead of fully overlapping the next
  ' game frame.
  LOCAL INTEGER fnbr = 1
  LOCAL INTEGER atlas_w = 112
  LOCAL INTEGER atlas_h = 16
  LOCAL INTEGER fy, x, tile, tx, dx, dy, d2
  LOCAL INTEGER bb, gg, rr, frame, wobble, n, r2
  OPEN "atlas.bmp" FOR OUTPUT AS #fnbr
  PRINT #fnbr, "BM";
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(54);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(40);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(atlas_w);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(atlas_h);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(1);CHR$(0);
  PRINT #fnbr, CHR$(24);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  FOR fy = atlas_h - 1 TO 0 STEP -1
    FOR x = 0 TO atlas_w - 1
      tile = x \ 16        ' 0=ball, 1..6 = explosion frames
      tx = x AND 15
      dx = tx - 8
      dy = fy - 8
      d2 = dx*dx + dy*dy
      bb = 0 : gg = 0 : rr = 0
      IF tile = 0 THEN
        ' Ball: red disk r=7 + 1px white highlight at (6,6)
        IF d2 <= 49 THEN
          IF dx = -2 AND dy = -2 THEN
            bb = &HFF : gg = &HFF : rr = &HFF
          ELSE
            bb = 0    : gg = 0    : rr = &HFF
          ENDIF
        ENDIF
      ELSE
        ' Frames cycle yellow -> orange -> red with bold colour bands
        ' that fill most of the tile. Tiny white hotspot only on frame 0
        ' — anything more and the white dominates perception at 50 FPS.
        ' wobble adds deterministic per-pixel noise so band boundaries
        ' are ragged instead of clean disks.
        frame = tile - 1
        wobble = ((dx*7 + dy*11 + frame*5) AND 7) - 3   ' -3..+4
        r2 = d2 + wobble
        SELECT CASE frame
          CASE 0
            ' Ignition: white hotspot core, yellow body.
            IF d2 <= 1 THEN
              bb = &HFF : gg = &HFF : rr = &HFF
            ELSE IF r2 <= 25 THEN
              bb = 0 : gg = &HFF : rr = &HFF
            ELSE IF r2 <= 36 THEN
              bb = 0 : gg = &H40 : rr = &HFF
            ENDIF
          CASE 1
            ' Growing: yellow body, orange edge.
            IF r2 <= 36 THEN
              bb = 0 : gg = &HFF : rr = &HFF
            ELSE IF r2 <= 64 THEN
              bb = 0 : gg = &H40 : rr = &HFF
            ENDIF
          CASE 2
            ' Peak: yellow center, big orange body, red shell.
            IF r2 <= 16 THEN
              bb = 0 : gg = &HFF : rr = &HFF
            ELSE IF r2 <= 49 THEN
              bb = 0 : gg = &H40 : rr = &HFF
            ELSE IF r2 <= 81 THEN
              bb = 0 : gg = 0 : rr = &HFF
            ENDIF
          CASE 3
            ' Cooling: orange center, red body.
            IF r2 <= 25 THEN
              bb = 0 : gg = &H40 : rr = &HFF
            ELSE IF r2 <= 81 THEN
              bb = 0 : gg = 0 : rr = &HFF
            ENDIF
          CASE 4
            ' Fading: red ring, hollow centre.
            IF r2 > 9 AND r2 <= 64 THEN
              bb = 0 : gg = 0 : rr = &HFF
            ENDIF
          CASE 5
            ' Sparks: scattered red pixels, no body.
            n = (dx*13 + dy*23) AND 31
            IF n < 4 AND d2 <= 64 THEN
              bb = 0 : gg = 0 : rr = &HFF
            ENDIF
        END SELECT
      ENDIF
      PRINT #fnbr, CHR$(bb);CHR$(gg);CHR$(rr);
    NEXT
  NEXT
  CLOSE #fnbr
END SUB

SUB CreatePaddleAtlas()
  ' Single tile of pw% x ph%. Solid green with a one-pixel white
  ' highlight on the top edge and left edge — same look as the old
  ' Draw3DHighlight call. Outer rim is opaque (no transparency
  ' boundary), so the demo doesn't need transparent=0 on the paddle
  ' draw (a 0 nibble would otherwise punch holes in the green).
  LOCAL INTEGER fnbr = 1
  LOCAL INTEGER fy, x
  LOCAL INTEGER row_pad
  LOCAL INTEGER bb, gg, rr
  OPEN "paddle.bmp" FOR OUTPUT AS #fnbr
  PRINT #fnbr, "BM";
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(54);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(40);CHR$(0);CHR$(0);CHR$(0);
  ' pw% might exceed 255 in theory; on this demo it's ~53. Keep the
  ' simple single-byte width path and assert.
  IF pw% > 255 OR ph% > 255 THEN ERROR "Paddle too wide for single-byte BMP author"
  PRINT #fnbr, CHR$(pw%);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(ph%);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(1);CHR$(0);
  PRINT #fnbr, CHR$(24);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
  ' BMP rows: bottom-up. Image y=0 is the visual top.
  ' Highlight: top row + left col = white.
  ' Padding bytes per row to align width*3 to 4 bytes.
  row_pad = (4 - ((pw% * 3) AND 3)) AND 3
  FOR fy = ph% - 1 TO 0 STEP -1
    FOR x = 0 TO pw% - 1
      IF fy = 0 OR x = 0 THEN
        bb = &HFF : gg = &HFF : rr = &HFF      ' White highlight
      ELSE
        bb = 0 : gg = &HFF : rr = 0            ' Solid green
      ENDIF
      PRINT #fnbr, CHR$(bb);CHR$(gg);CHR$(rr);
    NEXT
    FOR x = 1 TO row_pad
      PRINT #fnbr, CHR$(0);
    NEXT
  NEXT
  CLOSE #fnbr
END SUB

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
  ' Paddle sprite tile is exactly pw% x ph%, so (x,y) IS the upper-
  ' left corner — no centring offset. SPRITE DRAW happens once at the
  ' end of the frame after every Move call.
  TILEMAP SPRITE MOVE TM_PAD_SPRITE%, x%, y%
END SUB

SUB DrawBallAt(x%, y%)
  ' Tile sprite is 16x16 with the disk centred at (8,8). The sprite's
  ' own (x,y) is the upper-left corner, so subtract the centring
  ' offset to keep the visual centre identical to the old CIRCLE call.
  TILEMAP SPRITE MOVE TM_BALL_SPRITE%, x% + br% - 8, y% + br% - 8
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
  ' Spawn a fresh explosion sprite centred on the destroyed block.
  ' explosionColor% is no longer used (the atlas tile bakes the colour
  ' in) but kept for source compatibility with the original demo.
  explosionActive% = 1
  explosionX% = x% + w%/2
  explosionY% = y% + h%/2
  explosionFrame% = 0
  explosionColor% = blockColor%
  TILEMAP SPRITE CREATE TM_EXP_SPRITE%, 1, TM_EXP_FIRST_TILE%, explosionX% - 8, explosionY% - 8
END SUB

SUB DrawExplosion()
  ' Cycle the sprite's tile through TM_EXP_FRAMES% explosion frames,
  ' then destroy the sprite. The atlas's last frame is sparse-spark
  ' only so the trailing frame fades out instead of cutting hard.
  IF explosionActive% = 0 THEN EXIT SUB
  TILEMAP SPRITE SET TM_EXP_SPRITE%, TM_EXP_FIRST_TILE% + explosionFrame%
  explosionFrame% = explosionFrame% + 1
  IF explosionFrame% >= TM_EXP_FRAMES% THEN
    TILEMAP SPRITE DESTROY TM_EXP_SPRITE%
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

' ---- Setup TILEMAP SPRITE atlases ----
' Two flash slots, two tilemaps (mixed tile sizes can't share one):
'   slot 1 / tilemap 1 = 64x16 atlas of 16x16 tiles (ball + 3 explosion frames)
'   slot 2 / tilemap 2 = pw% x ph% paddle.
' Map data is a single dummy tile per tilemap; we never call TILEMAP DRAW
' (background tiles), only SPRITE DRAW. The map cell is required by
' TILEMAP CREATE for bookkeeping but doesn't get rendered here.
CreateAtlas
CreatePaddleAtlas
FLASH LOAD IMAGE 1, "atlas.bmp", O
FLASH LOAD IMAGE 2, "paddle.bmp", O
TILEMAP CREATE tm_dummymap, 1, 1, 16, 16, 7, 1, 1
TILEMAP CREATE tm_dummymap, 2, 2, pw%, ph%, 1, 1, 1
TILEMAP SPRITE CREATE TM_BALL_SPRITE%, 1, TM_BALL_TILE%, 0, 0
TILEMAP SPRITE CREATE TM_PAD_SPRITE%, 2, 1, INT(px!), INT(py!)
GOTO tm_skip_data
tm_dummymap:
DATA 1
tm_skip_data:

' ---- Setup FASTGFX ----
FASTGFX CREATE
FASTGFX FPS 50
CLS COL_BG%
DrawBlocks
DrawHUD
DrawPaddleAt INT(px!), INT(py!)
DrawBallAt INT(bx!), INT(by!)
TILEMAP SPRITE DRAW F, 0
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
  ' Sprite explosion is always 16x16 centred on (prevExpX, prevExpY).
  ' Erase that exact tile-sized region; expCleanup% gates the trailing
  ' frame after the sprite is destroyed so the last paint doesn't leave
  ' pixels behind.
  IF explosionActive% OR expCleanup% > 0 THEN
    BOX prevExpX% - 8, prevExpY% - 8, 16, 16, 0, , COL_BG%
    RedrawBlocksInRegion prevExpX% - 8, prevExpY% - 8, 16, 16
    IF explosionActive% = 0 THEN expCleanup% = expCleanup% - 1
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
  ' Each DrawXxxAt sets sprite position only; one TILEMAP SPRITE DRAW
  ' below paints all active sprites in one pass.
  DrawPaddleAt INT(px!), INT(py!)
  DrawBallAt INT(bx!), INT(by!)
  IF explosionActive% THEN
    prevExpX% = explosionX% : prevExpY% = explosionY%
    expCleanup% = 2
  END IF
  DrawExplosion
  TILEMAP SPRITE DRAW F, 0
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
