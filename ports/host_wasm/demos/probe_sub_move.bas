' probe_sub_move.bas - does TILEMAP SPRITE MOVE work from inside a SUB?
'
' Assumes atlas.bmp exists (run pico_blocks_tilemap.bas once first).
'
' Top-level MOVE to (50, 50) — expect ball at (50, 50).
' Then SUB moveTo calls MOVE with LOCAL params x%, y% — expect ball at (150, 100).
' If hypothesis is right (SUB-local bridge sync broken on WASM), the SUB's
' MOVE silently uses x%=0, y%=0 and the ball appears at (0, 0).
DIM INTEGER tm_dummymap

FLASH LOAD IMAGE 1, "atlas.bmp", O
CLS RGB(BLACK)
TILEMAP CREATE tm_dummymap, 1, 1, 16, 16, 7, 1, 1
TILEMAP SPRITE CREATE 1, 1, 1, 0, 0

' Top-level move first.
TILEMAP SPRITE MOVE 1, 50, 50
TILEMAP SPRITE DRAW F, 0
PAUSE 1500

' Now via SUB with local params.
moveTo 150, 100
TILEMAP SPRITE DRAW F, 0
PAUSE 3000

END

SUB moveTo(x%, y%)
  TILEMAP SPRITE MOVE 1, x%, y%
END SUB

GOTO skip_data
tm_dummymap:
DATA 1
skip_data:
