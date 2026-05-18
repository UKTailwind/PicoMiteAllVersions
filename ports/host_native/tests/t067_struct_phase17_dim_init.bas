' Phase 17: DIM / LOCAL struct initializer.
' Covers acceptance tests 15, 16, 17, 18, 21.

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type Person
  age As INTEGER
  height As FLOAT
  name As STRING
End Type

Type TestStruct
  num As INTEGER
  txt As STRING
End Type

' --- Test 15: scalar initializer ---
Dim initPoint As Point = (42, 99)
Print "15:"; initPoint.x; ",";initPoint.y

' --- Test 16: mixed-type scalar initializer ---
Dim initPerson As Person = (30, 1.80, "Bob")
Print "16:"; initPerson.age; ",";initPerson.height; ",";initPerson.name

' --- Test 17: array-of-struct initializer ---
Dim initArr(1) As Point = (10, 20, 30, 40)
Print "17:"; initArr(0).x; ",";initArr(0).y; "|";initArr(1).x; ",";initArr(1).y

' --- Test 18: mixed-type struct-array initializer ---
Dim mixArr(1) As TestStruct = (100, "first", 200, "second")
Print "18:"; mixArr(0).num; ",";mixArr(0).txt; "|";mixArr(1).num; ",";mixArr(1).txt

' --- Test 21: LOCAL with initializer (deferred from Phase 8) ---
Sub TestLocalInit
  Local initPt As Point = (777, 888)
  Print "21:"; initPt.x; ",";initPt.y
End Sub

TestLocalInit
