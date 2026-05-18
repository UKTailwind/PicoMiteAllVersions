' Test SUB / FUNCTION with parenthesis-less parameter list.
' Used by Picovaders.bas line 429: Sub debunk zx,zy
'
' MMBasic accepts both `Sub foo(a, b)` and `Sub foo a, b`.

' Parenthesis-less SUB call (legacy form)
NoParens 7, 8
ParensCall (3, 4)

Sub NoParens a, b
  Print "no-parens: "; a + b
End Sub

Sub ParensCall(a, b)
  Print "parens: "; a + b
End Sub

' Parenthesis-less SUB with one arg
Solo 99

Sub Solo n
  Print "solo: "; n
End Sub
