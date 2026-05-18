' Phase 12: STRUCT PRINT (single, array element, whole array).
' Covers acceptance tests 43, 44, 45.

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type Person
  name As STRING LENGTH 16
  age As INTEGER
  height As FLOAT
End Type

' --- Test 43: single struct ---
Dim printTest As Person
printTest.name = "Alice"
printTest.age = 25
printTest.height = 1.65
Print "[43]"
Struct Print printTest

' --- Test 44: array element ---
Dim printArr(2) As Point
printArr(0).x = 100 : printArr(0).y = 200
printArr(1).x = 300 : printArr(1).y = 400
printArr(2).x = 500 : printArr(2).y = 600
Print "[44]"
Struct Print printArr(0)

' --- Test 45: whole array ---
Print "[45]"
Struct Print printArr()
