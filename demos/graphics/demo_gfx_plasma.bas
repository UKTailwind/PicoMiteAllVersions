' demo_gfx_plasma.bas -- classic sine-wave plasma effect.
'
' Math is the same fixed-point algorithm as demo_gfx_plasma_asm.bas
' (1024-entry Q.15 sin LUT, octagonal sqrt approximation, packed RGB)
' written in plain BASIC. Every transcendental call is replaced by an
' integer table lookup so it runs reasonably even on FPU-less hardware
' under FRUN. The ASM version stays much faster — this is the fair
' apples-to-apples baseline for measuring the '!ASM speedup.
'
' Each row is rendered with a single vector BOX call (XS%, YS%, ROW%)
' to keep per-call dispatch from dominating.
'
' Memory: ~10KB (8KB sin table + three 80-int row buffers).
' Press any key to exit.

Option Explicit

Const BLK% = 4              ' plot every Nth pixel
Const SIN_LEN% = 1024
Const DT! = 0.25            ' phase advance per frame (radians)

' Fixed-point coefficients (rad * 1024/(2*Pi) * 256 = Q.8 table units).
Const COEFF_X% = 2503       ' 0.06 rad / pixel
Const COEFF_Y% = 2086       ' 0.05 rad / pixel
Const COEFF_R% = 3337       ' 0.08 rad / dist
Const PHASE_G% = 87381      ' 2.0944 rad
Const PHASE_B% = 174763     ' 4.1888 rad

Dim W% = MM.HRES, H% = MM.VRES
Dim NX% = W% \ BLK%, NY% = H% \ BLK%
Dim CX% = W% \ 2, CY% = H% \ 2

' Sin lookup: Q.15 (-32767..+32767), 1024 samples over [0, 2pi).
Dim SIN_TBL%(SIN_LEN% - 1)
Dim I%
For I% = 0 To SIN_LEN% - 1
  SIN_TBL%(I%) = Int(Sin(I% / SIN_LEN% * 2 * Pi) * 32767)
Next I%

' Per-row scratch buffers handed to vector BOX.
Dim XS%(NX% - 1), YS%(NX% - 1), ROW%(NX% - 1)
Dim BX%
For BX% = 0 To NX% - 1
  XS%(BX%) = BX% * BLK%
Next BX%

Dim T! = 0
Dim RAD_TO_Q8! = 1024.0 / (2 * Pi) * 256.0
Dim TX_FP%, TY_FP%, TR_FP%
Dim Y%, DX%, DY%, AX%, AY%, DIST%
Dim ANG_X%, ANG_Y%, ANG_R%, ANG_BASE%, ANG_C%
Dim SIN_X%, SIN_Y%, SIN_R%, SIN_C%, V2%
Dim R%, G%, B%

FRAMEBUFFER CREATE
FRAMEBUFFER WRITE F

Do
  TX_FP% = Int(T! * RAD_TO_Q8!)
  TY_FP% = Int(T! * 1.3 * RAD_TO_Q8!)
  TR_FP% = Int(T! * 0.7 * RAD_TO_Q8!)

  For Y% = 0 To H% - 1 Step BLK%
    DY% = Y% - CY%
    AY% = DY%
    If AY% < 0 Then AY% = -AY%
    ANG_Y% = DY% * COEFF_Y% + TY_FP%
    SIN_Y% = SIN_TBL%((ANG_Y% >> 8) And 1023)

    For BX% = 0 To NX% - 1
      DX% = BX% * BLK% - CX%
      AX% = DX%
      If AX% < 0 Then AX% = -AX%

      ANG_X% = DX% * COEFF_X% + TX_FP%
      SIN_X% = SIN_TBL%((ANG_X% >> 8) And 1023)

      ' octagonal sqrt approximation: max + min/2
      If AX% >= AY% Then
        DIST% = AX% + (AY% >> 1)
      Else
        DIST% = AY% + (AX% >> 1)
      EndIf

      ANG_R% = DIST% * COEFF_R% + TR_FP%
      SIN_R% = SIN_TBL%((ANG_R% >> 8) And 1023)

      V2% = (SIN_X% + SIN_Y% + SIN_R%) * 2
      ANG_BASE% = (V2% * 41721) >> 15

      SIN_C% = SIN_TBL%((ANG_BASE% >> 8) And 1023)
      R% = ((SIN_C% + 32768) * 127) >> 15

      ANG_C% = ANG_BASE% + PHASE_G%
      SIN_C% = SIN_TBL%((ANG_C% >> 8) And 1023)
      G% = ((SIN_C% + 32768) * 127) >> 15

      ANG_C% = ANG_BASE% + PHASE_B%
      SIN_C% = SIN_TBL%((ANG_C% >> 8) And 1023)
      B% = ((SIN_C% + 32768) * 127) >> 15

      ROW%(BX%) = (R% << 16) Or (G% << 8) Or B%
    Next BX%

    Math Set Y%, YS%()
    Box XS%(), YS%(), BLK%, BLK%, 0, ROW%(), ROW%()
  Next Y%

  FRAMEBUFFER COPY F, N
  T! = T! + DT!
  If T! > 2 * Pi Then T! = T! - 2 * Pi
Loop Until Inkey$ <> ""

FRAMEBUFFER WRITE N
FRAMEBUFFER CLOSE F
