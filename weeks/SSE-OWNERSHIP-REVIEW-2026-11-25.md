# SSE ownership and scaffold review

The SSE adapter and core preserve the accepted connection's ownership through partial writes, cancellation, and retirement. The scaffold is pinned at [`dc38e82e9`](https://github.com/tsotchke/eshkol/commit/dc38e82e939a73967d7a72994b182cc421abe3ed), the reviewed PR head is [`1fcbd295c`](https://github.com/tsotchke/eshkol/pull/954/commits/1fcbd295c71f599693e138655f80fae31afeb9fa), and the qualified Wave 9 composition is [`f71f96f4c`](https://github.com/Gabriel-Kahen/eshkol/commit/f71f96f4c3ec8e1cdf67af3387b5535130a5f150), tree `175eb71463de6790dc45266be1ce3e103229fa97`. [Maintainer review](https://github.com/tsotchke/eshkol/pull/954#issuecomment-7152341875)

## Ownership trace

| Transition | Descriptor, watch, borrow, and write state |
| --- | --- |
| Accept and adopt | The HTTP server owns the accepted FD and the listener. Native begin checks the network capability. Adoption prepares the nonblocking FD and the shared lease with its cleanup slot. On failure the client slot stays with the HTTP server and zero response bytes are written. On success the client slot clears, the lease owns the FD, and the server retains the listener. The stream starts with HTTP headers pending, offset zero, no live borrow or write watch, and a five-second deadline. [Begin](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/lib/agent/c/agent_http_sse_stream.c#L61-L89) · [Adoption](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/lib/agent/c/agent_http_server.c#L1374-L1434) |
| Short write | After headers drain, admit one frame and fix its absolute deadline. Progress takes one borrow, checks the lease and deadline, and writes the suffix starting at the saved offset. With a 14-byte send of the canonical chunk, the offset is 14, the next byte is `0xa9`, and the write watch is armed with the caller's cookie. The borrow returns to zero after this call. A zero-progress retry leaves the offset and deadline unchanged. [Write step](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/lib/agent/c/agent_http_server.c#L1456-L1550) |
| Resume and finish | Writability prompts another progress call. Expiry takes effect before the native send even on a writable wake. When the final byte of a frame drains, the adapter removes its watch and the core starts the separate 60-second idle deadline. Clean finish admits and drains `0\r\n\r\n` under its own write budget, then retires the lease. [Progress and finish](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/lib/agent/c/agent_http_sse_stream.c#L212-L255) |
| Cancel with a live borrow | Cancellation stops progress. Close retires the lease identity and gives the cleanup owner a pending ticket. The FD and watch stay live while the borrow count is one. At zero, cleanup removes the watch before attempting the FD close once. A watch-removal error retains the FD, watch, and loop for owner-thread retry. Uncertain descriptor close is terminal. Foreign watches keep their original registrations. [Adapter cleanup](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/lib/agent/c/agent_http_server.c#L1284-L1356) · [Mixed-loop trace](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/tests/agent/http_sse_mixed_loop_test.c#L219-L272) |

## Scheme contract and distinguishing checks

- `call-with-http-sse-stream` adopts on the caller's loop. `sse-send!` and `sse-finish!` admit work, `sse-progress!` advances it, and `sse-deadline-ms` tells the caller when to drive it on timeout. `sse-cancel!` retires immediately. The scope's `dynamic-wind` after-thunk detaches its cleanup ticket to the lease manager. A saved stream can recover a failed ticket through `sse-cleanup-ticket`. The checks preserve the callback result, exception, or continuation escape, observe zero extra writes, and reject continuation re-entry. [Scope API](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/lib/agent/http_sse.esk#L26-L102) · [JIT and AOT wire assertions](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/tests/integration/http_sse_scope.py#L48-L66)
- The one-frame slot includes HTTP chunk overhead in its 16 KiB limit. Busy and idle expiry take precedence over payload validation where applicable. The receiver checks `200 OK`, the SSE content type, chunked transfer encoding, UTF-8 split across reads, omission of an empty event type, and exactly one decoded event. The metadata controls require zero bytes on rejected CR/LF/NUL or invalid UTF-8. The retained-listener check serves an ordinary response while the SSE connection is active. [Native wire assertions](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/tests/agent/http_sse_stream_test.c#L165-L246) · [Capacity and idle checks](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/tests/agent/http_sse_stream_test.c#L249-L347)

## Scaffold findings

1. The scaffold's `send_override` pins the 14-byte short write, next UTF-8 byte, zero-progress retry, and native-call count at expiry. Those assertions distinguish offset reset and a late write that still returns timeout. The production adapter probe below adds kernel send-buffer pressure with the receiver paused. The native receiver case separately controls the TCP read boundaries and checks the decoded event. [Scaffold probe](https://github.com/tsotchke/eshkol/blob/dc38e82e939a73967d7a72994b182cc421abe3ed/tests/agent/http_sse_adapter_scaffold_test.c#L145-L208) · [Receiver test](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/tests/agent/http_sse_stream_test.c#L211-L244)
2. The original foreign-watch case uses a pipe. The integrated test drives a real subprocess stdout registration and a Unix-stream peer on the same loop. Both must deliver fresh bytes on their original FD and cookie while the borrow holds cleanup pending, after the retryable removal error, and after the SSE socket retires. The SSE peer independently sees the exact prefix and EOF. [Scaffold pipe](https://github.com/tsotchke/eshkol/blob/dc38e82e939a73967d7a72994b182cc421abe3ed/tests/agent/http_sse_adapter_scaffold_test.c#L291-L372) · [Peer assertions](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/tests/agent/http_sse_mixed_loop_test.c#L130-L155) · [Cleanup stages](https://github.com/Gabriel-Kahen/eshkol/blob/f71f96f4c3ec8e1cdf67af3387b5535130a5f150/tests/agent/http_sse_mixed_loop_test.c#L245-L272)

## Qualification evidence

| Source | Scope and outcome |
| --- | --- |
| `f71f96f4c`, Linux x86-64 epoll | Nov 25 focused CTest rerun passed 6/6. The earlier optimized and ASan/UBSan/leak runs passed the adapter 6/6, core 4/4, and the mixed-loop case. The separate 60-second idle check passed. Generated API and capability extern checks passed. [Linux and hosted record](https://github.com/tsotchke/eshkol/pull/954#issuecomment-7138262151) |
| `f71f96f4c`, macOS arm64 kqueue | Hosted optimized and ASan/UBSan suites, the separate idle run, and six TCP exchanges per Scheme engine passed. The hosted workflow records both its wrapper SHA and this candidate checkout. Tsotchke independently reproduced the five focused SSE CTests and the mixed-loop sanitizer run. [Native job](https://github.com/Gabriel-Kahen/eshkol/actions/runs/45439062153/job/138604775056) · [Scheme job](https://github.com/Gabriel-Kahen/eshkol/actions/runs/45439062153/job/138604774600) · [Independent verification](https://github.com/tsotchke/eshkol/pull/954#issuecomment-7152341875) |
| Track B [#956](https://github.com/tsotchke/eshkol/pull/956) | Integrates SSE and the two #955 CMake corrections on a `master` base. On Track B head `717484926` plus the fixture-only change below, the native stream passed 4/4 both optimized and under ASan/UBSan with leak detection. The hosted check rollup and release/package qualification belong to the integration and release heads. |

The `dc38e82e9..1fcbd295c` diff passes `git diff --check`. The wording and path scan located a literal request authority in the native test. The fixture-only follow-up at `fdb5be70c` derives the authority from the bound address and port using the same pattern as the mixed-loop test. [Fixture follow-up](https://github.com/Gabriel-Kahen/eshkol/commit/fdb5be70c0e78cd7c236f71917f819e86148dee2)

### Reproduce the Linux send-buffer probe

The peer sets a small receive buffer and pauses reads. The accepted TCP socket also has a small send buffer. The production write step first makes partial progress and then reports a stalled write with the saved offset intact. The peer drains queued bytes, the original FD and cookie report writable readiness, and resumption before the same deadline delivers exactly the expected bytes through EOF. The probe exercises the adapter write path. The C and Scheme fixtures above cover HTTP framing and the public SSE API.

Clang 22.1.8 on Linux x86-64 epoll recorded an initial write of 6,144 bytes, a later stall at offset 10,240 with zero bytes accepted, and an exact 16,384-byte match. The same result passed five optimized and five ASan/UBSan/leak repetitions at `f71f96f4c` and again at `fdb5be70c`. [Probe source](support/sse_tcp_backpressure.c)

From this notes checkout, point `source_dir` at the candidate checkout and use Bash with Clang on Linux. The source revision and tree are printed before execution.

```bash
source_dir=/path/to/candidate-checkout
fixture="$PWD/weeks/support/sse_tcp_backpressure.c"
work=$(mktemp -d)
flags=(-O2 -g -Wall -Wextra -Werror -pthread -I"$source_dir/inc")
# For sanitizer coverage use -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer instead of the optimization and warning flags.
files=("$fixture" "$source_dir/tests/agent/http_sse_fixture_support.c"
       "$source_dir/lib/agent/c/agent_http_server.c" "$source_dir/lib/core/resource_lease.c"
       "$source_dir/lib/core/event_loop.c" "$source_dir/lib/core/event_loop_epoll.c")
for file in "${files[@]}"; do
  clang -std=gnu11 "${flags[@]}" -c "$file" -o "$work/$(basename "$file").o" || exit
done
clang++ -std=c++17 "${flags[@]}" -c "$source_dir/lib/core/runtime_handle.cpp" -o "$work/runtime_handle.o" &&
  clang++ "${flags[@]}" "$work"/*.o -o "$work/pressure" || exit
git -C "$source_dir" rev-parse HEAD 'HEAD^{tree}'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 10s "$work/pressure"
```

[Runner review](IPC-RUNNER-REVIEW-2026-11-25.md) · [Transformer acceptance ledger](TRANSFORMER-ACCEPTANCE-LEDGER-2026-11-20.md) · [Nov 30 SDK checklist](EMBEDDING-SDK-CHECKLIST-2026-11-30.md)
