' FrogFind text browser for the host-WASM proxy build.
OPTION EXPLICIT
DIM INTEGER R%(32768/8)
DIM STRING CR$,HOST$,PATH$,REQ$
DIM INTEGER P%,I%,N%,L%

CR$=CHR$(13)+CHR$(10)
HOST$="frogfind.com"
PATH$="/"

CLS
PRINT "FrogFind text browser"
IF MM.INFO(TCPIP STATUS)<>1 THEN
  PRINT "Network proxy is not available."
  PRINT "Serve this app with host/wasm_network_proxy."
  END
ENDIF

DO
  PRINT
  PRINT "GET http://";HOST$;PATH$
  WEB OPEN TCP CLIENT HOST$,80,10000
  REQ$="GET "+PATH$+" HTTP/1.0"+CR$+"Host: "+HOST$+CR$+"Connection: close"+CR$+CR$
  WEB TCP CLIENT REQUEST REQ$,R%(),10000
  WEB CLOSE TCP CLIENT

  P%=LINSTR(R%(),CR$+CR$)
  IF P%=0 THEN
    P%=1
  ELSE
    P%=P%+4
  ENDIF
  L%=LLEN(R%())
  FOR I%=P% TO L% STEP 240
    N%=L%-I%+1
    IF N%>240 THEN N%=240
    PRINT LGETSTR$(R%(),I%,N%);
  NEXT I%
  PRINT
  PRINT
  INPUT "Path (/search?q=retro) or Q"; PATH$
  IF UCASE$(PATH$)="Q" THEN END
  IF PATH$="" THEN PATH$="/"
LOOP
