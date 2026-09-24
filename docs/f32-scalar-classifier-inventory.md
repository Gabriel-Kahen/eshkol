# True-binary32 scalar classifier inventory

Status: **native/FFI representation phase implemented and focused-tested;
compiler, VM, numeric semantics, formatting, and persistence remain incomplete**.

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
| `lib/backend/tagged_value_codegen.cpp` subtype helpers | Header reads branch on exact HEAP_PTR/CALLABLE tags. The generic base-tag helper preserves every tag `>= 8`, including 11. | Pointer-safe, but compiler f32 packing remains deferred. |
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
| `lib/types/hott_types.cpp` | Runtime tag conversion defaults to `Value`; nominal Float32 still uses Float64 representation. | Add the true `RuntimeRep::Float32` mapping in the LLVM/type phase. |
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

This phase does not support source literals, LLVM packing, HoTT runtime mapping,
ordinary numeric operations, type predicates, display/read, hashing/equality,
positive persistence, the bytecode VM, ESKB, AD, complex values, or accelerators.
Windows generated-shared-library probe retention/export is also unsupported.
It cannot satisfy a downstream true-f32 metrics claim by itself. The full feature
remains blocked on the separately owned allocator fix, a compatible union,
completed LLVM/VM/semantic phases, repeated classifier audit, supported build and
sanitizer evidence, and independent review.
