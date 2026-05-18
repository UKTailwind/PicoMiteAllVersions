' MM.INFO(FONTWIDTH) / MM.INFO(FONTHEIGHT) must compile and run under
' both interpreter and VM. Regression for: bc_source.c's MM.INFO
' intercept only knew HRES/VRES and errored with "Unsupported VM
' function: MM.INFO" on everything else — which broke matrix.bas and
' every other program that uses Const CHR_W = mm.info(fontwidth) to
' compute layout from the current font.
'
' VM uses BC_SYS_MM_FONTWIDTH / BC_SYS_MM_FONTHEIGHT syscalls that
' return the live gui_font_width / gui_font_height globals.
'
' Value check is guarded: the host test harness doesn't set up gui_font
' by default (sim/REPL/WASM do), so we just require the values match
' between interpreter and VM. run_tests.sh runs both engines and
' compares output.
Dim fw%, fh%
fw% = MM.INFO(FONTWIDTH)
fh% = MM.INFO(FONTHEIGHT)
Print fw%
Print fh%
