#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd "$1" && pwd -P)
output_dir=$2
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd -P)
cd "$source_dir"

case "$(uname -s)" in
    Darwin) backend=kqueue; leak_detection=0 ;;
    Linux) backend=epoll; leak_detection=1 ;;
    *) exit 1 ;;
esac
git show -s --format='Candidate %H%nTree %T'
git diff --exit-code HEAD --
uname -sm
clang --version

common_sources=(
    tests/agent/http_sse_fixture_support.c
    lib/agent/c/agent_http_server.c
    lib/core/resource_lease.c
    lib/core/event_loop.c
    "lib/core/event_loop_${backend}.c"
)

for mode in optimized sanitized; do
    output="$output_dir/$mode"
    mkdir -p "$output"
    flags=(-O2 -g -Wall -Wextra -Werror -pthread -Iinc -Ilib/agent/c)
    if [[ $mode == sanitized ]]; then
        flags+=(-O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
        export ASAN_OPTIONS="detect_leaks=$leak_detection:halt_on_error=1"
        export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
    fi
    clang++ -std=c++17 "${flags[@]}" -c lib/core/runtime_handle.cpp -o "$output/runtime_handle.o"
    for suite in adapter_scaffold stream; do
        sources=("tests/agent/http_sse_${suite}_test.c" "${common_sources[@]}")
        suite_flags=("${flags[@]}")
        if [[ $suite == adapter_scaffold ]]; then
            suite_flags+=(-DESHKOL_SSE_ADAPTER_TESTING)
        else
            sources+=(lib/agent/c/agent_http_sse_stream.c lib/agent/c/agent_sse.c)
        fi
        objects=()
        for file in "${sources[@]}"; do
            object="$output/$(basename "$file" .c).o"
            clang -std=gnu11 "${suite_flags[@]}" -c "$file" -o "$object"
            objects+=("$object")
        done
        clang++ "${flags[@]}" "${objects[@]}" "$output/runtime_handle.o" -o "$output/$suite"
        "$output/$suite" 2>&1 | tee "$output/$suite.log"
    done
    if [[ $mode == sanitized ]]; then
        ESHKOL_SSE_MIXED_SANITIZE=1 tests/agent/run_http_sse_mixed_loop_test.sh "$source_dir" | tee "$output/mixed.log"
    else
        tests/agent/run_http_sse_mixed_loop_test.sh "$source_dir" | tee "$output/mixed.log"
    fi
done
"$output_dir/optimized/stream" --idle | tee "$output_dir/idle.log"
