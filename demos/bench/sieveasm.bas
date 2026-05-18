' Sieve of Eratosthenes — inner loop rewritten in '!ASM.
' Counterpart to sieve.bas, for comparing BASIC vs register-micro-op
' performance on the same workload. Run under FRUN to use the bytecode
' VM (which is where '!ASM executes):
'
'   FRUN "sieveasm.bas"
'
' MaxN tuning: the sieve array is INTEGER, 8 bytes per slot. 6000 is the
' default that also fits sieve.bas under FRUN. The ASM version tends to
' run several times faster than the BASIC version for the same MaxN.

OPTION EXPLICIT

Const MaxN = 6000

Dim sieve%(MaxN)
Dim t0%, t1%
Dim count%, largest%

Sub ClearSieve(s%(), n%)
  Local i%
  '!ASM
  .const ONE,  1
  .array s%()

      mov      i, 0
  .clr:
      jgt      i, n, .done
      storei.a ONE, s, i
      addi     i, i, ONE
      jmp      .clr
  .done:
      exit
  '!ENDASM
End Sub

Sub RunSieve(s%(), n%)
  Local i%, j%, limit%, tmp%, flag%
  '!ASM
  .const ZERO, 0
  .const ONE,  1
  .array s%()

      mov      limit, n
      mov      i, 2
  .outer:
      muli     tmp, i, i
      jgt      tmp, limit, .end
      loadi.a  flag, s, i
      jne      flag, ONE, .skip
      mov      j, tmp
  .inner:
      jgt      j, limit, .skip
      storei.a ZERO, s, j
      addi     j, j, i
      jmp      .inner
  .skip:
      addi     i, i, ONE
      checkint
      jmp      .outer
  .end:
      exit
  '!ENDASM
End Sub

Function Count%(s%(), n%)
  Local i%, c%, largest_l%, flag%
  '!ASM
  .const ZERO, 0
  .const ONE,  1
  .array s%()

      mov      c, ZERO
      mov      largest_l, ZERO
      mov      i, 2
  .loop:
      jgt      i, n, .done
      loadi.a  flag, s, i
      jne      flag, ONE, .nxt
      addi     c, c, ONE
      mov      largest_l, i
  .nxt:
      addi     i, i, ONE
      jmp      .loop
  .done:
      exit
  '!ENDASM
  largest% = largest_l%
  Count% = c%
End Function

t0% = TIMER

ClearSieve sieve%(), MaxN
RunSieve   sieve%(), MaxN
count% = Count%(sieve%(), MaxN)

t1% = TIMER

PRINT count%;     " primes up to"; MaxN
PRINT "largest prime ="; largest%
PRINT "elapsed"; (t1% - t0%); "ms"
