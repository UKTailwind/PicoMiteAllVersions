' RUN_ARGS: --vm
' Sieve of Eratosthenes with '!ASM inline assembly inner loop
OPTION EXPLICIT

Dim sieve%(1000)

Sub ClearSieve(sieve%(), n%)
  Local i%
  '!ASM
  .const ZERO, 0
  .const ONE,  1
  .array sieve%()

      mov      i, ZERO
  .clr:
      jgt      i, n, .done
      storei.a ONE, sieve, i
      addi     i, i, ONE
      jmp      .clr
  .done:
      exit
  '!ENDASM
End Sub

Sub RunSieve(sieve%(), n%)
  Local i%, j%, limit%, tmp%, flag%
  '!ASM
  .const ONE,  1
  .const ZERO, 0
  .array sieve%()

      mov      limit, n
      mov      i, 2
  .outer:
      ; check i*i <= limit
      muli     tmp, i, i
      jgt      tmp, limit, .end
      ; if sieve%(i%) = 1
      loadi.a  flag, sieve, i
      jne      flag, ONE, .skip
      ; j = i * i
      mov      j, tmp
  .inner:
      jgt      j, limit, .skip
      storei.a ZERO, sieve, j
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

Function CountPrimes%(sieve%(), n%)
  Local i%, count%, tmp%
  '!ASM
  .const ZERO, 0
  .const ONE,  1
  .const TWO,  2
  .array sieve%()

      mov      count, ZERO
      mov      i, TWO
  .cnt:
      jgt      i, n, .done
      loadi.a  tmp, sieve, i
      jne      tmp, ONE, .nxt
      addi     count, count, ONE
  .nxt:
      addi     i, i, ONE
      jmp      .cnt
  .done:
      exit
  '!ENDASM
  CountPrimes% = count%
End Function

ClearSieve sieve%(), 1000
RunSieve sieve%(), 1000
Dim count%
count% = CountPrimes%(sieve%(), 1000)
If count% <> 168 Then ERROR "Expected 168 primes, got " + Str$(count%)
PRINT "asm sieve ok: " + Str$(count%) + " primes"
