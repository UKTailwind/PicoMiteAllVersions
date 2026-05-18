' RUN_ARGS: --interp
' TILEMAP DRAW + TILEMAP SPRITE DRAW render test. Authors a 2x2-tile
' atlas (each tile 4x4 pixels) where tile 1 = solid white, tile 2 =
' solid red, tile 3 = solid green, tile 4 = solid blue. Then draws
' both a tile-grid and a sprite onto the framebuffer and verifies
' PIXEL() reads back the expected RGB121-palette colour.
'
' Interp-only. Render correctness in compare mode would require the VM
' to emit identical paint events; bridge route is sufficient.

OPTION EXPLICIT

' --- Author 8x8 24bpp BMP: four 4x4 tiles arranged 2x2 ---
DIM INTEGER fnbr = 1
OPEN "B:/atlas.bmp" FOR OUTPUT AS #fnbr
PRINT #fnbr, "BM";
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(54);CHR$(0);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(40);CHR$(0);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(8);CHR$(0);CHR$(0);CHR$(0);   ' width 8
PRINT #fnbr, CHR$(8);CHR$(0);CHR$(0);CHR$(0);   ' height 8
PRINT #fnbr, CHR$(1);CHR$(0);
PRINT #fnbr, CHR$(24);CHR$(0);
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(&H13);CHR$(&H0B);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);
PRINT #fnbr, CHR$(0);CHR$(0);CHR$(0);CHR$(0);

' Pixel grid (BMP rows are bottom-up, so file order is row 7 -> row 0).
' Layout in image-coords (top-down):
'   (0..3,0..3) = tile1 white     (4..7,0..3) = tile2 red
'   (0..3,4..7) = tile3 green     (4..7,4..7) = tile4 blue
'
' For each file row, write 8 BGR triples.
DIM INTEGER y, x
DIM INTEGER bb, gg, rr
FOR y = 7 TO 0 STEP -1   ' file row 0 = image row 7 (bottom)
    FOR x = 0 TO 7
        IF y < 4 THEN
            ' Top half: tiles 1 + 2
            IF x < 4 THEN
                bb = &HFF : gg = &HFF : rr = &HFF   ' white
            ELSE
                bb = 0 : gg = 0 : rr = &HFF         ' red
            ENDIF
        ELSE
            ' Bottom half: tiles 3 + 4
            IF x < 4 THEN
                bb = 0 : gg = &HFF : rr = 0         ' green
            ELSE
                bb = &HFF : gg = 0 : rr = 0         ' blue
            ENDIF
        ENDIF
        PRINT #fnbr, CHR$(bb);CHR$(gg);CHR$(rr);
    NEXT
NEXT
CLOSE #fnbr

FLASH LOAD IMAGE 1, "B:/atlas.bmp", O

' --- TILEMAP CREATE: 1 tilemap, slot 1, tile 4x4, 2 tiles per row, 2x2 grid ---
'   Map is:    1 2
'              3 4
TILEMAP CREATE map1, 1, 1, 4, 4, 2, 2, 2
GOTO skip_data
map1:
DATA 1, 2
DATA 3, 4
skip_data:

CLS RGB(BLACK)

' --- TILEMAP DRAW id, dest, viewX, viewY, screenX, screenY, viewW, viewH ---
TILEMAP DRAW 1, F, 0, 0, 0, 0, 8, 8

' Verify each tile's centre pixel matches the painted colour. RGB121
' quantises each channel down so we compare to the palette-resolved
' expected value.
DIM INTEGER white_q = RGB(WHITE)
DIM INTEGER red_q   = RGB(RED)
DIM INTEGER green_q = RGB(GREEN)
DIM INTEGER blue_q  = RGB(BLUE)

IF Pixel(1, 1) <> white_q THEN ERROR "tile1 white miss " + Hex$(Pixel(1, 1))
IF Pixel(5, 1) <> red_q   THEN ERROR "tile2 red miss "   + Hex$(Pixel(5, 1))
IF Pixel(1, 5) <> green_q THEN ERROR "tile3 green miss " + Hex$(Pixel(1, 5))
IF Pixel(5, 5) <> blue_q  THEN ERROR "tile4 blue miss "  + Hex$(Pixel(5, 5))

' --- TILEMAP SPRITE DRAW into the same plane ---
' Sprite at (50, 50) referencing tile 2 (red) → expect red at (51, 51).
TILEMAP SPRITE CREATE 1, 1, 2, 50, 50
TILEMAP SPRITE DRAW F, -1
IF Pixel(51, 51) <> red_q THEN ERROR "sprite red miss " + Hex$(Pixel(51, 51))

TILEMAP CLOSE

PRINT "ok"
END
