' Phase 1 TYPE/STRUCT — scalar members only (tests 1, 2, 13 of struct_full.bas).
' Covers TYPE/END TYPE parsing, DIM s AS <type>, scalar numeric + float field
' read/write, and multiple TYPE definitions coexisting in one program.

TYPE Point
  x AS INTEGER
  y AS INTEGER
END TYPE

TYPE Particle
  px AS FLOAT
  py AS FLOAT
  life AS INTEGER
END TYPE

DIM p AS Point
p.x = 3
p.y = 4
PRINT p.x, p.y

p.x = p.x * p.x + p.y * p.y
PRINT p.x

DIM q AS Point
q.x = 10
q.y = 20
PRINT p.x, p.y, q.x, q.y

DIM part AS Particle
part.px = 1.5
part.py = -2.5
part.life = 42
PRINT part.px, part.py, part.life

' Multiple struct types coexisting + independent state.
p.x = 100
q.x = 200
PRINT p.x, q.x, part.life
