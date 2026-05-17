' Regression: a bridged command inside a SUB whose scalar parameter shares
' its base name with a global array raised "Array dimensions" because the
' post-bridge sync_mmbasic_to_vm rebind pass called findvar() while the
' synthesised local frame was still active.  findvar's local search
' matched the parameter scalar before the global array.
'
' Real-world trigger: Picovaders.bas, `Sub expl ex, ey, snd` versus the
' top-level `Dim snd%(4) = (...)`.  Note that expl's body does NOT
' reference snd% directly — the rebind sweeps all global arrays on every
' bridge call, so the collision fires even when only the local scalar is
' read inside the sub.

Dim snd%(4) = (100, 90, 85, 80, 70)
Dim noise%(5)
For f = 1 To 5 : noise%(f) = f * 10 : Next f

Print "global snd%(0)="; snd%(0)
Print "global snd%(4)="; snd%(4)

caller
Print "global snd%(2) after="; snd%(2)

Sub caller
  expl 1, 2, 0
  expl 3, 4, 1
End Sub

Sub expl ex, ey, snd
  Print "expl ex="; ex; " ey="; ey; " snd="; snd
  ' A bridged command (Play tone) inside the sub re-triggers the
  ' array-rebinding pass that the bug fired in.  expl deliberately does
  ' NOT reference snd%(...) — the bug fires from the sync layer alone.
  If snd = 1 Then
    For nse = 1 To 3
      Play tone noise%(nse), noise%(nse), 1
    Next nse
  EndIf
End Sub
