OPTION EXPLICIT
CONST PAD_ACCEL = 0.8
CONST PAD_DECAY = 0.95
CONST PAD_MAX   = 10
CONST BRADIUS   = 6
CONST BALL_SPEED_INIT = 1.2
CONST BALL_SPEED_ACCEL_PER_SEC = 0.012
CONST PADDLE_KICK = 0.2
CONST W% = MM.HRES
CONST H% = MM.VRES
CONST HUDH% = 18
CONST COL_BG%     = RGB(BLACK)
CONST COL_TXT%    = RGB(WHITE)
CONST COL_BORDER% = RGB(MYRTLE)
CONST COL_PAD%    = RGB(GREEN)
CONST COL_BALL%   = RGB(RED)
CONST LEVELS% = 10
CONST BLOCK_ROWS% = 5
CONST BLOCK_COLS% = 8
CONST BLOCK_W% = 35
CONST BLOCK_H% = 12
CONST BLOCK_GAP% = 4
CONST BLOCK_TOP% = 40
CONST BLOCK_RED% = 30
CONST BLOCK_ORANGE% = 20
CONST BLOCK_YELLOW_FULL% = 12
CONST BLOCK_YELLOW_DMG% = 11
CONST BLOCK_BLUE% = 99
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
DIM INTEGER explosionActive%
DIM INTEGER oldScore%, oldLives%
DIM INTEGER lastHitRow%, lastHitCol%, hitTimeout%
DIM INTEGER prevBallX%, prevBallY%
DIM INTEGER prevPadX%
DIM INTEGER prevBallLaunched%
DIM INTEGER prevExpSize%, expCleanup%

SUB BeepServe(): PLAY TONE 700,700 : PAUSE 40 : PLAY STOP : END SUB
SUB BeepPaddle(): PLAY TONE 800,800 : PAUSE 20 : PLAY STOP : END SUB

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
PRINT "Init done"
PRINT "pw="; pw%; " ph="; ph%
PRINT "score="; score%; " lives="; lives%
BeepServe
PRINT "Done"
END
