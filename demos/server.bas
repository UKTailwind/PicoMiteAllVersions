' Multi-page PicoMite WebMite HTTP server.
'
' Routes:
'   /           -> index.htm   (templated)
'   /status     -> status.htm  (templated)
'   /gpio       -> generated dynamically (live MM.INFO$(PIN GPnn))
'   /files      -> generated dynamically (full A: + B: listings)
'   /about      -> about.htm   (templated)
'   /style.css  -> style.css   (raw file, text/css)
'   anything else -> 404
'
' Setup (one-time, from the prompt):
'   OPTION TCP SERVER PORT 80
' Then:
'   RUN "B:server.bas"

OPTION DEFAULT NONE
OPTION EXPLICIT
OPTION BASE 0

' --- request buffer (long-string format, as documented) -------------------
DIM INTEGER reqBuf%(2048/8)

' --- response buffer for dynamic pages (4 KB long-string) -----------------
DIM INTEGER outBuf%(4096/8)

' Shared HTML chrome we glue around dynamic content. Each fragment is < 240
' chars (MMBasic string limit). LONGSTRING APPEND copies them into outBuf%().
DIM STRING NAV$ LENGTH 240
NAV$ = "<nav><ul><li><a href='/'>Home</a></li><li><a href='/status'>Status</a></li><li><a href='/gpio'>GPIO</a></li><li><a href='/files'>Files</a></li><li><a href='/about'>About</a></li></ul></nav>"

DRIVE "B:"
WEB CONNECT
WEB TCP INTERRUPT WebRequest
PRINT "WebMite up on http://"; MM.INFO(IP ADDRESS); ":"; STR$(MM.INFO(TCP PORT)); "/"
PRINT "Routes: /, /status, /gpio, /files, /about, /style.css"
PRINT "Ctrl-C to stop."

' --- main loop -------------------------------------------------------------
' Short PAUSE so cmd_pause's busy-wait pumps the lwIP poll often.
DO
  PAUSE 5
LOOP

' ===========================================================================
SUB WebRequest
  LOCAL INTEGER pcb%, p1%
  LOCAL STRING uri$
  FOR pcb% = 1 TO MM.INFO(MAX CONNECTIONS)
    IF MM.INFO(TCP REQUEST pcb%) = 0 THEN CONTINUE FOR
    WEB TCP READ pcb%, reqBuf%()
    uri$ = MM.INFO$(TCP PATH pcb%)
    p1% = INSTR(uri$, "?")
    IF p1% > 0 THEN uri$ = LEFT$(uri$, p1% - 1)
    DO WHILE LEN(uri$) > 0 AND (RIGHT$(uri$, 1) = " " OR RIGHT$(uri$, 1) = CHR$(13) OR RIGHT$(uri$, 1) = CHR$(10))
      uri$ = LEFT$(uri$, LEN(uri$) - 1)
    LOOP
    PRINT TIME$ + "  pcb=" + STR$(pcb%) + "  uri=[" + uri$ + "]"

    SELECT CASE uri$
      CASE "/"          : WEB TRANSMIT PAGE pcb%, "B:index.htm"
      CASE "/index.htm" : WEB TRANSMIT PAGE pcb%, "B:index.htm"
      CASE "/status"    : WEB TRANSMIT PAGE pcb%, "B:status.htm"
      CASE "/status.htm": WEB TRANSMIT PAGE pcb%, "B:status.htm"
      CASE "/about"     : WEB TRANSMIT PAGE pcb%, "B:about.htm"
      CASE "/about.htm" : WEB TRANSMIT PAGE pcb%, "B:about.htm"
      CASE "/style.css" : WEB TRANSMIT FILE pcb%, "B:style.css", "text/css"
      CASE "/gpio"      : ServeGpio pcb%
      CASE "/gpio.htm"  : ServeGpio pcb%
      CASE "/files"     : ServeFiles pcb%
      CASE "/files.htm" : ServeFiles pcb%
      CASE ELSE         : WEB TRANSMIT CODE pcb%, 404
    END SELECT
  NEXT pcb%
END SUB

' ===========================================================================
' Compose a full HTTP response in outBuf%() and stream it out.
' WEB TCP SEND sends raw bytes (no headers added), so we craft them ourselves.
SUB SendResponse(pcb% AS INTEGER)
  WEB TCP SEND pcb%, outBuf%()
  WEB TCP CLOSE pcb%
END SUB

SUB BeginHTML(title$ AS STRING)
  LONGSTRING CLEAR outBuf%()
  LONGSTRING APPEND outBuf%(), "HTTP/1.0 200 OK" + CHR$(13) + CHR$(10)
  LONGSTRING APPEND outBuf%(), "Content-Type: text/html; charset=utf-8" + CHR$(13) + CHR$(10)
  LONGSTRING APPEND outBuf%(), "Connection: close" + CHR$(13) + CHR$(10) + CHR$(13) + CHR$(10)
  LONGSTRING APPEND outBuf%(), "<!DOCTYPE html><html lang='en'><head>"
  LONGSTRING APPEND outBuf%(), "<meta charset='utf-8'><title>" + title$ + "</title>"
  LONGSTRING APPEND outBuf%(), "<link rel='stylesheet' href='/style.css'>"
  LONGSTRING APPEND outBuf%(), "</head><body>"
  LONGSTRING APPEND outBuf%(), NAV$
  LONGSTRING APPEND outBuf%(), "<main>"
END SUB

SUB EndHTML
  LONGSTRING APPEND outBuf%(), "</main><footer>Served by MMBasic on RP2350B &mdash; "
  LONGSTRING APPEND outBuf%(), TIME$
  LONGSTRING APPEND outBuf%(), "</footer></body></html>"
END SUB

' ---------------------------------------------------------------------------
' Live GPIO state - each pin's MMBasic-side configuration.
SUB ServeGpio(pcb% AS INTEGER)
  LOCAL INTEGER i%
  LOCAL STRING fn$, label$
  BeginHTML "PicoMite - GPIO"
  LONGSTRING APPEND outBuf%(), "<header><h1>GPIO state</h1><p>Live MM.INFO$(PIN GPnn) for every pin (auto-refresh 5s).</p></header>"
  LONGSTRING APPEND outBuf%(), "<section>"
  FOR i% = 0 TO 47
    fn$ = MM.INFO$(PIN i%)
    IF fn$ = "" THEN fn$ = "?"
    label$ = "<span class='pin'>GP" + TRIM$(STR$(i%)) + " " + fn$ + "</span> "
    LONGSTRING APPEND outBuf%(), label$
    IF (i% + 1) MOD 8 = 0 THEN LONGSTRING APPEND outBuf%(), "<br>"
  NEXT i%
  LONGSTRING APPEND outBuf%(), "</section>"
  LONGSTRING APPEND outBuf%(), "<meta http-equiv='refresh' content='5'>"
  EndHTML
  SendResponse pcb%
END SUB

' ---------------------------------------------------------------------------
' Full A: and B: file listings, no truncation.
SUB ServeFiles(pcb% AS INTEGER)
  LOCAL STRING name$
  BeginHTML "PicoMite - Files"
  LONGSTRING APPEND outBuf%(), "<header><h1>Filesystem</h1><p>A: (internal flash) and B: (SD card)</p></header>"

  LONGSTRING APPEND outBuf%(), "<section><h2>A:</h2><pre>"
  name$ = DIR$("A:*", ALL)
  DO WHILE name$ <> ""
    LONGSTRING APPEND outBuf%(), name$ + CHR$(10)
    name$ = DIR$()
  LOOP
  LONGSTRING APPEND outBuf%(), "</pre></section>"

  LONGSTRING APPEND outBuf%(), "<section><h2>B:</h2><pre>"
  name$ = DIR$("B:*", ALL)
  DO WHILE name$ <> ""
    LONGSTRING APPEND outBuf%(), name$ + CHR$(10)
    name$ = DIR$()
  LOOP
  LONGSTRING APPEND outBuf%(), "</pre></section>"

  EndHTML
  SendResponse pcb%
END SUB
