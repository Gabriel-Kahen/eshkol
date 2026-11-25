# Unix socket acceptance runner review

Reviewed November 25, 2026. The named November 16 patch is pending receipt. The [upstream Darwin fixture change](https://github.com/tsotchke/eshkol/commit/b6d4c3daa49252f21d125816d9eb56bd10657c2f) compiles a dummy observation dylib on macOS before launching the synthetic Python runner. The same test-script blob (`f7de2a17027d6e194829ca1b4067219ea646ad55`) is present in Wave 9, the inspected `master` revision, and Track B. The test driver is byte-identical at all three revisions tested below.

| Source | Linux x86_64, Python 3.14.7 | Scope |
| --- | --- | --- |
| [#922](https://github.com/tsotchke/eshkol/pull/922) at `b8cfedb2eaf97595aef69ede084ae11befcacea8` | 25/25 controls passed | Original synthetic fixture |
| [`master` at `6925e2f3154ba3b192e836fc1a05f473c9819bf7`](https://github.com/tsotchke/eshkol/commit/6925e2f3154ba3b192e836fc1a05f473c9819bf7) | 25/25 controls passed | Landed script's Linux path |
| [#956](https://github.com/tsotchke/eshkol/pull/956) at `717484926c30e62772de8dd2f51bd87276a7336b` | 25/25 controls passed | Track B script's Linux path |

From each pinned source directory, with the three fixtures under `tests/agent/` available:

```sh
runner_work="$(mktemp -d)"
timeout 35s python3 -B tests/agent/unix_socket_stream_runner_contract_test.py \
  --source-dir "$PWD" --work-root "$runner_work"
```

The fake compiler exited successfully without producing an artifact for both JIT and AOT. In each case the driver rejected the run. The owned-child deadline control also verified that the timed child process group was gone. Each receipt recorded zero Eshkol product executions. The Darwin dylib path and the separately described 27-control patch remain unverified in this run.

Keep the landed dummy-dylib change. Once the 27-control patch is available, compare its loader-configuration and fake-output assertions with the landed harness and carry forward any missing assertions. Record the original patch result against #922 and run any new assertions against the integration head before proposing adoption.
