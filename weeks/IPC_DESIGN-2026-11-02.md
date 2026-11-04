# Proposed decision: scoped subprocess streams on Eshkol's event loop

**Status:** Draft for tsotchke's review · **Date:** 2026-10-31

**Updated:** 2026-11-03, addressing the teardown, polling, and continuation
questions in tsotchke's response dated 2026-11-02. These clarifications remain proposed.

**Baseline:** `51a93586951b1c75b413b14531d0465c660ae579`,
`codex/v14-integration-wave9-20261021` · [Audit](IPC_AUDIT-2026-11-02.md).

## Language fit and scope

IPC supplies the resource-safe hosted substrate for Eshkol's scientific, ML, and
agent programs. It should compose with Scheme control flow, OALR lifetimes,
capability policy, and the existing ownership checker. Extend the current
subprocess implementation, event loop, and common handle registry. This week's
slice is stdout/stderr; Unix-domain stream sockets follow. Names below are
proposals. Runtime guards are an incremental foundation, not completion of the
planned statically verified resource profile.

## Proposed surface

| Operation | Contract |
|---|---|
| Existing `process-spawn-argv-options` | Opt-in, lifetime-immutable `io-mode: event-loop`; preserve default behavior; keep the native process behind a checked owner |
| `call-with-subprocess-streams loop argv options receiver` | Owns the process, lends one session covering both outputs, and initiates bounded teardown on exit; normal return carries the receiver value, cleanup status, and any cleanup ticket |
| `process-stream-read! session which buffer start count` | Nonblocking read into caller bytevector; validate ranges and return exact byte counts, would-block, EOF, or operation-labelled errors through predicates/accessors |
| Resource-aware extension of existing event-loop polling | Return checked watch identities/readiness using native `user_data`; reject stale watches and unmanaged registrations in this safe surface |
| `process-stream-status session`, `process-cancel! session grace-ms` | Reuse native nonblocking status; request loop-driven termination and observe its completion |
| Runtime cleanup query/drain | Discover outstanding tickets after scope exit; inspect a ticket or advance pending cleanup through the existing event loop with a bounded wait |
| Later Unix streams | Connect/listen/accept with deadlines, binary reads/partial writes, explicit close; extend existing network facilities and converge legacy adapters |

Keep watch/poll integration in the existing event-loop facility. Low-level raw-fd
compatibility remains outside this safe contract. The first scoped slice sets
stdin to `null`; piped stdin requires a later checked session operation and is
explicitly unsupported here. Do not expose the native process pointer for writes.

## Ownership and Scheme control flow

Use `eshkol_handle_registry_t` generations and exclusive leases. A PROCESS kind
and checker contracts require explicit additions; current kinds are FILE, SOCKET,
and EVENT_LOOP. One session holds one process lease and services both pipes;
separate stdout/stderr views do not independently borrow the same owner.

Normal return, exception, and continuation escape close the session: invalidate
its views, unregister watches, and end its lease before teardown. This ends access
to its output; unread bytes may be abandoned by explicit scope exit. Session
closure alone establishes neither pipe EOF nor child reap. Close with an active
lease returns busy; release it rather than invalidating an active borrower.
The outer loop outlives active sessions; its checked close rejects active watches.
Native bookkeeping stays outside movable regions and retains no unregistered
Scheme pointers or unbounded closed-process history. Owned sockets later follow
the same acquire/borrow/release/close rules. Runtime tokens do not establish
compiler-enforced linearity.

Use existing `dynamic-wind`/exception cleanup. Normal return exposes teardown
status alongside the receiver value. Exceptions keep the original exception, and
continuation escape keeps its original target/value; pending tickets are then
discoverable through the runtime cleanup query. Re-entry into a closed scope
raises a closed-session error before running receiver code or acquiring another
process. Captured sessions remain invalid. The after-thunk cannot repeat signal,
close, lease release, or ownership transfer steps already completed. Closing the
Scheme session must not prevent unfinished native cleanup from progressing.

## Progress, memory, and cancellation

Register both outputs with the existing loop; use bounded, fair read quanta and
poll batches. Reuse caller buffers and scratch-region result metadata. Do not
retain per-read history. Kernel-pipe backpressure preserves bytes when consumers
slow down; continued child progress requires consumption. Application-retained
output still counts against its own memory budget.

Event-loop mode excludes legacy wait/capture readers for the process lifetime.
Completion means observed child exit plus both output EOFs; inherited writers can
keep pipes open. Cancellation advances TERM/grace/forced-stop/reap independently
of output readiness and EOF. It covers the direct child; descendant groups need
an explicit policy. Output remains readable while the session remains open.

### Zero-timeout and deadline semantics

Zero performs one bounded nonblocking readiness/status pass, then returns ready
events, observed exit, or timeout/would-block. It does not sleep, join readers,
perform a blocking wait, or start cancellation. It may advance cancellation
already requested explicitly. Positive waits share one monotonic deadline across
retries and phases; interruption cannot restart it. A poll timeout does not close
streams or terminate the child. Validate timeout and byte-count bounds explicitly.

### Bounded teardown and retained ownership

Scope exit has a separate finite `teardown-ms` budget; proposed defaults are
500 ms total and 100 ms TERM grace. Both are explicit options. An earlier explicit
cancellation keeps its original grace deadline. Teardown never restarts deadlines
or waits for pipe EOF; it advances termination and nonblocking reap through the
existing runtime/loop facilities, without blocking reader joins. Pipe endpoints
close as separately tracked steps after session access ends; the process owner
and its reap obligation remain live until confirmed. Closed endpoints cannot be
closed a second time by final process disposal.

Reserve a native cleanup record before spawning and charge it to the resource
budget. It roots the process owner throughout the scope and teardown; entering
pending cleanup is allocation-free and atomic with session invalidation. Record
each completed native step so it cannot repeat or be skipped after unwinding.
If the child is confirmed reaped, release its remaining resources exactly
once and report `reaped` (or a structured cleanup error). If the budget expires
first, retain sole ownership in that record and report `cleanup-pending` with an
opaque observation ticket. The runtime retains the process owner; the caller is
not required to recover or close an expired session. Existing terminal registry
close/destroy callbacks must not run while reap remains unconfirmed: they would
retire ownership even on error or enter an unbounded reap wait.

Pending records survive the original loop and scope. Runtime query/drain calls
advance them using nonblocking status and the existing loop; progress requires
those calls, not a hidden worker or scheduler. Hosted runtime destruction must
preflight pending ownership and report busy until resolved; this is a required
adapter rule, not existing registry behavior. Never report successful closure
while ownership is unresolved. Terminal status consumption retires the record;
unconsumed records remain budgeted and may limit further spawning. Observation
tickets carry no native-resource authority.

## Policy, platforms, and evidence

Preserve `subprocess` and applicable `shell` acquisition checks; socket operations
use `network`. Propagate resource provenance through stream operations and check
the active policy; cleanup of an already-owned resource remains possible after
policy changes. No descriptor-based policy bypass. I/O/state and foreign effects
belong in existing checker/signature contracts; transport operations are not pure
or automatically differentiable. Extend the existing checker, not a separate one.
Its experimental strict profile currently excludes resource loops, continuations,
and VM; unsupported contracts must be rejected until integrated.

POSIX reuses epoll/kqueue; Windows output reads reuse pipe-readiness adaptation.
Windows AF_UNIX needs network-backend work and runtime availability checks;
unsupported operations report that explicitly. VM needs matching adapters or an
explicit unsupported result. Scheduling integration must not introduce another
scheduler or block a work-stealing worker implicitly.

Acceptance: NUL and >16 MiB preservation, slow-consumer memory, fair dual-stream
progress, zero/positive deadlines, exit-before-EOF, cancellation with inherited
writers, early close, and capability denial. Exercise reap delayed past teardown,
pending ownership after loop closure, query/drain completion, runtime shutdown
busy, exceptional/continuation escape, rejected re-entry, and exactly-once cleanup.
Include close-while-borrowed and stale watches. Record actual JIT/AOT execution and
VM support/rejection; track LLVM 22 compatibility separately from IPC work.

**Implementation scope:** use the existing three-hour block for a small stdout/
stderr draft against `codex/v14-integration-wave9-20261021`, stating "Builds on
#845", commands, measured results, and remaining work. A documented partial slice
is an appropriate checkpoint. Sockets and SSE remain in their scheduled weeks.

**Review:** the audit deliverable and overall direction were accepted in the
2026-11-02 response. Confirm the proposed pending-owner protocol, zero-timeout and
continuation rules, and common-registry/checker extensions before closing design review.

## Architectural basis at the audited revision

- [Resource ownership and existing checker](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/docs/development/strict_resource_ownership.md).
- [Implemented common handle/lease contract](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/inc/eshkol/core/handle.h).
- [Unified resource/effect trajectory](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/docs/design/adr/0004-type-system-trajectory.md#L836).
- [OALR residence and bounded-history invariants](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/docs/design/adr/0001-oalr-concurrent-resident.md#L146).
- [Existing capability policy](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/docs/reference/agent/capabilities.md).
