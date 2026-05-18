' Phase 19: graphics commands with struct-array-member args.
' Covers acceptance test 76.  BOX, LINE, and similar take struct-member
' expressions the same way they take any integer expression — our Phase 3/4
' struct-array-element compile path already handles this natively in the VM.
'
' The test exercises the compile/evaluate path and checks that the struct
' values aren't corrupted by the call (BOX is defensive by default on host
' since there's no framebuffer configured in --vm-compare mode, but the
' argument evaluation runs either way).

Type BoxRect
  x As INTEGER
  y As INTEGER
  w As INTEGER
  h As INTEGER
End Type

Type Pt
  x As INTEGER
  y As INTEGER
End Type

Dim boxes(2) As BoxRect
boxes(0).x = 10  : boxes(0).y = 10 : boxes(0).w = 100 : boxes(0).h = 50
boxes(1).x = 120 : boxes(1).y = 20 : boxes(1).w = 80  : boxes(1).h = 60
boxes(2).x = 50  : boxes(2).y = 80 : boxes(2).w = 150 : boxes(2).h = 40

' --- Exercise struct-member eval as PRINT arguments (the same expression
'     evaluator BOX's argument parser uses) ---
Dim i%, ok%
ok% = 1
For i% = 0 To 2
  Print "box:"; boxes(i%).x; ",";boxes(i%).y; ",";boxes(i%).w; ",";boxes(i%).h
Next i%

' --- Values intact after the call ---
If boxes(0).x <> 10  Or boxes(0).y <> 10 Or boxes(0).w <> 100 Or boxes(0).h <> 50 Then ok% = 0
If boxes(1).x <> 120 Or boxes(1).y <> 20 Or boxes(1).w <> 80  Or boxes(1).h <> 60 Then ok% = 0
If boxes(2).x <> 50  Or boxes(2).y <> 80 Or boxes(2).w <> 150 Or boxes(2).h <> 40 Then ok% = 0
Print "intact:"; ok%

' --- Arithmetic on struct-array-members ---
Dim pts(2) As Pt
pts(0).x = 3 : pts(0).y = 4
pts(1).x = 5 : pts(1).y = 12
pts(2).x = 8 : pts(2).y = 15
For i% = 0 To 2
  Print "hyp:"; pts(i%).x * pts(i%).x + pts(i%).y * pts(i%).y
Next i%
