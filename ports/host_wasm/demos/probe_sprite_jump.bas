' probe_sprite_jump.bas - does TILEMAP SPRITE MOVE redraw old cell?
'
' Uses atlas.bmp from pico_blocks_tilemap.bas (run that once first).
' Sets up FASTGFX like the real demo, then jumps the ball sprite
' around a grid.  Each frame: FASTGFX has no explicit CLS — just like
' pico_blocks_tilemap.  If the ball LEAVES a trail, TILEMAP SPRITE
' MOVE + FASTGFX SWAP isn't clearing the old cell.
DIM INTEGER tm_dummymap
DIM INTEGER x%, y%, i%

FLASH LOAD IMAGE 1, "atlas.bmp", O
TILEMAP CREATE tm_dummymap, 1, 1, 16, 16, 7, 1, 1
TILEMAP SPRITE CREATE 1, 1, 1, 0, 0

FASTGFX CREATE
FASTGFX FPS 10
CLS RGB(BLACK)

FOR i% = 0 TO 20
  x% = (i% * 30) MOD 280
  y% = 20 + (i% \ 10) * 40
  TILEMAP SPRITE MOVE 1, x%, y%
  TILEMAP SPRITE DRAW F, 0
  FASTGFX SWAP
  FASTGFX SYNC
NEXT
PAUSE 3000

GOTO skip_data
tm_dummymap:
DATA 1
skip_data:
