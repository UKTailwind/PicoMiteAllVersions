' Phase 18: Legacy dotted-name coexistence.
' Covers acceptance tests 55, 56, 57, 58, 59.

Type Point
  x As INTEGER
  y As INTEGER
End Type

Dim subTestResult%

Sub My.Test.Sub (a%, b%)
  subTestResult% = a% + b%
End Sub

Function My.Test.Function%(a%, b%)
  My.Test.Function% = a% * b%
End Function

' --- Test 55: dotted variables ---
Dim My.Variable% = 42
Dim Another.Dotted.Name! = 3.14159
Dim String.With.Dots$ = "Hello"
Print "55a:"; My.Variable%; ",";Another.Dotted.Name!; ",";String.With.Dots$
My.Variable% = 100
Another.Dotted.Name! = 2.718
String.With.Dots$ = "World"
Print "55b:"; My.Variable%; ",";Another.Dotted.Name!; ",";String.With.Dots$

' --- Test 56: dotted SUB ---
subTestResult% = 0
My.Test.Sub 10, 20
Print "56:"; subTestResult%

' --- Test 57: dotted FUNCTION ---
Print "57:"; My.Test.Function%(5, 7)

' --- Test 58: mix dotted vars + struct members ---
Dim coord.offset% = 50
Dim pt As Point
pt.x = 10 : pt.y = 20
Print "58a:"; pt.x + coord.offset%
pt.x = coord.offset%
Print "58b:"; pt.x
coord.offset% = pt.y
Print "58c:"; coord.offset%

' --- Test 59: dotted array ---
Dim Data.Array%(5)
Data.Array%(0) = 100
Data.Array%(3) = 300
Data.Array%(5) = 500
Print "59:"; Data.Array%(0); ",";Data.Array%(3); ",";Data.Array%(5)
