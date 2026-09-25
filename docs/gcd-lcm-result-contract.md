# GCD/LCM result contract and remaining decisions

This is the bounded non-F32 contract after the exact-wide GCD/LCM and
native/VM inexact-result parity changes. It does not admit F32 or claim full
R7RS numeric conformance. The [corrected R7RS numeric report](https://standards.scheme.org/corrected-r7rs/r7rs-Z-H-8.html)
specifies nonnegative GCD/LCM results, `(gcd) = 0`, `(lcm) = 1`, and an
inexact result for `(lcm 32.0 -36) = 288.0`. It requires an exact result for
representable all-exact integer inputs, while permitting an implementation
restriction or inexact coercion if an exact result cannot be delivered.

| Input and route | Current accepted behavior | Remaining contract gate |
| --- | --- | --- |
| Exact INT64 within signed magnitude, native raw or stored and VM binary direct or stored | Nonnegative exact INT64; zero and negative operands use magnitudes. | `INT64_MIN` magnitude still reports an implementation restriction in the int64-only path; a mixed exact bignum peer lets native and VM GCD/LCM compute it exactly. Native direct is variadic, whereas the VM entry is binary. |
| Exact bignum mixed with exact INT64/bignum, native GCD and VM binary direct or stored | Existing native `eshkol_gcd_tagged` and VM `bignum_gcd` compute exact GCD; both demote a fitting result to INT64 and retain a wide result as bignum. | VM wide GCD is bounded to all-exact operands. Native raw versus tagged paths and VM arity are not fully unified. |
| Exact bignum LCM mixed with exact INT64/bignum, native tagged and VM binary direct or stored | Reuses exact GCD, truncated quotient, multiplication, and absolute value; zero short-circuits division. A fitting result demotes to INT64; a wide result remains an exact bignum. | Native raw INT64-only and VM INT64-only paths still report an implementation restriction on magnitude/result overflow. This leaf does not alter those paths or VM binary arity. |
| Finite integral DOUBLE within signed magnitude, native and VM direct or stored | Accepted by safety guards; GCD/LCM return inexact DOUBLE after the integer-domain computation. The sign-normalized zero result is +0.0. | The operation still computes in the bounded int64 domain; wide LCM and separate F32 admission remain open. |
| Fractional, nonfinite, or out-of-range DOUBLE; wrong type | Catchable explicit error before float-to-int or signed overflow. | No coercion of these inputs is proposed. Mixed bignum/DOUBLE GCD and LCM explicitly reject on both substrates, including a zero DOUBLE peer, because a sufficiently wide result has no accepted inexact conversion contract. |
| Canonical or malformed F32 | Existing public full-carrier guards reject. | Revisit only after wide LCM, arity, and AD policy are reviewed. |
| AD dual operand | Native LLVM and VM public GCD/LCM reject explicitly. Integer-domain GCD/LCM has no accepted derivative, so neither returns a fabricated zero tangent. | Native reverse-tape AD-node behavior is not established by this dual-input gate. |

Relevant implementations: `lib/backend/llvm_codegen.cpp` (`codegenGCD`,
`codegenLCM`), `lib/core/bignum.cpp` (`eshkol_gcd_tagged`, existing
tagged quotient/multiplication ABI),
`lib/backend/vm_native.c` (public fid 224/225 and `vm_gcd_lcm_abs_operand`),
and `lib/backend/vm_bignum.c` (`bignum_gcd`, quotient, multiplication). The direct/stored test fixtures
in `tests/core/gcd_wide_exact_vm_parity.esk` and
`tests/core/gcd_wide_exact_vm_negative.esk` pin the bounded VM change.
