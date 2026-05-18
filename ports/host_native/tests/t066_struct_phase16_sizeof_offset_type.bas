' Phase 16: STRUCT(SIZEOF/OFFSET/TYPE).
' Covers acceptance tests 60, 61, 62, 63, 63a-63d, 85, 85b-85d.

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type Person
  age As INTEGER
  height As FLOAT
  name As STRING
End Type

' --- Test 60: SIZEOF Point = 16 ---
Print "60:"; Struct(SIZEOF "Point")

' --- Test 61: SIZEOF Person = 272 (8+8+256) ---
Print "61:"; Struct(SIZEOF "Person")

' --- Test 62: case insensitive ---
Print "62:"; Struct(SIZEOF "point"); ",";Struct(SIZEOF "POINT"); ",";Struct(SIZEOF "PoInT")

' --- Test 63: variable typename ---
Dim tn$ = "Point"
Print "63:"; Struct(SIZEOF tn$)

' --- Test 63a: OFFSET x, y ---
Print "63a:"; Struct(OFFSET "Point", "x"); ",";Struct(OFFSET "Point", "y")

' --- Test 63b: Person member offsets ---
Print "63b:"; Struct(OFFSET "Person", "age");",";Struct(OFFSET "Person", "height");",";Struct(OFFSET "Person", "name")

' --- Test 63c: OFFSET case insensitive ---
Print "63c:"; Struct(OFFSET "point", "x");",";Struct(OFFSET "POINT", "X");",";Struct(OFFSET "PoInT", "Y")

' --- Test 63d: variable typename + variable member ---
Dim tn2$ = "Point"
Dim mn$ = "y"
Print "63d:"; Struct(OFFSET tn2$, mn$)

' --- Test 85: TYPE returns T_INT/T_NBR/T_STR codes ---
' T_INT = 4, T_NBR = 1, T_STR = 2 (from MMBasic.h)
Print "85_age:";  Struct(TYPE "Person", "age")
Print "85_hgt:";  Struct(TYPE "Person", "height")
Print "85_name:"; Struct(TYPE "Person", "name")
