' Test INC on array element: Inc arr(i), n
' Used by Picovaders.bas line 281: Inc A_Bomb%(f,2),1

Dim a%(5) = (10, 20, 30, 40, 50, 60)
Dim i%
i% = 2

' Inc with explicit delta on array element
Inc a%(i%), 5
Print a%(0); a%(1); a%(2); a%(3); a%(4); a%(5)

' Inc with default delta (+1) on array element
Inc a%(0)
Print a%(0); a%(1); a%(2); a%(3); a%(4); a%(5)

' Inc with negative delta on array element
Inc a%(5), -10
Print a%(0); a%(1); a%(2); a%(3); a%(4); a%(5)

' Multi-dim array element
Dim m%(3, 3)
m%(1, 2) = 100
Inc m%(1, 2), 7
Print m%(1, 2)

' Float array
Dim f!(2) = (1.5, 2.5, 3.5)
Inc f!(1), 0.25
Print f!(0); f!(1); f!(2)
