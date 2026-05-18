' demo_draw_paint.bas -- incremental "paint" pattern, direct-draw.
' Plots a rotating rosette one segment at a time. Each step is one LINE
' and one PIXEL — accumulates on screen without any framebuffer.
' Press any key to exit.

Option Explicit
Dim W% = MM.HRES, H% = MM.VRES
Dim CX% = W% \ 2, CY% = H% \ 2
Dim RMAX! = (Min(W%, H%) / 2) - 4
Dim HUE%, R%, G%, B%
Dim STEP_ANGLE! = 137.5 * 3.14159 / 180   ' golden-angle seed for a phyllotaxis
Dim A! = 0, RADIUS! = 0
Dim I% = 0
Dim PX%, PY%

CLS RGB(BLACK)
TEXT 4, 4, "Phyllotaxis - key to exit", , 1, 1, RGB(WHITE)

Do
    I% = I% + 1
    RADIUS! = Sqr(I%) * 1.5
    If RADIUS! > RMAX! Then
        PAUSE 500
        CLS RGB(BLACK)
        TEXT 4, 4, "Phyllotaxis - key to exit", , 1, 1, RGB(WHITE)
        I% = 0
        A! = 0
    EndIf
    A! = A! + STEP_ANGLE!
    PX% = CX% + Cos(A!) * RADIUS!
    PY% = CY% + Sin(A!) * RADIUS!
    HUE% = (I% * 3) Mod 360
    ' Cheap HSV-ish mapping — walks the colour wheel
    R% = Int((Sin(HUE% * 3.14159 / 180) + 1) * 127)
    G% = Int((Sin((HUE% + 120) * 3.14159 / 180) + 1) * 127)
    B% = Int((Sin((HUE% + 240) * 3.14159 / 180) + 1) * 127)
    CIRCLE PX%, PY%, 2, 1, 1, RGB(R%, G%, B%), RGB(R%, G%, B%)
    PAUSE 10
Loop Until Inkey$ <> ""
