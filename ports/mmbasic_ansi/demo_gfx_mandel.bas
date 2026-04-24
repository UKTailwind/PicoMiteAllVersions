' demo_gfx_mandel.bas -- Mandelbrot set renderer.
' Iterates z = z^2 + c per pixel. Colour = escape count.
' Uses MM.HRES/MM.VRES and a uniform scale so pixels stay square
' regardless of viewport aspect ratio.
' Press key to exit.

Option Explicit
Const MAX% = 48
Const CR_CENTER! = -0.5         ' complex-plane centre to display
Const CI_CENTER! = 0.0
Const RE_SPAN!   = 3.5          ' minimum real range the viewport should cover
Const IM_SPAN!   = 2.4          ' minimum imaginary range the viewport should cover

Dim W% = MM.HRES, H% = MM.VRES
Dim X%, Y%, I%, R%, G%, B%
Dim CR!, CI!, ZR!, ZI!, TMP!

' Uniform "complex units per pixel" scale — pick the larger of the two
' axis scales so the base region always fits the viewport and pixels
' remain square (1 px X = 1 px Y in complex plane). Wider viewports
' show extra real range; taller ones show extra imaginary range.
Dim SCALE!
If RE_SPAN! / W% > IM_SPAN! / H% Then
  SCALE! = RE_SPAN! / W%
Else
  SCALE! = IM_SPAN! / H%
EndIf

For Y% = 0 To H% - 1
    CI! = (Y% - H% / 2) * SCALE! + CI_CENTER!
    For X% = 0 To W% - 1
        CR! = (X% - W% / 2) * SCALE! + CR_CENTER!
        ZR! = 0 : ZI! = 0
        I% = 0
        Do While ZR! * ZR! + ZI! * ZI! < 4 And I% < MAX%
            TMP! = ZR! * ZR! - ZI! * ZI! + CR!
            ZI! = 2 * ZR! * ZI! + CI!
            ZR! = TMP!
            I% = I% + 1
        Loop
        If I% = MAX% Then
            PIXEL X%, Y%, RGB(BLACK)
        Else
            R% = (I% * 8) Mod 256
            G% = (I% * 16) Mod 256
            B% = (I% * 32) Mod 256
            PIXEL X%, Y%, RGB(R%, G%, B%)
        EndIf
    Next X%
Next Y%

TEXT 4, H% - 14, "Mandelbrot - key to exit", , 1, 1, RGB(WHITE)
Do While Inkey$ = "" : Loop
