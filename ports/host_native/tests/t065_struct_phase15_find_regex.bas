' Phase 15: STRUCT(FIND) regex branch.
' Covers acceptance tests 42A, 42B, 42C, 42D, 42E, 42F.

Type Person
  name As STRING LENGTH 20
  age As INTEGER
  height As FLOAT
End Type

Dim arr(4) As Person
arr(0).name = "Alice Smith"
arr(1).name = "Bob Jones"
arr(2).name = "Charlie Brown"
arr(3).name = "Diana Prince"
arr(4).name = "Eve Wilson"

Dim matchLen%, idx%

' --- Test 42A: basic pattern ^Bob ---
idx% = Struct(FIND arr().name, "^Bob", , matchLen%)
Print "42A:";idx%;",";matchLen%

' --- Test 42B: substring "own" ---
idx% = Struct(FIND arr().name, "own", , matchLen%)
Print "42B:";idx%;",";matchLen%

' --- Test 42C: char class ^[A-D] ---
idx% = Struct(FIND arr().name, "^[A-D]", , matchLen%)
Print "42C:";idx%

' --- Test 42D: with start param ---
idx% = Struct(FIND arr().name, "^[A-D]", 2, matchLen%)
Print "42D1:";idx%
idx% = Struct(FIND arr().name, "^[A-D]", 3, matchLen%)
Print "42D2:";idx%

' --- Test 42E: no match ---
matchLen% = 99
idx% = Struct(FIND arr().name, "^Zara", , matchLen%)
Print "42E:";idx%;",";matchLen%

' --- Test 42F: digit pattern \d+ ---
arr(0).name = "Agent007"
arr(1).name = "User123"
arr(2).name = "Test456"
idx% = Struct(FIND arr().name, "\d+", , matchLen%)
Print "42F:";idx%;",";matchLen%
