' ==========================================
' PicoCalc Blocks Game (FASTGFX version)
' - Double-buffered with automatic scanline diffing
' - DMA transfers to SPI display on core1
' ==========================================
Option EXPLICIT
' ---- Tunables (calibrated for 50 FPS) ----
Const PAD_ACCEL = 0.8
Const PAD_DECAY = 0.95
Const PAD_MAX   = 10
Const BRADIUS   = 6
Const BALL_SPEED_INIT = 1.2
Const BALL_SPEED_ACCEL_PER_SEC = 0.012
Const PADDLE_KICK = 0.2
' ---- Screen & colors ----
Const W% = MM.HRES
Const H% = MM.VRES
Const HUDH% = 18
Const COL_BG%     = RGB(BLACK)
Const COL_TXT%    = RGB(WHITE)
Const COL_BORDER% = RGB(MYRTLE)
Const COL_PAD%    = RGB(GREEN)
Const COL_BALL%   = RGB(RED)
' ---- Block layout ----
Const LEVELS% = 10
Const BLOCK_ROWS% = 5
Const BLOCK_COLS% = 8
Const BLOCK_W% = 35
Const BLOCK_H% = 12
Const BLOCK_GAP% = 4
Const BLOCK_TOP% = 40
' ---- Block types ----
Const BLOCK_RED% = 30
Const BLOCK_ORANGE% = 20
Const BLOCK_YELLOW_FULL% = 12
Const BLOCK_YELLOW_DMG% = 11
Const BLOCK_BLUE% = 99
' ---- Level DATA ----
' Level 1 - Easy warmup
Data "0","0","0","0","0","0","0","0"
Data "0","0","0","0","0","0","0","0"
Data "0","0","R","R","R","R","0","0"
Data "0","R","R","Y","Y","R","R","0"
Data "R","R","R","R","R","R","R","R"
' Level 2
Data "0","0","R","R","R","R","0","0"
Data "0","O","O","O","O","O","O","0"
Data "Y","Y","Y","Y","Y","Y","Y","Y"
Data "Y","Y","Y","Y","Y","Y","Y","Y"
Data "Y","Y","Y","Y","Y","Y","Y","Y"
' Level 3
Data "R","R","O","O","O","O","R","R"
Data "R","O","Y","Y","Y","Y","O","R"
Data "O","Y","Y","Y","Y","Y","Y","O"
Data "O","Y","Y","Y","Y","Y","Y","O"
Data "Y","Y","Y","Y","Y","Y","Y","Y"
' Level 4
Data "0","0","0","R","R","0","0","0"
Data "0","0","O","O","O","O","0","0"
Data "0","Y","Y","Y","Y","Y","Y","0"
Data "Y","Y","Y","Y","Y","Y","Y","Y"
Data "0","0","0","B","B","0","0","0"
' Level 5
Data "R","R","R","R","R","R","R","R"
Data "R","0","0","0","0","0","0","R"
Data "R","0","B","B","B","B","0","R"
Data "R","0","0","0","0","0","0","R"
Data "R","R","R","R","R","R","R","R"
' Level 6
Data "R","R","0","0","0","0","O","O"
Data "R","Y","0","Y","Y","0","Y","O"
Data "0","0","B","Y","Y","B","0","0"
Data "Y","Y","0","B","B","0","Y","Y"
Data "O","O","0","0","0","0","R","R"
' Level 7
Data "R","0","0","Y","Y","0","0","R"
Data "O","O","B","Y","Y","B","O","O"
Data "Y","Y","Y","B","B","Y","Y","Y"
Data "Y","Y","B","O","O","B","Y","Y"
Data "0","0","0","R","R","0","0","0"
' Level 8
Data "R","O","R","O","R","O","R","O"
Data "Y","B","Y","B","Y","B","Y","B"
Data "O","Y","O","Y","O","Y","O","Y"
Data "B","Y","B","Y","B","Y","B","Y"
Data "R","O","R","O","R","O","R","O"
' Level 9
Data "0","0","R","R","R","R","0","0"
Data "0","0","O","B","B","O","0","0"
Data "Y","Y","Y","B","B","Y","Y","Y"
Data "Y","Y","Y","B","B","Y","Y","Y"
Data "0","0","O","O","O","O","0","0"
' Level 10
Data "R","B","O","B","O","B","O","R"
Data "B","Y","Y","0","0","Y","Y","B"
Data "O","Y","B","Y","Y","B","Y","O"
Data "B","Y","Y","0","0","Y","Y","B"
Data "R","B","O","B","O","B","O","R"
' ---- State ----
Dim INTEGER currentLevel%=1
Dim FLOAT bx!, by!
Dim INTEGER br%
Dim FLOAT vx!, vy!
Dim INTEGER lastAccelTime%
Dim px!, py!, pw%, ph%, pvx!
Dim INTEGER score%, lives%
Dim INTEGER ballLaunched%
Dim INTEGER frames%, t0%
Dim fps$
Dim k$
Dim INTEGER blocks%(BLOCK_ROWS%-1, BLOCK_COLS%-1)
Dim INTEGER totalBlocks%, blocksLeft%
Dim FLOAT hitPos!, angle!
Dim INTEGER explosionActive%, explosionX%, explosionY%, explosionFrame%, explosionColor%
Dim INTEGER oldScore%, oldLives%
Dim INTEGER lastHitRow%, lastHitCol%, hitTimeout%
' ---- Previous position tracking (for erase) ----
Dim INTEGER prevBallX%, prevBallY%
Dim INTEGER prevPadX%
Dim INTEGER prevBallLaunched%
Dim INTEGER prevExpX%, prevExpY%, prevExpSize%, expCleanup%
' ---- Beeps ----
Sub BeepServe(): Play TONE 700,700 : Pause 40 : Play STOP : End Sub
Sub BeepPaddle(): Play TONE 800,800 : Pause 20 : Play STOP : End Sub
Sub BeepWall(): Play TONE 600,600 : Pause 20 : Play STOP : End Sub
Sub BeepBlock(): Play TONE 1200,1200 : Pause 25 : Play STOP : End Sub
Sub BeepMiss(): Play TONE 200,200 : Pause 80 : Play STOP : End Sub
Sub ResetBall()
ballLaunched% = 0
bx! = px! + pw%/2 - br%
by! = py! - 2*br% - 2
If Rnd > 0.5 Then vx! = BALL_SPEED_INIT Else vx! = -BALL_SPEED_INIT
vy! = -BALL_SPEED_INIT
lastAccelTime% = Timer
hitTimeout% = 0
End Sub
Sub Draw3DHighlight(x%, y%, w%, h%)
Line x%, y%, x%+w%-1, y%, , RGB(WHITE)
Line x%, y%, x%, y%+h%-1, , RGB(WHITE)
End Sub
Sub DrawHUD()
Local s$
Box 0, 0, W%, HUDH%, 0, , COL_BG%
s$ = "L" + Str$(currentLevel%) + " Score " + Str$(score%) + " Lives " + Str$(lives%)
Text 6, 3, s$, "LT", , , COL_TXT%, COL_BG%
If fps$ <> "" Then
Text W%-4, 3, fps$, "RT", , , COL_TXT%, COL_BG%
End If
End Sub
Sub DrawPaddleAt(x%, y%)
Box x%, y%, pw%, ph%, 0, , COL_PAD%
Draw3DHighlight x%, y%, pw%, ph%
End Sub
Sub DrawBallAt(x%, y%)
Local cx%, cy%
cx% = x% + br%
cy% = y% + br%
Circle cx%, cy%, br%, 0, 1.0, , COL_BALL%
Circle cx%-2, cy%-2, 1, 0, 1.0, , RGB(WHITE)
End Sub
Sub DrawSingleBlock(r%, c%)
Local bx%, by%, blockType%
blockType% = blocks%(r%, c%)
If blockType% > 0 Then
bx% = GetBlockX(c%)
by% = GetBlockY(r%)
Box bx%, by%, BLOCK_W%, BLOCK_H%, 0, , GetBlockColor(blockType%)
Box bx%, by%, BLOCK_W%, BLOCK_H%, 1, COL_BORDER%
Draw3DHighlight bx%+1, by%+1, BLOCK_W%-2, BLOCK_H%-1
End If
End Sub
Sub RedrawBlocksInRegion(rx%, ry%, rw%, rh%)
Local r%, c%, r1%, r2%, c1%, c2%, by%
Local bStep%
bStep% = BLOCK_H% + BLOCK_GAP% + 3
r1% = (ry% - BLOCK_TOP%) \ bStep%
If r1% < 0 Then r1% = 0
r2% = (ry% + rh% - BLOCK_TOP%) \ bStep%
If r2% >= BLOCK_ROWS% Then r2% = BLOCK_ROWS% - 1
c1% = (rx% - BLOCK_GAP%) \ (BLOCK_W% + BLOCK_GAP%)
If c1% < 0 Then c1% = 0
c2% = (rx% + rw%) \ (BLOCK_W% + BLOCK_GAP%)
If c2% >= BLOCK_COLS% Then c2% = BLOCK_COLS% - 1
For r% = r1% To r2%
For c% = c1% To c2%
If blocks%(r%, c%) > 0 Then DrawSingleBlock r%, c%
Next
Next
End Sub
Sub TriggerExplosion(x%, y%, w%, h%, blockColor%)
explosionActive% = 1
explosionX% = x% + w%/2
explosionY% = y% + h%/2
explosionFrame% = 0
explosionColor% = blockColor%
prevExpSize% = 0
End Sub
Sub DrawExplosion()
Local size%
If explosionActive% = 0 Then Exit Sub
size% = (explosionFrame% + 1) * 4
Circle explosionX%, explosionY%, size%, 0, 1.0, , explosionColor%
Line explosionX%-size%, explosionY%, explosionX%+size%, explosionY%, , explosionColor%
Line explosionX%, explosionY%-size%, explosionX%, explosionY%+size%, , explosionColor%
If explosionFrame% > 0 Then
Local offset%
offset% = size% * 0.7
Line explosionX%-offset%, explosionY%-offset%, explosionX%+offset%, explosionY%+offset%, , explosionColor%
Line explosionX%-offset%, explosionY%+offset%, explosionX%+offset%, explosionY%-offset%, , explosionColor%
End If
If size% > prevExpSize% Then prevExpSize% = size%
explosionFrame% = explosionFrame% + 1
If explosionFrame% > 2 Then
explosionActive% = 0
expCleanup% = 2
End If
End Sub
Sub DestroyBlock(r%, c%, points%)
Local bx%, by%
bx% = GetBlockX(c%)
by% = GetBlockY(r%)
TriggerExplosion bx%, by%, BLOCK_W%, BLOCK_H%, GetBlockColor(blocks%(r%, c%))
blocks%(r%, c%) = 0
blocksLeft% = blocksLeft% - 1
score% = score% + points%
Box bx%, by%, BLOCK_W%, BLOCK_H%, 0, , COL_BG%
End Sub
Sub DrawBlocks()
Local r%, c%
For r% = 0 To BLOCK_ROWS%-1
For c% = 0 To BLOCK_COLS%-1
If blocks%(r%, c%) > 0 Then DrawSingleBlock r%, c%
Next
Next
End Sub
Sub InitBlocks()
Local r%, c%, blockChar$, skipRows%, i%
totalBlocks% = 0
Restore
skipRows% = (currentLevel% - 1) * BLOCK_ROWS% * BLOCK_COLS%
For i% = 1 To skipRows%
Read blockChar$
Next
For r% = 0 To BLOCK_ROWS%-1
For c% = 0 To BLOCK_COLS%-1
Read blockChar$
Select Case blockChar$
Case "R"
blocks%(r%, c%) = BLOCK_RED%
totalBlocks% = totalBlocks% + 1
Case "O"
blocks%(r%, c%) = BLOCK_ORANGE%
totalBlocks% = totalBlocks% + 1
Case "Y"
blocks%(r%, c%) = BLOCK_YELLOW_FULL%
totalBlocks% = totalBlocks% + 1
Case "B"
blocks%(r%, c%) = BLOCK_BLUE%
Case Else
blocks%(r%, c%) = 0
End Select
Next
Next
blocksLeft% = totalBlocks%
End Sub
Function GetBlockX(c%) As INTEGER
GetBlockX = c% * (BLOCK_W% + BLOCK_GAP%) + BLOCK_GAP%
End Function
Function GetBlockY(r%) As INTEGER
GetBlockY = BLOCK_TOP% + r% * (BLOCK_H% + BLOCK_GAP% + 3)
End Function
Function GetBlockColor(blockType%) As INTEGER
Select Case blockType%
Case BLOCK_RED%
GetBlockColor = RGB(RED)
Case BLOCK_ORANGE%
GetBlockColor = RGB(RUST)
Case BLOCK_YELLOW_FULL%
GetBlockColor = RGB(YELLOW)
Case BLOCK_YELLOW_DMG%
GetBlockColor = RGB(BROWN)
Case BLOCK_BLUE%
GetBlockColor = RGB(BLUE)
Case Else
GetBlockColor = RGB(CYAN)
End Select
End Function
Function CheckBlockCollision(ballX%, ballY%, ballR%) As INTEGER
Local r%, c%, bx%, by%, bx2%, by2%, blockType%
Local ballLeft%, ballRight%, ballTop%, ballBot%
Local r1%, r2%, c1%, c2%, bStep%
ballLeft% = ballX%
ballRight% = ballX% + 2*ballR%
ballTop% = ballY%
ballBot% = ballY% + 2*ballR%
If hitTimeout% > 0 Then hitTimeout% = hitTimeout% - 1
bStep% = BLOCK_H% + BLOCK_GAP% + 3
r1% = (ballTop% - BLOCK_TOP% - BLOCK_H%) \ bStep%
If r1% < 0 Then r1% = 0
r2% = (ballBot% - BLOCK_TOP%) \ bStep%
If r2% >= BLOCK_ROWS% Then r2% = BLOCK_ROWS% - 1
If r1% > r2% Then CheckBlockCollision = 0 : Exit Function
c1% = (ballLeft% - BLOCK_GAP% - BLOCK_W%) \ (BLOCK_W% + BLOCK_GAP%)
If c1% < 0 Then c1% = 0
c2% = (ballRight% - BLOCK_GAP%) \ (BLOCK_W% + BLOCK_GAP%)
If c2% >= BLOCK_COLS% Then c2% = BLOCK_COLS% - 1
For r% = r1% To r2%
For c% = c1% To c2%
blockType% = blocks%(r%, c%)
If blockType% > 0 Then
bx% = c% * (BLOCK_W% + BLOCK_GAP%) + BLOCK_GAP%
by% = BLOCK_TOP% + r% * bStep%
bx2% = bx% + BLOCK_W%
by2% = by% + BLOCK_H%
If ballRight% >= bx% And ballLeft% <= bx2% And ballBot% >= by% And ballTop% <= by2% Then
If hitTimeout% > 0 And r% = lastHitRow% And c% = lastHitCol% Then
' Still in timeout for this block
Else
lastHitRow% = r%
lastHitCol% = c%
hitTimeout% = 15
If blockType% = BLOCK_BLUE% Then
CheckBlockCollision = 1
Exit Function
Else If blockType% = BLOCK_YELLOW_FULL% Then
blocks%(r%, c%) = BLOCK_YELLOW_DMG%
DrawSingleBlock r%, c%
Else If blockType% = BLOCK_YELLOW_DMG% Then
DestroyBlock r%, c%, 10
Else If blockType% = BLOCK_ORANGE% Then
DestroyBlock r%, c%, 20
Else If blockType% = BLOCK_RED% Then
DestroyBlock r%, c%, 30
End If
CheckBlockCollision = 1
Exit Function
End If
End If
End If
Next
Next
CheckBlockCollision = 0
End Function
' ---- Init ----
pw% = W% \ 6 : If pw% < 30 Then pw% = 30
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
frames% = 0 : t0% = Timer
fps$ = ""
lastAccelTime% = Timer
prevExpSize% = 0 : expCleanup% = 0
InitBlocks
' ---- Setup FASTGFX ----
FASTGFX CREATE
FASTGFX FPS 60
CLS COL_BG%
DrawBlocks
DrawHUD
DrawPaddleAt Int(px!), Int(py!)
DrawBallAt Int(bx!), Int(by!)
Text W%\2, H%\2, "Hit SPACE to start", "CT", , , COL_TXT%, COL_BG%
FASTGFX SWAP
FASTGFX SYNC
prevBallX% = Int(bx!) : prevBallY% = Int(by!)
prevPadX% = Int(px!)
prevBallLaunched% = ballLaunched%
prevExpX% = 0 : prevExpY% = 0
BeepServe
' ---- Main loop ----
Do
k$ = Inkey$
If k$ <> "" Then
Select Case Asc(k$)
Case 130: pvx! = pvx! - PAD_ACCEL
Case 131: pvx! = pvx! + PAD_ACCEL
Case  32: If ballLaunched% = 0 Then ballLaunched% = 1 : BeepServe
Case  27: Exit Do
Case  49 To 56
Play TONE 220 * (2 ^ ((Asc(k$)-48)/12.0)), 0 : Pause 60 : Play STOP
End Select
EndIf
' ---- Erase old positions ----
Box prevBallX%-1, prevBallY%-1, br%*2+4, br%*2+4, 0, , COL_BG%
If prevBallY% < BLOCK_TOP% + BLOCK_ROWS% * (BLOCK_H% + BLOCK_GAP% + 3) Then
RedrawBlocksInRegion prevBallX%-1, prevBallY%-1, br%*2+4, br%*2+4
End If
Box prevPadX%-1, Int(py!)-1, pw%+2, ph%+2, 0, , COL_BG%
If explosionActive% Or expCleanup% > 0 Then
If prevExpSize% > 0 Then
Box prevExpX%-prevExpSize%-2, prevExpY%-prevExpSize%-2, prevExpSize%*2+4, prevExpSize%*2+4, 0, , COL_BG%
RedrawBlocksInRegion prevExpX%-prevExpSize%-2, prevExpY%-prevExpSize%-2, prevExpSize%*2+4, prevExpSize%*2+4
End If
If explosionActive% = 0 Then
expCleanup% = expCleanup% - 1
If expCleanup% <= 0 Then prevExpSize% = 0
End If
End If
If prevBallLaunched% = 0 Then
Box 0, H%\2 - 10, W%, 20, 0, , COL_BG%
RedrawBlocksInRegion 0, H%\2 - 10, W%, 20
End If
' ---- Paddle physics ----
pvx! = pvx! * PAD_DECAY
If pvx! >  PAD_MAX Then pvx! =  PAD_MAX
If pvx! < -PAD_MAX Then pvx! = -PAD_MAX
px! = px! + pvx!
If px! < 0 Then px! = 0 : If pvx! < 0 Then pvx! = 0
If px! > (W% - pw%) Then px! = W% - pw% : If pvx! > 0 Then pvx! = 0
' ---- Ball physics ----
If ballLaunched% = 0 Then
bx! = px! + pw%/2 - br%
by! = py! - 2*br% - 2
Else
If Timer - lastAccelTime% >= 1000 Then
If vy! > 0 Then
vy! = vy! + BALL_SPEED_ACCEL_PER_SEC
Else
vy! = vy! - BALL_SPEED_ACCEL_PER_SEC
End If
lastAccelTime% = lastAccelTime% + 1000
End If
bx! = bx! + vx!
by! = by! + vy!
End If
' Wall bounces
If Int(bx!) < 0 Then
bx! = 0 : vx! = -vx! : hitTimeout% = 0 : BeepWall
End If
If Int(bx!) > W% - 2*br% Then
bx! = W% - 2*br% : vx! = -vx! : hitTimeout% = 0 : BeepWall
End If
If Int(by!) < HUDH% + 1 Then
by! = HUDH% + 1 : vy! = -vy! : hitTimeout% = 0 : BeepWall
End If
' Block collision
If ballLaunched% = 1 And CheckBlockCollision(Int(bx!), Int(by!), br%) = 1 Then
vy! = -vy! : BeepBlock
End If
' Paddle collision
If ballLaunched% = 1 And Int(by!)+2*br% >= Int(py!) And Int(by!) <= Int(py!)+ph% And Int(bx!)+2*br% >= Int(px!) And Int(bx!) <= Int(px!)+pw% Then
by! = Int(py!) - 2*br%
vy! = -vy!
hitTimeout% = 0
hitPos! = (bx! + br% - px!) / pw%
If hitPos! < 0 Then hitPos! = 0
If hitPos! > 1 Then hitPos! = 1
angle! = (hitPos! - 0.5) * 2.0
vx! = vx! + angle! + pvx! * PADDLE_KICK
score% = score% + 1
BeepPaddle
EndIf
' Miss
If ballLaunched% = 1 And Int(by!) > H% - 2*br% Then
lives% = lives% - 1
BeepMiss
If lives% > 0 Then
ResetBall
BeepServe
Else
FASTGFX CLOSE
CLS COL_BG%
Print "GAME OVER!  Score="; score%
End
End If
EndIf
' Level complete
If blocksLeft% = 0 Then
If currentLevel% < LEVELS% Then
currentLevel% = currentLevel% + 1
ResetBall
CLS COL_BG%
InitBlocks
DrawBlocks
DrawHUD
oldScore% = score%
oldLives% = lives%
BeepServe
Else
FASTGFX CLOSE
CLS COL_BG%
Print "YOU WIN ALL LEVELS!  Score="; score%; "  Lives: "; lives%
End
End If
End If
' HUD update on change
If score% <> oldScore% Or lives% <> oldLives% Then
oldScore% = score%
oldLives% = lives%
DrawHUD
End If
' FPS counter
frames% = frames% + 1
If Timer - t0% >= 1000 Then
fps$ = Str$(frames%) + " FPS"
frames% = 0 : t0% = Timer
DrawHUD
End If
' ---- Draw new positions ----
DrawPaddleAt Int(px!), Int(py!)
DrawBallAt Int(bx!), Int(by!)
If explosionActive% Then
prevExpX% = explosionX% : prevExpY% = explosionY%
expCleanup% = 2
End If
DrawExplosion
If ballLaunched% = 0 Then
Text W%\2, H%\2, "Hit SPACE to start", "CT", , , COL_TXT%, COL_BG%
End If
' ---- Send frame ----
FASTGFX SWAP
FASTGFX SYNC
' Save positions for next frame
prevBallX% = Int(bx!) : prevBallY% = Int(by!)
prevPadX% = Int(px!)
prevBallLaunched% = ballLaunched%
Loop
FASTGFX CLOSE
CLS COL_BG%
Print "Thanks for playing!  Final Score: "; score%
End
