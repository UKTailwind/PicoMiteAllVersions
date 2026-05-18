' Phase 2 TYPE/STRUCT — string members + ERASE cleanup + packing
' (tests 2 & 31 of tests/acceptance/struct_full.bas).

' --- Default-length string member ---
TYPE Person
  age AS INTEGER
  height AS FLOAT
  name AS STRING
END TYPE

DIM p AS Person
p.age = 42
p.height = 1.85
p.name = "John"
PRINT p.age, p.height, p.name

' Reassign string to a different length to confirm inline storage
' (no separate heap allocation for the string member).
p.name = "Alexander"
PRINT p.name
p.name = ""
PRINT "[" + p.name + "]"

' --- LENGTH-qualified string packing before INT / FLOAT ---
' Exercises 8-byte alignment after a short string member.
TYPE PackTest
  label AS STRING LENGTH 5
  value AS INTEGER
  ratio AS FLOAT
END TYPE

DIM pk AS PackTest
pk.label = "Test"
pk.value = 12345
pk.ratio = 3.14
PRINT pk.label, pk.value, pk.ratio

' --- ERASE cleanup: re-DIM after ERASE must not fault ---
DIM q AS Person
q.age = 7
q.name = "Bob"
PRINT q.age, q.name
ERASE q
DIM q AS Person
q.age = 11
q.name = "Carol"
PRINT q.age, q.name
