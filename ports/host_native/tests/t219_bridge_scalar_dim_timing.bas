' Regression: bridge sync must not materialize future scalar DIM slots.
' The STRUCT(FIND) regex output parameter requires matchLen% to be visible
' to the bridged function after its DIM executes, but future% must not appear
' in LIST VARIABLES until control reaches its own DIM line.

Type Person
  name As STRING LENGTH 20
End Type

Dim arr(1) As Person
arr(0).name = "Bob"

Dim matchLen%, idx%
idx% = Struct(FIND arr().name, "^Bob", , matchLen%)
List Variables

Dim future%
