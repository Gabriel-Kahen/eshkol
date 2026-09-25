#!/usr/bin/env bash
# The C export thunk must be emitted after unresolved tail-body forwarders.
set -euo pipefail

run="$1"
source_root="${2:-$(cd "$(dirname "$0")/../.." && pwd)}"
work="$(mktemp -d "${TMPDIR:-/tmp}/eshkol-shared-tail.XXXXXX")"
trap 'rm -rf "$work"' EXIT

cat > "$work/caller.c" <<'C'
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include "eshkol/eshkol.h"

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    void (*init)(void *) = (void (*)(void *))dlsym(lib, "__eshkol_lib_init__");
    void *(*arena)(void) = (void *(*)(void))dlsym(lib, "get_global_arena");
    eshkol_tagged_value_t (*countdown)(eshkol_tagged_value_t, eshkol_tagged_value_t) =
        (eshkol_tagged_value_t (*)(eshkol_tagged_value_t, eshkol_tagged_value_t))
            dlsym(lib, "tail-countdown");
    if (!init || !arena || !countdown) return 2;
    init(arena());
    eshkol_tagged_value_t result = countdown(eshkol_make_int64(20000, true),
                                             eshkol_make_int64(41, true));
    if (result.type != ESHKOL_VALUE_INT64 ||
        result.flags != ESHKOL_VALUE_EXACT_FLAG || result.data.int_val != 42) {
        fprintf(stderr, "tail-countdown returned type=%u flags=%u value=%lld\n",
                result.type, result.flags, (long long)result.data.int_val);
        return 1;
    }
    return 0;
}
C

lib_ext=so
link_flags=(-ldl)
if [[ "$(uname -s)" == Darwin ]]; then
    lib_ext=dylib
    link_flags=()
fi
asan_runtime=""
if command -v ldd >/dev/null 2>&1; then
    asan_runtime="$(ldd "$run" 2>/dev/null | awk '/libasan|libclang_rt\.asan/ { print $3; exit }')"
fi
sanitizer_flags=()
if [[ -z "$asan_runtime" ]] && command -v nm >/dev/null 2>&1 &&
   nm -D "$run" 2>/dev/null | grep ' __asan_init$' >/dev/null; then
    # A statically linked ASan compiler emits an instrumented library whose
    # runtime symbols must be supplied by an instrumented dlopen host.
    # Function-type UBSan cannot verify dlsym casts across this generated DSO.
    sanitizer_flags=(-fsanitize=address,undefined -fno-sanitize=function)
fi
"${CC:-cc}" "${sanitizer_flags[@]}" -I"$source_root/inc" \
    "$work/caller.c" "${link_flags[@]}" -o "$work/caller"
for opt in 0 2; do
    (cd "$work" && "$run" --strict-types --no-stdlib -O "$opt" --shared-lib \
        --dump-ir "$source_root/tests/toolchain/fixtures/shared_lib_tail_transfer.esk" \
        -o "tail_o$opt") >"$work/build_o$opt.log" 2>&1 || {
        cat "$work/build_o$opt.log"
        exit 1
    }
    ir="$work/tail_o$opt.ll"
    lib="$work/libtail_o$opt.$lib_ext"
    test -s "$ir" && test -s "$lib"
    grep -F 'define internal %eshkol_tagged_value @tail-leaf__eshkol_tail_body' "$ir" >/dev/null
    grep -F 'call %eshkol_tagged_value @tail-leaf__eshkol_internal_abi' "$ir" >/dev/null
    grep -F 'define [2 x i64] @tail-countdown(' "$ir" >/dev/null

    # ASan-instrumented shared libraries require their runtime before dlopen.
    if [[ -n "$asan_runtime" ]]; then
        LD_PRELOAD="$asan_runtime" "$work/caller" "$lib"
    else
        "$work/caller" "$lib"
    fi
done
echo 'PASS: shared_lib_tail_transfer_test'
