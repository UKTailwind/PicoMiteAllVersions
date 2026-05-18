#!/bin/bash
# run_optimizer_tests.sh -- bytecode peephole/superinstruction assertions.

set -e
cd "$(dirname "$0")"

BINARY=${BINARY:-./mmbasic_test}
if [ ! -x "$BINARY" ]; then
    echo "Binary not found. Building..."
    ./build.sh
fi

PASSED=0
FAILED=0
ERRORS=""

run_mulshr_test() {
    local testfile="tests/frontend/t028_mulshr_peephole.bas"
    local tmpfile
    printf "  %-30s " "t028_mulshr_peephole"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e

    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t028_mulshr_peephole ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi

    if ! grep -q "MATH_MULSHR" "$tmpfile" && ! grep -q "\\?\\?\\? (0xEE)" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t028_mulshr_peephole ---\nExpected fused MULSHR opcode in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi

    if grep -q "IDIV_I" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t028_mulshr_peephole ---\nUnexpected IDIV_I in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi

    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_mulshr_equiv_test() {
    local testfile="tests/frontend/t029_mulshr_opt_equiv.bas"
    local tmp_o0
    local tmp_o1
    printf "  %-30s " "t029_mulshr_opt_equiv"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)

    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e

    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t029_mulshr_opt_equiv ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi

    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t029_mulshr_opt_equiv ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi

    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_inc_const_test() {
    local testfile="tests/frontend/t030_inc_const_peephole.bas"
    local tmpfile
    printf "  %-30s " "t030_inc_const_peephole"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e

    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t030_inc_const_peephole ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi

    if grep -q "INC_I" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t030_inc_const_peephole ---\nUnexpected INC_I in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi

    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_inc_const_equiv_test() {
    local testfile="tests/frontend/t031_inc_const_opt_equiv.bas"
    local tmp_o0
    local tmp_o1
    printf "  %-30s " "t031_inc_const_opt_equiv"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)

    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e

    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t031_inc_const_opt_equiv ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi

    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t031_inc_const_opt_equiv ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi

    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_sqrshr_test() {
    local testfile="tests/frontend/t032_sqrshr_peephole.bas"
    local tmpfile
    printf "  %-30s " "t032_sqrshr_peephole"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t032_sqrshr_peephole ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if ! grep -q "MATH_SQRSHR" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t032_sqrshr_peephole ---\nExpected MATH_SQRSHR in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_sqrshr_equiv_test() {
    local testfile="tests/frontend/t033_sqrshr_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t033_sqrshr_opt_equiv"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t033_sqrshr_opt_equiv ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t033_sqrshr_opt_equiv ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_mulshradd_test() {
    local testfile="tests/frontend/t034_mulshradd_peephole.bas"
    local tmpfile
    printf "  %-30s " "t034_mulshradd_peephole"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t034_mulshradd_peephole ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if ! grep -q "MATH_MULSHRADD" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t034_mulshradd_peephole ---\nExpected MATH_MULSHRADD in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_mulshradd_equiv_test() {
    local testfile="tests/frontend/t035_mulshradd_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t035_mulshradd_opt_equiv"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t035_mulshradd_opt_equiv ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t035_mulshradd_opt_equiv ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_mulshr_call_sqrshr_test() {
    local testfile="tests/frontend/t036_mulshr_call_sqrshr_peephole.bas"
    local tmpfile
    printf "  %-30s " "t036_mulshr_call_sqrshr"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t036_mulshr_call_sqrshr ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if ! grep -q "MATH_SQRSHR" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t036_mulshr_call_sqrshr ---\nExpected MATH_SQRSHR in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if grep -q "MATH_MULSHR" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t036_mulshr_call_sqrshr ---\nUnexpected MATH_MULSHR in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_mulshr_call_sqrshr_equiv_test() {
    local testfile="tests/frontend/t037_mulshr_call_sqrshr_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t037_mulshr_call_sqrshr_eq"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t037_mulshr_call_sqrshr_eq ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t037_mulshr_call_sqrshr_eq ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_cmp_branch_test() {
    local testfile="tests/frontend/t038_cmp_branch_peephole.bas"
    local tmpfile
    printf "  %-30s " "t038_cmp_branch_peephole"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t038_cmp_branch_peephole ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if ! grep -q "JCMP_I" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t038_cmp_branch_peephole ---\nExpected JCMP_I in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_cmp_branch_equiv_test() {
    local testfile="tests/frontend/t039_cmp_branch_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t039_cmp_branch_opt_equiv"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t039_cmp_branch_opt_equiv ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t039_cmp_branch_opt_equiv ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_cmp_branch_float_test() {
    local testfile="tests/frontend/t040_cmp_branch_float_peephole.bas"
    local tmpfile
    printf "  %-30s " "t040_cmp_branch_float"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t040_cmp_branch_float ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if ! grep -q "JCMP_F" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t040_cmp_branch_float ---\nExpected JCMP_F in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_cmp_branch_float_equiv_test() {
    local testfile="tests/frontend/t041_cmp_branch_float_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t041_cmp_branch_float_eq"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t041_cmp_branch_float_eq ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t041_cmp_branch_float_eq ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_inc_expr_test() {
    local testfile="tests/frontend/t042_inc_expr_peephole.bas"
    local tmpfile
    printf "  %-30s " "t042_inc_expr_peephole"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t042_inc_expr_peephole ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if grep -q "INC_I" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t042_inc_expr_peephole ---\nUnexpected INC_I in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_inc_expr_equiv_test() {
    local testfile="tests/frontend/t043_inc_expr_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t043_inc_expr_opt_equiv"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t043_inc_expr_opt_equiv ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t043_inc_expr_opt_equiv ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_incf_expr_test() {
    local testfile="tests/frontend/t044_incf_expr_peephole.bas"
    local tmpfile
    printf "  %-30s " "t044_incf_expr_peephole"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t044_incf_expr_peephole ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if grep -q "INC_F" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t044_incf_expr_peephole ---\nUnexpected INC_F in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_incf_expr_equiv_test() {
    local testfile="tests/frontend/t045_incf_expr_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t045_incf_expr_opt_equiv"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t045_incf_expr_opt_equiv ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t045_incf_expr_opt_equiv ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_mov_var_test() {
    local testfile="tests/frontend/t046_mov_var_peephole.bas"
    local tmpfile
    printf "  %-30s " "t046_mov_var_peephole"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm-disasm > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t046_mov_var_peephole ---\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    if ! grep -q "MOV_VAR" "$tmpfile"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t046_mov_var_peephole ---\nExpected MOV_VAR in disassembly\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

run_mov_var_equiv_test() {
    local testfile="tests/frontend/t047_mov_var_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t047_mov_var_opt_equiv"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t047_mov_var_opt_equiv ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t047_mov_var_opt_equiv ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_inc_side_effect_equiv_test() {
    local testfile="tests/frontend/t048_inc_side_effect_opt_equiv.bas"
    local tmp_o0 tmp_o1
    printf "  %-30s " "t048_inc_side_effect_eq"
    tmp_o0=$(mktemp)
    tmp_o1=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O0 > "$tmp_o0" 2>&1
    local ec0=$?
    "$BINARY" "$testfile" --vm -O1 > "$tmp_o1" 2>&1
    local ec1=$?
    set -e
    if [ $ec0 -ne 0 ] || [ $ec1 -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t048_inc_side_effect_eq ---\n-O0 exit=$ec0\n$(cat "$tmp_o0")\n\n-O1 exit=$ec1\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    if ! cmp -s "$tmp_o0" "$tmp_o1"; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- t048_inc_side_effect_eq ---\n-O0 output:\n$(cat "$tmp_o0")\n\n-O1 output:\n$(cat "$tmp_o1")\n"
        rm -f "$tmp_o0" "$tmp_o1"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmp_o0" "$tmp_o1"
    return 0
}

run_mand_opt_smoke_test() {
    local testfile="../demos/bench/mand.bas"
    local tmpfile
    printf "  %-30s " "mand_opt_smoke"
    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --vm -O1 --keys-after-ms 50 '\x1b' --timeout-ms 1500 > "$tmpfile" 2>&1
    local ec=$?
    set -e
    if [ $ec -ne 0 ]; then
        echo "FAIL"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- mand_opt_smoke ---\nexit=$ec\n$(cat "$tmpfile")\n"
        rm -f "$tmpfile"
        return 1
    fi
    echo "PASS"
    PASSED=$((PASSED + 1))
    rm -f "$tmpfile"
    return 0
}

echo "MMBasic VM Optimizer Tests"
echo "=========================="
echo ""

run_mulshr_test || true
run_mulshr_equiv_test || true
run_inc_const_test || true
run_inc_const_equiv_test || true
run_sqrshr_test || true
run_sqrshr_equiv_test || true
run_mulshradd_test || true
run_mulshradd_equiv_test || true
run_mulshr_call_sqrshr_test || true
run_mulshr_call_sqrshr_equiv_test || true
run_cmp_branch_test || true
run_cmp_branch_equiv_test || true
run_cmp_branch_float_test || true
run_cmp_branch_float_equiv_test || true
run_inc_expr_test || true
run_inc_expr_equiv_test || true
run_incf_expr_test || true
run_incf_expr_equiv_test || true
run_mov_var_test || true
run_mov_var_equiv_test || true
run_inc_side_effect_equiv_test || true
run_mand_opt_smoke_test || true

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo -e "$ERRORS"
    exit 1
fi
