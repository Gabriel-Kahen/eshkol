# Eshkol Development Roadmap

> **This is the canonical roadmap.** It is the single source of truth for
> Eshkol's release plan. Other roadmap-shaped documents are derived
> views — they exist to answer specific questions and should not
> contradict this file:
>
> - [`docs/COMPILER_ROADMAP.md`](docs/COMPILER_ROADMAP.md) — engineering
>   detail by release line (M0–M4 task tiers), including the per-release
>   work breakdown for compiler engineers.
> - [`docs/breakdown/ROADMAP.md`](docs/breakdown/ROADMAP.md) — short
>   summary for readers in the breakdown / per-subsystem documentation
>   set.
> - [`docs/vision/FUTURE_ROADMAP.md`](docs/vision/FUTURE_ROADMAP.md) —
>   forward-looking vision and long-horizon research items.
> - [`docs/NOESIS_TRAJECTORY.md`](docs/NOESIS_TRAJECTORY.md) — the
>   Noesis-readiness view, tracked separately because Noesis has a
>   distinct downstream cadence.
> - [`docs/design/adr/0000-unified-trajectory.md`](docs/design/adr/0000-unified-trajectory.md) —
>   the 14-stage architectural ladder (v1.3.3a through v2.0) that sequences
>   the load-bearing rewrites (binding/type identity, OALR ABI v2, staged AD
>   kernels, DBSP, resident sessions) underneath the release-line features
>   tracked here. This roadmap's release lines and that ladder's stages are
>   two views of the same plan and must not diverge; see "ADR-0000 stage
>   attainment" below for the honest current-attainment reconciliation
>   between them.
>
> When any of those docs disagrees with this one, this one is correct
> and the others should be updated.

This roadmap tracks Eshkol's evolution from the **completed v1.0-foundation release** through upcoming versions that will establish Eshkol as the definitive platform for gradient-based computing and integrated AI.

### ADR-0000 stage attainment (added 2026-08-25, conformity audit item a1)

The release lines below describe *what ships*; [ADR-0000](docs/design/adr/0000-unified-trajectory.md)
describes the *architectural substrate that has to land first* for the back
half of this roadmap (v1.4 onward) to be buildable rather than aspirational.
As of `4bf871a0` (2026-08-25), remeasured directly against the tree:

**0 of 14 ADR-0000 stages SATISFIED, 2 PARTIAL (Stage 1 ~30%, Stage 5
~35%), 12 NOT STARTED.** Every load-bearing artifact the ADR names as the
gate for v1.4+ — `BindingId`, `NodeId`/`SourceSpan`, `FlowEnv`,
`ESHKOL_MEMORY_ABI_V2`, `eshkol_compile_staged_value_grad` — is absent from
the tree. Full stage-by-stage detail (what exists, what's missing, why the
gate isn't meetable yet) is in ADR-0000's own "Attainment" section and in
`docs/design/AUDIT_2026_08_25_RESOLUTION.md`.

This is a statement about **present attainment, not a retraction of the
plan**: every one of the 14 stages stays on the ladder below, each mapped to
the release line it targets. The two CRITICAL implementation defects behind
most of the AD-related stalls — the dense tensor AD node path being
unreachable dead code, and the LLVM finite-difference counter having zero
callers — are called out explicitly under **v1.5-intelligence** and in
`docs/design/adr/0002-ad-staged-dense-kernels.md`, because they block
Stages 5, 7, and 8 until fixed.

> **Parallel platform program**: The internal freestanding / kernel / embedded architecture work begins during `v1.2-scale` as a mergeable infrastructure program and converges publicly at `v1.8-platform`. See [docs/platform/README.md](docs/platform/README.md) and [docs/platform/ROADMAP_ALIGNMENT.md](docs/platform/ROADMAP_ALIGNMENT.md).

---

## v1.0-foundation (2025) - COMPLETED

**Production Release Delivered**

### Completed Core Implementation
- [x] LLVM-based modular backend with 21 specialized codegen modules
- [x] Recursive descent parser with HoTT type expression support
- [x] Bidirectional type checker with gradual typing
- [x] Ownership and escape analysis for memory optimization
- [x] Module system with dependency resolution and cycle detection
- [x] Hygienic macro system (syntax-rules)
- [x] R7RS Scheme compatibility (subset)

### Completed Automatic Differentiation
- [x] Forward-mode AD (dual numbers)
- [x] Reverse-mode AD (computational graphs)
- [x] Nested gradients (32-level tape stack)
- [x] Vector calculus operators (8 total): derivative, gradient, jacobian, hessian, divergence, curl, laplacian, directional-derivative
- [x] Polymorphic arithmetic (int64/double/dual/tensor/AD-node)

### Completed Memory Management
- [x] Arena allocation with OALR (Ownership-Aware Lexical Regions)
- [x] Escape analysis (stack/region/shared allocation decisions)
- [x] with-region syntax for lexical memory scopes
- [x] Ownership tracking (owned, moved, borrowed states)
- [x] Zero garbage collection - fully deterministic

### Completed Data Structures
- [x] 16-byte tagged values with consolidated types
- [x] 32-byte cons cells supporting mixed-type lists
- [x] N-dimensional tensors with autodiff integration
- [x] Hash tables (FNV-1a hashing, open addressing)
- [x] Heterogeneous vectors
- [x] Exception handling (guard/raise)

### Completed Standard Library
- [x] 60+ list operations
- [x] 30+ string utilities
- [x] Functional programming (compose, curry, flip)
- [x] JSON/CSV/Base64 support
- [x] Math library (linear algebra, numerical methods, statistics)

### Completed Development Tools
- [x] Interactive REPL with LLVM ORC JIT
- [x] Standalone compiler (eshkol-run)
- [x] Library compilation mode
- [x] Comprehensive test suite (430+ files)
- [x] CMake build system
- [x] Docker containers

---

## v1.1-accelerate (Q1 2026) - COMPLETED

**Focus:** Performance acceleration through XLA, SIMD, and parallelism

### XLA Backend Integration
- [x] XLA type system and codegen infrastructure
- [x] XLA fusion for tensor operation chains
- [x] Automatic kernel generation
- [x] CPU/GPU code generation from single source
- [x] JIT compilation for dynamic shapes

### SIMD Vectorization
- [x] SSE/AVX/NEON instruction generation
- [x] Loop vectorization for tensor operations
- [x] Memory alignment optimization
- [x] Platform-specific tuning

### Concurrency Primitives
- [x] `parallel-map` for data parallelism
- [x] `parallel-fold` for parallel reduction
- [x] `future` for asynchronous computation
- [x] Work-stealing thread pool scheduler
- [x] Thread-safe memory management

### Extended Math Library
- [x] Complex numbers with autodiff
- [x] FFT/IFFT operations
- [x] Signal processing filters
- [x] Statistical distributions
- [x] Optimization algorithms (L-BFGS, conjugate gradient)

### Arbitrary-Precision Arithmetic (Added)
- [x] Bignum (arbitrary-precision integers) — full R7RS compliance
- [x] Rational numbers (exact fractions)
- [x] Overflow detection and automatic promotion (int64 → bignum)
- [x] Bignum demotion (normalize back to int64 when possible)
- [x] Bitwise operations on bignums (two's complement semantics)
- [x] All arithmetic, comparison, predicate, equality ops for bignums

### Consciousness Engine (Added)
- [x] Logic programming primitives (unification, substitutions, knowledge base)
- [x] Active inference engine (factor graphs, belief propagation, free energy)
- [x] Global workspace theory implementation (modules, softmax competition)
- [x] 22 builtin operations for logic, inference, and workspace

### R7RS Compliance Extensions (Added)
- [x] call/cc and dynamic-wind
- [x] guard/raise exception handling
- [x] Bytevectors
- [x] let-syntax / syntax-rules hygienic macros
- [x] Tail call optimization validation
- [x] Symbol operations
- [x] `(load "path")` R7RS file loading support

### Dual Backend Architecture (Added)
- [x] Bytecode VM — 63-opcode register+stack interpreter with 250+ native call IDs
- [x] ESKB binary format — section-based bytecode container with CRC32
- [x] `-B` flag for bytecode emission from eshkol-run
- [x] VM compiler integration — eshkol_vm.c linked into compiler build
- [x] Weight matrix transformer — programs as neural network weights (126/126 inline, 123/123 traced, 3-way verified)
- [x] qLLM bridge — Eshkol↔qLLM tensor conversion with AD integration *(design-era claim; the implementation landed in v1.3.4-evolve, #386/#392 — see below)*

### GPU Acceleration (Added)
- [x] Metal SF64/DF64/F32/FP24/FP53 precision tiers
- [x] Ozaki-II CRT-based exact matrix multiplication
- [x] CUDA backend with cuBLAS
- [x] Cost-model dispatch (SIMD → cBLAS → GPU)
- [x] Occupancy-aware kernel configuration

### Signal Processing (Added)
- [x] FFT/IFFT (Cooley-Tukey radix-2)
- [x] Window functions (Hamming, Hann, Blackman, Kaiser)
- [x] FIR/IIR filters
- [x] Butterworth filter design (lowpass, highpass, bandpass)

### Web Platform (Added)
- [x] WebAssembly compilation target
- [x] Browser-based REPL (web/)
- [x] JavaScript interop

### v1.1.12 Additions (April 2026)
- [x] Production bytecode VM (555+ builtins, 176/176 tests)
- [x] Forward-mode AD in bytecode VM (dual number propagation through all opcodes)
- [x] eshkol.ai website written in Eshkol, compiled to WASM (8 pages, browser REPL)
- [x] Interactive documentation with runnable code examples
- [x] GitHub Pages deployment workflow
- [x] R7RS control flow in VM: call/cc, guard/raise, dynamic-wind, values
- [x] Exact arithmetic in VM: rational literals, +nan.0/+inf.0/-inf.0

### v1.1.13 Additions (April 2026)
- [x] Native Windows ARM64 build path (VS 2022 + ClangCL + LLVM 21 aarch64 SDK)
- [x] 16-lane release matrix (linux/macos/windows × x64/arm64 × lite/xla/cuda)
- [x] Per-arch LLVM SDK caching on Windows runners
- [x] Two critical VM closure bug fixes (named-let nested closure PC offset, native 252 upvalue relay)
- [x] Windows setjmp hardening: x64 frameaddress, ARM64 sponentry, dynamic jmp_buf sizing
- [x] Runtime symbol renames (eshkol_fopen, eshkol_access, eshkol_remove, etc.) for MSVC POSIX shim disambiguation
- [x] Codegen fatal error flag — fail hard on undefined functions instead of generating runtime stubs
- [x] Mobile-responsive website (hamburger nav, internal scroll for code blocks, table responsiveness)
- [x] Browser REPL error display for invalid input
- [x] Consciousness engine in VM: KB pattern matching, factor graphs, workspace
- [x] Top-level mutual recursion via letrec-style group compilation

### Windows Platform Support (Added)
- [x] Native Windows build via MSYS2/MinGW64 (PR #9, mattneel)
- [x] UTF-8-safe REPL console output
- [x] Platform runtime abstraction layer

---

## Architecture Dependency Chain

```
v1.1 (COMPLETE)
 ├─ Consciousness engine (logic + inference + workspace)
 ├─ XLA/GPU backend
 ├─ Bytecode VM (production)
 ├─ Continuations + exact arithmetic
 └─ Web platform (WASM)
       │
v1.2 ──┤ Model serialization (requires tensors from v1.1)
       ├ Python bindings (requires stable API from v1.1)
       └ Per-thread arenas (requires OALR from v1.0)
             │
v1.3 ────────┤ R7RS library system (requires module system from v1.0)
             ├ String interpolation (parser extension)
             └ PGO (requires stable codegen from v1.1+)
                   │
v1.4 ──────────────┤ TCP/UDP + TLS (requires per-thread arenas from v1.2)
                   ├ Event loop (requires non-blocking I/O)
                   └ Linear resource types (requires HoTT from v1.0)
                         │
v1.5 ────────────────────┤ Symbol embeddings (requires tensors + KB from v1.1)
                         ├ Differentiable logic (requires AD + logic from v1.1)
                         └ LSTM/GRU (requires tensor backprop from v1.1)
                               │
v1.6 ──────────────────────────┤ Backward chaining (requires logic from v1.1)
                               ├ Knowledge graphs (requires KB + embeddings from v1.5)
                               └ Constraint solving (requires logic engine)
                                     │
v1.7 ────────────────────────────────┤ Neural-guided search (requires v1.5 bridge)
                                     ├ GNN (requires graph + tensor ops)
                                     └ Program synthesis (requires type system)
                                           │
v1.8 ──────────────────────────────────────┤ Windowing + event system (requires v1.4 I/O)
                                           ├ Real-time audio (requires signal from v1.1)
                                           └ Embedded targets (requires bare-metal LLVM)
                                                 │
v1.9 ────────────────────────────────────────────┤ Linear dependent types (requires HoTT)
                                                 ├ Effect types (requires type checker)
                                                 └ Algebraic effects (requires continuations)
                                                       │
v2.0 ──────────────────────────────────────────────────┤ Quantum types (requires linear dep types)
                                                       ├ Quantum gates + measurement
                                                       ├ Hybrid VQE/QAOA (requires AD + quantum)
                                                       └ Formal verification (requires dep types)
```

**Arbitrary-order AD (Taylor-tower) track — SHIPPED ahead of schedule:** the
campaign was originally planned to be threaded through the version themes
above as enabling substrate spread from v1.3.1 through v2.0 (P1 in v1.3.1,
P2/P3 in v1.3.2, P4/P6/P11 in v1.4, P5/P7/P9 in v1.5, P10 bridging v1.5-v1.7,
P12 in v1.6, P8 in v2.0). Instead, **all 13 phases (P0-P12) shipped complete
on the LLVM backend in v1.3.0-evolve** — see [CHANGELOG.md](CHANGELOG.md) and
[`docs/AD_CAMPAIGN.md`](docs/AD_CAMPAIGN.md) for the as-shipped detail. The
version rows below still show the original staging plan for historical
context; treat the AD line item in each as already delivered on LLVM.
**Engine qualifier added 2026-08-25 (conformity audit item a5):** of the ten
named operators, only two (`taylor`, `derivative-n`) are compiler builtins;
the other eight (`mixed-partial`, `gradient-n`, `taylor-model`, `tm-range`,
`tm-eval`, `taylor-ode-solve`, `taylor-root`, `sparse-hessian`) are Eshkol
library code in `lib/core/ad/*.esk` bottoming out in `derivative-n` — real,
but not compiler-level. The bytecode VM has none of the Taylor-tower
surface (`grep -i taylor lib/backend/vm_*.c` is empty; `op:DERIVATIVE_N` is
a `gap` row). **BUILD ITEM:** VM Taylor-tower builtins, target v1.4.1
(ADR-0000 Stage 3/4).

---

## v1.2-scale (May 2026) - SHIPPED

**Focus:** Get models into production. Save them, load them, deploy them — and stop being surprised by edge cases.

- [x] Model serialization (`.eshkol-model` ESKB-extended binary format)
- [x] Stable C FFI header + Python bindings (pybind11; numpy zero-copy)
- [x] Per-thread arenas (safe concurrent memory allocation)
- [x] Deep recursion: 512 MB main-thread stack on Darwin/Linux/Windows
      (linker flags wired into both single-step and compiled-files
      link paths); 100K-frame recursion-depth check with typed
      exception
- [x] Image I/O (PNG/JPEG/WebP/BMP read/write/resize) — backed by
      native platform/system codec APIs (ImageIO/CoreGraphics on
      macOS, system libpng/libjpeg/libwebp on Linux, GDI+ on
      Windows) so the active backend no longer depends on vendored
      third-party media decoders
- [x] CSV/DataFrame (tabular data loading for ML pipelines)
- [x] Improved error messages with file:line:col + caret underlines
      (preserves newlines in stripped comments + cumulative file-line
      tracking across `parse_next_ast` calls; 5-case regression suite)
- [x] Terminal plotting (`sparkline`, `bar-chart` in pure Eshkol stdlib)
- [x] Codegen modularisation: `tensor_codegen.cpp` 19,940 → 1,280 lines at
      the time of the v1.2 split (94% reduction) across 13 focused
      per-domain split files; re-measured for this documentation wave at
      1,867 lines as of commit `694c3179` (still-active file, grown with
      subsequent feature work — the 94% reduction was a point-in-time
      measurement, not an invariant). The
      remaining `llvm_codegen.cpp` extractions are complete: the
      `EshkolLLVMCodeGen` class contract is exposed in
      `inc/eshkol/backend/llvm_codegen.h`, with module initialization,
      builtin-factory, and REPL-resolution implementations in
      `module_init_codegen.cpp`, `builtin_factory_codegen.cpp`, and
      `repl_resolution_codegen.cpp`.
- [x] v1.2 edge-case + security regression suite (62 tests) wired into
      `run_all_tests.sh` and a new `linux-x64-asan-ubsan` CI lane.
      Includes 3 shell-style tests for compile-time diagnostics.
- [x] Tagged release artifact contract: the GitHub release workflow
      validates the full 16-asset platform matrix before publishing
      (Linux x64/ARM64 lite/XLA/CUDA, macOS arm64/x64 lite/XLA,
      Windows x64/ARM64 lite/XLA/CUDA) and emits `SHA256SUMS.txt`;
      published as the `v1.2.3-scale` packaging closeout.
- [x] Stdlib correctness: user `(define (foo …))` after `(require
      stdlib)` cleanly shadows stdlib's `foo` at link time
      (LinkOnceODR linkage on stdlib functions) and at call-site
      lowering (variadic-info hygiene clears stale entries on
      redefine).
- [x] `--wasm` is self-contained: WASM emit no longer falls through
      to native clang++ link.
- [x] AD scalar derivative on inline lambdas: `(derivative
      (lambda (x) …) point)` inside a wrapper function correctly
      flows through the runtime closure dispatch.  AD value-typed
      captures pass LLVM IR verification when capturing
      function-parameter `tagged_value` Arguments.
- [x] M1 stdlib finalised: `core.json_schema` (Draft 7 subset),
      reflection (`procedure-arity`, `record-fields`, `describe`),
      memoization/LRU, PRNG seeding + deterministic replay, lazy
      streams (SRFI 41), time API (ISO-8601), regex capture groups,
      CLI argument parser, structured logging (JSON-L),
      Prometheus metrics, extra AD ops
      (atan2 / asin / acos / softmax / gelu / silu / sinh / cosh),
      priority queues / sets / deques.
- [x] Hardening: subprocess shell-injection fix (CRITICAL), Python
      FFI AST-injection fix (CRITICAL), 3 integer-overflow guards
      (HIGH), 4 path-traversal/TOCTOU/Windows-buffer fixes (HIGH),
      36 silent-swallow sites surfaced (HIGH), ReDoS protection +
      SQL-injection guards + URL validator (MEDIUM).

---

## v1.3.0-evolve (July 2026) - SHIPPED

**Focus:** Make the language a joy to use day-to-day — and it grew into
much more: a full arbitrary-order automatic-differentiation system.

- [x] Full R7RS library system: `define-library` exports work end-to-end;
      `(rename (m) (a b))` import works. `(prefix (m) p-)` also works as
      bare prefix over the module's whole export list, via a deferred
      alias-emission path (`lib/frontend/parser.cpp:3973-3985` +
      `exe/eshkol-run.cpp:3157-3163`) — corrected 2026-08-25 from "requires
      an explicit `only`/`rename` clause" (conformity audit item a4; the
      doc was stale in the safe direction — the capability had already
      shipped).
- [x] String interpolation (`~{expr}` within strings)
- [x] Named keyword arguments (`(f #:key value)`)
- [x] Pattern matching in `let` bindings (destructuring `let-match`)
- [~] Profile-guided optimization — build-time scaffold landed:
      `cmake -DESHKOL_PGO=generate` instruments, `-DESHKOL_PGO=use
      -DESHKOL_PGO_PROFILE=<merged.profdata>` consumes.  Workload
      selection + canonical merge step (the "what do we train on?"
      side) is the remaining gap; the codegen-side machinery is ready.
- [ ] Whole-program optimization (cross-module inlining and dead code elimination)
- [x] **Native media handling, no vendoring**: image I/O uses native
      platform/system codec APIs (ImageIO/CoreGraphics on macOS,
      system libpng/libjpeg/libwebp on Linux, GDI+ on Windows).
      Going forward the project does not vendor third-party media
      decoders.
- [x] AD second-operand (`input2`) gradient plumbing for every tensor
      op — `tensor-matmul`, `conv2d`, `batch-norm`, `layer-norm`, and
      `scaled-dot-attention`. Each op's AD forward path unrolls into the
      scalar reverse-mode graph (`recordADNodeBinary`), so gradients flow
      to the second differentiable operand (matmul kernel, conv2d kernel,
      norm gamma, attention K/V) with no monolithic tape node left with a
      null `input2`. Verified end-to-end by the finite-difference AD oracle
      `tests/v1_3_edge_cases/ad_input2_test.esk` and smoke probes
      `ad_input2_conv2d_grad_works` / `ad_input2_batchnorm_grad_works` /
      `ad_input2_layernorm_grad_works` / `ad_input2_attention_grad_works`
      (JIT `-r` and AOT), which compare the AD gradient to central finite
      differences at tight tolerance.
- [x] **Arbitrary-order AD Taylor-tower campaign — fully delivered, all 13
      phases (P0-P12), well ahead of the original P1-only-in-v1.3 plan
      below**: runtime tower + `taylor`/`derivative-n` (P1); no-heap
      compile-time-K monomorphization (P2); JET8-subsumption analysis (P3);
      GUW arbitrary-order multivariate mixed partials, `mixed-partial`/
      `gradient-n` (P4); reverse-over-Taylor (P5); exact bignum/rational
      coefficient towers (P6); tensor-valued towers through
      `matmul`/`conv2d` (P7); validated Taylor models with interval-remainder
      bounds, `taylor-model` (P8); differentiable control flow (P9);
      checkpointed high-order reverse-mode (P10); tower-based user numerics,
      `taylor-ode-solve`/`taylor-root` (P11); sparse high-order tensors,
      `sparse-hessian` (P12). See [CHANGELOG.md](CHANGELOG.md), the
      [Automatic Differentiation guide](docs/guide/AUTOMATIC_DIFFERENTIATION.md),
      and `docs/AD_CAMPAIGN.md`. This closes ESH-0118 and supersedes the
      P1-v1.3.1 / P2-P3-v1.3.2 staging plan below — P4 through P12, originally
      spread across v1.4 through v2.0 in the Architecture Dependency Chain,
      shipped complete in this release instead.
- [x] **Full R7RS conformance on the portable corpus**: a new
      reference-Scheme differential oracle (P7a) reached 34/34 (100%) AGREE
      vs. chibi-scheme 0.12.0 on its 34-program corpus, fixing `apply` with
      leading arguments, multi-vector `vector-map`, quasiquoted vectors,
      `cond`/`case` `=>`, allocating `vector-copy` (incl. `#(...)` literals),
      the `error-object` family, `write` escaping, nested `syntax-rules`
      ellipsis, and 2-arg `substring` along the way.
- [x] **Robustness hardening**: proper mutual-tail-call TCO (AArch64),
      named-let TCO in every tail position (incl. through `guard`), the
      closure-capture ceiling raised 16→64, automatic per-iteration arena
      reclamation for bounded-RSS long-running loops, a shutdown-teardown
      race fix, a deep-recursion `SIGILL`-with-no-diagnostic fix, and
      transitive-dependency AOT/JIT cache invalidation.
- [x] **Permanent adversarial-testing infrastructure**: differential
      harness+fuzzer (P1), feature-pair edge matrix (P2), AD
      finite-difference oracle (P3), stress harness (P4), VM parity ratchet
      (P5), six depth-parametric sweep families (P6), and external oracles —
      reference-Scheme differential, sanitizer fuzzing, metamorphic-law
      checking (P7) — all wired into the ICC readiness oracle. See
      `docs/TESTING.md`.

Original v1.3.1/v1.3.2 staging plan for reference (superseded — all landed
in v1.3.0-evolve): P1 runtime `derivative^n` closes ESH-0118; P2
compile-time-K monomorphization and P3 JET8 subsumption gated by `ad-depth`
and `mono-equiv`.

---

## Development workstreams (v1.3.5 → v2.0)

**Re-dated 2026-08-24 (maintainer ruling R1, executed).** Every date from v1.4
onward in the previous published roadmap was stale — some already slipped,
the rest were not going to be hit at measured velocity (the v1.3.1→v1.3.4
line averaged roughly five weeks per point release, including hardening
waves). Rather than keep publishing dates the project would miss serially,
the ladder below is re-dated to what the shipped velocity supports: v2.0
moves from the previously published "Q1 2027" to **~Q4 2028**. The
per-version sections that follow, and the Release Timeline table, use the
re-dated ladder. Compression is possible (the v1.3.4 endgame proved
multi-lane parallel throughput), but the published dates should be ones the
project can hit.

Every release from v1.3.5 forward ships work from some mix of six standing
workstreams rather than a single theme:

- **W1 — Resident/DBSP spine.** `core.dbsp` incremental dataflow (shipped as
  a first slice in v1.3.3-evolve) grows toward a v1.5.0 GA and a unified
  `differentiate` primitive (`numeric` and `incremental` interpretations
  over the closed world) at v2.0.
- **W2 — Assurance.** The ADR-0010 gap ledger (A1-A13) closes on a
  per-version schedule, plus the adversarial-capability ramp: harness CI
  lanes, oracle/ledger schema checks, a documentation-truth ratchet, a SymPy
  external oracle, and a machine-checked-invariants track that begins with a
  Taylor-tower semantics proof sketch.
- **W3 — Performance.** Public, third-party-runnable benchmarks on Eshkol's
  own axes (exact-AD cost curves, flat-RSS resident loops) from v1.3.5
  onward, building toward native PGO (v1.5.0), a staged dense graph
  (v1.6.x), closed-world whole-program optimization (v1.8.1), and
  training-grade performance gates at v2.0 (>=80% of vendor-BLAS on
  GEMM-dominated staged throughput, 10k steps with no recompile, zero
  post-warmup allocations).
- **W4 — Codebase health.** One monolithic file decomposed per release
  behind a parity gate (`vm_run.c`, then `runtime_regions.cpp`, then
  `bignum.cpp`, then `vm_geometric.c`), shell-hardening, dead-code
  liveness sweeps, and a single semantic-tooling core underneath the
  compiler's own dev tools.
- **W5 — Interop & adoption.** The locked 2026-08-20 interop-first
  sequence: boundary exactness across the Python/NumPy edge, a
  silent-demotion CI gate, benchmarks on Eshkol's own axes, and a
  definition-of-done rule for every new AD/quantum feature (an
  external-oracle case plus a Python one-liner). ONNX/StableHLO export
  ships only once there is a training win worth exporting — not on a fixed
  date. **Amended negatives:** no chasing SciPy API parity, no
  ResNet/float64-training gates as an adoption bar; distributed computing
  is explicitly *not* one of these negatives (see W6).
- **W6 — Distributed computing (maintainer ruling 2026-08-20; two tiers).**
  Promoted to a first-class workstream, not an on-demand item. Eshkol
  already emits StableHLO and compiles/executes through XLA
  (`lib/backend/xla/`), but only single-device today — no PJRT, no
  sharding, no replicas in-tree. The thesis is differentiated, not
  parity-chasing: deterministic, exact, bitwise-reproducible distributed
  computing, with raw throughput kept honest by delegating to vendor
  collectives and vendor GEMM rather than rebuilding them.
  - **Tier 1 — scale (ride XLA).** Become a PJRT client; add sharding
    annotations to the staged dense graph so XLA's GSPMD partitioning,
    collectives, and multi-host machinery carry Eshkol at XLA-class
    distributed throughput on GPU/TPU clusters, as bridge work rather than
    runtime build-out.
  - **Tier 2 — truth (native mesh).** Exact-accumulation deterministic
    allreduce (i128/fixed-point), multi-node bit-identity as a mesh-CI
    parity gate (node count as a 4th parity axis), typed communication over
    the v1.4.0 sockets, no-GC tail latency — structurally unavailable to a
    pure-XLA client, since XLA re-associates reductions by design.
  - One workload dials between the two tiers (fast vs. exact) — "exactness
    is an axis," applied to distribution. Staging: v1.4.0 PJRT client spike
    + XLA multi-device single-host + native collectives over sockets;
    v1.5.0 Tier-1 data-parallel + Tier-2 mesh bit-identity gate; v1.6.x
    sharding annotations on the staged dense graph -> GSPMD multi-host +
    distributed DBSP; v1.8.x fault tolerance/elasticity; v2.0 gates per
    tier (Tier 1 >=85% scaling efficiency at 8 devices; Tier 2
    bit-identical gradients at any node count, zero post-warmup
    allocations per rank).

---

## v1.3.5 — the consolidation release (target: late Sep 2026) - PLANNED

**Flagship: SHIPPED (#461).** VM OALR Stage-1 evacuator port (SW-14
ruling) — the full heap-tag space deep-walked on the bytecode VM (a
compile-time-checked 33-wide table over the 28 `HeapType` members plus
the manifold-tag macros and unassigned slots), poison and flat-RSS
validation, so `with-region` reclaims on the VM the way it already does
on native codegen. Re-measured for this documentation wave against a
from-source build of the merge commit (`487c2a62`): flat 25-27 MB across
1,000/4,000/16,000 iterations of the same fixture vs. 793 MB with the
evacuator disabled and 704 MB for an unwrapped control — see
[docs/breakdown/RUNTIME_CONFIGURATION.md](docs/breakdown/RUNTIME_CONFIGURATION.md#bytecode-vm-region-reclamation).
The user-reachable region **handle** surface (`region-open`/`region-close`)
remains bookkeeping-only on the VM (Stage-2, not yet scheduled to a
release).

- Native checked promotion (#713): **IMPLEMENTED CANDIDATE, pending integration
  (2026-09-22); not released.** The complete P0–P3 union adds transactional
  promotion, prepublication scalar/batch mutation checks, fixed emergency
  transfer and the admitted constructor null checks. The focused supported
  Ubuntu 22.04 / LLVM 21.1.8 gate passed 11/11; the native ASan/UBSan gate passed
  7/7. See [checked-promotion-v1](docs/checked-promotion-v1.md) for reproducible
  commands, explicit raw-alias/layout limits, retained-memory measurements and
  constructor coverage gaps. The preserved production-OFF closure and existing
  continuation region-capture regression now pass as optimized AOT and full-file
  JIT through the immutable root getter. Current source and artifact manifests are
  recorded for implementation commit `714d20fe`; integration remains pending. The
  follow-up handler-reservation ABI is a distinct reviewed candidate required by
  downstream one-shot cleanup: it reserves only a requested simultaneous guard
  depth and retains the current single-runtime-thread limitation. Its supported
  LLVM 21.1.8 gate now passes 16/16, including the compiler-private direct-entry
  emergency-rethrow modifier as optimized AOT and cache-disabled JIT. Its native
  ASan/UBSan gate passed 8/8 with leak detection, and production-OFF AOT/JIT plus
  five-symbol closure checks passed. The transformer must still prove its current
  exact future high-water counts (5 direct P1 release, 6 C2 release, 11 LOAD
  rollback) and persistent allocator-failure behavior before the final toolchain
  pin; earlier consumer checks do not prove those downstream obligations.
- True-binary32 scalar runtime prerequisite: **NATIVE/FFI REPRESENTATION PHASE
  IMPLEMENTED AND SUPPORTED-GATED, pending compatible integration
  (2026-09-23); not released.** Native tag 11 now has a canonical raw-bit ABI,
  stable feature detection, exact native/FFI layout pins, deterministic f32-to-f64
  promotion at the embedding boundary, and pointer-free arena/region transport.
  Independent source review, strict C11/C++17 checks, the standalone C11 ABI
  check, five focused LLVM 21.1.8 release gates, and four native ASan+UBSan
  gates pass at `db0e83b5`.
  A follow-up source candidate adds canonical raw LLVM f32 packing with checked
  extraction, exact HoTT tag round trips, and versioned raw-bit VM host transport
  with frozen status codes, full-stack atomicity, pointer-free OALR/parallel
  transport, and an explicit unavailable stub profile. At `ceb1f746`, its
  supported Ubuntu 22.04/LLVM 21.1.8 Release gate passes 8/8 plus 15/15 HoTT
  checks. The matching ASan+UBSan build passes six focused CTest binaries plus
  15/15 HoTT checks. The allocator/F32 union at `a34ad60e` closes the two
  preexisting broad-VM failures plus a live output-string-port teardown leak:
  logic operations distinguish fact payloads from text and opaque heap values,
  compiler-local names retain ownership across stack-depth rollback, and VM
  teardown closes owned ports without closing the standard streams. The same
  candidate releases the compiled main chunk and the remaining handwritten-test
  VMs. Its clean pinned LLVM 21.1.8 gate passes 22/22 checked-promotion tests,
  11/11 focused Release tests, 15/15 HoTT checks, 64/64 type-checker checks, and
  18/18 strict ASan+UBSan tests with leak detection, including both broad VM
  binaries. The next main-LLVM leaf makes declared `extern f32` a bit-exact
  construction and inspection boundary: raw f32 returns become canonical tag
  11, canonical values checked-unpack directly into f32 arguments, and O0/O2
  AOT plus in-process JIT tests cover IEEE bit patterns and the reachable
  classifier, promotion, equality, hash, and display semantics; raw f64/int64
  arguments are rejected on all four native axes. The gate is non-Windows, so
  Windows execution remains unverified. Native/AOT sanitizer tests pass with
  ASan, UBSan, and LeakSanitizer; the in-process JIT tests pass with ASan and
  UBSan while leak detection is disabled because existing parser and
  macro-expander allocations survive `eshkol_eval_string`. This does not
  add literals or a reader, VM constants, persistence, AD, GPU support, or
  f32-preserving results. A follow-up bounded leaf makes main LLVM
  `eq?`/`eqv?`, canonical hash-codegen packing, and the existing runtime
  `equal?`/hash policy agree on same-tag IEEE equality, signed-zero
  normalization, NaN inequality,
  malformed-layout rejection, and cross-tag inequality. The latent pattern and
  case callbacks use the same helper, but source f32 literals are unavailable,
  so those routes are not execution-reachable in this leaf. The subsequent
  formatting leaf routes native and VM display/write, `number->string`, `format`,
  logic output, and error rendering through one raw-binary32 formatter. Boundary
  strings are pinned for zeros, subnormals, normals, infinities, and NaNs; O0/O2
  AOT and cache-disabled JIT reject non-decimal conversion and integer-only
  `~d`/`~x` formatting. It also preserves complete f32 tagged values through
  LLVM list construction and extraction so first-class and `apply` calls exercise
  the real tag-11 value. Its pinned LLVM 21.1.8 Release gate passes 18/18 focused
  tests and the complete f32 label passes 24/24. ASan+UBSan passes 10/10
  native/AOT tests with leak detection and 8/8 JIT tests with leak detection
  disabled for the existing parser/macro-expander retention. The next bounded
  persistence leaf makes native KB v2, VM KB save, and bytecode ESKB v1 reject
  runtime-shaped f32 values before opening output files, including nested logic
  terms and a 65-wrapper limit witness; traversal exhaustion fails closed. It
  removes ESKB's
  unknown-to-INT64/NIL defaults, preserves the accepted 59-byte ESKB fixture exactly,
  and passes 7/7 focused tests plus the 26/26 f32 label in the pinned LLVM 21.1.8
  image. The subsequent generic JSON leaf closes the native/VM inconsistency
  with an explicit shared rejection: source JSON no longer widens f32 to decimal,
  the VM no longer emits JSON null, and file entry points serialize before
  opening their destination. Direct, recursive list/object, and VM-vector cases
  cover finite and nonfinite values; supported JSON and INT64/F64 parsing remain
  unchanged. Its pinned LLVM 21.1.8 Release gate passes 7/7 focused tests, the
  existing JSON suite passes 3/3, and the complete f32 label passes 32/32.
  ASan+UBSan passes 5/5 VM/native/AOT tests with leak detection and 2/2 JIT
  tests with leak detection disabled for the existing frontend retention.
  Positive f32 persistence remains unsupported before any complete f32 or
  downstream trainer claim. The next bounded numeric-normalization leaf adds
  one checked raw/tagged f32-to-f64 lowering with a fixed positive quiet-NaN
  result for every binary32 NaN. Batch/layer norm now exact-promote canonical
  f32 gamma, beta, and epsilon in every four/five-argument form, while active AD
  rejects exact tag 11 before normalization node creation. O0/O2 AOT and
  cache-disabled JIT cover all 12 parameter positions, finite/nonfinite values,
  signed quiet/signaling NaNs with exact host bits, f64/int controls, and all 12
  AD refusals. The pinned LLVM 21.1.8 Release gate passes 7/7 focused tests,
  including the rebuilt semantic raw/tagged codegen gate, and
  the complete f32 label passes 38/38; ASan+UBSan passes the focused 7/7 with JIT
  leak detection disabled for the existing frontend retention. This does not
  add an f32 AD carrier, tensor dtype, source literal, or persistence encoding.
  The following bounded workspace leaf removes the native competition path's
  silent f32-to-zero default: canonical tag-11 salience now uses the same checked
  f32-to-f64 promotion before softmax, while malformed exact tag 11 raises before
  any module, content, or step-count mutation. Workspace salience remains
  nondifferentiable side-effect data, so `ws-step!` uses the same promotion when
  it executes inside a differentiated tensor body without claiming a gradient
  through salience. A direct malformed-layout atomicity test and public O0/O2
  AOT plus cache-disabled JIT fixtures cover finite competition, signed zero,
  infinities, signed quiet/signaling NaNs, and the active-tape case. In the
  pinned LLVM 21.1.8 image the focused Release matrix passes 5/5, the complete
  f32 label passes 43/43, and ASan+UBSan passes native/AOT 3/3 plus JIT 2/2
  (with leak detection disabled only for the existing eval-string retention).
  The following bounded system leaf splits quantity semantics from integer and
  resource domains. `format-relative` validates and promotes canonical tag 11
  through the shared f32-to-f64 authority, then uses its historical DOUBLE
  truncation at the 60/3600/86400-second boundaries. The shared integer
  extractor rejects every exact tag 11 before FD, regex/line/event/LRU/HTTP, or
  WebSocket handle lookup or mutation, closing the minimum-subnormal/handle-1
  alias without admitting f32 as a domain integer. A direct malformed-layout
  atomicity test and public `extern f32` O0/O2 AOT plus cache-disabled JIT
  resource-preservation fixtures pass the focused Release matrix 5/5; the
  complete f32 label passes 48/48. ASan+UBSan passes native/AOT 3/3 and JIT
  2/2, with leak detection disabled only for the existing eval-string
  retention on the JIT pair.
  The next bounded time-format leaf closes the remaining `format-iso8601`
  quantity fallback: canonical tag 11 is validated, exactly promoted, required
  to be finite and in `[-2^63, 2^63)`, then truncated through the existing
  finite DOUBLE contract. Malformed, nonfinite, and out-of-range f32 rejects
  before time conversion, string allocation, or wrapper-output assignment.
  Public O0/O2 AOT and cache-disabled JIT cover fractional values, signed zero,
  signed infinities and quiet/signaling NaNs, both signed-range boundaries, and
  INT64/F64 controls; a native test pins malformed-layout and rejection
  atomicity. The pinned LLVM 21.1.8 Release matrix passes 5/5 and the complete
  f32 label passes 53/53; the existing VM date/time surface and native time API
  suites pass 7/7 and 15/15. ASan+UBSan passes native/AOT 3/3 and cache-disabled
  JIT 2/2. The shared integer/resource extractor remains unchanged.
  The bounded sleep-inhibitor leaf closes a separate raw handle path:
  `allow-sleep` now rejects exact tag 11 through the established fail-closed
  resource diagnostic before extraction, lookup, slot mutation, or the Windows
  execution-state call. The raw extraction and every non-f32 branch remain
  byte-for-byte unchanged. Public O0/O2 AOT and cache-disabled JIT pin the
  minimum-subnormal/handle-1 alias and live-handle preservation; direct
  canonical and malformed tests pin diagnostics and wrapper-output atomicity.
  The pinned LLVM 21.1.8 Release system matrix passes 5/5, the complete f32
  label passes 53/53, and the VM system-info regression passes 6/6. ASan+UBSan
  passes native/AOT 3/3 and cache-disabled JIT 2/2.
  The bounded process-wait leaf closes the next raw PID path: exact tag 11 now
  reaches the same fail-closed resource diagnostic before PID extraction,
  `waitpid`, `OpenProcess`, or `WaitForSingleObject`. The original extraction
  and every non-f32 line remain byte-for-byte unchanged. Public O0/O2 AOT and
  cache-disabled JIT construct canonical f32 from a real child PID, prove that
  rejection leaves the INT64 PID waitable with status 7, and retain native
  canonical/malformed diagnostic and output-atomicity coverage. The pinned
  Release system matrix passes 5/5, the complete f32 label passes 53/53, and
  three existing VM process/system regressions pass. ASan+UBSan passes
  native/AOT 3/3 and cache-disabled JIT 2/2.
  The bounded `poll-fd` leaf closes the next raw descriptor/timeout path:
  exact tag 11 in either position reaches the established fail-closed resource
  diagnostic before either payload extraction or `poll`. The original raw
  extraction and every non-f32 line remain byte-for-byte unchanged. Public
  O0/O2 AOT and cache-disabled JIT use an actually ready pipe, prove both f32
  positions reject, then prove the INT64 descriptor remains ready; cleanup is
  unconditional. Native canonical/malformed tests pin both argument positions,
  diagnostics, output atomicity, and historical raw DOUBLE behavior. The
  pinned Release system matrix passes 5/5, the complete f32 label passes
  53/53, and the existing system completion regression passes 23/23 at O0 and
  O2. ASan+UBSan passes native/AOT 3/3 and cache-disabled JIT 2/2.
  The bounded `file-chmod` leaf closes the next raw integer bitmask path:
  exact tag 11 now reaches the established fail-closed resource diagnostic
  before path extraction, capability evaluation, raw mode extraction, or
  `chmod`. The original raw extraction and every non-f32 POSIX/Windows line
  remain byte-for-byte unchanged. Public O0/O2 AOT and cache-disabled JIT use
  PID-scoped real files, prove f32 rejection preserves mode 0644, then prove
  INT64 mode 0600; cleanup is unconditional. Native canonical/malformed tests
  pin the diagnostic, wrapper-output and filesystem atomicity, INT64 behavior,
  and historical raw DOUBLE behavior. The pinned Release system matrix passes
  5/5, the complete f32 label passes 53/53, and the system completion
  regression passes 23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 and
  cache-disabled JIT 2/2.
  The bounded `process-kill` leaf closes the next raw PID/signal path: ordered
  exact-tag-11 guards send PID and then signal through the established
  fail-closed integer/resource diagnostic before either raw payload read or
  operating-system action. The original raw extractions and every non-f32
  POSIX/Windows line remain byte-for-byte unchanged. Public O0/O2 AOT and
  cache-disabled JIT use readiness-handshaked signal-probe children plus
  bounded pipe polling to prove rejection delivers no SIGTERM marker before
  unconditional INT64 SIGKILL, wait, and pipe-state cleanup. Native canonical
  and malformed tests cover both positions, diagnostics, output/process
  atomicity, and prove INT64 and historical raw DOUBLE SIGTERM delivery. The
  pinned LLVM 21.1.8 Release system matrix passes 5/5, the complete f32 label
  passes 53/53, and the system completion regression passes 23/23 at O0 and
  O2. ASan+UBSan passes native/AOT 3/3 and cache-disabled JIT 2/2.
  The bounded `process-kill-tree` leaf closes the next raw PID/signal path:
  ordered exact-tag-11 guards send PID and then signal through the established
  fail-closed integer/resource diagnostic before either raw payload read,
  process-group send, or single-process fallback. The original raw extractions,
  group/fallback order, and every non-f32 POSIX/Windows line remain byte-for-byte
  unchanged. Readiness-handshaked probe children lead their own process groups,
  so public O0/O2 AOT and cache-disabled JIT exercise the primary
  `kill(-pid, SIGTERM)` branch, prove both f32 positions deliver no marker, and
  perform unconditional INT64 SIGKILL/wait/pipe cleanup. Native canonical and
  malformed tests cover both positions, diagnostics, output/group-signal
  atomicity, and INT64 and historical raw DOUBLE delivery. The pinned LLVM
  21.1.8 Release system matrix passes 5/5, the complete f32 label passes 53/53,
  and the system completion regression passes 23/23 at O0 and O2. The
  standalone process-tree regression passes, and ASan+UBSan passes native/AOT
  3/3 plus cache-disabled JIT 2/2.
  The bounded `process-setpgid` leaf closes the next raw PID/PGID path: ordered
  exact-tag-11 guards send PID and then PGID through the established fail-closed
  integer/resource diagnostic before either raw payload read or `setpgid`. The
  original raw extractions and every later non-f32 POSIX/Windows line remain
  byte-for-byte unchanged. Public witnesses use readiness-handshaked plain
  child targets plus a disposable group leader, prove both f32 positions leave
  group membership unchanged, then prove the supported INT64 moves before
  unconditional positive-PID SIGKILL, wait, and pipe-state cleanup. Native
  canonical and malformed tests cover both positions, exact diagnostics, and
  wrapper-output atomicity; disposable-child controls preserve INT64 and
  historical raw DOUBLE mutation behavior. The pinned LLVM 21.1.8 Release
  system matrix passes 5/5, the complete f32 label passes 53/53, and the system
  completion regression passes 23/23 at O0 and O2. The standalone process-tree
  regression passes, and ASan+UBSan passes native/AOT 3/3 plus cache-disabled
  JIT 2/2.
  The bounded `process-read-nonblocking` leaf closes the next raw descriptor and
  byte-count path: ordered exact-tag-11 guards send descriptor and then maximum
  through the established fail-closed integer/resource diagnostic before either
  raw payload read, validation, `fcntl`, arena allocation, or `read`. Every
  later non-f32 POSIX/Windows/WASM line remains byte-for-byte unchanged. Public
  ready-pipe witnesses prove both rejection paths consume no bytes before a
  supported INT64 read receives the complete payload and both descriptors are
  closed unconditionally. Native canonical and malformed cases cover both
  positions, exact diagnostics, wrapper-output sentinel, descriptor flags, and
  shared pipe-offset atomicity; controls preserve INT64 and historical raw
  DOUBLE behavior independently for descriptor and maximum. The pinned LLVM
  21.1.8 Release system matrix passes 5/5, the complete f32 label passes 53/53,
  and the system completion regression passes 23/23 at O0 and O2. The
  standalone process-tree regression passes, and ASan+UBSan passes native/AOT
  3/3 plus cache-disabled JIT 2/2.
  The bounded `socket-send` leaf closes the next raw descriptor path after the
  intervening string-classified `unix-socket-connect` operation. A
  first-operation exact-tag-11 guard sends the descriptor through the
  established fail-closed integer/resource diagnostic before raw payload read,
  string extraction, validation, or `send`; every later non-f32
  POSIX/Windows/WASM line remains byte-for-byte unchanged. Public real-socket
  witnesses prove rejection leaves the peer unreadable, then prove supported
  INT64 byte delivery before unconditional two-descriptor cleanup. Native
  canonical and malformed cases cover exact diagnostics, wrapper-output
  sentinel, peer non-readiness, and same-pair INT64 usability; controls preserve
  historical raw DOUBLE behavior. The pinned LLVM 21.1.8 Release system matrix
  passes 5/5, the complete f32 label passes 53/53, and the system completion
  regression passes 23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus
  cache-disabled JIT 2/2.
  The bounded `socket-recv` leaf closes the next raw descriptor and
  maximum-byte path. Ordered exact-tag-11 guards send the descriptor and then
  the maximum through the established fail-closed integer/resource diagnostic
  before either raw payload read, validation, capping, allocation, `fcntl`, or
  `recv`; every later non-f32 POSIX/Windows/WASM line remains byte-for-byte
  unchanged. Public independent preloaded socketpairs prove each rejection
  consumes no queued bytes before a supported INT64 call on the same pair
  receives the complete exact payload, treats descriptor 0 as valid, and
  performs unconditional cleanup. Native canonical and malformed cases cover
  both positions, exact diagnostics, wrapper-output sentinel, receiver flags,
  queue atomicity, and same-pair INT64 recovery; independent controls preserve
  INT64 and historical raw DOUBLE behavior in both positions. The pinned LLVM
  21.1.8 Release system matrix passes 5/5, the complete f32 label passes 53/53,
  and the system completion regression passes 23/23 at O0 and O2. ASan+UBSan
  passes native/AOT 3/3 plus cache-disabled JIT 2/2.
  The bounded `socket-close` leaf closes the next raw descriptor path. A
  first-operation exact-tag-11 guard sends the descriptor through the
  established fail-closed integer/resource diagnostic before raw payload read,
  sign validation, platform dispatch, or `close`; every later non-f32
  POSIX/Windows/WASM line remains byte-for-byte unchanged. Public real-socket
  witnesses prove rejection keeps the live endpoint open and usable before a
  supported INT64 close and unconditional peer cleanup, with descriptor 0
  treated as valid. Native canonical and malformed cases catch the exception
  in process and cover its exact type/message, wrapper-output sentinel,
  descriptor lifetime, same-pair usability, and subsequent INT64 close;
  independent controls preserve real INT64 and historical raw DOUBLE close
  behavior. The pinned LLVM 21.1.8 Release system matrix passes 5/5, the
  complete f32 label passes 53/53, and the system completion regression passes
  23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT
  2/2.
  The bounded `term-set-scroll-region` leaf closes the next raw terminal-row
  path. Ordered exact-tag-11 guards send top and then bottom through the
  established fail-closed integer/resource diagnostic before either payload
  read, range validation, TTY check, output, flush, or true return; every later
  non-f32 POSIX/Windows/WASM line remains byte-for-byte unchanged. Public fresh
  PTYs prove both f32 positions reject with zero emitted bytes and independently
  restored stdout before fresh supported INT64 controls emit exact DECSTBM.
  Native canonical and malformed cases cover both positions, exact exception
  types/messages, wrapper-output sentinel, and real-PTY no-write atomicity;
  independent controls preserve INT64 and historical raw DOUBLE behavior in
  both coordinates. The pinned LLVM 21.1.8 Release system matrix passes 5/5,
  the complete f32 label passes 53/53, and the system completion regression
  passes 23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus
  cache-disabled JIT 2/2.
  The bounded `fs-watch-poll` leaf closes the next raw watcher-handle path. A
  first-operation exact-tag-11 guard delegates to the established fail-closed
  integer/resource diagnostic before payload read, bounds/active lookup, stat,
  snapshot mutation, allocation, or return; every later non-f32
  POSIX/Windows line remains byte-for-byte unchanged. Public PID-scoped real
  files and watchers prove canonical rejection preserves an exact pending
  change for the same supported INT64 handle before its following poll returns
  `#f`, with independently bound cleanup and exit-time unlink fallback. Native
  canonical and malformed cases pin the exact exception type/message, output
  sentinel, and watcher snapshot atomicity; independent controls preserve
  live-handle INT64 and historical raw DOUBLE behavior. The pinned LLVM 21.1.8
  focused native/O0/O2 AOT/cache-disabled JIT matrix passes 5/5, the complete
  f32 label passes 53/53, and the system completion regression passes 23/23 at
  O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2.
  Positive watcher evidence is pinned Ubuntu/Linux;
  Windows `_stat64`, other POSIX stat behavior, and WASM were not executed. The
  guard itself is platform-neutral and all later non-f32 code is unchanged.
  The bounded `fs-unwatch` leaf closes the immediately following destructive
  watcher-handle path. A first-operation exact-tag-11 guard delegates to the
  established fail-closed integer/resource diagnostic before payload read,
  bounds/active lookup, slot `memset`, true return, or wrapper assignment;
  every later non-f32 line remains byte-for-byte unchanged. Public PID-scoped
  real files and watchers prove canonical rejection preserves the live slot and
  exact pending event through same-handle INT64 recovery, one successful
  supported unwatch, and a second `#f` unwatch. Native canonical and malformed
  cases pin the exact exception type/message, output sentinel, watcher lifetime,
  and pending-event atomicity; independent controls preserve live-handle INT64
  and historical forged raw DOUBLE release. The pinned LLVM 21.1.8 focused
  native/O0/O2 AOT/cache-disabled JIT matrix passes 5/5, the complete f32 label
  passes 53/53, and the system completion regression passes 23/23 at O0 and O2.
  ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Positive
  semantics ran on pinned Ubuntu/Linux; Windows, other POSIX systems, and WASM
  were not executed. The
  guard is platform-neutral and all later non-f32 code is unchanged.
  The bounded `string-truncate-display` leaf closes the next raw numeric
  maximum-width path while preserving its existing input-string validation
  order. Exact tag 11 now delegates to the established fail-closed
  integer/resource diagnostic immediately after successful input extraction
  and before maximum payload read, width early return, suffix extraction,
  prefix calculation, allocation, return, or wrapper assignment; every later
  non-f32 line remains byte-for-byte unchanged. Public canonical rejection
  preserves a source-level output sentinel; INT64 width two and historical
  forged raw DOUBLE payload word two return exact `".."` for `"abcdef"`, and
  INT64 width six returns the unchanged input. Native canonical and malformed
  cases pin the exact exception type/message and output sentinel with the same
  independent controls, plus a null-input control for the pre-existing
  empty-string precedence. The pinned LLVM 21.1.8 focused native/O0/O2
  AOT/cache-disabled JIT matrix passes 5/5, the complete f32 label passes
  53/53, and the system completion regression passes 23/23 at O0 and O2.
  ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Positive
  evidence ran on pinned Ubuntu/Linux;
  Windows, other POSIX systems, and WASM were not executed, and the VM uses a
  separate implementation. The compiled-runtime operation is platform-neutral.
  The bounded `string-index-of` leaf closes the next raw start-index path while
  preserving haystack extraction, string-or-character needle extraction, and
  invalid-input `#f` precedence. After both text arguments validate, exact tag
  11 delegates to the established fail-closed integer/resource diagnostic
  before start payload read, length/range handling, empty-needle return,
  `strstr`, result, or wrapper assignment; every later non-f32 line remains
  byte-for-byte unchanged. Public canonical rejection preserves a source-level
  assignment sentinel; INT64 starts zero/two return one/four, historical forged
  raw DOUBLE payload word two returns four, and empty-needle start two returns
  two. Native canonical and malformed cases pin the exact exception
  type/message and output sentinel, with character/empty-needle and invalid
  haystack/needle precedence controls. The pinned LLVM 21.1.8 focused
  native/O0/O2 AOT/cache-disabled JIT matrix passes 5/5, the complete f32 label
  passes 53/53, and the system completion regression passes 23/23 at O0 and O2.
  ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2. Evidence ran on
  pinned Ubuntu/Linux;
  Windows, other POSIX systems, WASM, and the separate VM implementation were
  not executed. The compiled-runtime operation is platform-neutral. The next
  bounded `string-pad-left` / `string-pad-right` width leaf now preserves input
  extraction and invalid-input `#f` precedence, then rejects exact tag 11 in
  the shared helper before width payload read, input length, early return,
  clamp, codepoint read, allocation, copy, result, or wrapper assignment. Every
  later non-f32 line remains byte-for-byte unchanged, including historical raw
  DOUBLE width behavior; the codepoint position was deferred to the follow-up
  below. Public
  canonical rejection covers both wrappers with independent assignment
  sentinels; INT64 width three and forged raw DOUBLE word three return exact
  `"007"` / `"700"`, and invalid input retains `#f` precedence. Native
  canonical and malformed tests for both directions pin the exact exception
  type/message and output sentinel with the same controls. The pinned LLVM
  21.1.8 focused native/O0/O2 AOT/cache-disabled JIT matrix passes 5/5, the
  complete f32 label passes 53/53, and the system completion regression passes
  23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT
  2/2. Within
  `system_builtins.c`, the known remaining bounded count inventory is one
  shared helper, one unguarded direct raw codepoint read, two public builtins,
  and two exposed argument positions. The guard-dominated width read remains
  syntactically present; this is not a whole-program exhaustive claim. The
  shared codepoint follow-up now preserves input and width precedence plus the
  unchanged no-padding early return, then rejects exact tag 11 before raw
  codepoint read, UTF-8 fallback, allocation, copy, result, or wrapper
  assignment. Public tests cover both wrappers, independent sentinels,
  both-f32 width-first rejection, lazy unused codepoints, invalid-input
  precedence, and exact INT64/raw-DOUBLE word-48 `"007"` / `"700"` controls;
  native canonical and malformed tests cover both directions with the exact
  diagnostic and unchanged wrapper output. The pinned focused Release matrix
  passes 5/5, the complete f32 label passes 53/53, and the system completion
  regression passes 23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus
  cache-disabled JIT 2/2.
  The next source-order leaf rejects exact tag 11 as the first operation of
  `file-lock`, before descriptor payload read, `fcntl`, result construction,
  or wrapper assignment. A forked child opening the same file proves
  rejection leaves it unlocked, while INT64 and historical raw DOUBLE
  controls acquire a real POSIX advisory lock and INT64 `file-unlock` releases
  it. Native canonical and malformed layouts preserve the exact diagnostic and
  unchanged wrapper output. The pinned focused Release matrix passes 5/5, the
  complete f32 label passes 53/53, and the system completion regression passes
  23/23 at O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT
  2/2.
  The final bounded source-order leaf rejects exact tag 11 as the first
  operation of `file-unlock`, before descriptor payload read, `fcntl`, result,
  or wrapper assignment. Tests establish the parent lock through INT64 and use
  a forked child opening the same file to prove rejection preserves it;
  same-handle INT64 and historical raw DOUBLE controls release it. Native
  canonical and malformed layouts preserve the exact diagnostic and unchanged
  wrapper output. The pinned focused Release matrix passes 5/5, the complete
  f32 label passes 53/53, and the system completion regression passes 23/23 at
  O0 and O2. ASan+UBSan passes native/AOT 3/3 plus cache-disabled JIT 2/2.
  The post-leaf direct tagged numeric payload scan is closed for the bounded
  integer/resource positions in `system_builtins.c`: the remaining raw reads
  are guard-dominated in their documented order. This translation-unit closure
  is not a whole-program or full-f32 claim.
  See the
  [classifier inventory](docs/f32-scalar-classifier-inventory.md).
  The allocator/F32 integration follow-up also guards generic numeric dispatch
  when a compile-time non-f32 constant makes the generated f32 arm unreachable.
  In a fresh pinned Release build, the checked-promotion suite passes 22/22 and
  the focused f32 tagged-codegen test passes; ASan+UBSan compilation of the
  original fixture passes at O0 and O2. A linked shared-library follow-up
  preserves the selected C++ driver's invocation name through symlink
  resolution, restoring the C++ runtime dependency; the C and ctypes ABI gate
  passes at O0 and O2. Independent review found and the union fixes a REPL
  rollback interaction before the clean gate; no sanitizer suppression or
  fallback was added.
  The bounded runtime type-symbol leaf adds the versioned pointer API
  `eshkol_type_of_ref_v1`, moves the complete semantic registry into the
  runtime archive, and keeps the legacy by-value C API as a delegating wrapper.
  Exhaustive C fixtures cover every direct tag, heap and callable subtype,
  malformed f32 byte, null/unknown control, and legacy conveyed-field parity.
  The pinned LLVM 21.1.8 focused Release gate passes 4/4 and the runtime-only
  ASan+UBSan gate passes 2/2 with leak detection. The accepted by-value padding
  limitation remains documented. The bounded native Scheme follow-up now
  preserves the original 16-byte carrier and delegates through the pointer
  mapper, returns the canonical interned semantic symbol, assigns `Symbol`
  typing, and supports direct, stored first-class, `apply`, and `map` routes.
  Literal and repeated `eq?` identity, representative direct/heap/callable
  subtype names, the unknown fallback, exhaustive malformed f32 controls, and
  AOT/JIT O0/O2 are covered under pinned LLVM 21.1.8. The bounded VM follow-up
  replaces the old string result with a VM-interned `VAL_SYMBOL`, roots its
  per-VM cache across region evacuation, and maps every declared VM value tag
  0--34 to the accepted semantic spelling. Raw binary32 classes, literal and
  repeated identity, direct/stored/`apply`/`map` routes, and the cached prelude
  are covered in Release and ASan+UBSan. Type-symbol spelling, heap-object, and
  cache-capacity allocation failures mark the VM fatal rather than exposing
  null as a semantic result; the string-allocation path has an injected failure
  witness. Compiler-authored callable metadata distinguishes uncaptured source
  lambdas (`lambda-sexpr`), captured source closures (`closure`), and builtin
  native wrappers (`primitive`) without inferring a subtype from arity or
  function PC. The metadata survives ESKB serialization, PC rebasing, region
  evacuation, and parallel publication; old, handwritten, and unmarked
  synthesized closures remain the contract's generic `procedure`.
  This is a type-reflection leaf, not a full-f32 or transformer-repin claim.
  A clean isolated provisional successor union now composes that leaf with the
  reviewed native type-symbol, region-open, VM scalar-activation, conjugate,
  VM string-pack signed-shift, and full-carrier region-evacuation repairs. On
  the pinned Ubuntu 22.04 / LLVM 21.1.8 image, Release passes the complete
  f32 label 59/59, focused runtime/VM coverage 7/7, standalone internals 80/80,
  and the dedicated VM type-symbol source gate. ASan+UBSan passes the same
  59/59 label with leak detection disabled for the known JIT parser-retention
  boundary; a native/AOT/runtime/VM LeakSanitizer subset passes 11/11, followed
  by the standalone internal and VM type-symbol source gates with leak
  detection enabled. This union remains blocked from acceptance: the VM maps
  source lambdas and captured closures to `procedure`, while the native
  semantic contract can classify them as `lambda-sexpr` and `closure`.
  Direct VM probes record `type_introspection_test.esk` at 20/21 (the
  `type-of lambda` case) and `mixed_types_stress_test.esk` failures for
  `lambda is CALLABLE` and `closure is CALLABLE`. The exhaustive nested
  operation matrix, allocation guards beyond the reviewed type-symbol path,
  and whole-f32 acceptance also remain pending.
  A bounded VM unary min/max parity leaf now sends the prelude's one-argument
  calls through its existing `_min2`/`_max2` numeric entry, so canonical host
  f32 is explicitly promoted and returned as DOUBLE. Direct and stored
  first-class source calls cover both zero signs, subnormal, finite, infinity,
  and fixed positive quiet NaN bits; DOUBLE and integer controls retain their
  existing result kinds. On the pinned LLVM 21.1.8 image, focused Release
  checks pass 5/5 and ASan+UBSan+LSan VM checks pass 4/4. The leaf is composed
  with the reviewed VM closure-arity successor, which preserves public unary
  min/max and enforces zero-argument rejection and 255/256/512 boundaries.
  A bounded VM numeric follow-up routes canonical host F32 through checked
  promotion for the VM-only `sign` builtin and makes VM `numerator` reject F32
  explicitly instead of returning integer zero. Public direct and stored
  first-class calls cover both signs; integer, DOUBLE, and rational VM behavior
  stays as before. Full `numerator` F32 support still requires resolving the
  existing DOUBLE result-kind mismatch: native returns its DOUBLE input while
  the VM truncates it to INT64. Native `sign` has no builtin route. This is not
  whole-F32 parity. Pinned LLVM 21.1.8 Release and ASan+UBSan+LSan VM gates
  each pass 4/4 with leak detection enabled in the sanitizer lane.
  The provisional exact composition at `fc702166`/tree `6ff63467` includes
  the compatible closure-arity width/minimum repair, VM F32 unary min/max,
  VM F32 sign, and explicit numerator refusal. Pinned LLVM 21 Release and
  ASan+UBSan+LSan affected CTest each pass 14/14, including native unary
  AOT/JIT, VM source/ESKB arity, type and first-class controls, and prelude
  freshness. The sanitizer build uses the repository's scoped suppression
  for pre-existing frontend AST leaks while retaining leak detection; no
  transformer runtime pin or whole-F32 acceptance changes here.
  A bounded VM closure/upvalue transport witness adds genuine host-bit F32
  capture, stored first-class reads, shared captured mutation, and closures
  escaping `with-region` before read or mutation. The public raw-bit inspector
  verifies exact signed-zero, subnormal, finite, quiet-NaN payload, and
  signaling-NaN payload words. INTEGER and DOUBLE closure results remain
  unchanged and explicitly fail the F32 inspector without consuming the VM
  value. Pinned LLVM 21.1.8 Release and ASan+UBSan+LSan focused VM gates each
  pass 7/7 with leak detection enabled in the sanitizer lane. This is a
  test-only carrier proof, not whole-F32 acceptance.
  The bounded numeric follow-up makes `denominator` accept canonical F32 on
  native AOT/JIT and the VM, returning the same exact INT64 1 as each existing
  DOUBLE route. Native checks the canonical carrier before result mutation;
  direct and stored source calls, signed zeros, subnormals, infinities, NaN,
  malformed native carriers, and non-F32 controls are pinned. `sign` remains
  VM-only with its existing F32 handling. `numerator` still rejects F32 on
  both engines because native DOUBLE returns DOUBLE unchanged while VM DOUBLE
  truncates to INT64; reconciling that public result contract is a separate
  whole-F32 prerequisite. No R7RS numerator/denominator semantics are claimed
  by this compatibility leaf. Pinned LLVM 21.1.8 Release and ASan+UBSan+LSan
  focused native AOT/JIT, VM, and ABI gates each pass 7/7 with leak detection
  enabled in the sanitizer lane.
  A bounded VM continuation/exception carrier witness now seeds genuine
  host-bit F32 and checks exact scalar and vector payloads after `call/cc`
  and `call-with-current-continuation` reentry from a returned function, and
  through `guard` and `with-exception-handler`. It covers both zero signs,
  subnormals, finite values, signed quiet/signaling NaN payloads, and INTEGER/
  DOUBLE wrong-tag controls. The continuation uses a vector-backed mutable
  store because VM REPL top-level slots below the saved stack top can rewind
  on reentry; that existing representation limit is separate from F32 bit
  transport. `error-object-irritants` remains unsupported in the VM, so this
  does not claim that surface or whole-F32 acceptance. Pinned LLVM 21.1.8
  Release and ASan+UBSan+LSan focused VM gates each pass 6/6 with leak
  detection enabled; existing integer continuation and guard source controls
  also produce their documented results.
  The separate shared-library tail-finalizer repair `97c40c9d` is composed
  byte-identically onto this provisional runtime line as `6be7c29b`.
  Tail-body forwarders now bind the original Eshkol function before C ABI
  export wrappers rename it; the fixed compiler links the 46-source private
  TR3 root that failed with 52 missing-public-entry errors on pinned `81298`.
  The base repair is sealed at `tr3-shared-tail-finalizer-97c40c9d/SHA256SUMS`
  (`90f316c...`). On the composed source, pinned LLVM 21 Release shared-library
  and F32 unary AOT/JIT gates pass 6/6. ASan+UBSan passes the new shared-tail
  and four F32 route gates 5/5 with leak detection disabled for existing
  parser/macro-expander allocations; the existing ABI sanitizer harness cannot
  dlopen a statically instrumented library. Both limits and the exact build
  artifacts are sealed at `f32-combined-shared-tail-6be7c29b/SHA256SUMS`
  (`6d2ab54...`). This compiler prerequisite is not whole-F32 acceptance or a
  transformer runtime repin.
  The bounded integer/rational F32 safety matrix `5091032b` is composed onto
  this provisional line as `8b937b67`. VM gcd/lcm/modulo/quotient now reject
  F32 before unsafe integer coercion or unreviewed result semantics; native
  lcm rejects F32 before FPToSI. Native modulo/remainder/quotient and VM
  nonzero remainder keep their established F32-to-DOUBLE routes. Exact leaf
  Release and ASan+UBSan+LSan gates pass 6/6 each, sealed at
  `f32-integer-rational-matrix-20260924/SHA256SUMS` (`78ad8ead...`). On the
  composed compiler/runtime source, the same focused Release and sanitizer
  gates pass 6/6 each; root sealed the combined logs at
  `f32-integer-rational-combined-8b937b67-20260925/SHA256SUMS`
  (`89fe8f98...`). Sanitizer AOT compilation uses the repository LSan
  suppressions for inherited parser/macro allocations. Native/VM DOUBLE
  modulo/quotient and inexact remainder-zero policies still disagree at that
  boundary. The independently reviewed VM modulo/quotient parity correction
  `b9f90ec9`/tree `90c0a2c` is composed byte-identically as `527f9fb1`.
  Stored DOUBLE modulo now uses floored fmod and stored DOUBLE quotient uses
  truncated division with DOUBLE results, matching native and the direct VM
  modulo route. Canonical host-bit F32 follows the same checked path in
  direct/stored calls; all-INT64 and bignum controls remain pinned. Exact
  source/tree Release and ASan+UBSan+LSan gates each pass 8/8, sealed at
  `f32-modulo-quotient-parity-20260924/SHA256SUMS` (`6f9b0041...`). Direct VM
  modulo zero still uses a fatal opcode rather than a catchable stored-call
  error, and inexact remainder-zero, GCD/LCM integer-domain policy and
  numerator remain unresolved. Whole-F32 acceptance and transformer repin
  remain pending.
  A bounded `numerator` successor first makes VM DOUBLE and exact bignum
  direct/stored results match the existing native tagged helper without an
  unsafe float-to-int cast; exact rational halves and native wrong-tag
  passthrough remain pinned. It then admits canonical host-bit F32 through
  checked promotion to that inexact DOUBLE result kind in native AOT/JIT and
  VM direct/stored calls. Signed zero, subnormal, finite, infinity, fixed NaN,
  malformed native carriers, and non-F32 controls are covered. This is the
  existing native compatibility policy, not full R7RS lowest-terms fraction
  behavior for inexact `numerator`/`denominator`; whole-F32 acceptance and
  transformer repin remain pending.
  The sealed no-change GCD/LCM domain audit on exact `4e483c89` found a
  prerequisite wider than F32: native direct `gcd 6.0 4.0` returns exact 2,
  but stored native `gcd` returns exact 0; the VM returns exact 2 on both.
  Both substrates truncate fractional DOUBLE arguments, native/VM bignum
  routes diverge, and the VM sanitizer catches an out-of-range float-to-int
  cast at `gcd 1e300 4.0`. [R7RS §6.2.6](https://standards.scheme.org/corrected-r7rs/r7rs-Z-H-8.html)
  requires integer arguments and an inexact result when an accepted argument
  is inexact. Preserve F32 rejection until
  the non-F32 domain, result kind, direct/stored parity, and bignum/overflow
  rules are reconciled. Evidence:
  `f32-gcd-lcm-domain-blocker-4e483c89-20260924/SHA256SUMS`
  (`5aa474e5...`); no production code or transformer pin changed.
  A bounded VM-only GCD/LCM safety prerequisite now checks the input domain
  before float-to-int conversion: direct and stored calls reject non-number,
  unsupported bignum, fractional/nonfinite/out-of-range DOUBLE, and
  INT64_MIN magnitude with a catchable error; LCM also rejects an int64
  result overflow. Valid exact INT64 and finite integral in-range DOUBLE
  retain the VM's historical exact INT64 result. This does not fix native
  direct/stored GCD parity, implement inexact R7RS result kind or wide exact
  GCD/LCM, or admit F32; those remain separate gates.
  The native LLVM GCD/LCM safety prerequisite now checks raw, tagged/stored,
  and dual-primal inputs before float-to-int conversion or signed negation.
  Direct and stored calls reject wrong types, fractional/nonfinite/out-of-range
  DOUBLE, and INT64_MIN magnitude with a catchable error; LCM also rejects
  unsupported bignum and int64 result overflow. Exact INT64 and existing
  GCD bignum paths remain, and valid in-range integral DOUBLE has the same
  exact INT64 outcome in direct and stored calls. Canonical F32 still rejects.
  This is a safety and bounded parity step, not the R7RS inexact result-kind
  or wide-integer policy; those remain prerequisites to F32 admission.
  The subsequent VM exact-wide GCD leaf uses its existing bignum kernel for
  binary direct/stored all-exact INT64/bignum operands, matching the native
  exact GCD path and normalizing a fitting answer back to INT64. Mixed wide
  and inexact operands still reject; public LCM stays bounded to INT64 and
  F32 stays fail-closed. The source-backed matrix in
  `docs/gcd-lcm-result-contract.md` records the remaining inexact result-kind,
  wide LCM, arity, and AD policy decisions before F32 GCD/LCM admission.
  A later non-F32 result-kind leaf preserves the exact INT/bignum kernels
  while making native and VM direct/stored GCD/LCM return DOUBLE whenever
  an accepted int64-valued integral DOUBLE participates. Mixed wide-exact/
  DOUBLE GCD explicitly rejects on both substrates; a huge exact value with
  inexact zero previously exposed native NaN versus VM infinity. Fractional,
  nonfinite, out-of-range, malformed F32, and unsupported wide LCM remain
  guarded.
  This resolves the documented R7RS inexact example for bounded inputs, not
  arity, wide LCM, AD, or whole-F32 policy.
  The bounded F32 GCD/LCM successor admits canonical host-bit F32 only when
  finite, integral, and within the established int64 magnitude domain. Native
  JIT/AOT and VM direct/stored calls return the same DOUBLE result kind as an
  accepted integral DOUBLE. Fractional/nonfinite/out-of-range F32, malformed
  native layouts, and mixed exact-wide/inexact inputs reject explicitly.
  Variadic arity and dual rejection remain intact. Native reverse-tape AD-node
  operands also reject explicitly; reverse-tape admission remains blocked
  pending a derivative policy, with no gradient assigned.
  The browser WASM import glue now covers the five F32-era runtime imports in
  both REPL and site hosts. Its checked write barrier copies tagged carriers
  because browser regions are never reclaimed; exact F32 text formatting,
  type reflection, and native emergency transfer explicitly trap until their
  browser semantics are implemented. This is import coverage, not WASM F32
  feature acceptance.
- Interop wave 1: **H1 NumPy capsule-lifetime fix, SHIPPED (#458)** — the
  Python bindings' zero-copy tensor array now holds a strong reference to
  its owning `Context` via its NumPy capsule, so the array stays valid past
  the `Context` object's own lifetime (closed `.icc/silent-wrong-ledger.yaml`
  SW-44). See [docs/reference/bindings/python.md](docs/reference/bindings/python.md).
  The separate exactness-across-the-Python-boundary design doc remains a
  v1.4.0-connection item (implementation, not this fix).
- AD: SW-05 forward-over-reverse; ESH-0101 (recursion-depth guard coverage
  for top-level `define`d self-recursive functions, maintainer ruling
  2026-08-13 — see KNOWN_ISSUES.md); P6/P11 exact-coefficient and
  user-numerics re-cut on post-P5 master (verify exact-coefficient and
  reverse-Taylor suites together — both were shipped complete in
  v1.3.0-evolve and this is a re-verification pass, not new staging).
- Correctness debt: the `(or X null)` miscompile lineage (#229), the REPL
  no-return verifier (#244), and an `ArithmeticCodegen::mod` srem-vs-modulo
  audit.
- Assurance (W2, ADR-0010 v1.3.5 set): CI lanes for previously-unwired
  harnesses, oracle/ledger schema checks in CI (`completion-oracles.yaml`
  parse + ID-uniqueness), a self-verdict scanner, build fingerprints, and
  ICC adversarial eval scenarios (dirty worktrees, stale artifacts,
  model-server outage, disk pressure, failed gates).
- Performance (W3): benchmarks-on-our-axes wave 1, published and
  reproducible (exact-AD cost curves; flat-RSS resident loops).
- Codebase (W4): decompose `vm_run.c` (the file every VM fix touches); the
  docs-only CI-context fix; KNOWN_ISSUES version targets re-pinned; this
  ROADMAP re-dated.

---

## v1.4.0-connection — the systems profile (target: Nov 2026) - PLANNED

**Focus:** A resource-sound systems profile — connect to the outside world
under the same discipline that made `Qubit` linear.

- [ ] Signed-curvature stereographic geometry (ADR-0012): one κ-stereographic
      chart for all curvature signs in the canonical-flat `alpha = 1` gauge,
      analytic through `K = 0` with curvature jets, squared-distance geodesic
      attention, versioned coordinate migration (`x_v2 = 2 x_v1`), and the
      binary128 reference-grid gates across every `K` binade
- [ ] Stochastic Binary Lambda Calculus, first slice (ADR-0011): `core.sblc`
      with the four-form `sblc-v1` prefix code, fair choice, seeded sampling,
      exact rational distribution semantics under a step bound, the exact
      `2^-length` prior, bounded enumeration and conditioning, and the SBLC gate
      (codec oracle, exact mass conservation, coin-trace oracle, JIT/AOT parity)
- [ ] TCP/UDP sockets with linear resource types (guaranteed close)
- [ ] TLS/SSL via system libraries
- [x] Non-blocking I/O with event loop (kqueue / epoll / IOCP) - SHIPPED in v1.3.4-evolve
- [ ] Unix domain sockets for local IPC
- [ ] HTTP client/server and WebSocket, on the shipped event loop
- [ ] Linear types for all handles: `open → borrowed → closed` with compile-time tracking
- [ ] Borrow pattern for temporary resource access
- [ ] Agent runtime unblocked: SSE streaming, subprocess pipe contract,
      durable session persistence
- [ ] W5 interop wave 2: exactness across the Python/NumPy boundary and a
      silent-demotion CI gate; the definition-of-done rule (external-oracle
      case + Python one-liner per new AD/quantum feature) goes live
- [ ] qLLM backward completion: `input2` wiring for conv2d/batchnorm/
      layernorm/attention tape nodes
- [ ] Assurance: ADR-0010 v1.4 set (A10-A13), TSan-required lane, SymPy
      oracle pilot on the exact-AD surface
- [ ] Performance: benchmarks wave 2 (Ozaki CRT vs. cuBLAS/Accelerate,
      accuracy and throughput, pinned hardware)
- [ ] Codebase: native image I/O dependency removal; decompose
      `runtime_regions.cpp`; doc-truth ratchet phase 1 (rank + quota, not
      yet release-blocking)
- [ ] W6 distributed: PJRT client spike + XLA multi-device single-host +
      native collectives over sockets

---

## v1.4.1 — the ABI release (target: Dec 2026) - PLANNED

- [ ] OALR ABI v2 (32-byte header, layout descriptors, escape ledgers,
      transfer capsules); portable tail transfer (musttail + bounded-stack)
- [ ] PGO: canonical training workload + one-shot `llvm-profdata` merge
- [ ] Codebase: decompose `bignum.cpp`; shell-hardening epic wave 1

---

## v1.5.0-intelligence (target: Q1 2027) - PLANNED

**Focus:** Neural and symbolic computation flow bidirectionally.

Informed by the [Neuro-Symbolic Architecture](docs/future/NEURO_SYMBOLIC_COMPLETE_ARCHITECTURE.md).

**Flagship:** `core.dbsp` GA (W1) + native-product PGO in the release
workflow (ADR-0007 Phase 1).

- [ ] SBLC second slice (ADR-0011): universal `U⊕` self-interpreter, resumable
      E6 search with deterministic mesh sharding, DBSP/N3 receipts, VM PRNG
      parity, experimental differentiable proposals (REINFORCE, projected
      relaxations) — all under exact-verification gates
- [ ] Symbol embeddings (learnable vector representations of KB symbols)
- [ ] Soft unification (differentiable similarity — gradients flow through matching)
- [ ] LSTM and GRU cells (standard recurrent neural architectures)
- [ ] Differentiable logic programs (gradients flow through rule application)
- [ ] Attention over knowledge base (neural query mechanism over symbolic facts)
- [ ] Gradient estimators for discrete operations (Gumbel-Softmax, straight-through)
- [ ] Noesis M2 surface: HNSW, BPE, sparse tensors, int/complex tensors
- [ ] Assurance: A6 + A8 full race matrix; SymPy oracle becomes a release
      gate; machine-checked-invariants ramp begins (Taylor-tower semantics
      proof sketch)
- [ ] Codebase: decompose `vm_geometric.c`; ADR-0008 tooling core lands
      (`eshkol check` + LSP on a shared workspace-analysis core)
- [ ] W6 distributed: Tier-1 data-parallel + Tier-2 mesh bit-identity gate

Note: the arbitrary-order AD substrate this section used to stage here
(P5/P7/P9/P10) shipped complete in v1.3.0-evolve, ahead of this plan — see
[Arbitrary-order AD track](#arbitrary-order-ad-taylor-tower-track----shipped-ahead-of-schedule)
below.

---

## v1.5.1 (target: Q1-Q2 2027) - PLANNED

- [ ] DBSP circuits + resident A/B sessions with a hard steady-state bound
- [ ] Doc-truth gate becomes release-blocking (ratchet reaches zero
      unsupported claims in release-critical docs)

---

## v1.6.0-reasoning (target: Q2 2027) - PLANNED

**Focus:** Make the logic engine production-grade.

- [ ] Backward chaining inference (Prolog-style goal-directed proof search with backtracking)
- [ ] Forward chaining inference (production rules with fixed-point derivation)
- [ ] Constraint solving (finite domain constraints, SAT solver integration)
- [ ] Knowledge graphs (RDF-style triple store with SPO/POS/OSP indexing)
- [ ] Knowledge graph embeddings (entity-relation-entity triples as learnable vectors)
- [ ] Staged AD ABI (ADR-0002b Phase G): the dense primitive registry as a
      real table, cotangent-layout and error ABI first-class, strict-mode
      kernel flag
- [ ] W6 distributed: sharding annotations on the staged dense graph ->
      GSPMD multi-host + distributed DBSP

Note: sparse high-order AD tensors (P12), originally staged here, shipped
complete in v1.3.0-evolve.

---

## v1.6.1 (target: Q3 2027) - PLANNED

- [ ] DBSP traces + staged scratch plan (ADR-0007 Phase 2: staged dense
      graph + static memory plan)
- [ ] Region-safety machine-checked-invariant work begins

---

## v1.7.0-synthesis (target: Q3-Q4 2027) - PLANNED

**Focus:** Programs that write and improve programs.

- [ ] Neural-guided program search (beam search with neural scoring for candidate ranking)
- [ ] Type-directed synthesis holes (`??` syntax — compiler searches for well-typed completions)
- [ ] Graph Neural Networks (message passing, neighborhood aggregation, graph attention)
- [ ] Synthesis from input-output examples (inductive programming)
- [ ] Neural theorem provers (neural heuristic guides symbolic proof search, using v1.5 embeddings + v1.6 chaining)
- [ ] Recursive IVM; staged optimizer; program-capsule foundations (ADR-0005)

---

## v1.8.0-platform (target: Q4 2027) - PLANNED

**Focus:** Eshkol runs on everything, controls everything.

Informed by the [Multimedia System Architecture](docs/future/MULTIMEDIA_SYSTEM_ARCHITECTURE.md).

- [ ] Cross-platform windowing (X11/Wayland, Cocoa, Win32)
- [ ] Event system (keyboard, mouse, touch, window events)
- [ ] Real-time audio (CoreAudio, ALSA, WASAPI with callback-based I/O)
- [ ] MIDI input/output for instrument control
- [ ] Vulkan Compute for cross-platform GPU (beyond Metal/CUDA)
- [ ] Embedded cross-compilation (ARM bare-metal, RISC-V)
- [ ] `core.memory` as a Z-set; resident recurrent AD

Multi-GPU / multi-node dispatch is no longer gated behind "demonstrated
demand" here — it is W6's ladder (Tier 1 PJRT/XLA multi-device lands at
v1.4.0; GSPMD multi-host at v1.6.x). This section keeps only the
single-machine platform surface.

---

## v1.8.1 (target: Q1 2028) - PLANNED

- [ ] Resident-agent circuit pilot
- [ ] Closed-world whole-program optimization (ADR-0007 Phase 3)
- [ ] W6 distributed: fault tolerance / elasticity (checkpoint/restart
      pulled forward from what was v1.9.1 below)

---

## v1.9.0-types (target: Q1-Q2 2028) - PLANNED

**Focus:** The type system becomes a proof system.

- [ ] Full dependent type enforcement (compile-time errors, not just warnings)
- [ ] Refinement types (`(Refine Integer (> x 0))` with SMT solver integration)
- [ ] Effect types (tracking `Pure`, `IO`, `State`, `Exception` at the type level)
- [ ] Algebraic effects and handlers (structured side-effect management)
- [ ] Row polymorphism for records (structural subtyping)
- [ ] Higher-rank types (rank-2 polymorphism for combinators)
- [ ] Session types for communication protocols
- [ ] `IncrementalizePass` (DBSP incrementalization as a compiler pass)

---

## v1.9.1 (target: Q2 2028) - PLANNED

- [ ] Checkpoint/restart; session protocol
- [ ] AD-aware debugger/profiler (inspect dual numbers, reverse tape,
      Taylor coefficients, region lifetimes) on the ADR-0008 execution core

---

## v1.9.2 (target: Q3 2028) - PLANNED

- [ ] Spill tier
- [ ] Reflective self-modification (capsule `psi_program` updates)

---

## v2.0-starlight (target: Q4 2028) - RESEARCH

**Focus:** Quantum computing meets formal verification, and the AD/DBSP
lines converge into one primitive.

Leverages OALR linear types (no-cloning theorem) and AD (variational circuits).

### Unified differentiation
- [ ] Unified `differentiate` primitive: `numeric` and `incremental`
      interpretations over the closed world (W1 endpoint)
- [ ] Typed-static-reverse (#216) north-star work begins only after the
      resident tape (#214) has a public training win

### Quantum Type System
- [x] Qubit type with linear resource tracking - SHIPPED in v1.3.4-evolve.
      Shipped as a **warning-level type annotation** (conformity audit item a7,
      corrected 2026-08-25 from "no-cloning enforced at compile time": a clone
      printed `[WARN]`, exited 0 and wrote a runnable binary). **Made a real
      compile-time error in v1.3.5:** a linearity violation now stops code
      generation, exits nonzero and writes no artifact in the DEFAULT mode, not
      only under `--strict-types`; `--unsafe` remains the documented bypass.
      Enforcement is a worst-case-path analysis of the binder's own body
      (`TypeChecker::analyzeLinearUses`), covering `define`/`lambda` parameters
      and `let` bindings, `if` branch-exclusivity, sequences and short-circuit
      forms, and `let` alias chains. **Extended in the same release** to the
      remaining control-flow forms — `cond`, `case`, `when`, `unless`, `do`,
      `guard`, `match` and `set!` are all walked, clause ladders by their exact
      worst path — to alias laundering through an immediately applied `lambda`
      and through a `set!` move, and to rejecting a bare linear reference stored
      into an untyped container. **VM engine parity closed**: the bytecode VM
      runs the same judgment (not a second implementation), so source execution
      and `--emit-eskb` both refuse a violating program and write no bytecode.
      **Remaining BUILD ITEMS**, each announced per binding on stderr rather
      than silently assumed, and specified in
      `docs/COMPLETE_LANGUAGE_SPECIFICATION.md` 3.6.8: loop-carried accounting,
      so a named `let` or `do` whose body names the qubit can be ruled on rather
      than reported undecidable; a `guard` whose handler names the binding;
      `call/cc` re-entry; once-closures / affine closure typing for dynamic
      duplication; and the last name-keyed residue — a qubit returned from a
      function with an unannotated return type, which needs interprocedural
      inference and is what ADR-0004's **place-keyed** PlaceId/FlowEnv closes,
      for linear types and ownership `move` alike (ADR-0004, ADR-0000
      Stage 12).
- [ ] Quantum register types `qreg<n>` with compile-time dimension
- [ ] `define-quantum-region` scoping for qubit allocation and deallocation
- [ ] Quantum region compilation, QAOA — on the quantitative types from v1.9

### Quantum Operations
- [x] Gate primitives: H, CNOT, Rz, T, S, SWAP, Toffoli, arbitrary unitaries - SHIPPED in v1.3.3-evolve
- [x] Measurement with classical outcome - SHIPPED in v1.3.3-evolve
- [ ] Circuit compilation and optimization (gate fusion, qubit mapping)
- [x] AD integration for variational algorithms - SHIPPED in v1.3.3-evolve (custom-VJP tape nodes carry Moonlab's exact adjoint)

### Hybrid Classical-Quantum
- [x] Variational Quantum Eigensolver (VQE) - SHIPPED in v1.3.3-evolve
- [ ] Quantum Approximate Optimization Algorithm (QAOA)
- [x] Quantum machine learning (parameterized circuits with AD) - SHIPPED in v1.3.3-evolve
- [x] Integration with Moonlab quantum simulator - SHIPPED in v1.3.3-evolve; pinned to Moonlab v1.2.0 in v1.3.4-evolve

### Performance gates (W3 endpoint)
- [ ] >=10k steps with no recompile, 1 primal + 1 reverse pass, zero
      post-warmup allocations, GEMM-dominated staged throughput >=80% of
      native vendor-BLAS; application/kernel IR PGO (ADR-0007 Phase 4)

### Distributed gates (W6 endpoint)
- [ ] Tier 1: >=85% scaling efficiency at 8 devices
- [ ] Tier 2: bit-identical gradients at any node count, zero post-warmup
      allocations per rank

### Formal Verification
- [ ] Integration with proof assistants (Lean) for certified compilation —
      the Lean kernel export re-checks the compiler on the normative
      corpus, and "HoTT-inspired" is retired as a claim in favor of this
      concrete, checkable one (ADR-0004)
- [ ] Quantitative type theory for unified linear/quantum resource tracking
- [ ] Lean-certified formal verification of the validated-AD Taylor models
      (P8) already shipped in v1.3.0-evolve (`taylor-model`, `tm-range`,
      `tm-eval`) — proving the interval-remainder enclosures sound and
      order-tightening, beyond the current dense-sampling remainder
      estimate

---

## Release Timeline

| Version | Date | Theme | Key Deliverables |
|---------|------|-------|-----------------|
| **v1.1.13** | Apr 2026 | Accelerate | Windows ARM64, 16-lane release matrix, VM closure fixes, mobile site |
| **v1.2** | May 2026 | Scale | Model serialization, Python bindings, image I/O |
| **v1.3.0-evolve** | Jul 2026 | Evolve | **SHIPPED.** R7RS libraries, string interpolation; arbitrary-order AD **P0–P12 complete** (Taylor towers, exact coefficients, GUW multivariate, reverse-over-Taylor, tensor towers, Taylor models, sparse tensors — closes ESH-0118, delivered ahead of the original P1-only plan); full R7RS conformance (34/34 vs. chibi-scheme); TCO/closure/memory robustness hardening; permanent adversarial-testing infrastructure |
| **v1.3.1 → v1.3.4-evolve** | Jul-Aug 2026 | Evolve | **SHIPPED 2026-08-19** (tag `v1.3.4-evolve`, commit `694c3179`). v1.3.1: flat memory for resident/daemon loops, iterative reader. v1.3.2: thread-safe regions, deeper evacuation. v1.3.3: opt-in differentiable quantum computing (Moonlab VQE/CHSH), ML-KEM post-quantum crypto, `core.dbsp` incremental dataflow, 100% executable language coverage. v1.3.4: automatic per-iteration reclamation matching explicit regions (ESH-0214e), race-free `parallel-map`, exact gradients through every callable form on the LLVM backend (the bytecode VM's `divergence`/`curl` are still central-difference FD — corrected 2026-08-25, conformity audit item a13, BUILD ITEM to remove them targets v1.5.0), shortest-round-trip float printing, checked `(the <type> expr)` ascription + predicate narrowing, linear `Qubit`, high-precision numerics (Ozaki-II exact/fast GEMM, mixed-precision `linear-solve`, `i128`), Moonlab v1.2.0 (QGT/QNG), full hosted-VM tensor-matmul parity. Plus the consumer-hardening correctness wave: fatal compile diagnostics, tag-decided exactness on both engines, exact-point differentiation, same-unit `define-library` on all three back ends, a real `--shared-lib` (#377), the portable event loop, the fixed-point/`i128` accumulation engine, region handles, **the qLLM bridge implementation (#386/#392 — the completion the v1.1 line above claimed early)**, and embedding/Fréchet-mean backward passes. **Release gates** (RELEASE_NOTES.md, measured on the release cut): aggregate suite 45/45 suites / 770 tests; CTest 190/190 (remeasured 2026-08-25 against `4bf871a0`, `evidence/audit/07_ctest.log`; corrects the stale 183/183 figure); executable language coverage 1,106/1,106 (100.0%, canonical count — corrects the stale 1,091/1,091 figure, conformity audit item d3); SICP full-book gate 88/88; reference-Scheme differential 34/34 AGREE vs. chibi-scheme 0.12.0; VM parity differential 188/188 (remeasured 2026-08-25, `evidence/audit/06_vm_parity.log`; corrects "184/184", the corpus-differential count, not the full manifest) over a 956-row manifest (581/331/44) plus 328 further names in `tests/vm_parity/SURFACE_BASELINE.tsv` outside that ledger; qLLM oracle gate 10/10; ICC readiness 100, verdict `ready` |
| **v1.3.5** | late Sep 2026 | Consolidation | VM OALR Stage-1 evacuator, **SHIPPED (#461)**; H1 Python-bindings capsule-lifetime fix, **SHIPPED (#458)**; assurance wave 1 (ledger-integrity/oracle-schema gates), **SHIPPED (#454)**; docs-only CI fix, **SHIPPED (#455)**; AD re-verification wave; correctness debt (#229/#244/mod-srem); W3 benchmarks wave 1; W4 `vm_run.c` decomposition — see "Development workstreams" above |
| **v1.4.0-connection** | Nov 2026 | Systems profile | TCP/UDP/TLS, Unix sockets, HTTP/WebSocket, linear resource types; W5 interop wave 2; W6 PJRT spike *(AD substrate P4/P6/P11 already delivered in v1.3.0-evolve, ahead of schedule)* |
| **v1.4.1** | Dec 2026 | ABI | OALR ABI v2, portable tail transfer, PGO training workload, `bignum.cpp` decomposition |
| **v1.5.0-intelligence** | Q1 2027 | Intelligence | `core.dbsp` GA, native PGO, Noesis M2 surface, symbol embeddings, differentiable logic, LSTM/GRU; W6 Tier-1 data-parallel + Tier-2 mesh bit-identity gate *(high-order AD P5/P7/P9/P10 already delivered in v1.3.0-evolve, ahead of schedule)* |
| **v1.5.1** | Q1-Q2 2027 | — | DBSP circuits, resident A/B sessions, doc-truth gate becomes release-blocking |
| **v1.6.0-reasoning** | Q2 2027 | Reasoning | Backward/forward chaining, constraint solving, knowledge graphs, staged AD ABI; W6 GSPMD multi-host *(sparse high-order AD tensors P12 already delivered in v1.3.0-evolve, ahead of schedule)* |
| **v1.6.1** | Q3 2027 | — | DBSP traces + staged scratch plan, region-safety machine-checked-invariant work begins |
| **v1.7.0-synthesis** | Q3-Q4 2027 | Synthesis | Neural-guided search, program synthesis, GNN, recursive IVM |
| **v1.8.0-platform** | Q4 2027 | Platform | Windowing, audio, Vulkan, embedded targets, `core.memory` as Z-set |
| **v1.8.1** | Q1 2028 | — | Resident-agent circuit pilot, closed-world WPO, W6 fault tolerance/elasticity |
| **v1.9.0-types** | Q1-Q2 2028 | Types | Dependent types, effects, algebraic effects, session types, `IncrementalizePass` |
| **v1.9.1** | Q2 2028 | — | Checkpoint/restart, session protocol, AD-aware debugger/profiler |
| **v1.9.2** | Q3 2028 | — | Spill tier, reflective self-modification |
| **v2.0-starlight** | Q4 2028 | Starlight | Unified `differentiate` primitive, quantum region compilation, QAOA, formal verification (Lean kernel export); training-grade performance gates; W6 gates per tier |

> **Re-dating note (maintainer ruling R1, executed 2026-08-24):** every date
> from v1.4 onward supersedes the previously published table. The previous
> table's dates (v1.4 "Jul 2026" through v2.0 "Q1 2027") were not going to be
> hit at measured velocity; this table is deliberately coarser and
> velocity-anchored instead. See
> the "Development workstreams" section above for the
> six workstreams every release now draws from, and the point-release rows
> (v1.4.1, v1.5.1, v1.6.1, v1.8.1, v1.9.1, v1.9.2) for the finer-grained
> staging.

> **Arbitrary-order AD (Taylor-tower) track — SHIPPED.** Phases P0–P12 were
> originally planned to thread through the version themes above as enabling
> substrate; instead all 13 phases landed complete in v1.3.0-evolve. See
> [`docs/AD_CAMPAIGN.md`](docs/AD_CAMPAIGN.md) for the as-planned phase →
> version → ICC-gate map and [CHANGELOG.md](CHANGELOG.md) for the as-shipped
> detail. Each phase was gated by an `ad-*` ICC oracle criterion.

---

## Component Status

### Core Compiler
- [x] Parser - Complete
- [x] Type Checker - Complete
- [x] LLVM Backend - Complete (42,993 lines, `wc -l lib/backend/llvm_codegen.cpp` — corrected 2026-08-25 from "34,928", conformity audit item a8)
- [x] Module System - Complete
- [x] Macro System - Complete

### Automatic Differentiation
- [x] Forward Mode - Complete
- [x] Reverse Mode - Complete
- [x] Nested Gradients - Complete (32-level tape stack)
- [x] Vector Calculus - Complete (8 operators)
- [x] Arbitrary-Order Taylor Towers (v1.3.0-evolve) - Complete (P0-P12: exact
      coefficients, GUW multivariate, reverse-over-Taylor, tensor towers,
      validated Taylor models, sparse tensors, differentiable control flow,
      checkpointed reverse, tower-based numerics)

### Memory Management
- [x] Arena Allocation - Complete
- [x] OALR System - Complete
- [x] Ownership Tracking - Complete
- [x] Escape Analysis - Complete

### Standard Library (v1.1)
- [x] Core Functions (60+ list ops, 30+ string utils) - Complete
- [x] Math Library (linear algebra, statistics, ODE solvers) - Complete
- [x] Signal Processing (FFT, filters, window functions) - Complete
- [x] ML Library (optimizers, activations, normalization) - Complete
- [x] Web Platform (80+ DOM API functions, WASM target) - Complete
- [x] JSON/CSV/Base64 Support - Complete

### Development Tools
- [x] REPL with JIT (stdlib preloading, cross-eval persistence) - Complete
- [x] Compiler (eshkol-run, AOT + script mode) - Complete
- [x] Package Manager (eshkol-pkg, TOML manifest) - Complete
- [x] LSP Server (diagnostics, completion, hover) - Complete
- [x] VSCode Extension (syntax highlighting, LSP client) - Complete
- [x] Test Suite (45 suites, 770 tests) - Complete

### v1.1-accelerate (Complete)
- [x] XLA Backend (StableHLO/MLIR + LLVM-direct) - Complete
- [x] GPU Acceleration (Metal SF64 + CUDA cuBLAS) - Complete
- [x] SIMD Vectorization (SSE/AVX/NEON) - Complete
- [x] Parallel Primitives (work-stealing thread pool) - Complete
- [x] Exact Arithmetic (bignums + rationals + complex) - Complete
- [x] Consciousness Engine (logic, inference, workspace — 22 builtins) - Complete
- [x] ML Framework (75+ builtins: activations, losses, optimizers, CNN, transformers) - Complete
- [x] Signal Processing (FFT, filters, window functions) - Complete
- [x] R7RS Extensions (call/cc, dynamic-wind, bytevectors) - Complete

### v1.1.12 Additions
- [x] Production Bytecode VM (555+ builtins, 176/176 tests, dual number AD) - Complete
- [x] eshkol.ai Website (Eshkol→WASM, browser REPL, interactive tutorials) - Complete
- [x] GitHub Pages Deployment - Complete

### v1.1.13 Additions
- [x] Native Windows ARM64 (VS 2022 + ClangCL + LLVM 21 aarch64) - Complete
- [x] 16-lane release matrix with per-arch LLVM SDK caching - Complete
- [x] VM closure bug fixes (named-let nested closure PC + native 252 upvalue relay) - Complete
- [x] Windows setjmp hardening (x64 frameaddress, ARM64 sponentry) - Complete
- [x] Mobile-responsive website + browser REPL error display - Complete

### Planned (v1.3.5+)
- [x] Model Serialization + Python Bindings — v1.2 (shipped)
- [x] R7RS Library System + String Interpolation + arbitrary-order AD — v1.3.0-evolve (shipped)
- [x] VM region evacuator (with-region reclaims on the bytecode VM) — v1.3.5 (shipped, #461)
- [ ] Networking + Linear Resource Types — v1.4.0-connection
- [ ] Distributed computing, two-tier (W6: PJRT/XLA scale + native-mesh exact
      allreduce) — spike at v1.4.0, gates at v2.0 (no longer gated behind
      "demonstrated demand"; see the "Development workstreams" section above)
- [ ] Neuro-Symbolic Bridge — v1.5.0-intelligence
- [ ] Backward Chaining + Knowledge Graphs — v1.6.0-reasoning
- [ ] Program Synthesis + Neural Search — v1.7.0-synthesis
- [ ] Platform Abstraction (windows, audio, embedded) — v1.8.0-platform
- [ ] Advanced Type Theory (dependent, effects, algebraic) — v1.9.0-types
- [ ] Quantum Computing + Formal Verification — v2.0-starlight

---

## Research Directions

**Active Research:**
- Polyhedral optimization for nested tensor loops
- Linear type systems for hardware resources — shipped for `Qubit` in
  v1.3.4-evolve; generalizing to sockets/handles in v1.4.0-connection
- Neuro-symbolic bridging — differentiable symbolic operations (v1.5.0)
- Effect systems for purity tracking and algebraic effects (v1.9.0/v2.0)
- Exact, bitwise-reproducible distributed computing (W6 Tier 2: i128/
  fixed-point deterministic allreduce, multi-node bit-identity as a mesh-CI
  parity gate) — differentiated from throughput-only distributed ML, not a
  parity chase against it

**Exploratory Research:**
- Quantum machine learning — AD through parameterized quantum circuits
- Probabilistic programming with exact inference via factor graphs
- Formal verification of automatic differentiation correctness
- Hardware-software co-design for quantum-classical hybrid systems
- Self-improving programs via gradient descent on code embeddings

---

## Community Engagement

**Open Source Development:**
- GitHub repository with MIT license
- Active issue tracking and PR reviews
- Quarterly release cycle
- Community contribution guidelines

**Academic Partnerships:**
- University curriculum integration
- Research collaborations
- Conference presentations
- Student project sponsorship

**Enterprise Support:**
- Professional consulting
- Custom feature development
- Training and workshops
- Priority support

---

## How to Contribute

We welcome contributions in all areas:

**Core Development:**
- Implement planned features
- Optimize existing code
- Fix bugs and issues
- Improve test coverage

**Research:**
- Explore new AD techniques
- Investigate type system extensions
- Study memory management innovations
- Publish findings

**Documentation:**
- Improve user guides
- Write tutorials
- Create examples
- Update specifications

**Ecosystem:**
- Develop libraries
- Create tools
- Build integrations
- Share use cases

See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed contribution guidelines.

---

*Last Updated: 2026-08-25 (v1.3.5 documentation wave — re-dated ladder,
six standing workstreams, distributed computing promoted to W6; plus the
2026-08-25 conformity-audit resolution pass layered on top — ADR-0000
cross-reference, engine-qualified AD/parity claims, remeasured gate
numbers)*

*The arbitrary-order automatic-differentiation (Taylor-tower) campaign — phases
P0–P12, spanning core high-order AD, exact-coefficient and tensor-valued towers,
GUW multivariate recovery, differentiable control flow, and validated Taylor
models — shipped complete in v1.3.0-evolve rather than being threaded through
the version themes above as originally planned. It is the
differentiable-programming substrate for the neuro-symbolic (v1.5.0–v1.7.0) and
quantum/formal-verification (v2.0) arc, and its successor, the unified
`differentiate` primitive (W1), is the v2.0 endpoint. See
[`docs/AD_CAMPAIGN.md`](docs/AD_CAMPAIGN.md).*

*Eshkol v1.1-accelerate is complete with 47/47 roadmap items delivered plus the v1.1.12 and v1.1.13 additions (production VM, web platform, browser AD, Windows ARM64, mobile site). The v1.3 line shipped complete through v1.3.4-evolve (tagged 2026-08-19). The roadmap progresses through data & deployment (v1.2-scale), language maturity (v1.3-evolve), consolidation (v1.3.5), networking & resources (v1.4.0-connection), the ABI release (v1.4.1), neuro-symbolic intelligence (v1.5.0-intelligence), symbolic reasoning (v1.6.0-reasoning), program synthesis (v1.7.0-synthesis), platform & hardware (v1.8.0-platform), advanced type theory (v1.9.0-types), and quantum computing with formal verification (v2.0-starlight) — with a two-tier distributed-computing workstream (W6) running underneath the whole v1.4.0→v2.0 span rather than confined to one release.*
