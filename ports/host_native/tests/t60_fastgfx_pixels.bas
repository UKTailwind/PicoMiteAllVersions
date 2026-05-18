OPTION EXPLICIT

CONST BG% = RGB(BLACK)
CONST RED% = RGB(RED)
CONST GREEN% = RGB(GREEN)
CONST WHITE% = RGB(WHITE)

SUB DrawScene(boxX%, ballX%, boxCol%, label$)
  LOCAL cy%
  CLS BG%
  BOX boxX%, 10, 10, 10, 0, , boxCol%
  cy% = 30
  CIRCLE ballX%, cy%, 4, 0, 1.0, , RED%
  TEXT boxX%, 50, label$, "LT", , , WHITE%, BG%
END SUB

FASTGFX CREATE
FASTGFX FPS 1000

DrawScene 10, 15, RED%, "A"
FASTGFX SWAP

DrawScene 30, 40, GREEN%, "C"
FASTGFX SWAP

FASTGFX CLOSE
