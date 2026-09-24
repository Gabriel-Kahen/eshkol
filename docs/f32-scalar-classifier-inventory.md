# True-binary32 scalar classifier inventory

Status: **native/FFI representation phase implemented and supported-gated;
LLVM raw packing and VM host transport are source-complete candidates;
numeric semantics, formatting, and persistence remain incomplete**.

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
| `lib/backend/tagged_value_codegen.cpp` subtype helpers | Header reads branch on exact HEAP_PTR/CALLABLE tags. The generic base-tag helper preserves every tag `>= 8`, including 11. | Pointer-safe. The phase-two helper packs/unpacks raw LLVM f32 and validates the complete canonical tag-11 layout; main compiler wiring remains deferred. |
| `lib/backend/llvm_codegen.cpp` mirrored base-tag helper | Preserves tag 11 because it passes through tags `>= 8`; object subtype reads remain behind exact pointer tests. | Pointer-safe. Semantic/codegen support remains deferred and must be coordinated with allocator work. |

Native arena retain/release accepts raw pointers rather than tagged values; there
is no separate native tagged-value GC mark/release switch. Region evacuation and
the iteration-scope scan above are the relevant native lifetime classifiers.

## Native masks, defaults, and semantic dispatch

These sites do not dereference a tag-11 payload, but they do not yet implement the
accepted scalar semantics. They are recorded so later work cannot mistake
pointer-safety for feature completion.

| Surface | Current tag-11 result | Required later action |
|---|---|---|
| `lib/core/runtime_display_hosted.cpp` | `>= 8` preserves tag 11; default prints an unknown raw value without dereference. | Add the shared deterministic f32 formatter. |
| `lib/core/runtime_errors_hosted.cpp` | 0x0f masking leaves 11; switch reports unknown. | Add the float32 type/error name and remove any false numeric fallback. |
| `lib/core/introspection.cpp` | Exact pointer cases guard header reads; tag 11 defaults to unknown. | Add `type-of`, predicates, and numeric classification. |
| `lib/core/runtime_deep_equal.cpp` | Tag 11 passes through and reaches the raw default. | Add the accepted same-tag IEEE equality policy. |
| `lib/core/runtime_hash_table.cpp` | Tag 11 passes through; the default hashes raw payload bits. | Add the tag-aware numeric hash and canonicalize both zero signs. |
| `lib/core/logic.cpp` | Switch defaults to an unknown-type rendering. | Retain explicit unsupported behavior or add the ordinary scalar case. |
| `lib/types/hott_types.cpp` | Runtime tag conversion preserves exact tag 11 without a low-bit mask; folded tags 27 and 43 remain `Value`. | Phase two records `RuntimeRep::Float32` and complete `Float32`/tag-11 round trips; source construction and general compiler lowering remain disabled. |
| `lib/core/kb_persistence.cpp` | Writer switch rejects tag 11 through its unsupported/default path; reader has no tag-11 encoding. | Pin explicit failure-atomic rejection tests; positive encoding remains deferred. |
| `lib/core/dnc_api.c`, `lib/core/inference.cpp` | DNC scalar/vector paths report or return their established type failure for tag 11. Inference numeric readers return failure; they do not consume f32 payload bits. | Add future admissions only through the canonical promotion helper. |
| `lib/core/sdnc_api.c` | Scalar tag 11 reports unsupported; heterogeneous vector reads fail instead of retaining a pre-zeroed slot. | Add an explicit policy with the accelerator phase; do not infer one from its internal float buffer. |
| `lib/core/runtime_tensor_index.cpp`, `runtime_list_helpers.cpp` | Index, tensor construction, and AD extraction report tag 11 as unsupported. No path casts f32 NaN/infinity to an integer or stores an invented zero. | Add later admissions only through the canonical conversion and a defined domain policy. |
| `lib/core/runtime_taylor.c`, `lib/core/ad_tape_builtins.c`, and AD list coercion | Tag 11 is classified as a scalar so it reaches an explicit numeric refusal rather than collection dereference; Taylor normalization/seeding/extraction, tape const/var, and list/extraction helpers reject it. | Define and test AD semantics in its later phase. |
| `lib/core/bignum.cpp`, `lib/core/rational.cpp` public tagged arithmetic | Arithmetic, comparison, gcd, numerator, denominator, and rational construction entry points reject tag 11 before any integer/double payload fallback. | Replace rejection only when ordinary f32 arithmetic is implemented. |
| Other semantic defaults, including model/workspace/system builtins | Outside this representation/transport slice; no positive tag-11 admission is claimed. In particular, model norm-parameter readers retain their non-double defaults, workspace salience retains its unsupported-type zero, and the system-builtin integer extractor is not an f32 authority. | Repeat a complete semantic audit and replace these legacy defaults with explicit rejection or admitted conversion before source construction or general invocation is enabled. |

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
This helper is not yet wired into source literals or the allocator-owned main
LLVM generator, and the generic numeric predicate continues to reject f32.

The bytecode VM has a separate immediate `VAL_FLOAT32` transport value whose
union member stores the raw 32-bit word. Versioned host-callback push/pop calls
return a frozen int32 status domain (success, invalid argument, empty stack,
type mismatch, or full stack) and round-trip all binary32 encodings without
conversion. Pop is failure-atomic for wrong types and push is failure-atomic on
a full stack. The OALR walker and parallel worker clone/publish classifiers both
treat the value as pointer-free, even when its raw word equals a live heap index.
Existing legacy integer/double host converters reject it. Native Windows/stub
profiles export rejecting link-stable calls and advertise the feature as zero.
There is deliberately no ESKB
constant kind or source syntax for this value. VM `number?` and arithmetic also
reject it, so transport cannot silently enable double-backed computation.

The focused source candidate adds raw-pattern and malformed-layout LLVM tests,
dynamic checked-extraction IR verification, HoTT round trips, VM host round-trips,
distinct failure-status and full-stack atomicity checks, pointer-shaped OALR and
parallel transport tests (including an actual nested-region pop that reclaims
the same-index heap object), stub-profile ABI checks, and explicit numeric-predicate/
arithmetic rejection. Its supported LLVM 21 release and sanitizer gates remain
pending the shared build lease.

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

## Remaining acceptance boundary

This phase does not support source literals, main LLVM lowering, bytecode
constants, ordinary numeric operations, positive numeric type predicates, display/read, hashing/equality,
positive persistence, general bytecode VM/ESKB construction, AD, complex values, or accelerators.
Windows generated-shared-library probe retention/export is also unsupported.
It cannot satisfy a downstream true-f32 metrics claim by itself. The full feature
still requires a compatible union with the separately owned allocator fix,
completed LLVM/VM/semantic phases, a repeated full classifier audit, and
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
