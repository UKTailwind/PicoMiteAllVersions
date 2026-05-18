' probe_sprite_move.bas - TILEMAP SPRITE MOVE smoke test.
'
' Assumes atlas.bmp exists in the sim filesystem (run
' pico_blocks_tilemap.bas once first; it creates atlas.bmp during
' startup via CreateAtlas + FLASH WRITE IMAGE).
'
' Creates a sprite at (0,0), draws it for 0.8s, then MOVEs it to
' (100,80) and draws again for 3s.  If the ball tile stays at (0,0)
' instead of jumping to (100,80), TILEMAP SPRITE MOVE is the
' regression.
DIM INTEGER tm_dummymap

FLASH LOAD IMAGE 1, "atlas.bmp", O
CLS RGB(BLACK)
TILEMAP CREATE tm_dummymap, 1, 1, 16, 16, 7, 1, 1
TILEMAP SPRITE CREATE 1, 1, 1, 0, 0
TILEMAP SPRITE DRAW F, 0
PAUSE 800

TILEMAP SPRITE MOVE 1, 100, 80
TILEMAP SPRITE DRAW F, 0
PAUSE 3000

GOTO skip_data
tm_dummymap:
DATA 1
skip_data:
