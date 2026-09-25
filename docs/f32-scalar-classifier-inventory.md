# True-binary32 scalar classifier inventory

Status: **native/FFI representation, raw LLVM/VM transport, and main-LLVM
extern-f32 reachability are implemented. Native, LLVM, and VM scalar semantics
cover classification, equality, shared formatting, and explicit promotion into the
existing f64 arithmetic, elementary-function, and numeric normalization domains. Source construction,
f32-preserving arithmetic, AD, and positive persistence remain
unsupported**.

This inventory records the exact `81298b4a9608fb92eb6f351a2eabd8392da7d9ef`
source audit used to introduce native runtime tag 11. It is intentionally narrower
than the eventual full true-binary32 acceptance inventory. A later phase must repeat
the audit against the allocator-compatible integration successor and add every LLVM
and VM disposition before claiming the complete runtime feature.

## Canonical representation

`ESHKOL_VALUE_FLOAT32` is the pure native tag 11. Canonical values have flags
exactly `ESHKOL_VALUE_INEXACT_FLAG`, zero reserved and implicit-padding bytes,
and a zero-extended binary32 word in the low 32 bits of `data.raw_val`. The
versioned inspectors reject every noncanonical combination without changing their
outputs.

Exactness and port bits must never be folded into this tag:

- `11 | 0x10 == 27`, which enters existing port-oriented encodings;
- `11 | 0x20 == 43`, which enters the deprecated `>= 32` pointer-storage range;
- existing 0x0f/0x3f masks do not provide a safe recovery rule for either value.

The public native and FFI structs remain 16 bytes, aligned to 8, with offsets
`type=0`, `flags=1`, `reserved=2`, and `data=8`. This phase changes no heap-object
layout and therefore does not change the object ABI fingerprint.

## Native pointer and lifetime dispositions

| Surface | Audited behavior for raw tag 11 | Phase disposition |
|---|---|---|
| `inc/eshkol/eshkol.h` exact pointer macros | `ESHKOL_IS_ANY_HEAP_TYPE`, `ESHKOL_IS_ANY_CALLABLE_TYPE`, and `ESHKOL_IS_ANY_PTR_TYPE` enumerate pointer tags; tag 11 is excluded. Header/subtype helpers require one of those exact tags. | Safe unchanged. Added the exact FLOAT32 predicate. |
| `inc/eshkol/eshkol.h` storage macro | `ESHKOL_IS_INT_STORAGE_TYPE` enumerates ordinary scalar/pointer tags and legacy pointer tags 32-40. | Fixed the former open-ended `>= 32` range so folded tag 43 is rejected. |
| `lib/core/runtime_arena_core.cpp` iteration-scope escape scan | The old classifier skipped only tags through CHAR and EOF, then interpreted every other payload as a possible pointer. A pointer-shaped tag-11 payload could spuriously commit a scope. | Fixed: exact FLOAT32 is pointer-free. The regression uses an actual in-scope address as a deliberately malformed payload and proves reclamation. |
| `lib/core/runtime_regions.cpp` escape/barrier/evacuation | Pointer carrying values are the explicit heap/callable/legacy set, port encodings, dual/complex pointer carriers, or multimedia 16–19. Tag 11 reaches the immediate copy path. | Safe unchanged. Focused test uses a real header-backed payload owned by an active inner region and proves tag 11 is copied byte-for-byte without evacuation. |
| `lib/core/runtime_tagged_cons.cpp` | Full-value setters/getters copy all 16 bytes. Specialized int/double/pointer helpers reject tag 11. | Safe full-value transport; specialized rejection retained. |
| `lib/ffi/eshkol_ffi.cpp` heap inspection | Header reads require exact `ESHKOL_FFI_TYPE_HEAP_PTR`. | Safe unchanged. New f32 calls use pointer inputs and canonical native validation. |
| `lib/backend/tagged_value_codegen.cpp` subtype helpers | Header reads branch on exact HEAP_PTR/CALLABLE tags. The generic base-tag helper preserves every tag `>= 8`, including 11. | Pointer-safe. The helper packs/unpacks raw LLVM f32 and validates the complete canonical tag-11 layout. |
| `lib/backend/llvm_codegen.cpp` mirrored base-tag helper | Preserves tag 11 because it passes through tags `>= 8`; object subtype reads remain behind exact pointer tests. | Pointer-safe. The main generator uses the canonical helper at declared `extern f32` return and argument boundaries; other construction surfaces remain deferred. |

Native arena retain/release accepts raw pointers rather than tagged values; there
is no separate native tagged-value GC mark/release switch. Region evacuation and
the iteration-scope scan above are the relevant native lifetime classifiers.

## Native masks and semantic dispatch

These sites do not dereference a tag-11 payload. The rows distinguish the scalar
semantics added in this phase from the remaining explicit rejection boundaries.

| Surface | Current tag-11 result | Required later action |
|---|---|---|
| `lib/core/runtime_display_hosted.cpp` | Canonical tag 11 is rendered by the shared raw-binary32 formatter using the exact widened-f64 contract; malformed tag 11 prints `#<invalid-float32>`. | Implemented. This display is not a claim that the source reader can reconstruct f32. |
| `lib/core/runtime_errors_hosted.cpp` | Canonical tag 11 reports `float32` and numeric error values use the shared formatter; malformed tag 11 reports `invalid-float32`. Folded tags 27 and 43 are not recovered through a low-nibble mask. | Implemented. |
| `lib/core/introspection.cpp` | Canonical tag 11 reports the interned `float32` type; malformed layouts remain unknown. | Implemented. |
| `lib/core/runtime_deep_equal.cpp` | Canonical same-tag values use IEEE equality after exact promotion: both zero signs compare equal and every NaN compares unequal. Cross-tag f32/int64/f64 values compare unequal. | Implemented. |
| `lib/core/runtime_hash_table.cpp` | Canonical values hash their binary32 word with both zero signs normalized to the same hash. Malformed tag 11 has a deterministic nonnumeric hash. Table copies use `memcpy` so the canonical padding bytes are preserved. | Implemented. |
| `lib/core/logic.cpp` | Canonical tag 11 uses the shared raw-binary32 formatter for fact and substitution output. | Implemented. |
| `lib/types/hott_types.cpp` | Runtime tag conversion preserves exact tag 11 without a low-bit mask; folded tags 27 and 43 remain `Value`. | Phase two records `RuntimeRep::Float32` and complete `Float32`/tag-11 round trips; source construction and general compiler lowering remain disabled. |
| `lib/core/kb_persistence.cpp` | Writer preflights the complete KB and rejects tag 11 before opening the destination, including f32 nested through cons/fact term carriers. Depth, capacity, cycle, and malformed-fact exits fail closed before file open. Reader rejects tag 11 before assigning it to the output carrier. | Implemented and pinned through O0/O2 AOT/JIT with direct, nested, and 65-wrapper signaling-NaN payloads and sentinel-file preservation. Positive encoding remains deferred. |
| `lib/backend/eskb_writer.c`, `eskb_reader.c`, and `eshkol_vm.c` | ESKB v1 has no f32 constant kind. Runtime-shaped constant tags 11 and 34 and every other unknown tag reject; the writer does so before opening the destination. VM materialization no longer converts an unknown constant to INT64, and emission refuses `VAL_FLOAT32` instead of substituting NIL or INT64. | Implemented. The accepted 59-byte NIL/INT64/F64/BOOL/STRING fixture remains byte-identical. No format version or constant ID was added. |
| `lib/backend/vm_native.c` KB save | VM facts can contain `VM_VAL_FLOAT32` tag 11 directly or inside structural nested facts, but the legacy VM KB layout has no admitted f32 encoding. Save recursively preflights all admitted term carriers, reports the unsupported value, returns false, and preserves an existing destination. | Implemented and pinned through the public VM C API with a supported-value control plus direct and nested f32 cases. There is no VM KB-load admission. |
| `lib/core/dnc_api.c`, `lib/core/inference.cpp` | DNC scalar/vector paths report or return their established type failure for tag 11. Inference numeric readers return failure; they do not consume f32 payload bits. | Add future admissions only through the canonical promotion helper. |
| `lib/core/sdnc_api.c` | Scalar tag 11 reports unsupported; heterogeneous vector reads fail instead of retaining a pre-zeroed slot. | Add an explicit policy with the accelerator phase; do not infer one from its internal float buffer. |
| `lib/core/runtime_tensor_index.cpp`, `runtime_list_helpers.cpp` | Index, tensor construction, and AD extraction report tag 11 as unsupported. No path casts f32 NaN/infinity to an integer or stores an invented zero. | Add later admissions only through the canonical conversion and a defined domain policy. |
| `lib/core/runtime_taylor.c`, `lib/core/ad_tape_builtins.c`, and AD list coercion | Tag 11 is classified as a scalar so it reaches an explicit numeric refusal rather than collection dereference; Taylor normalization/seeding/extraction, tape const/var, and list/extraction helpers reject it. | Define and test AD semantics in its later phase. |
| `lib/core/bignum.cpp`, `lib/core/rational.cpp` public tagged arithmetic | Arithmetic, comparison, gcd, and rational construction still reject tag 11 before integer/double payload fallback. `numerator` validates the full canonical f32 carrier, explicitly promotes it, and returns the same inexact DOUBLE value as its established native DOUBLE path; VM direct/stored routes now agree. `denominator` returns the same exact INT64 1 as its DOUBLE path. | These `numerator`/`denominator` compatibility paths are not a full R7RS lowest-terms fraction implementation. Other rejections need separately reviewed numeric routes. |
| `gcd`, `lcm`, `modulo`, `remainder`, `quotient` public numeric helpers | Native and VM direct/stored `modulo` and `quotient` promote canonical f32 into their inexact DOUBLE result paths. VM direct/stored `remainder` likewise promotes f32 and now raises on ±0 divisor, matching native DOUBLE and F32 behavior. Their all-INT64 and bignum VM branches remain unchanged. Malformed/folded native carriers reject before numeric coercion. Native and VM GCD/LCM admit canonical finite integral f32 within signed magnitude through checked promotion to DOUBLE and return DOUBLE; fractional, nonfinite, out-of-range, malformed, and folded carriers reject before integer conversion. VM GCD/LCM raise a catchable error on non-number, non-integral/nonfinite/out-of-range DOUBLE, INT64_MIN magnitude in the int64-only path, or overflowing LCM result before unsafe coercion. VM binary direct/stored GCD now uses its existing bignum kernel for all-exact INT64/bignum operands and normalizes a fitting answer to INT64; VM exact-wide LCM uses the reviewed bignum kernel. Native LLVM raw, tagged/stored, and dual-primal GCD/LCM now guard the same unsafe conversions; native GCD retains exact bignum support, while native LCM supports exact bignum through the reviewed tagged kernel. Valid int64-valued integral DOUBLE now returns inexact DOUBLE from native and VM direct/stored GCD/LCM; exact INT/bignum GCD routes retain exact results. Mixed bignum/DOUBLE GCD explicitly rejects on both substrates after a huge exact/inexact-zero control exposed divergent NaN/Inf results. | Bounded integral-F32 admission is implemented. INT64-only magnitude/result limits remain, mixed F32/bignum rejects, and AD operands reject; `numerator` is covered above. Exact VM integer/bignum zero errors for other helpers keep their historical fatal boundary; inexact zero errors are catchable. This numeric subset does not establish whole-f32 acceptance. |
| `lib/core/json.esk` and the VM JSON writer in `lib/backend/vm_native.c` | Generic JSON has no accepted tag-preserving f32 encoding. Native and VM serializers now reject direct or nested f32 with the same diagnostic instead of widening it to decimal or substituting JSON null. Native file writers serialize before opening the destination, and output-port writes serialize before their first write. | Implemented as negative persistence only. Supported JSON values and INT64/F64 parser results remain unchanged; no positive f32 JSON encoding was added. |
| `lib/backend/tagged_value_codegen.cpp`, `arithmetic_codegen.cpp`, `tensor_conv_codegen.cpp`, and `lib/core/model_io.cpp` | One checked lowering accepts raw LLVM f32 or a complete canonical tag-11 layout, exactly widens finite values, signed zero, and infinities, and maps every signed quiet/signaling NaN to binary64 bits `0x7ff8000000000000`. Batch/layer norm numeric gamma, beta, and epsilon use that promotion in all four- and five-argument forms. Active AD rejects exact tag 11 before creating normalization nodes. | Implemented for ordinary numeric normalization. Existing f64/int behavior remains pinned; no f32 AD carrier or f32-preserving tensor dtype is introduced. |
| `lib/core/workspace.cpp` native workspace salience | `ws-step!` closure results preserve canonical tag 11 through `cons`; finalize uses the shared checked promotion into its existing f64 softmax domain instead of substituting `0.0`. Malformed exact tag 11 raises before softmax, module salience, content, or step-count mutation. Workspace salience is nondifferentiable side-effect data: the same promotion remains valid when `ws-step!` executes while a reverse tape is active, without claiming gradients through salience. | Implemented and pinned through a direct malformed-layout atomicity test plus public `extern f32` O0/O2 AOT and cache-disabled JIT winner witnesses. Finite, signed zero, infinities, and signed quiet/signaling NaNs follow the shared promotion contract; f64/int behavior remains unchanged. Focused Release passes 5/5, the complete f32 label 43/43, and ASan+UBSan native/AOT 3/3 plus JIT 2/2 in the pinned LLVM 21.1.8 image. |
| `lib/core/system_builtins.c` integer/resource extraction and `format-relative` | The shared integer extractor rejects exact tag 11 before integer, descriptor, or resource-handle lookup/mutation. `format-relative` is split out as the one audited quantity caller: canonical f32 uses `eshkol_value_f32_to_double_v1` and then the same truncating cast as DOUBLE. Malformed exact tag 11 raises before output allocation. | Implemented for this extractor family. INT64/DOUBLE/BOOL/CHAR/other historical behavior is unchanged. Canonical f32 is not admitted as an FD, regex/line/event/LRU/HTTP/WebSocket handle, count, timeout, port, status, or formatting integer. |
| `lib/core/system_builtins.c` `format-iso8601` nanosecond quantity | The old non-DOUBLE fallback reinterpreted the low binary32 word as an integer timestamp. Canonical f32 now uses `eshkol_value_f32_to_double_v1`, requires a finite value in `[-2^63, 2^63)`, and then uses the historical DOUBLE truncation. Malformed, nonfinite, and out-of-range tag 11 raises before `gmtime`, formatting, string allocation, or wrapper-output assignment. | Implemented locally without admitting f32 through the shared integer/resource extractor. INT64, DOUBLE, and other-tag behavior is unchanged. |
| `lib/core/system_builtins.c` `allow-sleep` inhibitor handle | The local raw-payload extractor formerly let minimum-subnormal f32 bits alias live inhibitor handle 1 and clear its slot. An exact-tag-11 guard now delegates to the shared fail-closed resource extractor before handle extraction, lookup, table mutation, or the Windows execution-state call. | Implemented locally. The original raw extraction and every non-f32 branch remain byte-for-byte unchanged, including historical DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `process-wait` PID handle | The local raw-payload extractor formerly let a canonical f32 word alias a live child PID and reap it through `waitpid` or the Windows process APIs. An exact-tag-11 guard now delegates to the shared fail-closed resource extractor before PID extraction or operating-system action. | Implemented locally. The original raw extraction and every non-f32 line remain byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `poll-fd` descriptor and timeout | Both arguments formerly used raw payload extraction, so canonical f32 descriptor bits could alias a live ready pipe and f32 timeout bits entered the integer-millisecond domain. Exact-tag-11 guards now delegate to the shared fail-closed resource extractor before either extraction or `poll`. | Implemented locally for both positions. The original raw extractions and every non-f32 line remain byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `file-chmod` mode bitmask | The mode formerly used raw payload extraction, so canonical f32 word 384 silently became octal mode 0600 and mutated a real file. An exact-tag-11 guard now delegates to the shared fail-closed resource extractor before path extraction, capability evaluation, mode extraction, or `chmod`. | Implemented locally. The original raw extraction and every non-f32 POSIX/Windows line remain byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `file-lock` descriptor | The descriptor formerly used raw payload extraction, so canonical f32 made from a live descriptor acquired a real POSIX advisory lock. A first-operation exact-tag-11 guard now delegates to the shared fail-closed resource extractor before payload read, `fcntl`, result construction, or wrapper assignment. | Implemented locally for the descriptor. Every later non-f32 POSIX/Windows line remains byte-for-byte unchanged, including INT64 and historical raw DOUBLE payload behavior. Windows exact tag 11 rejects before the existing no-op path; positive lock effects were measured on pinned Ubuntu/Linux. |
| `lib/core/system_builtins.c` `file-unlock` descriptor | The descriptor formerly used raw payload extraction, so canonical f32 made from a live descriptor released its real POSIX advisory lock. A first-operation exact-tag-11 guard now delegates to the shared fail-closed resource extractor before payload read, `fcntl`, result construction, or wrapper assignment. | Implemented locally for the descriptor. Every later non-f32 POSIX/Windows line remains byte-for-byte unchanged, including INT64 and historical raw DOUBLE payload behavior. Windows exact tag 11 rejects before the existing no-op path; positive unlock effects were measured on pinned Ubuntu/Linux. |
| `lib/core/system_builtins.c` `process-kill` PID and signal | Both arguments formerly used raw payload extraction, so canonical f32 made from a live child PID targeted that process and f32 word `0x0000000f` became `SIGTERM`. Ordered exact-tag-11 guards now delegate PID and then signal to the shared fail-closed resource extractor before either payload read or operating-system action. | Implemented locally for both positions. The original raw extractions and every non-f32 POSIX/Windows line remain byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `process-kill-tree` PID and signal | Both arguments formerly used raw payload extraction before the process-group send and single-process fallback. Canonical f32 could therefore select a live group leader or become a signal number. Ordered exact-tag-11 guards now delegate PID and then signal to the shared fail-closed resource extractor before either payload read or signal attempt. | Implemented locally for both positions. The original raw extractions, group/fallback order, and every non-f32 POSIX/Windows line remain byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `process-setpgid` PID and PGID | Both arguments formerly used raw payload extraction, so canonical f32 made from a direct child's PID could move that child into a different process group. Ordered exact-tag-11 guards now delegate PID and then PGID to the shared fail-closed resource extractor before either payload read or `setpgid`. | Implemented locally for both positions. The original raw extractions and every later non-f32 POSIX/Windows line remain byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `process-read-nonblocking` descriptor and maximum byte count | Both arguments formerly used raw payload extraction. Canonical f32 made from a ready-pipe descriptor selected that fd, while binary32 word 3 became a three-byte count; both paths consumed bytes. Ordered exact-tag-11 guards now delegate descriptor and then maximum to the shared fail-closed resource extractor before either payload read, validation, `fcntl`, allocation, or `read`. | Implemented locally for both positions. The original raw extractions and every later non-f32 POSIX/Windows/WASM line remain byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `socket-send` descriptor | The descriptor formerly used raw payload extraction, so canonical f32 made from a real socket descriptor sent bytes to its peer. A first-operation exact-tag-11 guard now delegates to the shared fail-closed resource extractor before payload read, string extraction, validation, or `send`. | Implemented locally for the descriptor. Every later non-f32 POSIX/Windows/WASM line remains byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `socket-recv` descriptor and maximum byte count | Both arguments formerly used raw payload extraction. Canonical f32 made from a live receiver descriptor selected that socket, while binary32 word 3 became a three-byte maximum; both paths consumed queued bytes. Ordered exact-tag-11 guards now delegate descriptor and then maximum to the shared fail-closed resource extractor before either payload read, validation, capping, allocation, `fcntl`, or `recv`. | Implemented locally for both positions. Every later non-f32 POSIX/Windows/WASM line remains byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `socket-close` descriptor | The descriptor formerly used raw payload extraction, so canonical f32 made from a live socket endpoint closed that exact descriptor. A first-operation exact-tag-11 guard now delegates to the shared fail-closed resource extractor before payload read, sign validation, the platform branch, or `close`. | Implemented locally for the descriptor. Every later non-f32 POSIX/Windows/WASM line remains byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `term-set-scroll-region` top and bottom rows | Both coordinates formerly used raw payload extraction. Canonical f32 words 1 and 2 independently formed a valid range, returned true, and emitted DECSTBM on a real PTY. Ordered exact-tag-11 guards now delegate top and then bottom to the shared fail-closed resource extractor before either payload read, range validation, TTY check, output, flush, or true return. | Implemented locally for both positions. Every later non-f32 POSIX/Windows/WASM line remains byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `fs-watch-poll` handle | The handle formerly used raw payload extraction, so canonical f32 made from a live watcher slot returned its exact pending file-change event and advanced the saved snapshot. A first-operation exact-tag-11 guard now delegates to the shared fail-closed resource extractor before payload read, bounds/active lookup, stat, snapshot mutation, allocation, or return. | Implemented locally for the poll handle. Every later non-f32 POSIX/Windows line remains byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `fs-unwatch` handle | The handle formerly used raw payload extraction, so canonical f32 made from a live watcher slot cleared the slot with `memset`, discarded its pending event, and returned true. A first-operation exact-tag-11 guard now delegates to the shared fail-closed resource extractor before payload read, bounds/active lookup, slot clearing, or return. | Implemented locally for the unwatch handle. Every later non-f32 line remains byte-for-byte unchanged, including historical raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `string-truncate-display` maximum width | After validating the input string, the maximum formerly used raw payload extraction. Canonical f32 word 2 silently became a width of two columns and returned `".."` for `"abcdef"`; other f32 words could select incorrect early-return, suffix, prefix, and allocation branches. Exact tag 11 now delegates to the shared fail-closed integer/resource diagnostic immediately after successful input extraction and before width payload read or any later result-producing work. | Implemented locally for the maximum width. Input-string validation order and every later non-f32 line remain byte-for-byte unchanged, including historical forged raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `string-index-of` start index | After validating the haystack and string-or-character needle, the start formerly used raw payload extraction. Canonical f32 word 2 silently became start index two, changing the search result instead of rejecting the integer index-domain mismatch. Exact tag 11 now delegates to the shared fail-closed integer/resource diagnostic immediately after both text arguments validate and before start payload read, length/range handling, empty-needle return, search, or result. | Implemented locally for the start index. Haystack/needle validation precedence and every later non-f32 line remain byte-for-byte unchanged, including historical forged raw DOUBLE payload behavior. |
| `lib/core/system_builtins.c` `string-pad-left` / `string-pad-right` width | After validating the input string, the shared helper formerly read the width payload directly. Canonical f32 word 3 silently became width three and padded `"7"` to `"007"` or `"700"` instead of rejecting the integer count-domain mismatch. Exact tag 11 now delegates to the shared fail-closed integer/resource diagnostic immediately after successful input extraction and before width payload read, input length, early return, clamp, codepoint read, allocation, copy, result, or wrapper assignment. | Implemented locally for the shared width position. Invalid-input precedence and every later non-f32 line remain byte-for-byte unchanged, including historical forged raw DOUBLE width behavior. The codepoint position is covered separately below. |
| `lib/core/system_builtins.c` `string-pad-left` / `string-pad-right` codepoint | When padding was required, the shared helper read the codepoint payload directly. Canonical f32 word 48 silently became U+0030 and padded `"7"` to `"007"` or `"700"`. After input and width processing and the unchanged-width early return, exact tag 11 now delegates to the shared fail-closed diagnostic before codepoint payload read, UTF-8 fallback, allocation, copy, result, or wrapper assignment. | Implemented locally for the shared codepoint position. Invalid-input and width-first rejection remain ordered; an unused codepoint remains lazy when the width is no greater than the input byte length. INT64 and historical forged raw DOUBLE codepoint word 48 behavior is unchanged. |
| Other remaining semantic defaults | Outside this system slice; no further positive tag-11 admission is claimed. | Requires a separate reviewed slice before any broader system/runtime claim. |

The phase-one audit is exhaustive for pointer/lifetime classifiers and for the
public construction, inspection, explicit promotion, and full-value transport
surfaces named below. The semantic table records reviewed rejection/default
families but is not a claim that every numeric operation is complete. A later
phase must repeat the DOUBLE/mask/default search across `lib/core`, `lib/backend`,
and `lib/ffi` before source construction or general invocation is enabled.

## Phase-two LLVM and VM transport prerequisite

The compiler helper now has explicit raw LLVM f32 operations. `packFloat32`
bitcasts f32 to i32, zero-extends it to the 64-bit payload, and emits the
canonical tag-11 header. `unpackFloat32` validates the entire canonical layout
before reversing it without promotion through binary64: malformed constants are
rejected at code-generation time and dynamic values branch to a runtime raise.
`ensureTagged` and raw-type inspection recognize LLVM f32.
The canonical predicate checks the exact tag, flags, reserved field, implicit
padding, and zero high payload word; folded tags 27 and 43 remain invalid.
This helper is not wired into source literals. The main LLVM generator uses the
checked unpacker for classification, explicit promotion into f64 numeric
operations, and declared `extern f32` arguments. A raw f32 extern return is
packed directly as canonical tag 11. It never invents an f32 arithmetic result.

The bytecode VM has a separate immediate `VAL_FLOAT32` transport value whose
union member stores the raw 32-bit word. Versioned host-callback push/pop calls
return a frozen int32 status domain (success, invalid argument, empty stack,
type mismatch, or full stack) and round-trip all binary32 encodings without
conversion. Pop is failure-atomic for wrong types and push is failure-atomic on
a full stack. The OALR walker and parallel worker clone/publish classifiers both
treat the value as pointer-free, even when its raw word equals a live heap index.
Existing legacy integer/double host converters reject it. Native Windows/stub
profiles export rejecting link-stable calls and advertise the feature as zero.
There is deliberately no ESKB constant kind or source syntax for this value.
The later VM semantic slice admits explicitly enumerated scalar operations while
keeping raw construction and inspection in these versioned host calls.

The focused source candidate adds raw-pattern and malformed-layout LLVM tests,
dynamic checked-extraction IR verification, scalar classification and native
value-semantics tests, HoTT round trips, VM host round-trips,
distinct failure-status and full-stack atomicity checks, pointer-shaped OALR and
parallel transport tests (including an actual nested-region pop that reclaims
the same-index heap object), stub-profile ABI checks, and explicit numeric-predicate/
arithmetic rejection. Its supported LLVM 21 release and sanitizer gates remain
recorded below.

## Phase-one ABI and tests

The phase-one implementation exports:

```c
uint32_t eshkol_runtime_has_f32_scalar_v1(void);
int32_t eshkol_value_f32_from_bits_v1(
    eshkol_tagged_value_t *out, uint32_t bits);
int32_t eshkol_value_f32_to_bits_v1(
    const eshkol_tagged_value_t *value, uint32_t *out_bits);
int32_t eshkol_value_is_f32_v1(
    const eshkol_tagged_value_t *value);
int32_t eshkol_value_f32_to_double_v1(
    const eshkol_tagged_value_t *value, double *out);

int32_t eshkol_ffi_float32_from_bits_v1(
    uint32_t bits, eshkol_ffi_value_t *out);
int32_t eshkol_ffi_float32_to_bits_v1(
    const eshkol_ffi_value_t *value, uint32_t *out_bits);
int32_t eshkol_ffi_is_float32_v1(
    const eshkol_ffi_value_t *value);
int32_t eshkol_ffi_float32_to_double_v1(
    const eshkol_ffi_value_t *value, double *out);
```

The feature macro is the boolean `ESHKOL_HAS_F32_SCALAR_ABI_V1=1`. Dynamic
consumers on the supported Ubuntu 22.04/LLVM 21 lane resolve the exported probe
and require it to return one; the existing shared-library ABI harness checks that
lookup. Windows generated shared libraries do not yet guarantee retention and PE
export of an otherwise-unreferenced runtime probe and are explicitly unsupported
for this phase. The new calls use pointer/out parameters; the legacy by-value
integer, double, and boolean FFI conversions are not f32 authorities and
diagnose tag 11.

`tests/core/f32_scalar_abi_test.cpp` pins layout, canonical bytes, native/FFI
parity, failure atomicity, raw round-trips for zeros/subnormals/normals/infinities
and multiple quiet/signaling NaNs, deterministic promotion, region copying, and
tagged-cons copying. Its negative region case uses an active inner-region object,
so a mistaken pointer classification would visibly evacuate the payload.
`tests/core/f32_scalar_c_abi_test.c` compiles both public headers as C11 and links
the native calls from `libeshkol-runtime.a`. `tests/core/runtime_arena_core_test.cpp`
pins the corrected iteration-scope classification with a pointer-shaped payload.
The focused C++ test also verifies reported rejection in indexing, tensor/AD,
Taylor/tape AD, bignum/rational, DNC, inference, and SDNC paths; unsupported
paths do not consume the raw payload as an integer or substitute a numeric zero.

## Phase-three native and LLVM scalar semantics

Canonical tag 11 is now admitted by the ordinary `number?`, `real?`, `inexact?`,
and `complex?` classifications. `integer?` follows the existing inexact-number
rule by comparing the promoted value with its floor, while `exact?` remains
false. Malformed layouts are never accepted as numeric.

Accepted scalar operations explicitly unpack binary32 and extend it to binary64.
Their results use the existing f64 tagged representation. This includes unary
negation and absolute value, add/subtract/multiply/divide, modulo/remainder/
quotient, power, minimum/maximum, comparisons, square, rounding and conversion
paths, and the existing elementary-function dispatch. The basic four arithmetic
operators and every accepted binary secondary operation restrict f32 peers to
int64, f64, or f32 and raise a specific error for wider numeric-tower peers.
AD-node, dual, and complex conversion entry points also reject f32 explicitly.
Folded tags 27 and 43 are rejected before arithmetic default dispatch, and
numeric predicates classify them false. Negative f32 inputs to `sqrt` and `log` retain the
existing inexact IEEE NaN behavior rather than entering exact-value complex
promotion.

`tests/core/runtime_deep_equal_test.cpp` pins same-tag IEEE equality, signed-zero
hash agreement and hash-table lookup, NaN behavior, malformed/folded rejection,
and nested-container equality. `tests/core/f32_scalar_abi_test.cpp` pins type
names, `type-of`, display and logic parity with the shared formatter, numeric
error rendering, and invalid layout diagnostics. `tests/backend/f32_tagged_codegen_test.cpp` verifies the
canonical numeric predicate and the dynamic checked f32-to-f64 extraction IR.

## Phase-four VM scalar semantics

The bytecode VM now classifies exact `VAL_FLOAT32` tag 34 as an inexact real
number. Direct arithmetic/comparison opcodes and their first-class native
counterparts decode the stored binary32 word with `memcpy`, extend the resulting
`float` to `double`, and return the established `VAL_FLOAT` or boolean result.
The admitted unary, elementary, rounding, conversion, and secondary binary
operations match the phase-three native/LLVM set. Identity and structural
equality use IEEE comparison for same-tag f32 values: signed zeros compare equal
and NaNs do not.

Binary f32 operations accept only int64, f64, or f32 peers. Wider numeric-tower,
complex-only, dual, hyper-dual, and AD entry points reject f32 before legacy
coercion can invent zero or promote it into an unsupported carrier. VM hash
tables also reject f32 keys because their current key representation discards
the value tag and cannot preserve signed-zero and cross-type invariants. The
versioned host push/pop API remains the bit-exact construction and inspection
surface; the legacy host double pop remains unchanged. No f32 ESKB constant,
reader syntax, persistence encoding, or f32-preserving result was added.

## Main LLVM extern-f32 reachability

Declared `extern f32` calls are the first main-compiler construction and raw
inspection boundary. A raw LLVM f32 return is bitcast and packed directly into
the canonical tag-11 carrier. Passing that carrier to a declared f32 parameter
uses the checked direct unpacker; malformed dynamic layouts raise, statically
invalid layouts fail code generation, and there is no f64 round trip or numeric
fallback. A tagged f32 passed to an f64 parameter uses the existing explicit
checked promotion path.

The changed main-generator paths are limited to raw-f32 recognition in
`TypedValue`, `codegenTypedAST`, `detectValueType`, `ensureTaggedValue`, and
`typedValueToTaggedValue`; raw-f32 packing for tagged parameters; declared f32
argument unpacking; declared f64 argument promotion; and raw-f32 return packing.
The AOT and in-process JIT fixtures exercise O0 and O2 with both zeros,
minimum and maximum subnormal, minimum normal, maximum finite, both infinities,
a normal value, and quiet and signaling NaNs. They inspect every binary32 word at a C ABI boundary and then
exercise `number?`, `real?`, `inexact?`, `exact?`, numeric `type-of` tag 11,
f64-promoting arithmetic, comparison, same-tag deep equality, NaN inequality,
signed-zero hash lookup, and deterministic display. Separate AOT and
cache-disabled JIT refusal tests at O0 and O2 prove that raw f64 and int64
arguments cannot enter the declared f32 boundary through generic numeric
coercion.

Main LLVM identity and default equality now share one canonical FLOAT32
comparison rule across direct and first-class `eq?`/`eqv?` and the existing
runtime `equal?` path: exact tag 11 is required,
both layouts must be canonical, IEEE ordered equality makes the two zero signs
equal and every NaN unequal, and cross-representation comparisons are false.
Folded tags 27 and 43 are not recovered as f32. The latent literal-pattern and
case comparison callbacks route through the same helper, but source f32
literals remain unavailable, so those callbacks are not execution-reachable in
this slice. HashCodegen's raw-value storage helper packs raw LLVM f32 directly
as tag 11; backend coverage supplies raw LLVM f32 to that helper and checks all
representative bit patterns. Extern returns are already tag 11 before entering
HashCodegen. The runtime hash and key-equality helpers continue to normalize
signed zero and reject NaN equality.

The CMake integration gate is currently enabled on non-Windows hosts. The
implementation uses target-independent LLVM f32 operations, but Windows AOT/JIT
linkage and execution have not been measured by this slice and remain unverified.
The native and AOT sanitizer gate passes with ASan, UBSan, and LeakSanitizer.
The in-process JIT gate passes with ASan and UBSan, but LeakSanitizer must be
disabled because the existing `eshkol_eval_string` frontend retains parser and
macro-expander allocations after evaluation. This is a measured frontend
ownership limitation rather than an f32-specific suppression.

The formatting leaf adds one shared raw-binary32 formatter for native and VM
display/write, decimal `number->string`, `format` (`~a`, `~s`, and `~f`), logic
output, and error rendering. It pins exact boundary strings and numeric reader
round trips into DOUBLE. Non-decimal f32 `number->string` and integer-only
`~d`/`~x` reject explicitly. Main LLVM list construction and extraction copy the
complete tagged f32 carrier, so first-class and `apply` equality witnesses retain
tag 11 instead of reinterpreting the payload as integer storage.

This is a reachability, formatting, negative persistence, and numeric normalization slice, not general source construction. There is still no
f32 literal or reader spelling, ESKB/bytecode constant, persistence encoding, AD
carrier, GPU path, f32-to-complex promotion, or f32-preserving arithmetic result.
The remaining workspace/system semantic defaults require separate reviewed
slices before a complete runtime claim.

## Checked f32 promotion and numeric normalization

`TaggedValueCodegen::promoteFloat32ToDouble` is the central LLVM lowering for
raw f32 and canonical tag 11. Tagged inputs pass the complete layout validator
before their low binary32 word is read. The helper uses exact IEEE widening for
finite values, both zeros, and both infinities. It detects every NaN and selects
the fixed positive quiet binary64 NaN bit pattern
`0x7ff8000000000000`, independent of source sign, signaling state, or payload.
Both raw and tagged `ArithmeticCodegen::extractAsDouble` paths use this helper;
the affected paths contain no independent f32 `CreateFPExt`.

Batch- and layer-normalization route gamma, beta, and epsilon scalar SSA values
through the same lowering. The numeric runtime decoder separately admits only a
canonical tag-11 gamma or beta via `eshkol_value_f32_to_double_v1`; malformed
exact tag 11 returns failure instead of silently selecting the old default.
Epsilon reaches the runtime only after the checked LLVM promotion. Folded tags
27 and 43 are not recovered. When reverse AD is active, exact tag 11 in any of
the three parameter positions raises the established unsupported-AD diagnostic
before the dispatch creates its zero, count, or parameter nodes.

The native gate covers batch/layer × four/five arguments × gamma/beta/epsilon,
finite and nonfinite parameters, and f64/int controls at O0 and O2 in both AOT
and cache-disabled JIT execution. Exact host-bit witnesses cover finite values,
negative zero, both infinities, and positive/negative quiet/signaling NaNs.
Unhandled AOT tests pin the AD diagnostic; guarded AOT/JIT tests cover all 12 AD
positions. Canonical source injection cannot construct malformed tag 11, so no
fabricated malformed source carrier was added. The existing full-layout native
ABI tests remain the malformed-carrier authority. The rebuilt backend gate checks
the unordered NaN predicate, exact fixed-bit select, raw/tagged lowering, and
constant signed quiet/signaling NaN witnesses. Pinned LLVM 21.1.8 Release and
ASan+UBSan gates pass; JIT sanitizer runs disable leak detection for the existing
`eshkol_eval_string` frontend retention described above.

## Negative KB and ESKB persistence

The native KB v2 writer, the VM KB writer, and the ESKB v1 bytecode writer now
reject f32 before opening their destinations. The KB walkers cover direct and
nested logic terms with bounded ancestor-cycle checks whose exhaustion fails
closed before file open. Existing files therefore
remain unchanged after a rejected save. Native KB loading rejects type 11 before it can
publish a tagged value. ESKB loading rejects both runtime-shaped f32 tags (native
11 and VM 34) centrally, so downstream materializers cannot reinterpret their
payloads as INT64, F64, NIL, or zero.

This is a negative contract only. ESKB remains version 1 with the same five
constant kinds, and native KB remains version 2 with no f32 payload rule. The
compatibility test pins the complete accepted 59-byte ESKB artifact containing NIL,
INT64, F64, BOOL, and the string `C2`; its SHA-256 remains
`082fe6f4760000139a670b2266c2ec65e59d4a19a8f6df10353946ccd6de10c7`.
The downstream transformer C2 1.0 checkpoint codec is a separately owned format
and is untouched by this runtime change.

## Negative generic JSON persistence

The source `core.json` serializer and the VM `json-stringify-pretty` primitive
now reject f32 with `JSON serialization: float32 is unsupported in persistence`.
The source-library recursive dispatch reaches f32 inside lists and JSON objects,
and the VM additionally reaches f32 inside vectors. Neither reinterprets finite
values, infinities, or NaNs as f64 or maps them to JSON null. Native
`json-write-file` computes the complete serialization before opening its
destination, so `json-write`, `json-write-file`, and
`alist-write-json` preserve existing files when f32 rejection raises. Output-port
serialization likewise raises before its first write.

`core.json` has no source-vector encoding: every source vector already reaches
its generic unsupported-value branch before element traversal. This leaf does
not claim source-vector persistence. The VM vector serializer is an existing
supported path, and it now rejects an embedded f32 explicitly.

This remains a negative contract. Supported integers and f64 values retain their
established JSON output, and `json-parse` continues to produce INT64 or DOUBLE,
never FLOAT32. The private source-library guard accommodates the existing
substrate difference in `type-of`: main LLVM exposes numeric tag 11 while the VM
reports the name `float32`. It matches tag 11 exactly and does not recover folded
tags 27 or 43.

The LLVM source gate obtains f32 only through the canonical `extern f32` return
ABI. That ABI canonicalizes the result and has no tagged-value return form, so
this slice does not claim a source-level injection test for a malformed raw
tag-11 value. The guard still compares the raw `type-of` result to exactly 11;
the existing tag-generation gates cover the exclusion of folded tags 27 and 43.

## System integer/resource split

`format-relative` historically accepts DOUBLE and converts it with an
`(int64_t)` cast before choosing seconds, minutes, hours, or days. Canonical f32
now follows exactly that numeric contract: the runtime validates and promotes
the complete tag-11 layout through `eshkol_value_f32_to_double_v1`, then applies
the existing truncation. Finite threshold witnesses cover values on both sides
of 60, 3600, and 86400 seconds, negative input clamping, and INT64/F64 controls.
Nonfinite-to-integer behavior remains the existing C DOUBLE-cast limitation and
is inherited rather than newly specified by this leaf.

The same old `sys_extract_int64` helper also serves integer and resource domains.
Those callers must not inherit quantity promotion: binary32 raw bits can alias a
live descriptor or handle (`0x00000001`, the minimum subnormal, aliases handle
1). Exact tag 11 therefore raises at the shared extractor before regex, FD,
line-reader, event-loop, LRU, HTTP, or WebSocket lookup or mutation. The audit
also covers the helper's max-result/count/timeout/port/status and formatting
callers; their historical non-f32 behavior is preserved. `sleep-ms` and the
integer-only format directives already reject tag 11 in earlier guards.

Public `extern f32` tests create the min-subnormal alias and f32 values whose raw
bits equal actual FD and generation-tagged event-loop handles. They prove the
live regex, pipe/line-reader, event loop, LRU, and loopback HTTP resources remain
usable after catchable rejection. The WebSocket probe is service-independent:
it proves rejection before handle/timeout lookup but does not fabricate a live
WebSocket server. A native malformed-layout test additionally pins rejection
before `format-relative` output assignment and before regex-handle lookup.

`format-iso8601` is a separate numeric nanosecond quantity. Its former generic
non-DOUBLE branch silently read the binary32 payload as an integer, so `1.5f`
became `1069547520` nanoseconds. Exact tag 11 now validates through the shared
promotion authority, accepts only finite promoted values in `[-2^63, 2^63)`,
and then applies the same truncation as finite in-range DOUBLE. Public O0/O2 AOT
and cache-disabled JIT witnesses cover fractional positive/negative values,
both zero signs, signed infinities, signed quiet/signaling NaNs, and the nearest
binary32 values at both signed-64-bit endpoints. A native forked test pins exact
diagnostics and wrapper-output atomicity for malformed, nonfinite, and
out-of-range inputs. This local admission does not change the shared resource
extractor or add an f32 domain integer.

`allow-sleep` had a separate raw handle extraction outside the shared helper.
The minimum-subnormal word `0x00000001` therefore aliased the first live sleep
inhibitor and cleared its table slot. The function now checks exact tag 11 as
its first operation and delegates that case to the established fail-closed
integer/resource diagnostic. The existing raw extraction statement and every
non-f32 path remain unchanged. Public O0/O2 AOT and cache-disabled JIT witnesses
prove rejection leaves the live INT64 handle releasable exactly once. Native
canonical and malformed tag-11 tests pin the diagnostic and wrapper-output
sentinel; native controls preserve INT64 release and the historical raw DOUBLE
payload behavior.

`process-wait` had the next separate raw resource extraction. A canonical f32
constructed from an actual child PID word therefore reached `waitpid`, returned
the child's exit status, and reaped it before the original INT64 PID could be
used. The function now checks exact tag 11 as its first operation and delegates
that case to the established fail-closed integer/resource diagnostic. Public
O0/O2 AOT and cache-disabled JIT witnesses prove rejection leaves the INT64 PID
waitable with status 7. Native canonical and malformed tests pin the diagnostic
and wrapper-output sentinel; controls preserve INT64 and historical raw DOUBLE
behavior. This system operation creates no AD node and has no AD crossing.

`poll-fd` had the next raw resource and integer-timeout extractions. A canonical
f32 constructed from an actual ready pipe descriptor therefore reached `poll`
and reported readiness. Exact tag 11 in either argument now delegates to the
established fail-closed integer/resource diagnostic before either payload is
read. Public O0/O2 AOT and cache-disabled JIT witnesses make a pipe ready,
prove descriptor and timeout rejection, then prove the INT64 descriptor still
reports ready before unconditionally closing both ends. Native canonical and
malformed tests pin each argument position, diagnostic, and wrapper-output
sentinel; controls preserve INT64 and historical raw DOUBLE behavior. This
system operation creates no AD node and has no AD crossing.

`file-chmod` had the next raw integer-bitmask extraction. Canonical f32 word
384 therefore reached `chmod` as octal mode 0600 and changed a real file. Exact
tag 11 now delegates to the established fail-closed integer/resource diagnostic
as the function's first operation. Public O0/O2 AOT and cache-disabled JIT use
PID-scoped files, prove rejection preserves mode 0644, then prove the INT64
control applies mode 0600 before unconditional deletion. Native canonical and
malformed tests pin the diagnostic, wrapper-output sentinel, and filesystem
atomicity; controls preserve INT64 and historical raw DOUBLE behavior. This
system operation creates no AD node and has no AD crossing.

`process-kill` had the next raw PID and signal extractions. A canonical f32
constructed from a live child's actual PID bits therefore targeted that child,
while canonical word `0x0000000f` became `SIGTERM`. Exact tag 11 in
the PID position and then the signal position now delegates to the established
fail-closed integer/resource diagnostic before either payload is read or
the POSIX `kill` or Windows no-op branch can run. Public O0/O2 AOT and
cache-disabled JIT witnesses use readiness-handshaked signal-probe children;
bounded pipe polling proves rejection delivers no SIGTERM marker, followed by
unconditional INT64 SIGKILL, wait, and pipe-state cleanup.
Native canonical and malformed tests pin both argument positions, the exact
diagnostic, wrapper-output atomicity, and real-child signal atomicity; controls
prove INT64 and historical raw DOUBLE SIGTERM delivery. This system operation
creates no AD node and has no AD crossing.

`process-kill-tree` had the next raw PID and signal extractions before its
process-group send and single-process fallback. Exact tag 11 in the PID
position and then the signal position now delegates to the established
fail-closed integer/resource diagnostic before either payload is read or any
signal attempt occurs. The shared readiness-handshaked probe child becomes its
own process-group leader before acknowledging readiness, so public O0/O2 AOT
and cache-disabled JIT controls exercise the primary `kill(-pid, SIGTERM)`
branch. Both f32 positions deliver no marker; unconditional INT64 SIGKILL,
wait, and pipe-state cleanup follows every case. Native canonical and malformed
tests pin both positions, the exact diagnostic, wrapper-output atomicity, and
real-group signal atomicity; INT64 and historical raw DOUBLE controls prove
SIGTERM delivery. This system operation creates no AD node and has no AD
crossing.

`process-setpgid` had the next raw PID and PGID extractions. A canonical f32
constructed from a direct child's actual PID bits could therefore select that
child or a disposable process group and mutate group membership. Exact tag 11
in the PID position and then the PGID position now delegates to the established
fail-closed integer/resource diagnostic before either payload is read or
`setpgid` can run. Public O0/O2 AOT and cache-disabled JIT witnesses keep the
ownership rule valid: readiness-handshaked plain child targets and a disposable
group leader remain direct children of the Eshkol process. Before/after group
queries prove both rejection paths are atomic, supported INT64 calls perform
the intended moves, and every child is then killed and reaped by positive PID
before its host pipe state is released. Native canonical and malformed cases
pin both argument positions, the exact diagnostic, and wrapper-output
atomicity; disposable-child controls prove INT64 and historical raw DOUBLE
mutation. This system operation creates no AD node and has no AD crossing.

`process-read-nonblocking` had the next raw descriptor and maximum-byte
extractions. A canonical f32 constructed from a ready pipe's actual descriptor
bits selected that fd, while canonical binary32 word 3 became a three-byte
limit; both cases allocated output and consumed pipe contents. Exact tag 11 in
the descriptor position and then the maximum position now delegates to the
established fail-closed integer/resource diagnostic before either payload is
read, validated, or reaches `fcntl`, arena allocation, or `read`. Public O0/O2
AOT and cache-disabled JIT witnesses preload independent pipes, prove each f32
position rejects without consuming a byte, then use supported INT64 arguments
to read the complete contents before unconditional descriptor cleanup. Native
canonical and malformed tests pin both positions, the exact diagnostic,
wrapper-output sentinel, descriptor flags, and shared pipe-offset atomicity;
controls preserve INT64 and historical raw DOUBLE behavior independently in
the descriptor and maximum positions. This system operation creates no AD node
and has no AD crossing.

`socket-send` had the next public raw descriptor extraction after the intervening
string-classified `unix-socket-connect` path. A canonical f32 constructed from
a real AF_UNIX socket descriptor therefore selected that socket and sent bytes
to its peer. Exact tag 11 now delegates to the established fail-closed
integer/resource diagnostic as the function's first operation, before payload
read, string extraction, validation, or `send`. Public O0/O2 AOT and
cache-disabled JIT use a real socketpair, prove the peer remains unreadable
after f32 rejection, then prove the supported INT64 send delivers exact bytes
before unconditional two-descriptor cleanup. Native canonical and malformed
tests pin the diagnostic, wrapper-output sentinel, and peer non-readiness, then
prove the same socketpair remains usable through exact INT64 delivery; controls
also preserve historical raw DOUBLE delivery. This system
operation creates no AD node and has no AD crossing.

`socket-recv` had the next public raw descriptor and maximum-byte extractions.
A canonical f32 constructed from a live AF_UNIX receiver descriptor selected
that socket, while canonical binary32 word 3 became a three-byte maximum; both
cases allocated output and consumed queued data. Exact tag 11 in the descriptor
position and then the maximum position now delegates to the established
fail-closed integer/resource diagnostic before either payload is read or reaches
validation, capping, allocation, `fcntl`, or `recv`. Public O0/O2 AOT and
cache-disabled JIT use independent preloaded socketpairs, prove each f32
position rejects without consuming queued bytes, then use supported INT64
arguments on the same pair to receive the complete exact payload before
unconditional cleanup. Descriptor 0 remains valid. Native canonical and
malformed tests pin both positions, the exact diagnostic, wrapper-output
sentinel, receiver flags, and queue atomicity through same-pair INT64 recovery;
controls preserve INT64 and historical raw DOUBLE behavior independently in
both positions. The pinned LLVM 21.1.8 Release matrix passes 5/5, the complete
f32 label passes 53/53, the system completion regression passes 23/23 at O0 and
O2, and ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. This
system operation creates no AD node and has no AD crossing.

`socket-close` had the next public raw descriptor extraction. A canonical f32
constructed from a live AF_UNIX endpoint closed that exact descriptor, returned
true, and left it at `EBADF`. Exact tag 11 now delegates to the established
fail-closed integer/resource diagnostic as the function's first operation,
before payload read, sign validation, the platform branch, or `close`. Public
O0/O2 AOT and cache-disabled JIT use a real socketpair, prove rejection keeps
the endpoint open and usable for exact marker delivery, then close the same
endpoint with supported INT64 before unconditional peer cleanup. Descriptor 0
remains valid. Native canonical and malformed tests catch the exception in
process so they can pin its exact type/message, unchanged wrapper-output
sentinel, descriptor lifetime, same-pair marker usability, and subsequent
INT64 close; independent controls preserve real INT64 and historical raw DOUBLE
close behavior. The pinned LLVM 21.1.8 Release matrix passes 5/5, the complete
f32 label passes 53/53, the system completion regression passes 23/23 at O0 and
O2, and ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. This
system operation creates no AD node and has no AD crossing.

`term-set-scroll-region` had the next public raw integer-coordinate
extractions after `socket-close`. Canonical f32 words 1 and 2 independently
formed the accepted range, returned true, and emitted exact `ESC[1;2r` on real
PTYs. Exact tag 11 in the top position and then the bottom position now
delegates to the established fail-closed integer/resource diagnostic before
either payload read, range validation, TTY check, `printf`, flush, or true
return. Public O0/O2 AOT and cache-disabled JIT use fresh PTYs for each f32
position, prove rejection emits zero bytes while restoring stdout and closing
the PTY independently, then prove fresh supported INT64 controls emit exact
DECSTBM. Native canonical and malformed tests pin both positions, the exact
exception type/message, wrapper-output sentinel, and real-PTY no-write
atomicity; independent controls preserve INT64 and historical raw DOUBLE output
for each coordinate. The pinned LLVM 21.1.8 Release matrix passes 5/5, the
complete f32 label passes 53/53, the system completion regression passes 23/23
at O0 and O2, and ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2.
This system operation creates no AD node and has no AD crossing.

`fs-watch-poll` had the next public raw resource-handle extraction after the
intervening payload-independent terminal functions and watcher string inputs.
Canonical f32 made from a live watcher slot returned the exact pending
`change\tPATH` event and advanced the stored existence/mtime/size snapshot, so
the next supported INT64 poll returned `#f`. Exact tag 11 now delegates to the
established fail-closed integer/resource diagnostic before handle read, range
or active-slot lookup, stat, snapshot mutation, arena/string allocation, or
return. Public O0/O2 AOT and cache-disabled JIT use PID-scoped real files and
watchers: canonical rejection preserves the pending exact event for the same
supported INT64 handle, whose following poll then returns `#f`; cleanup is
bound independently and backed by an exit-time unlink. Native canonical and
malformed cases pin the exact exception type/message, unchanged wrapper-output
sentinel, and real watcher snapshot atomicity. Independent controls preserve
live-handle INT64 and historical raw DOUBLE behavior. The pinned LLVM 21.1.8
focused native/O0/O2 AOT/cache-disabled JIT matrix passes 5/5, the complete
f32 label passes 53/53, and the system completion regression passes 23/23 at
O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. This
system operation creates no AD node and has no AD crossing. Positive watcher
evidence ran on pinned Ubuntu/Linux; Windows `_stat64`, other POSIX stat
behavior, and WASM were not executed. The new guard is platform-neutral, and
all later non-f32 code remains unchanged.

`fs-unwatch` was the immediately following public raw resource-handle
extraction. Canonical f32 made from a live watcher slot cleared the slot,
discarded its pending deterministic size-change event, and returned true.
Exact tag 11 now delegates to the established fail-closed integer/resource
diagnostic before handle read, bounds or active-slot lookup, `memset`, true
return, or wrapper assignment. Public O0/O2 AOT and cache-disabled JIT use a
PID-scoped real file and watcher: canonical rejection preserves the exact
pending `change\tPATH` event for same-handle INT64 recovery, the next poll
returns `#f`, the first supported unwatch returns true, and the second returns
`#f`. Cleanup is bound independently and backed by exit-time unlink. Native
canonical and malformed cases pin the exact exception type/message, unchanged
wrapper-output sentinel, watcher lifetime, and pending-event atomicity.
Independent fresh controls preserve live-handle INT64 and historical forged
raw DOUBLE release behavior. The pinned LLVM 21.1.8 focused
native/O0/O2 AOT/cache-disabled JIT matrix passes 5/5, the complete f32 label
passes 53/53, and the system completion regression passes 23/23 at O0 and O2.
ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. This operation
creates no AD node and has no AD crossing. Positive semantics ran on pinned
Ubuntu/Linux; Windows, other POSIX systems, and WASM were not executed. The
guard is platform-neutral, and all later non-f32 code remains unchanged.

`string-truncate-display` was the next public raw numeric site after the
string-only `ansi-strip` and `string-display-width` operations. Its input
string was already validated first, but canonical f32 word 2 was then read as
the integer width two and returned `".."` for input `"abcdef"` and suffix
`".."` instead of rejecting the integer count-domain mismatch. Exact tag 11
now delegates to the established fail-closed integer/resource diagnostic
immediately after successful input extraction and before maximum payload read,
width early return, suffix extraction, prefix calculation, allocation, return,
or wrapper assignment. Public O0/O2 AOT and cache-disabled JIT prove canonical
rejection leaves a source-level output sentinel unchanged, while supported
INT64 width two and historical forged raw DOUBLE payload word two both return
exact `".."`; an INT64 width-six control returns the unchanged input. Native
canonical and malformed cases pin the exact exception type/message and
unchanged wrapper-output sentinel, with the same three independent controls;
a null-input control preserves the existing empty-string return before the f32
maximum guard.
The pinned LLVM 21.1.8 focused native/O0/O2 AOT/cache-disabled JIT matrix
passes 5/5, the complete f32 label passes 53/53, and the system completion
regression passes 23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus
cache-disabled JIT 2/2. This pure string operation creates no AD node and has
no AD crossing. Positive
evidence ran on pinned Ubuntu/Linux; Windows, other POSIX systems, and WASM were
not executed, and the VM uses a separate implementation. The guard and later
compiled-runtime string logic are platform-neutral.

`string-index-of` was the next public raw numeric site. Existing haystack
extraction, string-or-character needle extraction, and invalid-input `#f`
precedence remain first. After both text arguments validate, canonical f32 word
2 formerly became raw start index two: searching `"abcabc"` for `"bc"`
returned four instead of rejecting the integer index-domain mismatch. Exact tag
11 now delegates to the established fail-closed integer/resource diagnostic
before start payload read, haystack length or range handling, empty-needle
return, `strstr`, result, or wrapper assignment. Public O0/O2 AOT and
cache-disabled JIT prove canonical rejection leaves a source-level assignment
sentinel unchanged; supported INT64 starts zero and two return one and four,
historical forged raw DOUBLE payload word two returns four, and an empty needle
at start two returns two. Native canonical and malformed cases pin the exact
exception type/message and unchanged wrapper-output sentinel. Independent
controls cover the same indices, character-needle extraction, empty-needle
return, and invalid haystack/needle precedence with an f32 start. The pinned
LLVM 21.1.8 focused native/O0/O2 AOT/cache-disabled JIT matrix passes 5/5, the
complete f32 label passes 53/53, and the system completion regression passes
23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT
2/2. This pure string operation creates no AD node and has no AD crossing.
Evidence ran on pinned
Ubuntu/Linux; Windows, other POSIX systems, WASM, and the separate VM
implementation were not executed. The compiled-runtime operation is
platform-neutral.

The shared `eshkol_builtin_string_pad_v` width leaf preserves input-string
extraction and invalid-input `#f` precedence, then rejects exact tag 11 before
the raw width read, input length, early return, million-column clamp, codepoint
read, allocation, copy, result, or wrapper assignment. Public O0/O2 AOT and
cache-disabled JIT cover canonical width rejection through both wrappers with
independent assignment sentinels. Supported INT64 width three and historical
forged raw DOUBLE payload word three return exact `"007"` and `"700"`; invalid
input with an f32 width still returns `#f`. Native canonical and malformed cases
for both directions pin the exact exception type/message and unchanged output
sentinel, with the same independent controls and invalid-input precedence. The
pinned LLVM 21.1.8 focused native/O0/O2 AOT/cache-disabled JIT matrix passes
5/5, the complete f32 label passes 53/53, and the system completion regression
passes 23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled
JIT 2/2. This pure string operation creates no AD node and has no AD crossing.
Evidence ran on pinned Ubuntu/Linux; Windows, other POSIX systems, WASM, and
the separate VM implementation were not executed. The compiled-runtime
operation is platform-neutral.

The following codepoint leaf preserves the same input and width order, including
the unchanged return when width is no greater than the input byte length. Only
when padding is required does exact tag 11 delegate to the fail-closed
integer/resource diagnostic, before raw codepoint read, UTF-8 fallback,
allocation, copy, result, or wrapper assignment. Public tests cover both
wrappers with independent sentinels, both-f32 width-first rejection, lazy
unused codepoints, invalid-input precedence, and exact `"007"` / `"700"`
controls for INT64 and historical forged raw DOUBLE codepoint word 48. Native
canonical and malformed cases cover both directions with the exact exception
type/message and unchanged output sentinel. The pinned LLVM 21.1.8 focused
native/O0/O2 AOT/cache-disabled JIT matrix passes 5/5, the complete f32 label
passes 53/53, and the system completion regression passes 23/23 at O0 and O2.
ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2.

The next source-order leaf rejects exact tag 11 as the first operation of
`file-lock`, before descriptor payload read, `fcntl`, result construction, or
wrapper assignment. A forked child opening the same file independently proves
that rejection leaves it unlocked, while INT64 and historical raw DOUBLE
controls acquire a real POSIX advisory lock and an INT64 `file-unlock` releases
it. Native canonical and malformed layouts preserve the exact exception and an
unchanged output sentinel. The pinned LLVM 21.1.8 focused Release matrix passes
5/5, the complete f32 label passes 53/53, and the system completion regression
passes 23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled
JIT 2/2.

The final bounded source-order leaf rejects exact tag 11 as the first operation
of `file-unlock`, before descriptor payload read, `fcntl`, result construction,
or wrapper assignment. Tests establish the parent lock through INT64 and use a
forked child opening the same file to prove rejection leaves that lock held;
same-handle INT64 and historical raw DOUBLE controls release it. Native
canonical and malformed layouts preserve the exact exception and an unchanged
output sentinel. The pinned LLVM 21.1.8 focused Release matrix passes 5/5, the
complete f32 label passes 53/53, and the system completion regression passes
23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT
2/2.

The post-leaf source-order scan of direct tagged numeric payload reads in
`system_builtins.c` is closed for the bounded integer/resource positions in
this audit: no unguarded exact-tag-11 descriptor, handle, count, timeout, mode,
signal, index, width, or codepoint payload read remains. The `file-lock`,
`file-unlock`, and string-pad reads remain syntactically present but are
guard-dominated in their documented order. This bounded translation-unit
closure is not a whole-program or full-f32 claim.

## VM hash-key semantics

VM `hash-ref`, `hash-set!`, `hash-delete!`, and `hash-has-key?` accept canonical
f32 keys. Default equality requires matching VM representation tags and IEEE
binary32 equality, so positive and negative zero denote the same key, every NaN
comparison is false, and numerically equal f32 and f64 values remain distinct.
The f32 hash includes the representation tag and normalizes both zero signs to
one payload. VM hash slots retain boxed tagged `Value` objects so key/value tags
survive insertion, lookup, mutation, deletion, and region evacuation.

## Remaining acceptance boundary

This phase does not support source literals, an f32 reader round trip, f32-preserving
arithmetic results, bytecode constants, positive persistence, AD,
f32-to-complex promotion, or accelerators.
Windows generated-shared-library probe retention/export is also unsupported.
It cannot satisfy a downstream true-f32 metrics claim by itself. The full feature
still requires a compatible union with the separately owned allocator fix,
completed source/persistence phases, a repeated full classifier audit, and
downstream parity and performance evidence.

## Supported gate evidence

The representation slice was measured at commit
`db0e83b5c7e1ea40c9088643be3ede7fd173253c`, tree
`aca2ad00d69d059c1149d7d433ddb1159a8e6d30`, in the immutable image
`eshkol-checked-promotion-llvm21@sha256:f31d1db76958339e6ebd2a2f667052cdb85aeb5229914ffb10ac4fcdc6db22e6`.
The toolchain reported Clang/LLVM 21.1.8 on Ubuntu 22.04.

The release profile passed all five focused gates: the C++ and C11 f32 ABI
tests, arena pointer classification, runtime-core source placement, and the
O0/O2 generated-shared-library ABI test that resolves and calls the feature
probe. The release CTest log SHA-256 is
`7b8e6da7f135359d99d5fa1db00f025a293cc7e2afa7a6b5b72d567a2720c39d`;
the release runtime archive and f32 C++ test hashes are respectively
`4bd9d8d9bb247ab874acb4ef85d2de19f9bbf6ccf40b8e32cc999552bcf1dfbf`
and `65a520a7802d1e7dc58ce024a99d956067a0ac8fa037380bfa1a33eabe013eb3`.

A separate fresh RelWithDebInfo build with ASan and UBSan passed the four native
gates (C++ ABI, C11 ABI, arena classification, and runtime-core boundary) with
leak detection and halt-on-error enabled. Its CTest log SHA-256 is
`b33ea72b1388b4e12b365e5e41ac86bc065aaf1f9bbab1ee3c14619342a49654`;
the sanitizer runtime archive and f32 C++ test hashes are respectively
`3afa9368fdd1a70241c3eb3222fcde8fc688886604287f8f67fe44feff02ad57`
and `c48aaa6ed1d224a4877759920e0af85764f764a3312a44689a768a75a8deeef9`.
The sanitizer claim is native-only; generated shared-library execution was
measured in the release profile.

The LLVM/HoTT/VM transport slice was then measured at commit
`ceb1f746f57f7ce4bab960efbd6d40c4b857d090`, tree
`ec33f62b2ccbad8d5ba1748208eccf01508b66f1`, in the same immutable image and
toolchain. The source was mounted read-only into separate fresh Release and
RelWithDebInfo build directories. The Release CTest selection passed 8/8:
tagged f32 codegen, native and C11 scalar ABI, arena and runtime-core boundaries,
the VM C API, standalone VM tests including nested-region OALR and parallel
clone/publish transport, and the rejecting stub ABI. Its manually compiled HoTT
test passed 15/15. The Release CTest and HoTT log SHA-256 values are respectively
`25f50df71110ae8fc955909cc9fb10859014a9f37690b4e40c696fefb7f0bd76`
and `bbe65c1bf4d597e4df0c586ffd3de46e10fd5b95ca1e8db7a88513b99d6fba5f`.

The ASan+UBSan build used leak detection and halt-on-error. Six of the eight
selected CTest binaries passed, including tagged codegen, both scalar ABI tests,
both runtime boundaries, and the stub ABI; the separate HoTT test passed 15/15.
Both broad VM binaries executed their f32 assertions successfully before failing
on preserved defects outside this slice: `eshkol_vm_standalone_smoke` reports a
misaligned `VmObjectHeader` access in `vm_occurs_impl`
(`lib/backend/vm_logic.c:377`), and `test_vm_c_api` reports a 14-byte allocation
from `add_local` (`lib/backend/vm_parser.c:888`) retained by the embedded-ESKB
test. The aggregate sanitizer CTest result is therefore 6/8, not green. Its CTest
and HoTT log SHA-256 values are respectively
`6bfcc34b1f84a072773f89412929957be42189ee9d9565aa4d06bd59a9b30d40`
and `bbe65c1bf4d597e4df0c586ffd3de46e10fd5b95ca1e8db7a88513b99d6fba5f`.
The retained evidence directory also contains the exact source-input, toolchain,
log, and Release/sanitizer artifact manifests.

The phase-three scalar-semantic candidate has only a focused local Debug
measurement with LLVM 21.1.8. Its three selected tests (`f32_scalar_abi_test`,
`f32_tagged_codegen_test`, and `runtime_deep_equal_test`) pass 3/3. A supported
Release/sanitizer gate was intentionally deferred while another task owns that
shared build slot; no broader supported-build claim is made for this candidate.

The phase-four VM scalar-semantic candidate likewise has only a focused local
Debug measurement with LLVM 21.1.8. The expanded `test_vm_c_api` passes 1/1 and
covers raw host construction/inspection, the configured computed-goto bytecode
loop, admitted first-class native operations, promotion to f64, classification,
equality, rendering, and explicit rejection at wider numeric, complex, dual,
AD, and hash boundaries. The non-GCC/Clang switch fallback has matching source
changes but was not executed by this measurement. The shared supported
Release/sanitizer build remains deferred; no broad-build claim is made for this
candidate.

The formatting leaf was measured in the pinned LLVM 21.1.8 image. Release passes
18/18 focused native, VM, O0/O2 AOT, and cache-disabled JIT tests; the complete
`f32-scalar` label passes 24/24. ASan+UBSan passes 10/10 native/AOT tests with
LeakSanitizer enabled and 8/8 in-process JIT tests with leak detection disabled
for the measured parser/macro-expander retention described above. The retained
logs are under `/home/gabe/.codex/evidence/f32-formatting-20260924`.

The negative KB/ESKB persistence leaf was measured in the same pinned LLVM
21.1.8 image. The low-level byte-compatibility and malformed-reader test, internal
emitter/materializer test, public VM C API test, and O0/O2 AOT/JIT reachability
tests pass 7/7; the complete f32 label passes 26/26. ASan+UBSan passes 5/5
native/AOT tests with leak detection and 2/2 JIT tests with leak detection
disabled for the existing frontend retention described above. Evidence is under
`/home/gabe/.codex/evidence/f32-negative-persistence-20260924`.

The negative generic JSON persistence leaf was measured in the same pinned LLVM
21.1.8 image. Release passes 7/7 focused VM, O0/O2 AOT, and cache-disabled JIT
tests; the existing JSON suite passes 3/3; and the complete `f32-scalar` label
passes 32/32. ASan+UBSan passes 5/5 VM/native/AOT tests with LeakSanitizer enabled
and 2/2 in-process JIT tests with leak detection disabled for the existing
frontend retention described above. Evidence is under
`/home/gabe/.codex/evidence/f32-json-20260924`.

The system integer/resource split was measured in the same pinned LLVM 21.1.8
image. Release passes 5/5 focused native, O0/O2 AOT, and cache-disabled JIT
tests; the complete `f32-scalar` label passes 48/48; and the existing event-loop
runtime smoke passes. ASan+UBSan passes native/AOT 3/3 with LeakSanitizer enabled
and JIT 2/2 with leak detection disabled for the existing eval-string frontend
retention. Evidence is under
`/home/gabe/.codex/evidence/f32-system-int-20260924`.

The `format-iso8601` quantity leaf was measured in the same pinned LLVM 21.1.8
image. Release passes 5/5 focused native, O0/O2 AOT, and cache-disabled JIT
tests; the complete `f32-scalar` label passes 53/53; the VM date/time surface
passes all seven checks; and the native time API suite passes 15/15. ASan+UBSan
passes native/AOT 3/3 with LeakSanitizer enabled and JIT 2/2 with leak detection
disabled for the existing eval-string frontend retention. Evidence is under
`/home/gabe/.codex/evidence/f32-time-format-20260924`.

The `allow-sleep` inhibitor-handle leaf was measured in the same pinned LLVM
21.1.8 image. The extended Release system matrix passes 5/5, the complete
`f32-scalar` label passes 53/53, and the existing VM system-info regression
passes all six checks. ASan+UBSan passes native/AOT 3/3 with LeakSanitizer
enabled and JIT 2/2 with leak detection disabled for the existing eval-string
frontend retention. Evidence is under
`/home/gabe/.codex/evidence/f32-allow-sleep-20260924`.

The `process-wait` PID-handle leaf was measured in the same pinned LLVM 21.1.8
image. The extended Release system matrix passes 5/5 and proves a real child is
not reaped by canonical f32 under O0/O2 AOT or cache-disabled JIT. Native tests
cover canonical and malformed diagnostics, wrapper-output atomicity, INT64, and
historical raw DOUBLE behavior. Final full-label, regression, and sanitizer
results are recorded under
`/home/gabe/.codex/evidence/f32-process-wait-20260924`.

The `poll-fd` descriptor/timeout leaf was measured in the same pinned LLVM
21.1.8 image. The extended Release system matrix passes 5/5 with a real ready
pipe under O0/O2 AOT and cache-disabled JIT. Native tests cover canonical and
malformed layouts in both argument positions, wrapper-output atomicity, INT64,
and historical raw DOUBLE behavior. The complete f32 label passes 53/53, the
existing system completion regression passes 23/23 at O0 and O2, and
ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Results are under
`/home/gabe/.codex/evidence/f32-poll-fd-20260924`.

The `file-chmod` mode-bitmask leaf was measured in the same pinned LLVM 21.1.8
image. The extended Release system matrix passes 5/5 with PID-scoped real-file
atomicity under O0/O2 AOT and cache-disabled JIT. Native tests cover canonical
and malformed layouts, wrapper-output and filesystem atomicity, INT64, and
historical raw DOUBLE behavior. The complete f32 label passes 53/53, the
existing system completion regression passes 23/23 at O0 and O2, and
ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Evidence is under
`/home/gabe/.codex/evidence/f32-file-chmod-20260924`.

The `process-kill` PID/signal leaf was measured in the same pinned LLVM 21.1.8
image. The focused Release system matrix passes 5/5 with readiness-handshaked
signal observation and unconditional cleanup under O0/O2 AOT and
cache-disabled JIT. The complete f32 label passes 53/53, the existing system
completion regression passes 23/23 at O0 and O2, and ASan+UBSan passes
native/AOT 3/3 plus cache-disabled JIT 2/2. Native tests cover canonical and
malformed layouts in both argument positions, exact diagnostics,
wrapper-output and process atomicity, and INT64 and historical raw DOUBLE
SIGTERM delivery. The original O0/O2 counterexample is under
`/home/gabe/.codex/evidence/f32-process-kill-counterexample-20260924`; final
repair evidence is under
`/home/gabe/.codex/evidence/f32-process-kill-20260924`.

The `process-kill-tree` PID/signal leaf was measured in the same pinned LLVM
21.1.8 image. The focused Release system matrix passes 5/5 with
readiness-handshaked process-group signal observation and unconditional cleanup
under O0/O2 AOT and cache-disabled JIT. Native tests cover canonical and
malformed layouts in both argument positions, exact diagnostics,
wrapper-output and group-signal atomicity, and INT64 and historical raw DOUBLE
SIGTERM delivery. The complete f32 label passes 53/53, the existing system
completion regression passes 23/23 at O0 and O2, the standalone process-tree
regression passes, and ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT
2/2. Final evidence is under
`/home/gabe/.codex/evidence/f32-process-kill-tree-20260924`.

The `process-setpgid` PID/PGID leaf was measured in the same pinned LLVM 21.1.8
image. The focused Release system matrix passes 5/5 across native, O0/O2 AOT,
and cache-disabled O0/O2 JIT. Public witnesses prove both f32 positions preserve
direct-child group membership before supported INT64 moves and unconditional
positive-PID cleanup. Native tests cover canonical and malformed layouts in
both positions, exact diagnostics, wrapper-output atomicity, and INT64 and
historical raw DOUBLE mutation controls. The complete f32 label passes 53/53,
the existing system completion regression passes 23/23 at O0 and O2, the
standalone process-tree regression passes, and ASan+UBSan passes native/AOT
3/3 plus cache-disabled JIT 2/2. Windows behavior was not executed. Final
evidence is under
`/home/gabe/.codex/evidence/f32-process-setpgid-20260924`.

The `process-read-nonblocking` descriptor/maximum leaf was measured in the same
pinned LLVM 21.1.8 image. The Release system matrix passes 5/5 across
native, O0/O2 AOT, and cache-disabled O0/O2 JIT. Public ready-pipe witnesses
prove both f32 positions consume no bytes before a supported INT64 read receives
the complete payload and unconditional cleanup closes both descriptors. Native
tests cover canonical and malformed layouts in both positions, exact
diagnostics, wrapper-output sentinel, descriptor flags, shared pipe-offset
atomicity, and INT64 plus independent historical raw DOUBLE descriptor/maximum
controls. The complete f32 label passes 53/53, the existing system completion
regression passes 23/23 at O0 and O2, the standalone process-tree regression
passes, and ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2.
Windows behavior was not executed. Final evidence is under
`/home/gabe/.codex/evidence/f32-process-read-nonblocking-20260924`.

The `socket-send` descriptor leaf was measured in the same pinned LLVM 21.1.8
image. The Release system matrix passes 5/5 across native, O0/O2 AOT,
and cache-disabled O0/O2 JIT. Public real-socket witnesses prove canonical f32
rejection leaves the peer unreadable before supported INT64 delivery and
unconditional two-descriptor cleanup. Native tests cover canonical and malformed
layouts, the exact diagnostic, wrapper-output sentinel, peer non-readiness,
same-pair INT64 usability, and historical raw DOUBLE delivery controls. The
complete f32 label passes 53/53, the existing system completion regression
passes 23/23 at O0 and O2, and ASan+UBSan passes native/AOT 3/3 plus
cache-disabled JIT 2/2. Windows behavior was not executed. Final evidence is
under
`/home/gabe/.codex/evidence/f32-socket-send-20260924`.

The `socket-recv` descriptor/maximum leaf was measured in the same pinned LLVM
21.1.8 image. The Release system matrix passes 5/5 across native, O0/O2 AOT,
and cache-disabled O0/O2 JIT. Public independent preloaded socketpair witnesses
prove both f32 positions preserve the complete queued payload before supported
same-pair INT64 recovery and unconditional two-descriptor cleanup. Native tests
cover canonical and malformed layouts in both positions, the exact diagnostic,
wrapper-output sentinel, receiver flags, queue atomicity, and independent INT64
plus historical raw DOUBLE descriptor/maximum controls. The complete f32 label
passes 53/53, the existing system completion regression passes 23/23 at O0 and
O2, and ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Windows
and WASM behavior was not executed. Final evidence is under
`/home/gabe/.codex/evidence/f32-socket-recv-20260924`.

The `socket-close` descriptor leaf was measured in the same pinned LLVM 21.1.8
image. The Release system matrix passes 5/5 across native, O0/O2 AOT, and
cache-disabled O0/O2 JIT. Public real-socket witnesses prove f32 rejection
keeps the endpoint live and usable before a supported same-descriptor INT64
close and unconditional peer cleanup. Native canonical and malformed tests
catch the exception in process and cover its exact type/message, unchanged
wrapper output, descriptor lifetime, same-pair marker delivery, subsequent
INT64 close, and independent real INT64/historical raw DOUBLE close controls.
The complete f32 label passes 53/53, the existing system completion regression
passes 23/23 at O0 and O2, and ASan+UBSan passes native/AOT 3/3 plus
cache-disabled JIT 2/2. Windows and WASM behavior was not executed. Final
evidence is under `/home/gabe/.codex/evidence/f32-socket-close-20260924`.

The `term-set-scroll-region` top/bottom leaf was measured in the same pinned
LLVM 21.1.8 image. The Release system matrix passes 5/5 across native, O0/O2
AOT, and cache-disabled O0/O2 JIT. Public fresh-PTY witnesses prove both f32
positions reject with zero emitted bytes and restored stdout before independent
supported INT64 controls emit exact `ESC[1;2r`. Native canonical and malformed
tests cover both positions, the exact exception type/message, unchanged wrapper
output, real-PTY no-write atomicity, and independent INT64 plus historical raw
DOUBLE top/bottom controls. The complete f32 label passes 53/53, the existing
system completion regression passes 23/23 at O0 and O2, and ASan+UBSan passes
native/AOT 3/3 plus cache-disabled JIT 2/2. Positive PTY behavior was measured
on Linux; Windows, WASM, and other POSIX PTYs were not executed. Final evidence
is under `/home/gabe/.codex/evidence/f32-term-scroll-20260924`.

The `fs-watch-poll` handle leaf was measured in the same pinned LLVM 21.1.8
image. The Release system matrix passes 5/5 across native, O0/O2 AOT, and
cache-disabled O0/O2 JIT. Public real-file witnesses prove f32 rejection
preserves the exact pending change for supported same-handle INT64 recovery,
whose following poll returns `#f`. Native canonical and malformed tests cover
the exact exception type/message, unchanged wrapper output, real watcher
snapshot atomicity, and independent INT64 plus historical raw DOUBLE controls.
The complete f32 label passes 53/53, the existing system completion regression
passes 23/23 at O0 and O2, and ASan+UBSan passes native/AOT 3/3 plus
cache-disabled JIT 2/2. Positive watcher behavior was measured on Linux;
Windows `_stat64`, WASM, and other POSIX stat behavior were not executed.
Final evidence is under
`/home/gabe/.codex/evidence/f32-fs-watch-poll-20260924`.

The `fs-unwatch` handle leaf was measured in the same pinned LLVM 21.1.8
image. The Release system matrix passes 5/5 across native, O0/O2 AOT, and
cache-disabled O0/O2 JIT. Public real-file witnesses prove f32 rejection
preserves the live watcher and exact pending event for supported same-handle
INT64 recovery, followed by one true and one false unwatch. Native canonical
and malformed tests cover the exact exception type/message, unchanged wrapper
output, watcher lifetime, pending-event atomicity, and independent INT64 plus
historical raw DOUBLE release controls. The complete f32 label passes 53/53,
the existing system completion regression passes 23/23 at O0 and O2, and
ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Positive watcher
behavior was measured on Linux; Windows, WASM, and other POSIX systems were not
executed. Final evidence is under
`/home/gabe/.codex/evidence/f32-fs-unwatch-20260924`.

The `string-truncate-display` maximum-width leaf was measured in the same
pinned LLVM 21.1.8 image. The Release system matrix passes 5/5 across native,
O0/O2 AOT, and cache-disabled O0/O2 JIT. Public tests prove canonical f32
rejection leaves an assignment sentinel unchanged; supported INT64 width two
and historical raw DOUBLE payload word two return exact `".."`, while INT64
width six returns the unchanged input. Native canonical and malformed tests
cover the exact exception type/message, unchanged wrapper output, the same
controls, and null-input validation precedence. The complete f32 label passes
53/53, the existing system completion regression passes 23/23 at O0 and O2,
and ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Positive
compiled-runtime evidence ran on Linux; Windows, other POSIX systems, WASM,
and the separate VM implementation were not executed. Final evidence is under
`/home/gabe/.codex/evidence/f32-string-truncate-display-20260924`.

The `string-index-of` start-index leaf was measured in the same pinned LLVM
21.1.8 image. The Release system matrix passes 5/5 across native, O0/O2 AOT,
and cache-disabled O0/O2 JIT. Public canonical rejection preserves an
assignment sentinel; supported INT64 starts zero and two return indices one and
four, historical raw DOUBLE payload word two returns four, and empty-needle
start two returns two. Native canonical and malformed tests cover the exact
exception type/message, unchanged wrapper output, character and empty needles,
and invalid-text validation precedence. The complete f32 label passes 53/53,
the existing system completion regression passes 23/23 at O0 and O2, and
ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Positive
compiled-runtime evidence ran on Linux; Windows, other POSIX systems, WASM,
and the separate VM implementation were not executed. Final evidence is under
`/home/gabe/.codex/evidence/f32-string-index-of-20260924`.

The shared `string-pad-left` / `string-pad-right` width leaf was measured in
the same pinned LLVM 21.1.8 image. The Release system matrix passes 5/5 across
native, O0/O2 AOT, and cache-disabled O0/O2 JIT. Public canonical rejection
covers both wrappers with independent assignment sentinels; supported INT64
width three and historical raw DOUBLE payload word three return exact `"007"`
and `"700"`, while invalid input retains `#f` precedence. Native canonical and
malformed tests cover both directions, the exact exception type/message,
unchanged wrapper output, and the same controls. The complete f32 label passes
53/53, the existing system completion regression passes 23/23 at O0 and O2,
and ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Positive
compiled-runtime evidence ran on Linux; Windows, other POSIX systems, WASM,
and the separate VM implementation were not executed. Final evidence is under
`/home/gabe/.codex/evidence/f32-string-pad-width-20260924`.
