#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cc_bin="${1:-cc}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$cc_bin" -std=c11 -I"$root/inc" -c "$root/lib/core/abi_fingerprint.c" -o "$tmp/new-runtime.o"
cat > "$tmp/new-object.c" <<'EOF'
#include <stddef.h>
#include <eshkol/abi_fingerprint.h>
int main(void) { return ESHKOL_ABI_FINGERPRINT_SYMBOL == 8 ? 0 : 1; }
EOF
cat > "$tmp/old-object.c" <<'EOF'
#include <stddef.h>
extern const size_t eshkol_object_abi_v1_h8_s0_a8;
int main(void) { return eshkol_object_abi_v1_h8_s0_a8 == 8 ? 0 : 1; }
EOF
cat > "$tmp/old-runtime.c" <<'EOF'
#include <stddef.h>
const size_t eshkol_object_abi_v1_h8_s0_a8 = 8;
EOF
"$cc_bin" -std=c11 -I"$root/inc" -c "$tmp/new-object.c" -o "$tmp/new-object.o"
"$cc_bin" -std=c11 -c "$tmp/old-object.c" -o "$tmp/old-object.o"
"$cc_bin" -std=c11 -c "$tmp/old-runtime.c" -o "$tmp/old-runtime.o"

nm -g --defined-only "$tmp/new-runtime.o" | grep -q ' eshkol_object_abi_v1_h8_s0_a8_tag2$'
if nm -g --defined-only "$tmp/new-runtime.o" | grep -q ' eshkol_object_abi_v1_h8_s0_a8$'; then
    echo 'new runtime still exports old semantic guard' >&2
    exit 1
fi
"$cc_bin" "$tmp/new-object.o" "$tmp/new-runtime.o" -o "$tmp/new-new"
"$tmp/new-new"
"$cc_bin" "$tmp/old-object.o" "$tmp/old-runtime.o" -o "$tmp/old-old"
"$tmp/old-old"
if "$cc_bin" "$tmp/old-object.o" "$tmp/new-runtime.o" -o "$tmp/old-new" 2>"$tmp/old-new.err"; then
    echo 'old object linked against new runtime' >&2
    exit 1
fi
grep -q 'eshkol_object_abi_v1_h8_s0_a8' "$tmp/old-new.err"
if "$cc_bin" "$tmp/new-object.o" "$tmp/old-runtime.o" -o "$tmp/new-old" 2>"$tmp/new-old.err"; then
    echo 'new object linked against old runtime' >&2
    exit 1
fi
grep -q 'eshkol_object_abi_v1_h8_s0_a8_tag2' "$tmp/new-old.err"
