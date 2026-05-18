' demo_gfx_stars.bas -- Star Trek-style starfield warp.
' Stars move outward from the centre, accelerating as they approach
' the edge. Press any key to exit.

Option Explicit
Const N% = 60
Dim W% = MM.HRES, H% = MM.VRES
Dim CX! = W% / 2, CY! = H% / 2

Dim X!(N%), Y!(N%), Z!(N%)
Dim I%, SX!, SY!, B%, C%, RAD%
For I% = 0 To N% - 1
    X!(I%) = (Rnd() - 0.5) * W%
    Y!(I%) = (Rnd() - 0.5) * H%
    Z!(I%) = Rnd() * 0.9 + 0.1
Next I%

FRAMEBUFFER CREATE
FRAMEBUFFER WRITE F

Do
    CLS RGB(BLACK)
    For I% = 0 To N% - 1
        Z!(I%) = Z!(I%) - 0.015
        If Z!(I%) <= 0.02 Then
            X!(I%) = (Rnd() - 0.5) * W%
            Y!(I%) = (Rnd() - 0.5) * H%
            Z!(I%) = 1.0
        EndIf
        SX! = X!(I%) / Z!(I%) + CX!
        SY! = Y!(I%) / Z!(I%) + CY!
        If SX! >= 0 And SX! < W% And SY! >= 0 And SY! < H% Then
            B% = Int((1 - Z!(I%)) * 255)
            If B% < 0 Then B% = 0
            If B% > 255 Then B% = 255
            C% = RGB(B%, B%, B%)
            RAD% = Int((1 - Z!(I%)) * 3)
            If RAD% < 0 Then RAD% = 0
            If RAD% < 1 Then
                PIXEL SX!, SY!, C%
            Else
                CIRCLE SX!, SY!, RAD%, 1, 1, C%, C%
            EndIf
        EndIf
    Next I%
    FRAMEBUFFER COPY F, N
Loop Until Inkey$ <> ""

FRAMEBUFFER WRITE N
FRAMEBUFFER CLOSE F
