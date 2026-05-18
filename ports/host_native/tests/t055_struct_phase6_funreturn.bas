' Phase 6 function-returns-struct (acceptance tests 29, 30, 32, 73).

Type Point
  x As INTEGER
  y As INTEGER
End Type

Type Person
  name As STRING LENGTH 20
  age As INTEGER
  height As FLOAT
End Type

Type WithArray
  id As INTEGER
  values(3) As INTEGER
End Type

' --- Test 29 / 73 analog: scalar struct returned from function ---
Dim p29 As Point
p29 = MakePoint(123, 456)
Print p29.x; ","; p29.y

Dim p73 As Point
p73 = MakePoint(777, 888)
Print p73.x; ","; p73.y

' --- Test 30: mixed-types struct returned from function ---
Dim p30 As Person
p30 = MakePerson("Bob", 35, 1.75)
Print p30.name; "/"; p30.age; "/"; p30.height

' --- Test 32: struct with array-member returned from function ---
Dim wa As WithArray
wa = MakeWithArray(99, 10, 20, 30, 40)
Print wa.id; "/"; wa.values(0); ","; wa.values(1); ","; wa.values(2); ","; wa.values(3)

End

Function MakePoint(xv%, yv%) As Point
  MakePoint.x = xv%
  MakePoint.y = yv%
End Function

Function MakePerson(n$, a%, h!) As Person
  MakePerson.age = a%
  MakePerson.height = h!
  MakePerson.name = n$
End Function

Function MakeWithArray(id%, v0%, v1%, v2%, v3%) As WithArray
  MakeWithArray.id = id%
  MakeWithArray.values(0) = v0%
  MakeWithArray.values(1) = v1%
  MakeWithArray.values(2) = v2%
  MakeWithArray.values(3) = v3%
End Function
