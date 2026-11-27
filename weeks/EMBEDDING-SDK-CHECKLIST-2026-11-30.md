# November 30 embedding SDK audit

- [ ] Confirm the v1.4.1 package scope with Tsotchke. The candidate targets opt-in, 64-bit native Lite archives for Linux, macOS, and Windows. [Candidate at `07301bfae`](https://github.com/Gabriel-Kahen/eshkol/tree/07301bfaebfbd15092eb6ca39509be2d94344245)
- [ ] Qualify on the selected release base. Keep the Track B runtime, type-diagnostic, and toolchain-flag manifest checks alongside the new SDK inventory check. [Track B](https://github.com/tsotchke/eshkol/pull/956)
- [ ] Compile `include/eshkol/eshkol_ffi.h` from a clean C11 translation unit and check that it uses only public or standard headers. Review same-thread use and arena value lifetimes through `eshkol_ffi_shutdown`.
- [ ] Build with `ESHKOL_BUILD_EMBEDDING_SDK=ON`, build `eshkol-embedding-sdk`, and stage `--config Release --component EmbeddingSDK` into a fresh prefix.
- [ ] Check the package for `EshkolEmbeddingConfig.cmake`, the public header, the `eshkol-static` and `eshkol-repl-lib` archives, `stdlib.bc`, `stdlib.o`, and any enabled agent archives and dependencies.
- [ ] Configure a separate C/C++ consumer with CMake 3.24+, `find_package(EshkolEmbedding CONFIG REQUIRED)`, and `Eshkol::embed`. Link with the C++ driver and resolve the config and files from the relocated SDK prefix.
- [ ] On Linux and macOS, match the producer LLVM version and library mode. Verify whole-archive retention and `--export-dynamic` on Linux or `-export_dynamic` on macOS, plus the platform dependencies selected by the package.
- [ ] On Windows, match the architecture and exact LLVM package, use ClangCL and `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, and verify the installed `/DEF:` export list, compiler-rt, and platform libraries.
- [ ] Run `scripts/run_embedding_sdk_consumer.py --archive <archive>` against the staged package in an isolated consumer directory. Require `(+ 40 2)` to return integer 42, confirm the stdlib loads from `EshkolEmbedding_STDLIB_DIR` through `ESHKOL_LIB_DIR`, and require invalid bitcode to fail initialization.
- [ ] Check that missing SDK files and mismatched LLVM are rejected. Run the existing generated-program package smoke against the default runtime install and verify the Linux `stdlib.o`, runtime archive, and TLS link order.
- [ ] Run both the SDK consumer and release manifest gates on the selected release head for x64 and arm64 on all three hosts. Check Homebrew separately if it will carry the SDK. [Six green candidate jobs](https://github.com/Gabriel-Kahen/eshkol/actions/runs/45535425070)
- [ ] Record the exact release SHA, LLVM version and mode, install inventory, and per-platform results in the upstream handoff. [Producer and consumer guide](https://github.com/Gabriel-Kahen/eshkol/blob/07301bfaebfbd15092eb6ca39509be2d94344245/docs/BUILD_INTEGRATION.md#L281)

## November 26 Linux validation

The native Lite embedding SDK passed a clean Linux x86-64 Release build with LLVM 21.1.8 and agent FFI enabled. The SDK changes were applied to upstream `master` at [`690b8453`](https://github.com/tsotchke/eshkol/commit/690b8453253b030e7abb307d2d93c65e0664e486), producing local commit `0e6747cf9e2d4c495cded51c5c0d77e1ca915289` and tree `07ed2c4f8fb8bbe6cb3975100e5d63054926ac7b`.

- Building `eshkol-embedding-sdk` and installing the `EmbeddingSDK` component produced a fresh archive with the public C header, the installed CMake config, and matching `stdlib.bc` and `stdlib.o`.
- `scripts/run_embedding_sdk_consumer.py --archive` extracted the archive into a separate directory. A C consumer linked through `Eshkol::embed`, loaded the packaged standard library, and returned integer `42`. The invalid-bitcode control failed initialization as expected.
- `scripts/run_system_package_integration_test.sh --build-dir` passed discovery, compilation, linking, and execution through the standard `find_package(Eshkol)` interface using a separately staged runtime prefix.

The six-platform hosted run covers the earlier candidate at `07301bfae`. The rebased candidate's hosted matrix remains pending. [Earlier hosted results](https://github.com/Gabriel-Kahen/eshkol/actions/runs/45535425070)
