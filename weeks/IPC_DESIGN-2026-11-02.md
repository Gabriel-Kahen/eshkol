# Proposed decision: scoped subprocess streams on Eshkol's event loop

**Status:** Draft for tsotchke's review · **Date:** 2026-10-31

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
| `call-with-subprocess-streams loop argv options receiver` | Scoped convenience over that launcher; owns the process, lends one session covering both outputs, and cleans up on exit |
| `process-stream-read! session which buffer start count` | Nonblocking read into caller bytevector; validate ranges and return exact byte counts, would-block, EOF, or operation-labelled errors through predicates/accessors |
| Resource-aware extension of existing event-loop polling | Return checked watch identities/readiness using native `user_data`; reject stale watches and unmanaged registrations in this safe surface |
| `process-stream-status session`, `process-cancel! session grace-ms` | Reuse native nonblocking status; request loop-driven termination and observe its completion |
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

Normal return, exception, and continuation escape unregister watches, end the
lease, then close/reap the owner exactly once through existing cleanup machinery.
Use a scoped `dynamic-wind`/exception integration; reject re-entry or escaped
session use after scope exit. Close with an outstanding lease returns busy;
cleanup must release it rather than forcibly invalidate an active borrower.
The outer loop outlives sessions; its checked close rejects active watches.
Native bookkeeping stays outside movable regions and retains no unregistered
Scheme pointers or unbounded closed-process history. Owned sockets later follow
the same acquire/borrow/release/close rules. Proved transfers can eventually use ownership HIR; a runtime token alone
is not a claim of compiler-enforced linearity.

## Progress, memory, and cancellation

Register both outputs with the existing loop; use bounded, fair read quanta and
poll batches. Reuse caller buffers and scratch-region result metadata. Do not
retain per-read history. Kernel-pipe backpressure preserves bytes when consumers
slow down; continued child progress requires consumption. Application-retained
output still counts against its own memory budget.

Event-loop mode excludes legacy wait/capture readers for the process lifetime.
Completion means observed child exit plus both output EOFs; inherited writers can
keep pipes open. Check child status between bounded loop waits. Timeout zero
polls once; positive waits use one monotonic deadline and never imply termination.
Cancellation advances TERM/grace/forced-stop/reap through the same loop, without
waiting for pipe EOF or joining blocking readers in an I/O callback. Final teardown
must have an explicit bounded execution boundary. The initial contract covers the
direct child; descendant groups require an explicit policy. During cancellation,
output remains readable until the owning scope exits.

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

Acceptance: byte preservation including NUL and >16 MiB, slow-consumer memory,
deadlines, exit-before-EOF, cancellation, exceptional/continuation cleanup,
close-while-borrowed, stale watches, and capability denial. Record JIT/AOT and
VM support/rejection; the audit's LLVM build obstacle still blocks engine runs.

**Review:** confirm API placement, common-registry/checker extensions, scoped
control-flow semantics, and a first implementation slice fitting the weekly budget.

## Architectural basis at the audited revision

- [Resource ownership and existing checker](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/docs/development/strict_resource_ownership.md).
- [Implemented common handle/lease contract](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/inc/eshkol/core/handle.h).
- [Unified resource/effect trajectory](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/docs/design/adr/0004-type-system-trajectory.md#L836).
- [OALR residence and bounded-history invariants](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/docs/design/adr/0001-oalr-concurrent-resident.md#L146).
- [Existing capability policy](https://github.com/tsotchke/eshkol/blob/51a93586951b1c75b413b14531d0465c660ae579/docs/reference/agent/capabilities.md).
