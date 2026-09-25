# GCD/LCM result contract and remaining decisions

This is the bounded GCD/LCM contract after the exact-wide, inexact-result,
variadic, AD-rejection, and canonical F32 admission changes. It does not claim full
R7RS numeric conformance. The [corrected R7RS numeric report](https://standards.scheme.org/corrected-r7rs/r7rs-Z-H-8.html)
specifies nonnegative GCD/LCM results, `(gcd) = 0`, `(lcm) = 1`, and an
inexact result for `(lcm 32.0 -36) = 288.0`. It requires an exact result for
representable all-exact integer inputs, while permitting an implementation
restriction or inexact coercion if an exact result cannot be delivered.

| Input and route | Current accepted behavior | Remaining contract gate |
| --- | --- | --- |
| Exact INT64 within signed magnitude, native and VM direct or stored | Nonnegative exact INT64; zero and negative operands use magnitudes. Public calls are variadic: zero arguments yield exact 0/1 for GCD/LCM, unary gives the magnitude, and 2+ arguments fold all operands. | `INT64_MIN` magnitude still reports an implementation restriction in the int64-only path; a mixed exact bignum peer lets native and VM GCD/LCM compute it exactly. |
| Exact bignum mixed with exact INT64/bignum, native and VM direct or stored | Existing native `eshkol_gcd_tagged` and VM `bignum_gcd` compute exact GCD; both demote a fitting result to INT64 and retain a wide result as bignum. | VM wide GCD is bounded to all-exact operands. Native raw INT64-only overflow remains a separate implementation restriction. |
| Exact bignum LCM mixed with exact INT64/bignum, native tagged and VM direct or stored | Reuses exact GCD, truncated quotient, multiplication, and absolute value; zero short-circuits division. A fitting result demotes to INT64; a wide result remains an exact bignum. | Native raw INT64-only and VM INT64-only paths still report an implementation restriction on magnitude/result overflow. |
| Finite integral DOUBLE within signed magnitude, native and VM direct or stored | Accepted by safety guards; GCD/LCM return inexact DOUBLE after the integer-domain computation. The sign-normalized zero result is +0.0. | The operation still computes in the bounded int64 domain; canonical integral F32 admission follows the same limit. |
| Fractional, nonfinite, or out-of-range DOUBLE; wrong type | Catchable explicit error before float-to-int or signed overflow. | No coercion of these inputs is proposed. Mixed bignum/DOUBLE GCD and LCM explicitly reject on both substrates, including a zero DOUBLE peer, because a sufficiently wide result has no accepted inexact conversion contract. |
| Canonical host-constructed F32, finite integral and within signed magnitude | Native JIT/AOT and VM direct or stored calls widen through the checked DOUBLE integer domain and return DOUBLE, including +0.0 for zero results. | The int64 magnitude and result limits still apply; no F32 source literal or VM malformed carrier exists. |
| Fractional, nonfinite, or out-of-range F32; malformed native F32 layout | Catchable rejection before float-to-int conversion. | A canonical F32 plus exact wide bignum explicitly rejects, even with a zero peer. |
| AD dual or native reverse-tape node operand | Public native GCD/LCM reject explicitly; VM dual remains rejected. Integer-domain GCD/LCM has no accepted derivative. | Reverse-tape admission remains blocked until a derivative policy is accepted; the rejection probe does not assign a gradient. |

Relevant implementations: `lib/backend/llvm_codegen.cpp` (`codegenGCD`,
`codegenLCM`, the GCD/LCM-specific variadic first-class wrapper),
`lib/backend/vm_prelude_source.h` (variadic public wrappers over the checked
binary `_gcd2`/`_lcm2` entries). Both wrappers inspect the original arguments
before folding and seed an all-exact fold with a wide operand when one is
present, so an earlier `INT64_MIN` does not spuriously fail before the wide
kernel is selected. `lib/core/bignum.cpp` (`eshkol_gcd_tagged`, existing
tagged quotient/multiplication ABI),
`lib/backend/vm_native.c` (public fid 224/225 and `vm_gcd_lcm_abs_operand`),
and `lib/backend/vm_bignum.c` (`bignum_gcd`, quotient, multiplication). The direct/stored test fixtures
in `tests/core/gcd_wide_exact_vm_parity.esk` and
`tests/core/gcd_wide_exact_vm_negative.esk` pin the bounded VM change.
