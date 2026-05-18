' Phase 14: STRUCT(FIND) — non-regex.
' Covers acceptance tests 37, 38, 39, 40, 42.
' Regex cases (42A-42F) land in Phase 15.

Type Person
  name As STRING LENGTH 16
  age As INTEGER
  height As FLOAT
End Type

Dim arr(4) As Person
arr(0).name = "Alice"   : arr(0).age = 25 : arr(0).height = 1.60
arr(1).name = "Bob"     : arr(1).age = 30 : arr(1).height = 1.75
arr(2).name = "Charlie" : arr(2).age = 35 : arr(2).height = 1.80
arr(3).name = "Diana"   : arr(3).age = 40 : arr(3).height = 1.65
arr(4).name = "Eve"     : arr(4).age = 45 : arr(4).height = 1.70

' --- Test 37: FIND by integer member ---
Print "37:"; Struct(FIND arr().age, 35)

' --- Test 38: FIND by string member ---
Print "38:"; Struct(FIND arr().name, "Diana")

' --- Test 39: FIND not-found returns -1 ---
Print "39:"; Struct(FIND arr().name, "Zara")

' --- Test 40: FIND by float member ---
arr(2).height = 1.77
Print "40:"; Struct(FIND arr().height, 1.77)

' --- Test 42: FIND with start param iterates duplicates ---
arr(0).age = 30
arr(1).age = 30
arr(4).age = 30
Dim f% = Struct(FIND arr().age, 30)
Print "42a:";f%
f% = Struct(FIND arr().age, 30, f% + 1)
Print "42b:";f%
f% = Struct(FIND arr().age, 30, f% + 1)
Print "42c:";f%
f% = Struct(FIND arr().age, 30, f% + 1)
Print "42d:";f%
