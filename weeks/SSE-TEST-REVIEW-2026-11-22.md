# SSE test review and mixed-loop cleanup control

The byte and deadline assertions are ready to carry into the SSE acceptance scaffold. The local tests use a fake writer and clock or a fixture adapter backed by the real Linux lease and loop implementation. Native HTTP SSE acceptance remains open. [SSE wire and ownership proposal](https://github.com/Gabriel-Kahen/eshkol/blob/034b395170260e7004b73a672e21ec51fdef54de/weeks/SSE-STUDY-2026-11-18.md)

## Write checks

| Plausible incorrect behavior | Independent assertion | Local result |
|---|---|---|
| Retry starts at byte zero after a 14-byte write. | Record the suffix offered on every attempt and compare the complete wire bytes with the canonical 37-byte chunk. After byte 14, the next byte offered is `0xa9`. | The deliberately broken writer reports completion. The byte comparison rejects it. |
| Repeated would-block restarts the write deadline. | After attempts at 0, 4,000 and 4,999 ms, require timeout at the original 5,000 ms deadline. | The deliberately broken writer misses the original deadline and fails the assertion. |
| The writer returns timeout after attempting a final late write. | Compare the send-attempt log to the expected three calls. At or after expiry, the count must stay unchanged even when the socket reports writable. Inspect emitted bytes independently. | The deliberately broken writer returns timeout and still fails because it made a fourth send attempt. |

All five checks in `sse_write_controls.py` pass, including deadline cases with and without partial progress. The shared wait can return readiness before its post-poll expiry check. The SSE writer therefore needs to check its original deadline immediately before the native send. [Five write controls](https://github.com/Gabriel-Kahen/eshkol/blob/ab8daef5969105f99066f80b8a38bec12af7dba2/weeks/sse_write_controls.py) · [Shared wait](https://github.com/Gabriel-Kahen/eshkol/blob/667c382d5f2e8a992c4c6e9053d734110c987773/lib/core/resource_lease.c#L587-L603)

## Mixed-loop cancellation and teardown

**Prediction.** With one 14-byte prefix already sent, cancel while a connection borrow is active and request close. The identity retires immediately, the descriptor stays open during the borrow, and the cleanup ticket stays pending. After release, the write watch is removed and the descriptor closes once. If removal fails, the ticket reports retryable watch failure. The descriptor and loop stay live until the owner retries. An unrelated ready watch must continue to deliver its original cookie and data throughout.

**Observed.** A fixture adapter registered a write watch on a real socketpair and a separate pipe read watch on the same Linux epoll loop. Under the posted #926 source `667c382d5f2e8a992c4c6e9053d734110c987773`, tree `ea94dcda8356dec02caa43d0268685d1a62fd73c`, both the normal cancellation case and the injected watch-removal failure case passed with Clang 22.1.8 in optimized and ASan/UBSan/leak builds. During a live borrow the descriptor remained open. After the failed removal, a fresh byte still arrived via the unrelated watch, the loop could not close, and retry removed the original watch before exactly one descriptor close. The peer read the 14-byte prefix and then EOF. After retirement, the unrelated watch delivered another fresh byte.

**Stronger native assertion.** Checking an SSE cleanup result and a live loop handle can miss lost registrations. Have the actual PROCESS or SOCKET peer produce a fresh byte while teardown is pending and again after SSE retires. Require delivery on the original watch and verify the bytes. Separately capture all SSE sends and the HTTP receiver result. The local fixture used a pipe as the unrelated watch and drove the cancellation through a test adapter.

## Reproduce the controls

Use Linux with Git, Python 3, Clang, and Clang++. From this notes checkout, use a separate checkout for the posted lease source. The runner prints the source commit, source tree and tracked-change status before running both builds.

```sh
git fetch origin gabe/shared-loop-leases
lease_source=$(mktemp -d)
git worktree add --detach "$lease_source" 667c382d5f2e8a992c4c6e9053d734110c987773
python3 -B weeks/sse_write_controls.py
python3 -B weeks/support/run_sse_mixed_watch_control.py "$lease_source"
git worktree remove "$lease_source"
```

The same two cases passed in both builds on a local merge preview combining Wave 9 `a29974cf0c870f6ba54d2aa2587e5c0352d48dd7` and the posted #926 head, with tree `a80b03eacb61c939354fee2240bdc0c021dcc37a`. This preview exercises the shared lease code with the fixture adapter. [Native fixture](support/sse_mixed_watch_control.c) · [Runner](support/run_sse_mixed_watch_control.py)

The named portable runner-control patch remains pending review. The mentor review identifies 27 synthetic checks at the coordinator’s draft head. The product scaffold still needs the agreed adapter, source pin and native runner, with cases for capacity, busy, peer disconnect, scope exit and mixed PROCESS/SOCKET/SSE lifecycle.
