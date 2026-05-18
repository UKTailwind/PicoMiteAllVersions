' demo_gfx_bounce.bas -- classic bouncing balls with FASTGFX double buffering
' Press any key to exit.

Option Explicit
Const NB% = 8
Const R%  = 8
Dim W% = MM.HRES, H% = MM.VRES

Dim X(NB%), Y(NB%), VX(NB%), VY(NB%), COL%(NB%)
Dim COLS%(7)
COLS%(0) = RGB(RED)    : COLS%(1) = RGB(YELLOW)
COLS%(2) = RGB(GREEN)  : COLS%(3) = RGB(CYAN)
COLS%(4) = RGB(MAGENTA): COLS%(5) = RGB(WHITE)
COLS%(6) = RGB(ORANGE) : COLS%(7) = RGB(PINK)

Dim I%
For I% = 0 To NB% - 1
    X(I%) = R% + Rnd() * (W% - 2 * R%)
    Y(I%) = R% + Rnd() * (H% - 2 * R%)
    VX(I%) = (Rnd() - 0.5) * 4
    VY(I%) = (Rnd() - 0.5) * 4
    COL%(I%) = COLS%(I% Mod 8)
Next I%

FRAMEBUFFER CREATE
FRAMEBUFFER WRITE F

Do
    CLS RGB(BLACK)
    For I% = 0 To NB% - 1
        X(I%) = X(I%) + VX(I%)
        Y(I%) = Y(I%) + VY(I%)
        If X(I%) < R% Then X(I%) = R% : VX(I%) = -VX(I%)
        If X(I%) > W% - R% Then X(I%) = W% - R% : VX(I%) = -VX(I%)
        If Y(I%) < R% Then Y(I%) = R% : VY(I%) = -VY(I%)
        If Y(I%) > H% - R% Then Y(I%) = H% - R% : VY(I%) = -VY(I%)
        CIRCLE X(I%), Y(I%), R%, 1, 1, COL%(I%), COL%(I%)
    Next I%
    TEXT 4, 4, "FRAMEBUFFER bounce - key to exit", , 1, 1, RGB(WHITE)
    FRAMEBUFFER COPY F, N
Loop Until Inkey$ <> ""

FRAMEBUFFER WRITE N
FRAMEBUFFER CLOSE F
