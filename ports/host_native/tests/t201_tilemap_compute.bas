' RUN_ARGS: --interp
' TILEMAP compute-only test: builds a tiny tile atlas, creates a 4x3 map
' from DATA, then exercises the non-rendering query surface — TILE,
' COLLISION (with and without ATTR mask), VIEWX/VIEWY, COLS/ROWS, and
' SPRITE X/Y/TILE/HIT/W/H.
'
' Interp-only — TILEMAP is bridged, no VM opcode. Render path covered
' by t202_tilemap_render.bas.

OPTION EXPLICIT

' --- Author a 16x8 24bpp BMP into B:/atlas.bmp ---
DIM INTEGER fnbr = 1
OPEN "B:/atlas.bmp" FOR OUTPUT AS #fnbr
' Helper: bfSize and biSizeImage are informational; FLASH LOAD IMAGE
' parses width/height/bpp directly from the DIB header. Zeros are fine.
PRINT #fnbr, "BM";
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);         ' bfSize (ignored)
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);         ' bfReserved
PRINT #fnbr, CHR$(54);CHR$(0);CHR$(0);CHR$(0);        ' bfOffBits = 54
PRINT #fnbr, CHR$(40);CHR$(0);CHR$(0);CHR$(0);        ' biSize
PRINT #fnbr, CHR$(16);CHR$(0);CHR$(0);CHR$(0);        ' width = 16
PRINT #fnbr, CHR$(8);CHR$(0);CHR$(0);CHR$(0);         ' height = 8 (bottom-up)
PRINT #fnbr, CHR$(1);CHR$(0);                         ' planes = 1
PRINT #fnbr, CHR$(24);CHR$(0);                        ' bpp = 24
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);         ' compression = 0
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);         ' biSizeImage (ignored)
PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
DIM INTEGER row, col
FOR row = 0 TO 7
    FOR col = 0 TO 15
        PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);
    NEXT
NEXT
CLOSE #fnbr

FLASH LOAD IMAGE 1, "B:/atlas.bmp", O

' --- TILEMAP CREATE label, id, slot, tw, th, tiles_per_row, cols, rows ---
TILEMAP CREATE map1, 1, 1, 8, 8, 2, 4, 3
GOTO skip_data
map1:
DATA 1, 2, 3, 4
DATA 0, 0, 2, 1
DATA 4, 3, 0, 0
attrs1:
DATA 1, 2, 4, 8
skip_data:

' --- TILEMAP ATTR label, id, num_tiles ---
TILEMAP ATTR attrs1, 1, 4

' --- Cols / Rows ---
IF TILEMAP(COLS 1) <> 4 THEN ERROR "cols"
IF TILEMAP(ROWS 1) <> 3 THEN ERROR "rows"

' --- TILE query ---
' Map[0,0] = 1 (top-left), tile size 8x8 → pixel (0,0) → tile 1
IF TILEMAP(TILE 1, 0, 0) <> 1 THEN ERROR "tile(0,0)"
' pixel (10, 0) → col=1 → map[1,0] = 2
IF TILEMAP(TILE 1, 10, 0) <> 2 THEN ERROR "tile(10,0)"
' pixel (8, 8) → col=1, row=1 → map[1,1] = 0
IF TILEMAP(TILE 1, 8, 8) <> 0 THEN ERROR "tile(8,8) gap"
' pixel (24, 16) → col=3, row=2 → map[3,2] = 0 (last row, col 3)
IF TILEMAP(TILE 1, 24, 16) <> 0 THEN ERROR "tile(24,16)"
' pixel (24, 0) → col=3, row=0 → map[3,0] = 4
IF TILEMAP(TILE 1, 24, 0) <> 4 THEN ERROR "tile(24,0)"
' Out-of-bounds → 0
IF TILEMAP(TILE 1, 100, 100) <> 0 THEN ERROR "tile out-of-bounds"

' --- ATTR query ---
IF TILEMAP(ATTR 1, 1) <> 1 THEN ERROR "attr 1"
IF TILEMAP(ATTR 1, 2) <> 2 THEN ERROR "attr 2"
IF TILEMAP(ATTR 1, 3) <> 4 THEN ERROR "attr 3"
IF TILEMAP(ATTR 1, 4) <> 8 THEN ERROR "attr 4"

' --- COLLISION (no mask = any non-zero tile) ---
' Box (0,0)-(8,8) overlaps map[0,0]=1 only → first hit = 1
IF TILEMAP(COLLISION 1, 0, 0, 1, 1) <> 1 THEN ERROR "collision(0,0,1,1)"
' Box (8,8)-(16,16) spans cols 1-2 + rows 1-2; first non-zero hit is map[2,1]=2
IF TILEMAP(COLLISION 1, 8, 8, 9, 9) <> 2 THEN ERROR "collision(8,8,9,9)"
' Box entirely in zero region: map[0,1]=0, map[1,1]=0 → 0
IF TILEMAP(COLLISION 1, 0, 8, 16, 1) <> 0 THEN ERROR "collision empty"

' --- COLLISION with mask ---
' Mask 1 should match attr[1-1]=1 (tile=1). Box hitting only tile 2 (attr=2) → 0
IF TILEMAP(COLLISION 1, 8, 0, 1, 1, 1) <> 0 THEN ERROR "mask 1 missed"
' Mask 1 box hitting tile 1 → 1
IF TILEMAP(COLLISION 1, 0, 0, 1, 1, 1) <> 1 THEN ERROR "mask 1 hit"
' Mask 4 should match tile 3 (attr=4)
IF TILEMAP(COLLISION 1, 16, 0, 1, 1, 4) <> 3 THEN ERROR "mask 4 hit"

' --- VIEW + VIEWX/VIEWY ---
TILEMAP VIEW 1, 12, 7
IF TILEMAP(VIEWX 1) <> 12 THEN ERROR "viewx"
IF TILEMAP(VIEWY 1) <> 7 THEN ERROR "viewy"
TILEMAP SCROLL 1, -2, 1
IF TILEMAP(VIEWX 1) <> 10 THEN ERROR "scroll viewx"
IF TILEMAP(VIEWY 1) <> 8 THEN ERROR "scroll viewy"

' --- TILEMAP SET → mutate map[2,1] from 2 to 7 ---
TILEMAP SET 1, 2, 1, 7
IF TILEMAP(TILE 1, 16, 8) <> 7 THEN ERROR "set then tile"

' --- SPRITE subsystem ---
TILEMAP SPRITE CREATE 1, 1, 1, 100, 50
IF TILEMAP(SPRITE X 1) <> 100 THEN ERROR "sprite x"
IF TILEMAP(SPRITE Y 1) <> 50 THEN ERROR "sprite y"
IF TILEMAP(SPRITE TILE 1) <> 1 THEN ERROR "sprite tile"
IF TILEMAP(SPRITE W 1) <> 8 THEN ERROR "sprite w"
IF TILEMAP(SPRITE H 1) <> 8 THEN ERROR "sprite h"

TILEMAP SPRITE MOVE 1, 110, 60
IF TILEMAP(SPRITE X 1) <> 110 THEN ERROR "sprite move x"
IF TILEMAP(SPRITE Y 1) <> 60 THEN ERROR "sprite move y"

TILEMAP SPRITE SET 1, 3
IF TILEMAP(SPRITE TILE 1) <> 3 THEN ERROR "sprite set tile"

' --- Sprite HIT collision — sprite 2 overlaps sprite 1 by 1 pixel ---
TILEMAP SPRITE CREATE 2, 1, 2, 117, 67
IF TILEMAP(SPRITE HIT 1, 2) <> 1 THEN ERROR "hit overlap"

' --- Move sprite 2 just past the overlap → no hit ---
TILEMAP SPRITE MOVE 2, 200, 200
IF TILEMAP(SPRITE HIT 1, 2) <> 0 THEN ERROR "hit miss"

' --- Cleanup ---
TILEMAP SPRITE DESTROY 1
TILEMAP SPRITE DESTROY 2
TILEMAP DESTROY 1

PRINT "ok"
END
