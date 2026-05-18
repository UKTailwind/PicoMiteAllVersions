' demo_draw_clock.bas -- analog clock, direct-draw (no framebuffer).
' Each second, erase the old hands and draw new ones in place.
' The dial is drawn once at startup. Press any key to exit.

Option Explicit
Dim W% = MM.HRES, H% = MM.VRES
Dim CX% = W% \ 2, CY% = H% \ 2
Dim RAD% = (Min(W%, H%) \ 2) - 8
Dim BG% = RGB(BLACK)

CLS BG%

' --- Dial: outer ring + hour ticks ---
CIRCLE CX%, CY%, RAD%, 2, 1, RGB(WHITE)
Dim I%, TH!
Dim TX!, TY!
For I% = 0 To 11
    TH! = I% * 30 * 3.14159 / 180
    TX! = Sin(TH!)
    TY! = -Cos(TH!)
    LINE CX% + TX! * (RAD% - 6), CY% + TY! * (RAD% - 6), CX% + TX! * RAD%, CY% + TY! * RAD%, 2, RGB(WHITE)
Next I%

TEXT CX%, H% - 14, "analog clock - key to exit", "CT", 1, 1, RGB(CYAN)

' --- Hand tips saved so we can erase them next tick ---
Dim OHX% = CX%, OHY% = CY%   ' hour hand tip
Dim OMX% = CX%, OMY% = CY%   ' minute hand tip
Dim OSX% = CX%, OSY% = CY%   ' second hand tip

Dim HR%, MN%, SC%
Dim HA!, MA!, SA!
Dim HX%, HY%, MX%, MY%, SX%, SY%

Do
    ' Parse TIME$ = "HH:MM:SS"
    HR% = Val(Mid$(Time$, 1, 2))
    MN% = Val(Mid$(Time$, 4, 2))
    SC% = Val(Mid$(Time$, 7, 2))

    HA! = ((HR% Mod 12) + MN% / 60.0) * 30 * 3.14159 / 180
    MA! = (MN% + SC% / 60.0) * 6 * 3.14159 / 180
    SA! = SC% * 6 * 3.14159 / 180

    HX% = CX% + Sin(HA!) * RAD% * 0.55
    HY% = CY% - Cos(HA!) * RAD% * 0.55
    MX% = CX% + Sin(MA!) * RAD% * 0.80
    MY% = CY% - Cos(MA!) * RAD% * 0.80
    SX% = CX% + Sin(SA!) * RAD% * 0.90
    SY% = CY% - Cos(SA!) * RAD% * 0.90

    ' Erase previous hands.
    LINE CX%, CY%, OHX%, OHY%, 3, BG%
    LINE CX%, CY%, OMX%, OMY%, 2, BG%
    LINE CX%, CY%, OSX%, OSY%, 1, BG%

    ' Draw new.
    LINE CX%, CY%, HX%, HY%, 3, RGB(WHITE)
    LINE CX%, CY%, MX%, MY%, 2, RGB(CYAN)
    LINE CX%, CY%, SX%, SY%, 1, RGB(RED)
    CIRCLE CX%, CY%, 3, 1, 1, RGB(YELLOW), RGB(YELLOW)

    OHX% = HX% : OHY% = HY%
    OMX% = MX% : OMY% = MY%
    OSX% = SX% : OSY% = SY%

    PAUSE 200
Loop Until Inkey$ <> ""
