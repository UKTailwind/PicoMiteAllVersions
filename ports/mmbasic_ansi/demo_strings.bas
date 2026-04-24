PRINT "String function demo"
PRINT "===================="
DIM s$ = "Hello, PicoCalc!"
PRINT "Original: "; s$
PRINT "Length:   "; LEN(s$)
PRINT "Upper:    "; UCASE$(s$)
PRINT "Lower:    "; LCASE$(s$)
PRINT "Left 5:   "; LEFT$(s$, 5)
PRINT "Right 9:  "; RIGHT$(s$, 9)
PRINT "Mid(8,4): "; MID$(s$, 8, 4)
PRINT "Instr ',':"; INSTR(s$, ",")
PRINT
DIM i%
PRINT "Hex/Oct/Bin of 255:"
PRINT "  HEX$ = "; HEX$(255)
PRINT "  OCT$ = "; OCT$(255)
PRINT "  BIN$ = "; BIN$(255)
PRINT
PRINT "Building a string..."
DIM result$ = ""
FOR i% = 1 TO 10
  result$ = result$ + STR$(i%) + " "
NEXT i%
PRINT result$
