' Test stray ENDIF tolerance — MMBasic interp silently ignores an
' ENDIF that has no matching IF.  Picovaders.bas line 317 has one.
'
' VM must match interp behaviour: stray ENDIF compiles to nothing.

Print "before"
EndIf
Print "after"

' Stray ENDIF inside a SUB.
Foo
End

Sub Foo
  Print "in foo before"
  EndIf
  Print "in foo after"
End Sub
