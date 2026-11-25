# Eshkol v1.4.1 embedding SDK outline

A downstream C application should be able to initialize the in-process Eshkol JIT, evaluate an expression, inspect the result, and shut down using an installed SDK. The first acceptance test is a C program built outside the Eshkol checkout that evaluates `(+ 40 2)` and checks for the integer `42`.

## Wave 9 starting point

The native release workflows stage the REPL archive. The installed CMake and pkg-config interfaces expose the generated-program runtime and its `stdlib.o`. Compiler embedding also needs `eshkol/eshkol_ffi.h`, the compiler archive, and a link interface that retains the JIT bridge in the REPL archive. The Wave 9 CMake install rules and release copy lists omit the FFI header and compiler archive. [Release staging](../../.github/workflows/release.yml#L342) · [Installed runtime contract](../../cmake/FindEshkol.cmake#L45)

## Linux install and consumer results

- The out-of-tree `stdlib` build on Wave 9 `93f0072aa` failed to recognize the shipped capability module. Local commit `688314aab` supplies the shipped module root to the stdlib build rule. The target then produced both stdlib artifacts and the install completed.
- Wave 9 evaluated the REPL install guard before the target existed. The stock install omitted the archive. Local commit `2fa594d03` places the install rule after target creation, and the restaged archive matched the build artifact byte for byte.
- The stock C consumer stopped at the missing FFI header. A scratch copy of the corrected install with the header and compiler archive added returned `42` on Linux x86_64 with LLVM 21.1.8 and OpenSSL. The checkout and build tree were hidden, and the host loaded `stdlib.bc` from the read-only scratch prefix via `ESHKOL_LIB_DIR`. Linking the REPL archive normally discarded its JIT bridge registrar, so initialization failed. Whole-archive retention and runtime symbol export produced the passing executable.
- The existing generated-program CMake smoke resolved the stock install and compiled its fixture but failed to link. The same object linked and ran with `stdlib.o` before `libeshkol-runtime.a`, followed by `-lssl -lcrypto`. The installed Linux runtime interface needs both the link order and the TLS dependency closure.

[Stdlib build rule](../../CMakeLists.txt#L3673) · [REPL install rule](../../CMakeLists.txt#L3800)

## Proposed package contract

| Area | First deliverable |
| --- | --- |
| Public header | Install `eshkol/eshkol_ffi.h`. Document the context and value lifetimes, the single-threaded calling contract, and the supported object ABI. Compile the header from a C translation unit as part of the consumer test. |
| Compiler and JIT linkage | Package the compiler and REPL archives. Use an opt-in CMake component with an imported embedding target. Its link interface must carry the matching LLVM and platform dependencies, retain the REPL bridge registrar, and expose the runtime symbols the JIT resolves. Derive the platform-specific link flags from the supported native JIT hosts. |
| Standard library | Ship the matching stdlib artifacts with the embedding component. Document how a host binary outside the install prefix locates them. Require the smoke test to identify the stdlib loaded from its staged prefix. |
| Distribution | Keep CMake install, release staging, and the package manifest in sync. Apply the same contract to the Homebrew path when it carries the embedding component. |

[C FFI header](../../inc/eshkol/eshkol_ffi.h) · [Native JIT link setup](../../CMakeLists.txt#L3903) · [Package manifest](../../.icc/package-manifest.yaml)

## Acceptance sequence

1. Qualify one Linux x86_64 install from a clean consumer directory. Compile the C fixture, link through the installed embedding target, run it with an empty cache, and verify `42` and the installed stdlib path. Hide the producer checkout and build tree during the consumer run.
2. Confirm the gate catches a missing public header, a discarded REPL bridge registrar, and a mismatched stdlib artifact. Run the existing generated-program package smoke as a compatibility control.
3. Repeat the cold consumer test on macOS arm64 and the Windows configurations selected for the SDK. Record the supported LLVM version, platform link inputs, and archive retention mechanism for each row.

[Existing generated-program package smoke](../../scripts/run_system_package_integration_test.sh) · [Stdlib identity test](../../tests/toolchain/stdlib_identity_test.sh)
