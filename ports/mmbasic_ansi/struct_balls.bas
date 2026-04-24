' struct_balls.bas — TYPE/STRUCT demo for the web emulator.
' Exercises:
'   Type / End Type
'   Dim arr(N) As MyType
'   Struct field read/write in expressions
'   STRUCT(SIZEOF typename$)
'   STRUCT PRINT  (on-screen dump of ball(0) — key "p")
'   STRUCT SAVE   (binary dump of whole array to balls.dat — key "d")
'   STRUCT LOAD   (round-trip read — key "l")

Option Default None

Type Ball
  x  As Integer
  y  As Integer
  vx As Integer
  vy As Integer
  r  As Integer
  c  As Integer
End Type

Const N = 8
Dim balls(N - 1) As Ball

' Seed the array with mixed vectors + RGB colours.
Dim i%
For i% = 0 To N - 1
  balls(i%).x  = 40 + (i% * 31) Mod 240
  balls(i%).y  = 40 + (i% * 53) Mod 160
  balls(i%).vx = 1 + (i% Mod 3)
  balls(i%).vy = 1 + ((i% + 1) Mod 3)
  balls(i%).r  = 5 + (i% Mod 4)
  balls(i%).c  = Rgb(255 - i% * 20, (i% * 63) Mod 256, (i% * 37 + 80) Mod 256)
Next i%

CLS Rgb(Black)

' Banner showing SIZEOF — proves STRUCT() function works.
Text 4, 4, "Ball size: " + Str$(Struct(SIZEOF "Ball")) + " bytes, N=" + Str$(N), , , , Rgb(White), Rgb(Black)
Text 4, 16, "d=save balls.dat   l=reload   p=print   q=quit", , , , Rgb(Cyan), Rgb(Black)

Dim W% = MM.HRes, H% = MM.VRes
Dim k$, running% = 1, frame% = 0

Do While running%
  ' Erase previous positions.
  For i% = 0 To N - 1
    Circle balls(i%).x, balls(i%).y, balls(i%).r, , , Rgb(Black), Rgb(Black)
  Next i%

  ' Move + bounce.
  For i% = 0 To N - 1
    balls(i%).x = balls(i%).x + balls(i%).vx
    balls(i%).y = balls(i%).y + balls(i%).vy
    If balls(i%).x < balls(i%).r Or balls(i%).x > W% - balls(i%).r Then balls(i%).vx = -balls(i%).vx
    If balls(i%).y < balls(i%).r + 24 Or balls(i%).y > H% - balls(i%).r Then balls(i%).vy = -balls(i%).vy
  Next i%

  ' Draw new positions.
  For i% = 0 To N - 1
    Circle balls(i%).x, balls(i%).y, balls(i%).r, , , balls(i%).c, balls(i%).c
  Next i%

  frame% = frame% + 1
  k$ = Inkey$
  If k$ = "q" Or k$ = "Q" Then running% = 0

  If k$ = "d" Or k$ = "D" Then
    ' STRUCT SAVE writes the raw struct bytes — N * SIZEOF("Ball") bytes total.
    Open "balls.dat" For Output As #1
    Struct Save #1, balls()
    Close #1
    Text 4, 28, "Wrote balls.dat (" + Str$(N * Struct(SIZEOF "Ball")) + " bytes)     ", , , , Rgb(Yellow), Rgb(Black)
  EndIf

  If k$ = "l" Or k$ = "L" Then
    ' Round-trip: scramble the array, then reload to prove persistence.
    For i% = 0 To N - 1
      balls(i%).x = 0 : balls(i%).y = 0
    Next i%
    Open "balls.dat" For Input As #1
    Struct Load #1, balls()
    Close #1
    Text 4, 28, "Reloaded balls.dat                              ", , , , Rgb(Green), Rgb(Black)
  EndIf

  If k$ = "p" Or k$ = "P" Then
    ' On-screen pretty-print of one struct.
    Struct Print balls(0)
  EndIf

  Pause 16    ' ~60 fps cap; web yield hook unhangs Inkey$ between frames.
Loop

CLS Rgb(Black)
Text 10, 10, "Bye — ran " + Str$(frame%) + " frames.", , , , Rgb(White), Rgb(Black)
End
