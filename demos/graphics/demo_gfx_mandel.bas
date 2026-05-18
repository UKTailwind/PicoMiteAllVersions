' demo_gfx_mandel.bas -- Mandelbrot set renderer.
' Iterates z = z^2 + c per pixel. Colour = escape count.
' Takes a second or three depending on resolution. Press key to exit.

Option Explicit
Const MAX% = 48
Dim W% = MM.HRES, H% = MM.VRES
Dim X%, Y%, I%, R%, G%, B%
Dim CR!, CI!, ZR!, ZI!, TMP!

For Y% = 0 To H% - 1
    CI! = (Y% - H% / 2) * 2.4 / H%
    For X% = 0 To W% - 1
        CR! = (X% - W% / 2) * 3.5 / W% - 0.5
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
