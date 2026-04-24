' demo_gfx_shapes.bas -- static showcase of the PicoMite drawing primitives
' CLS, BOX, RBOX, LINE, CIRCLE, PIXEL, TRIANGLE, TEXT, COLOUR.

Option Explicit
Dim W% = MM.HRES, H% = MM.VRES
Dim CX% = W% \ 2, CY% = H% \ 2
Dim I%, Dx!, Dy!

CLS RGB(BLACK)

' --- Title ---
TEXT CX%, 6, "MMBasic Graphics", "CT", 1, 1, RGB(CYAN)
LINE 0, 22, W% - 1, 22, 1, RGB(CYAN)

' --- Filled & outline boxes ---
BOX 10, 30, 60, 40, 1, RGB(RED), RGB(RED)
BOX 70, 30, 120, 40, 1, RGB(YELLOW)
RBOX 130, 30, 60, 40, 8, RGB(GREEN), RGB(GREEN) * &HFF \ 4

' --- Circles ---
CIRCLE 40, 110, 22, 1, 1, RGB(MAGENTA)
CIRCLE 100, 110, 22, 1, 1, RGB(CYAN), RGB(CYAN)
CIRCLE 160, 110, 22, 1, 1, RGB(WHITE), RGB(GRAY)

' --- Lines radiating ---
For I% = 0 To 359 Step 12
    Dx! = Cos(I% / 180 * 3.14159) * 40
    Dy! = Sin(I% / 180 * 3.14159) * 40
    LINE 250, 110, 250 + Dx!, 110 + Dy!, 1, RGB(YELLOW)
Next I%

' --- Triangles ---
TRIANGLE 20, 220, 60, 160, 100, 220, RGB(BLUE), RGB(BLUE)
TRIANGLE 120, 220, 160, 160, 200, 220, RGB(MAGENTA)

' --- Pixel grid ---
For I% = 0 To 200 Step 4
    PIXEL 220 + (I% Mod 60), 160 + (I% \ 4), RGB(GREEN)
Next I%

' --- Footer text ---
TEXT CX%, H% - 14, "Press any key to exit", "CT", 1, 1, RGB(WHITE)

' Wait for a key so the result stays on screen
Do While Inkey$ = "" : Loop
