' Test parameter limits up to and beyond BC_MAX_PARAMS (16)
SUB Show8(a%, b%, c%, d%, e%, f%, g%, h%)
  PRINT a%; b%; c%; d%; e%; f%; g%; h%
END SUB

FUNCTION Sum8%(a%, b%, c%, d%, e%, f%, g%, h%)
  Sum8% = a% + b% + c% + d% + e% + f% + g% + h%
END FUNCTION

SUB Show16(a%, b%, c%, d%, e%, f%, g%, h%, i%, j%, k%, l%, m%, n%, o%, p%)
  PRINT a%; b%; c%; d%; e%; f%; g%; h%; i%; j%; k%; l%; m%; n%; o%; p%
END SUB

FUNCTION Sum16%(a%, b%, c%, d%, e%, f%, g%, h%, i%, j%, k%, l%, m%, n%, o%, p%)
  Sum16% = a% + b% + c% + d% + e% + f% + g% + h% + i% + j% + k% + l% + m% + n% + o% + p%
END FUNCTION

' 17 params — exceeds BC_MAX_PARAMS, must still work via type inference fallback
SUB Show17(a%, b%, c%, d%, e%, f%, g%, h%, i%, j%, k%, l%, m%, n%, o%, p%, q%)
  PRINT a%; b%; c%; d%; e%; f%; g%; h%; i%; j%; k%; l%; m%; n%; o%; p%; q%
END SUB

FUNCTION Sum17%(a%, b%, c%, d%, e%, f%, g%, h%, i%, j%, k%, l%, m%, n%, o%, p%, q%)
  Sum17% = a% + b% + c% + d% + e% + f% + g% + h% + i% + j% + k% + l% + m% + n% + o% + p% + q%
END FUNCTION

Show8 1, 2, 3, 4, 5, 6, 7, 8
PRINT Sum8%(1, 2, 3, 4, 5, 6, 7, 8)
Show16 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
PRINT Sum16%(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)
Show17 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17
PRINT Sum17%(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17)
