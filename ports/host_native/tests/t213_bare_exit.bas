' Test bare EXIT (no keyword) — exits innermost enclosing DO loop.
' Matches MMBasic interpreter quirk: bare EXIT only matches DO, not FOR.
'
' Used by Picovaders.bas line 113: If aDth%=1 Then ... :Exit (inside Do/Loop)

' --- Inside a Do loop ---
Dim i%
i% = 0
Do
  Inc i%
  If i% = 3 Then Exit
Loop
Print "do: "; i%

' --- Bare Exit inside If inside Do ---
Dim j%
j% = 0
Do
  Inc j%
  If j% = 7 Then
    Print "before exit: "; j%
    Exit
  EndIf
Loop
Print "do/if: "; j%

' --- Bare Exit inside FOR inside DO: pops the DO, not the FOR ---
Dim k%, total%
total% = 0
Do
  For k% = 1 To 3
    total% = total% + k%
  Next k%
  Exit
Loop
Print "for-in-do: "; total%
