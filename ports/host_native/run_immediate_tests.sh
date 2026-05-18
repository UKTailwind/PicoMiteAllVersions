#!/bin/bash
# Immediate mode test suite
# Tests bc_try_compile_line() and bc_run_immediate() via the host harness.

cd "$(dirname "$0")"

BINARY=${BINARY:-./build/mmbasic_test}
if [ ! -x "$BINARY" ]; then
    echo "Binary not found. Building..."
    ./build.sh
fi

PASS=0
FAIL=0
TOTAL=0

pass() {
    printf "  %-40s PASS\n" "$1"
    PASS=$((PASS + 1))
    TOTAL=$((TOTAL + 1))
}

fail() {
    printf "  %-40s FAIL: %s\n" "$1" "$2"
    FAIL=$((FAIL + 1))
    TOTAL=$((TOTAL + 1))
}

# ----------------------------------------------------------
# Section 1: try-compile tests (does it compile?)
# ----------------------------------------------------------

try_compile_yes() {
    local name="$1"
    local line="$2"
    if "$BINARY" --try-compile "$line" >/dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "expected compile success"
    fi
}

try_compile_no() {
    local name="$1"
    local line="$2"
    if "$BINARY" --try-compile "$line" >/dev/null 2>&1; then
        fail "$name" "expected compile failure"
    else
        pass "$name"
    fi
}

echo "--- Immediate Mode: try-compile ---"

# Valid BASIC statements
try_compile_yes "compile_print"          'PRINT 2+2'
try_compile_yes "compile_print_short"    '? "hello"'
try_compile_yes "compile_print_expr"     'PRINT 1+2*3'
try_compile_yes "compile_print_string"   'PRINT "test string"'
try_compile_yes "compile_files"          'FILES'
try_compile_yes "compile_files_pattern"  'FILES "*.bas"'
try_compile_yes "compile_cls"            'CLS'
try_compile_yes "compile_dim_scalar"     'DIM a%'
try_compile_yes "compile_dim_array"      'DIM arr(10)'
try_compile_yes "compile_let_assign"     'LET a = 5'
try_compile_yes "compile_assign"         'a = 5'
try_compile_yes "compile_assign_str"     'a$ = "hello"'
try_compile_yes "compile_for_next"       'FOR i = 1 TO 10 : PRINT i : NEXT'
try_compile_yes "compile_if_then"        'IF 1 > 0 THEN PRINT "yes"'
try_compile_yes "compile_mkdir"          'MKDIR "/test"'
try_compile_yes "compile_chdir"          'CHDIR "/"'
try_compile_yes "compile_drive"          'DRIVE "A:"'
try_compile_yes "compile_open"           'OPEN "/test.txt" FOR OUTPUT AS #1'
try_compile_yes "compile_pixel"          'PIXEL 10, 20, RGB(255,0,0)'
try_compile_yes "compile_circle"         'CIRCLE 100, 100, 50'
try_compile_yes "compile_box"            'BOX 10, 10, 50, 50'
try_compile_yes "compile_line"           'LINE 0, 0, 100, 100'
try_compile_yes "compile_colour"         'COLOUR 7'
try_compile_yes "compile_color"          'COLOR 7, 0'
try_compile_yes "compile_text"           'TEXT 10, 10, "hello"'
try_compile_yes "compile_pause"          'PAUSE 0'
try_compile_yes "compile_data"           'DATA 1, 2, 3'
try_compile_yes "compile_inc"            'INC a%'
try_compile_yes "compile_multi_stmt"     'a = 1 : b = 2 : PRINT a + b'
try_compile_yes "compile_end"            'END'
try_compile_yes "compile_comment"        "' this is a comment"
try_compile_yes "compile_empty"          ''

# Invalid / non-BASIC input (should fail to compile)
try_compile_no  "nocompile_for_missing_to_expr" 'FOR i = 1 TO'
try_compile_no  "nocompile_open_missing_file"   'OPEN FOR OUTPUT'
try_compile_no  "nocompile_if_missing_condition" 'IF THEN'
try_compile_no  "nocompile_bad_syntax"   'PRINT +'

echo ""

# ----------------------------------------------------------
# Section 2: immediate execution tests (correct output?)
# ----------------------------------------------------------

immediate_expect() {
    local name="$1"
    local line="$2"
    local expected="$3"
    local actual
    actual=$("$BINARY" --immediate "$line" 2>/dev/null | tr -d '\r')
    if [ "$actual" = "$expected" ]; then
        pass "$name"
    else
        fail "$name" "expected '$expected', got '$actual'"
    fi
}

immediate_expect_match() {
    local name="$1"
    local line="$2"
    local pattern="$3"
    local actual
    actual=$("$BINARY" --immediate "$line" 2>/dev/null | tr -d '\r')
    if echo "$actual" | grep -qE "$pattern"; then
        pass "$name"
    else
        fail "$name" "output did not match /$pattern/, got '$actual'"
    fi
}

immediate_expect_error() {
    local name="$1"
    local line="$2"
    if "$BINARY" --immediate "$line" >/dev/null 2>&1; then
        fail "$name" "expected error but succeeded"
    else
        pass "$name"
    fi
}

echo "--- Immediate Mode: execution ---"

# Arithmetic
immediate_expect "exec_add"              'PRINT 2+2'            ' 4'
immediate_expect "exec_mul"              'PRINT 3*7'            ' 21'
immediate_expect "exec_expr"             'PRINT 1+2*3'          ' 7'
immediate_expect "exec_div"              'PRINT 10/2'           ' 5'
immediate_expect "exec_mod"              'PRINT 17 MOD 5'       ' 2'
immediate_expect "exec_neg"              'PRINT -42'            '-42'
immediate_expect "exec_float"            'PRINT 3.14'           ' 3.14'
immediate_expect "exec_paren"            'PRINT (2+3)*4'        ' 20'

# Strings
immediate_expect "exec_str_lit"          '? "hello world"'      'hello world'
immediate_expect "exec_str_concat"       '? "abc" + "def"'      'abcdef'
immediate_expect "exec_str_len"          '? LEN("hello")'       ' 5'
immediate_expect "exec_str_left"         '? LEFT$("hello", 3)'  'hel'
immediate_expect "exec_str_right"        '? RIGHT$("hello", 3)' 'llo'
immediate_expect "exec_str_mid"          '? MID$("hello", 2, 3)' 'ell'
immediate_expect "exec_str_ucase"        '? UCASE$("hello")'    'HELLO'
immediate_expect "exec_str_lcase"        '? LCASE$("HELLO")'    'hello'
immediate_expect "exec_str_val"          '? VAL("42")'          ' 42'
immediate_expect "exec_str_str"          '? STR$(42)'           '42'
immediate_expect "exec_chr"              '? CHR$(65)'           'A'
immediate_expect "exec_asc"              '? ASC("A")'           ' 65'
immediate_expect "exec_instr"            '? INSTR("hello", "ll")' ' 3'
immediate_expect "exec_hex"             '? HEX$(255)'           'FF'
immediate_expect "exec_space"            '? SPACE$(3) + "x"'    '   x'

# Math functions
immediate_expect "exec_abs"              'PRINT ABS(-5)'        ' 5'
immediate_expect "exec_sgn_pos"          'PRINT SGN(42)'        ' 1'
immediate_expect "exec_sgn_neg"          'PRINT SGN(-1)'        '-1'
immediate_expect "exec_sgn_zero"         'PRINT SGN(0)'         ' 0'
immediate_expect "exec_int"              'PRINT INT(3.7)'       ' 3'
immediate_expect "exec_fix"              'PRINT FIX(-3.7)'      '-3'
immediate_expect "exec_sqr"              'PRINT SQR(25)'        ' 5'
immediate_expect "exec_max"              'PRINT MAX(3, 7)'      ' 7'
immediate_expect "exec_min"              'PRINT MIN(3, 7)'      ' 3'

# Variable assignment + print
immediate_expect "exec_var_assign"       'a = 42 : PRINT a'     ' 42'
immediate_expect "exec_var_str"          'a$ = "hi" : PRINT a$' 'hi'
immediate_expect "exec_var_float"        'a! = 3.14 : PRINT a!' ' 3.14'
immediate_expect "exec_multi_var"        'a = 1 : b = 2 : PRINT a + b' ' 3'

# Control flow (single-line)
immediate_expect "exec_if_true"          'IF 1 > 0 THEN PRINT "yes"'     'yes'
immediate_expect "exec_if_false"         'IF 0 > 1 THEN PRINT "yes"'     ''
immediate_expect "exec_for_next"         'FOR i = 1 TO 3 : PRINT i; : NEXT' ' 1 2 3'

# DIM and arrays
immediate_expect "exec_dim_use"          'DIM a(3) : a(1) = 10 : a(2) = 20 : PRINT a(1) + a(2)' ' 30'

# Type suffixes
immediate_expect "exec_int_suffix"       'a% = 42 : PRINT a%'  ' 42'

# CLS (should succeed without error)
immediate_expect "exec_cls"              'CLS'                  ''

# Semicolon separator
immediate_expect "exec_semi"             '? 1; 2; 3'            ' 1 2 3'

# Comma separator
immediate_expect_match "exec_comma"      '? 1, 2, 3'            '^ 1'

# Multiple PRINT
immediate_expect "exec_multi_print"      'PRINT "a" : PRINT "b"' "$(printf 'a\nb')"

# Error cases
immediate_expect_error "exec_div_zero"   'PRINT 1/0'

# RUN as syscall (create file on virtual FAT, then RUN it)
try_compile_yes "compile_run"            'RUN "/t.bas"'
immediate_expect "exec_run_file"         'OPEN "/t.bas" FOR OUTPUT AS #1 : PRINT #1, "PRINT 99" : CLOSE #1 : RUN "/t.bas"' ' 99'

echo ""
printf "Results: %d passed, %d failed\n" "$PASS" "$FAIL"
exit $FAIL
