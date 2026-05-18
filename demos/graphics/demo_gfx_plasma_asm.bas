' demo_gfx_plasma_asm.bas -- plasma effect with the per-pixel hot loop
' rewritten in '!ASM (OP_FAST_LOOP register micro-ops).
'
' Same visual effect as demo_gfx_plasma.bas but with two changes that
' compound for substantial speedup under FRUN:
'
'   1. The trig kernel runs in inline assembly. sin() becomes a
'      1024-entry Q.15 lookup table; sqrt(dx^2+dy^2) becomes the
'      octagonal approximation max + min/2 (~3% error). All math is
'      fixed-point integer — no float ops in the inner loop.
'
'   2. BOX is called once per scanline in vector form (XS%(), YS%(),
'      ROW%() arrays of size NX). Without batching, per-call dispatch
'      hides most of the kernel speedup. We deliberately stay row-sized
'      so total heap stays tiny (~10KB) — the alternative full-frame
'      buffers blow the device heap.
'
' Run under FRUN to execute the bytecode VM where '!ASM lives:
'   FRUN "demo_gfx_plasma_asm.bas"
'
' Press any key to exit.

Option Explicit

Const BLK% = 4              ' plot every Nth pixel
Const SIN_LEN% = 1024

Dim W% = MM.HRES, H% = MM.VRES
Dim NX% = W% \ BLK%
Dim NY% = H% \ BLK%
Dim CX% = W% \ 2, CY% = H% \ 2

' Sin lookup: Q.15 (-32767..+32767), 1024 samples over [0, 2pi).
Dim SIN_TBL%(SIN_LEN% - 1)
Dim I%
For I% = 0 To SIN_LEN% - 1
  SIN_TBL%(I%) = Int(Sin(I% / SIN_LEN% * 2 * Pi) * 32767)
Next I%

' Per-row scratch buffers (filled by ASM, then handed to vector BOX).
Dim XS%(NX% - 1), YS%(NX% - 1), ROW%(NX% - 1)
Dim BX%
For BX% = 0 To NX% - 1
  XS%(BX%) = BX% * BLK%
Next BX%

Dim T! = 0
Dim DT! = 0.25
' RAD_TO_Q8 = (1024 / (2*Pi)) * 256 = 41722.96 — convert radians to
' Q.8 table units (1 table unit = 1/1024 of a full revolution).
Dim RAD_TO_Q8! = 1024.0 / (2 * Pi) * 256.0
Dim TX_FP%, TY_FP%, TR_FP%
Dim BY%, PY%, DY%

FRAMEBUFFER CREATE
FRAMEBUFFER WRITE F

Do
  TX_FP% = Int(T! * RAD_TO_Q8!)
  TY_FP% = Int(T! * 1.3 * RAD_TO_Q8!)
  TR_FP% = Int(T! * 0.7 * RAD_TO_Q8!)

  For BY% = 0 To NY% - 1
    PY% = BY% * BLK%
    DY% = PY% - CY%
    PlasmaRow ROW%(), SIN_TBL%(), NX%, BLK%, CX%, DY%, TX_FP%, TY_FP%, TR_FP%
    Math Set PY%, YS%()
    Box XS%(), YS%(), BLK%, BLK%, 0, ROW%(), ROW%()
  Next BY%

  FRAMEBUFFER COPY F, N
  T! = T! + DT!
  If T! > 2 * Pi Then T! = T! - 2 * Pi
Loop Until Inkey$ <> ""

FRAMEBUFFER WRITE N
FRAMEBUFFER CLOSE F

' --------------------------------------------------------------------
' Per-row plasma kernel. Fills row%(0..nx-1) with packed 0xRRGGBB.
' All math is fixed-point integer; sin() is a table lookup.
' Locals + constants together must stay under MAX_FAST_REGS (64).
' --------------------------------------------------------------------
Sub PlasmaRow(row%(), sintbl%(), nx%, blk%, cx%, dy%, tx_fp%, ty_fp%, tr_fp%)
  Local x%, sx%, dx%, ax%, ay%, dist%
  Local ang_x%, ang_y%, ang_r%, ang_base%, ang_c%, idx%, tmp%
  Local sin_x%, sin_y%, sin_r%, sin_c%, vsum%, v2%
  Local r%, g%, b%, color%

  '!ASM
  .const ZERO,         0
  .const ONE,          1
  .const BITS8,        8
  .const BITS15,       15
  .const BITS16,       16
  .const MASK1023,     1023
  .const OFF32768,     32768
  .const C127,         127
  ; Per-axis radian->Q.8-table-units multipliers (rad * 1024/(2*Pi) * 256).
  .const COEFF_X,      2503     ; 0.06 rad / pixel
  .const COEFF_Y,      2086     ; 0.05 rad / pixel
  .const COEFF_R,      3337     ; 0.08 rad / dist
  ; Color phases in Q.8 table units (2.0944 and 4.1888 radians).
  .const PHASE_G,      87381
  .const PHASE_B,      174763
  ; Convert V*2 (Q.15 radians) to Q.8 table units in one mulshr:
  ;   ang_q8 = (V2 * 41721) >> 15 = V2 * (1024/(2*Pi)) / 128
  .const RAD_Q15_TO_Q8, 41721
  .array sintbl%()
  .array row%()

      ; sin_y depends only on dy/ty_fp — compute once for the whole row.
      muli  ang_y, dy, COEFF_Y
      addi  ang_y, ang_y, ty_fp
      shr   tmp, ang_y, BITS8
      and   idx, tmp, MASK1023
      loadi.a sin_y, sintbl, idx

      ; |dy|
      mov   ay, dy
      jge   ay, ZERO, .ay_pos
      negi  ay, ay
  .ay_pos:

      mov   x, ZERO
  .col:
      jge   x, nx, .done
      muli  sx, x, blk
      subi  dx, sx, cx

      ; sin_x = sin(dx*COEFF_X + tx_fp)
      muli  ang_x, dx, COEFF_X
      addi  ang_x, ang_x, tx_fp
      shr   tmp, ang_x, BITS8
      and   idx, tmp, MASK1023
      loadi.a sin_x, sintbl, idx

      ; |dx|
      mov   ax, dx
      jge   ax, ZERO, .ax_pos
      negi  ax, ax
  .ax_pos:

      ; dist = max(|dx|,|dy|) + min(|dx|,|dy|) / 2  (octagonal sqrt approx)
      jge   ax, ay, .ax_big
      shr   tmp, ax, ONE
      addi  dist, ay, tmp
      jmp   .have_dist
  .ax_big:
      shr   tmp, ay, ONE
      addi  dist, ax, tmp
  .have_dist:

      ; sin_r = sin(dist * COEFF_R + tr_fp)
      muli  ang_r, dist, COEFF_R
      addi  ang_r, ang_r, tr_fp
      shr   tmp, ang_r, BITS8
      and   idx, tmp, MASK1023
      loadi.a sin_r, sintbl, idx

      ; vsum = sin_x + sin_y + sin_r   (Q.15 each)
      addi  vsum, sin_x, sin_y
      addi  vsum, vsum, sin_r
      ; v2 = vsum * 2  (Q.15 radians)
      addi  v2, vsum, vsum

      ; ang_base = (v2 * 41721) >> 15  — Q.8 table units, computed once.
      mulshr ang_base, v2, RAD_Q15_TO_Q8, BITS15

      ; --- RED: phase 0 ---
      shr   tmp, ang_base, BITS8
      and   idx, tmp, MASK1023
      loadi.a sin_c, sintbl, idx
      addi  tmp, sin_c, OFF32768
      muli  tmp, tmp, C127
      shr   r, tmp, BITS15

      ; --- GREEN: phase G ---
      addi  ang_c, ang_base, PHASE_G
      shr   tmp, ang_c, BITS8
      and   idx, tmp, MASK1023
      loadi.a sin_c, sintbl, idx
      addi  tmp, sin_c, OFF32768
      muli  tmp, tmp, C127
      shr   g, tmp, BITS15

      ; --- BLUE: phase B ---
      addi  ang_c, ang_base, PHASE_B
      shr   tmp, ang_c, BITS8
      and   idx, tmp, MASK1023
      loadi.a sin_c, sintbl, idx
      addi  tmp, sin_c, OFF32768
      muli  tmp, tmp, C127
      shr   b, tmp, BITS15

      ; color = (r << 16) | (g << 8) | b
      shl   tmp, r, BITS16
      shl   color, g, BITS8
      or    color, color, tmp
      or    color, color, b

      storei.a color, row, x
      addi  x, x, ONE
      jmp   .col
  .done:
      checkint
      exit
  '!ENDASM
End Sub
