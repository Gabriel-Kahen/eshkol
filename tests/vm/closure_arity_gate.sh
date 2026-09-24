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
    local log="$OUTPUT_DIR/$name.log"
    if ! ESHKOL_VM_NO_DISASM=1 "$VM" "$CASES/$name.esk" >"$log" 2>&1; then
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
    local log="$OUTPUT_DIR/$name.log"
    if ESHKOL_VM_NO_DISASM=1 "$VM" "$CASES/$name.esk" >"$log" 2>&1; then
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

echo "PASS: VM closure arity enforcement"
