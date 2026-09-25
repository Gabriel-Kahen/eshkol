#!/usr/bin/env bash
set -euo pipefail

VM=${1:?usage: closure_arity_gate.sh VM SOURCE_ROOT OUTPUT_DIR}
SOURCE_ROOT=${2:?usage: closure_arity_gate.sh VM SOURCE_ROOT OUTPUT_DIR}
OUTPUT_DIR=${3:?usage: closure_arity_gate.sh VM SOURCE_ROOT OUTPUT_DIR}
CASES="$SOURCE_ROOT/tests/vm/call_arity"

mkdir -p "$OUTPUT_DIR"

run_ok() {
    local name=$1
    local expected=$2
    local source=${3:-$CASES/$name.esk}
    local log="$OUTPUT_DIR/$name.log"
    if ! ESHKOL_VM_NO_DISASM=1 "$VM" "$source" >"$log" 2>&1; then
        echo "FAIL: $name unexpectedly failed"
        cat "$log"
        exit 1
    fi
    if ! grep -Fq "$expected" "$log"; then
        echo "FAIL: $name omitted expected output: $expected"
        cat "$log"
        exit 1
    fi
}

run_rejected() {
    local name=$1
    local expected=$2
    local source=${3:-$CASES/$name.esk}
    local log="$OUTPUT_DIR/$name.log"
    if ESHKOL_VM_NO_DISASM=1 "$VM" "$source" >"$log" 2>&1; then
        echo "FAIL: $name accepted a fixed-arity mismatch"
        cat "$log"
        exit 1
    fi
    if ! grep -Fq "$expected" "$log"; then
        echo "FAIL: $name omitted expected diagnostic: $expected"
        cat "$log"
        exit 1
    fi
}

# Generate boundary cases instead of checking large, repetitive fixtures into
# Git.  named exercises define syntax; lambda exercises the separate lambda
# compiler path.  A dotted signature uses its fixed prefix as the minimum.
emit_boundary_case() {
    local name=$1 fixed=$2 actual=$3 dotted=$4 form=$5
    local source="$OUTPUT_DIR/$name.esk"
    {
        if [[ $form == named ]]; then printf '(define (f';
        else printf '(define f (lambda ('; fi
        for ((i = 0; i < fixed; i++)); do printf ' p%d' "$i"; done
        if [[ $dotted == yes ]]; then printf ' . rest'; fi
        printf ') '
        if [[ $dotted == yes ]]; then printf '(length rest)';
        else printf 'p%d' "$((fixed - 1))"; fi
        if [[ $form == named ]]; then printf ')\n'; else printf '))\n'; fi
        printf '(display (f'
        for ((i = 0; i < actual; i++)); do printf ' %d' "$i"; done
        printf '))\n'
    } >"$source"
}

run_boundary_ok() {
    local name=$1 expected=$2
    run_ok "$name" "$expected" "$OUTPUT_DIR/$name.esk"
    if ! grep -Fxq "$expected" "$OUTPUT_DIR/$name.log"; then
        echo "FAIL: $name omitted exact result $expected"
        cat "$OUTPUT_DIR/$name.log"
        exit 1
    fi
}

run_eskb_roundtrip() {
    local name=$1 expected=$2
    local source="$OUTPUT_DIR/$name.esk"
    local bytecode="$OUTPUT_DIR/$name.eskb"
    local log="$OUTPUT_DIR/$name-eskb.log"
    if ! "$VM" "$source" --emit-eskb "$bytecode" >"$OUTPUT_DIR/$name-emit.log" 2>&1 ||
       ! ESHKOL_VM_NO_DISASM=1 "$VM" "$bytecode" >"$log" 2>&1 ||
       ! grep -Fxq "$expected" "$log"; then
        echo "FAIL: $name ESKB round trip"
        cat "$log"
        exit 1
    fi
}

run_eskb_rejected() {
    local name=$1 expected=$2
    local bytecode="$OUTPUT_DIR/$name.eskb"
    rm -f "$bytecode"
    "$VM" "$OUTPUT_DIR/$name.esk" --emit-eskb "$bytecode" >"$OUTPUT_DIR/$name-emit.log" 2>&1 || :
    if [[ ! -s $bytecode ]]; then
        echo "FAIL: $name did not emit ESKB"
        cat "$OUTPUT_DIR/$name-emit.log"
        exit 1
    fi
    run_rejected "$name-eskb" "$expected" "$bytecode"
}

run_ok controls "PASS: VM fixed and variadic closure arity controls"
run_rejected min_too_few "ARITY ERROR: closure expected at least 2 arguments, got 1"
run_rejected max_too_few "ARITY ERROR: closure expected at least 2 arguments, got 1"
run_rejected min_first_class_too_few "ARITY ERROR: closure expected at least 2 arguments, got 1"
run_rejected max_first_class_too_few "ARITY ERROR: closure expected at least 2 arguments, got 1"
run_rejected fixed_call_too_few "ARITY ERROR: closure expected 1 argument, got 0"
run_rejected fixed_call_too_many "ARITY ERROR: closure expected 1 argument, got 2"
run_rejected fixed_tail_too_few "ARITY ERROR: closure expected 1 argument, got 0"
run_rejected fixed_tail_too_many "ARITY ERROR: closure expected 1 argument, got 2"
run_rejected variadic_too_few "ARITY ERROR: closure expected at least 1 argument, got 0"
run_rejected native_bridge_too_few "ARITY ERROR: closure expected 2 arguments, got 1"

emit_boundary_case fixed_255_exact 255 255 no named
emit_boundary_case fixed_255_too_many 255 256 no named
emit_boundary_case fixed_256_exact 256 256 no named
emit_boundary_case fixed_256_too_few 256 255 no named
emit_boundary_case fixed_256_too_many 256 257 no named
emit_boundary_case fixed_512_exact 512 512 no lambda
emit_boundary_case fixed_512_too_few 512 511 no lambda
emit_boundary_case variadic_255_min 255 255 yes named
emit_boundary_case variadic_255_too_few 255 254 yes named
emit_boundary_case variadic_256_min 256 256 yes named
emit_boundary_case variadic_256_extra 256 258 yes lambda
emit_boundary_case variadic_256_too_few 256 255 yes named
emit_boundary_case variadic_512_min 512 512 yes lambda
emit_boundary_case variadic_512_too_few 512 511 yes lambda

run_boundary_ok fixed_255_exact "254"
run_rejected fixed_255_too_many "ARITY ERROR: closure expected 255 arguments, got 256" "$OUTPUT_DIR/fixed_255_too_many.esk"
run_boundary_ok fixed_256_exact "255"
run_rejected fixed_256_too_few "ARITY ERROR: closure expected 256 arguments, got 255" "$OUTPUT_DIR/fixed_256_too_few.esk"
run_rejected fixed_256_too_many "ARITY ERROR: closure expected 256 arguments, got 257" "$OUTPUT_DIR/fixed_256_too_many.esk"
run_boundary_ok fixed_512_exact "511"
run_rejected fixed_512_too_few "ARITY ERROR: closure expected 512 arguments, got 511" "$OUTPUT_DIR/fixed_512_too_few.esk"
run_boundary_ok variadic_255_min "0"
run_rejected variadic_255_too_few "ARITY ERROR: closure expected at least 255 arguments, got 254" "$OUTPUT_DIR/variadic_255_too_few.esk"
run_boundary_ok variadic_256_min "0"
run_boundary_ok variadic_256_extra "2"
run_rejected variadic_256_too_few "ARITY ERROR: closure expected at least 256 arguments, got 255" "$OUTPUT_DIR/variadic_256_too_few.esk"
run_boundary_ok variadic_512_min "0"
run_rejected variadic_512_too_few "ARITY ERROR: closure expected at least 512 arguments, got 511" "$OUTPUT_DIR/variadic_512_too_few.esk"
run_eskb_roundtrip fixed_256_exact "255"
run_eskb_roundtrip variadic_256_extra "2"
run_eskb_rejected fixed_256_too_few "ARITY ERROR: closure expected 256 arguments, got 255"
run_eskb_rejected variadic_256_too_few "ARITY ERROR: closure expected at least 256 arguments, got 255"

echo "PASS: VM closure arity enforcement"
