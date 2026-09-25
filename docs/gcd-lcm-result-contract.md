# GCD/LCM result contract and remaining decisions

This is the bounded non-F32 contract at provisional runtime `09e65bf8` plus
the VM exact-wide GCD change. It does not admit F32 or claim full R7RS numeric
conformance. The [corrected R7RS numeric report](https://standards.scheme.org/corrected-r7rs/r7rs-Z-H-8.html)
specifies nonnegative GCD/LCM results, `(gcd) = 0`, `(lcm) = 1`, and an
inexact result for `(lcm 32.0 -36) = 288.0`. It requires an exact result for
representable all-exact integer inputs, while permitting an implementation
restriction or inexact coercion if an exact result cannot be delivered.

| Input and route | Current accepted behavior | Remaining contract gate |
| --- | --- | --- |
| Exact INT64 within signed magnitude, native raw or stored and VM binary direct or stored | Nonnegative exact INT64; zero and negative operands use magnitudes. | `INT64_MIN` magnitude still reports an implementation restriction in the int64-only path; mixed VM bignum GCD computes it exactly. Native direct is variadic, whereas the VM entry is binary. |
| Exact bignum mixed with exact INT64/bignum, native GCD and VM binary direct or stored | Existing native `eshkol_gcd_tagged` and VM `bignum_gcd` compute exact GCD; both demote a fitting result to INT64 and retain a wide result as bignum. | VM wide GCD is bounded to all-exact operands. Native raw versus tagged paths and VM arity are not fully unified. |
| Exact bignum LCM or result beyond INT64 | Native and VM public LCM raise rather than fabricate a result. | A reviewed exact-wide LCM kernel and representation policy are needed before claiming wide parity. |
| Finite integral DOUBLE within signed magnitude, native and VM direct or stored | Accepted by safety guards; result remains historical **exact INT64**. | This result kind disagrees with the corrected R7RS inexact example. Decide whether to change the established API across both substrates before F32 admission. |
| Fractional, nonfinite, or out-of-range DOUBLE; wrong type | Catchable explicit error before float-to-int or signed overflow. | No coercion of these inputs is proposed. Mixed bignum/DOUBLE VM GCD remains an explicit error pending inexact result-kind policy. |
| Canonical or malformed F32 | Existing public full-carrier guards reject. | Revisit only after the DOUBLE result-kind, wide LCM, and arity policy are reviewed. |
| AD dual operand | Native LLVM GCD/LCM retains its historical zero-tangent dual on an integral primal; VM public GCD/LCM has no matching dual result path. | No differentiability or cross-substrate AD claim follows from that legacy behavior. Review AD separately; this change does not expand it. |

Relevant implementations: `lib/backend/llvm_codegen.cpp` (`codegenGCD`,
`codegenLCM`), `lib/core/bignum.cpp` (`eshkol_gcd_tagged`),
`lib/backend/vm_native.c` (public fid 224/225 and `vm_gcd_lcm_abs_operand`),
and `lib/backend/vm_bignum.c` (`bignum_gcd`). The direct/stored test fixtures
in `tests/core/gcd_wide_exact_vm_parity.esk` and
`tests/core/gcd_wide_exact_vm_negative.esk` pin the bounded VM change.
