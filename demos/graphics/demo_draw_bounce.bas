' demo_draw_bounce.bas -- bouncing balls with NO framebuffer.
' Each frame: erase at old position (black circle), update, draw at new.
' Cheap per-frame network traffic — a handful of shape ops, not a full
' 400 KB BLIT. Good fit for scenes with few moving objects.
' Press any key to exit.

Option Explicit
Const NB% = 8
Const R%  = 8
Dim W% = MM.HRES, H% = MM.VRES
Dim BG% = RGB(BLACK)

Dim X(NB%), Y(NB%), VX(NB%), VY(NB%), COL%(NB%)
Dim OX(NB%), OY(NB%)
Dim COLS%(7)
COLS%(0) = RGB(RED)     : COLS%(1) = RGB(YELLOW)
COLS%(2) = RGB(GREEN)   : COLS%(3) = RGB(CYAN)
COLS%(4) = RGB(MAGENTA) : COLS%(5) = RGB(WHITE)
COLS%(6) = RGB(ORANGE)  : COLS%(7) = RGB(PINK)

Dim I%
For I% = 0 To NB% - 1
    X(I%) = R% + Rnd() * (W% - 2 * R%)
    Y(I%) = R% + Rnd() * (H% - 2 * R%)
    VX(I%) = (Rnd() - 0.5) * 5
    VY(I%) = (Rnd() - 0.5) * 5
    COL%(I%) = COLS%(I% Mod 8)
    OX(I%) = X(I%)
    OY(I%) = Y(I%)
Next I%

CLS BG%
TEXT 4, 4, "Direct-draw bounce (no framebuffer)", , 1, 1, RGB(WHITE)

Do
    For I% = 0 To NB% - 1
        ' Erase the ball at its previous position.
        CIRCLE OX(I%), OY(I%), R%, 1, 1, BG%, BG%
        ' Move.
        X(I%) = X(I%) + VX(I%)
        Y(I%) = Y(I%) + VY(I%)
        If X(I%) < R% Then X(I%) = R% : VX(I%) = -VX(I%)
        If X(I%) > W% - R% Then X(I%) = W% - R% : VX(I%) = -VX(I%)
        If Y(I%) < R% Then Y(I%) = R% : VY(I%) = -VY(I%)
        If Y(I%) > H% - R% Then Y(I%) = H% - R% : VY(I%) = -VY(I%)
        ' Draw at the new position.
        CIRCLE X(I%), Y(I%), R%, 1, 1, COL%(I%), COL%(I%)
        OX(I%) = X(I%)
        OY(I%) = Y(I%)
    Next I%
Loop Until Inkey$ <> ""
