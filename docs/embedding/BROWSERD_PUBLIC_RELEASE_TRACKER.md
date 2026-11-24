# browserd — Public Release Readiness Progress Tracker

Status: **Living tracker** · Baseline: `5461673d1e9` (CI/WPT PASSED) · Written: 2026-11-24

This tracker records the gates that must be satisfied before `browserd` is offered for
**public** use. It is deliberately separate from the CI/WPT work that preceded it: a
clean Main run and a strict WPT run establish **beta / dogfood readiness only**, not
public-release sign-off.

The implementing tickets and their step-by-step instructions live in
[`BROWSERD_PUBLIC_RELEASE_PLAN.md`](./BROWSERD_PUBLIC_RELEASE_PLAN.md). Strategic context
is in [`PRODUCTION_REPORT.md`](../../PRODUCTION_REPORT.md) and
[`PRODUCTION_READINESS.md`](./PRODUCTION_READINESS.md); the earlier embedding tickets are
in [`AGENT_BUILD_PLAN.md`](./AGENT_BUILD_PLAN.md).

> **Do not mark a gate `PASSED` without a run id or an equivalent recorded artifact.**
> An item you could not execute is `NOT_RUN`, not `done`. This is the discipline used
> throughout `AGENT_BUILD_PLAN.md` and it is not relaxed here.

---

## 1. Status vocabulary

| State | Meaning |
|---|---|
| `PASSED` | Verification completed; every required check green; evidence (run id / log / artifact) recorded. |
| `FAILED` | Verification completed with a genuine failure. Fix in place; never mask. |
| `BLOCKED` | Cannot proceed without a human decision, specific hardware, or an upstream change. |
| `NOT_RUN` | No verification attempt has been made yet. |
| `IN_PROGRESS` | Verification run is still executing; conclusion not yet known. |
| `DONE` | Implementation committed and pushed. **Not** equivalent to `PASSED`. |
| `NOT_CONFIRMED` | Gate exists but has no passing evidence yet — the default for unstarted release work. |

Two hard rules:

1. `--always-succeed` is permitted **only** for the scheduled `wpt-sync-from-upstream`
   workflow. Its presence in a Main or WPT verification run makes that gate `FAILED`
   regardless of individual job conclusions.
2. Install only from **successful** CI artifacts. Never install output from a failed run,
   and never downgrade a dependency or toolchain without explicit approval.

---

## 2. Baseline evidence (CI/WPT — already achieved)

| Item | Commit | Run id | Result | Notes |
|---|---|---|---|---|
| ktfmt final verification | `5461673d1e96d5db8ab22020f96112b51a0ae42c` | `45396796842` | `PASSED` | `Lint ktfmt` = Success. |
| Main CI | `5461673d1e96d5db8ab22020f96112b51a0ae42c` | `45396797175` | `PASSED` | 194 jobs: 79 success, 115 skipped, 0 failures. |
| Strict WPT | `5461673d1e96d5db8ab22020f96112b51a0ae42c` | `45396797175` | `PASSED` | 20/20 WPT shards success; `Process WPT Results` success; `WPT_ALWAYS_SUCCEED_ARG` empty; sample shard log `Filtering 0 unexpected results`. |
| Re-enable previously skipped WPT suites | `f5992a160b6d7ca29883258a1cf82107fac0cc4c` | `45413720692` | `FAILED` (infrastructure, not code) | Attempt 1: 78 success / 115 skipped / **1 cancelled** / 0 genuine failures — `MacOS Arm64 … C API` job `138514276569` never got a runner (`runner_id=0`). Attempt 2 (single-job rerun): `Result` failed with `No artifacts found matching pattern 'cargo-timings-*'` because a one-job rerun cannot merge first-attempt artifacts. Strict WPT results remained clean. |
| Fresh full run on docs commit (re-enabled suites) | `fd4655eb3b6b39ad1698aa5571bba45869aa4283` | `45428660357` | `PASSED` | 194 jobs: 79 success, 115 skipped, **0 failures / 0 cancellations**; 20/20 WPT shards and `Process WPT Results` success; `WPT_ALWAYS_SUCCEED_ARG` empty; sample shard log `Filtering 0 unexpected results`. Current `main` differs from this commit only in docs and `etc/ci/wpt_pass_rate.py`. |
| REL-2 lifecycle test attempt | `5881869781f` | `45434483363` | `FAILED` (genuine, reverted) | All three C API jobs panicked at `ffi/capi/lib.rs:882`: `misaligned pointer dereference … address 0x1`. The tests assumed handle-liveness checks that are present in uncommitted local work but **not** in committed `main`, where `servo_builder_set_event_loop_waker` dereferences the garbage handle. Reverted in this change. |
| Separate Embedding API run on docs commit | `fd4655eb3b6b39ad1698aa5571bba45869aa4283` | `45428660374` | `FAILED` (infrastructure) | Linux and Windows C API jobs passed; `C API (macOS)` (`138567556244`) was cancelled with no runner. This is a different workflow from the Main run above and is recorded under REL-2, not REL-0. |
| Local heavy builds / test suites | — | — | `NOT_RUN` | By design: all builds and tests run in GitHub Actions only. |

Older failures are covered by later fixes and are recorded for traceability, not as open
work: `45384672757` (Main, `Install crown`) and `45384672761` (Lint ktfmt) were resolved
by `903f8325122`, `f0fba21fa6f`, and `5461673d1e9`.

**Overall status: `CI_AND_WPT = PASSED` at `fd4655eb3b6` (`45428660357`), including the
re-enabled WPT suites; `PUBLIC_RELEASE_READINESS = NOT_CONFIRMED`.** The separate Embedding
API workflow on that commit is not green (see `45428660374`), so REL-2 remains unconfirmed;
green Main/WPT is beta/dogfood readiness only.

---

## 3. Master tracker

Effort: **S** a few days · **M** 1–3 weeks · **L** 1–3 months · **XL** quarters+.
"Depends on" refers to gate IDs in this table; upstream embedding tickets are named where
they provide a prerequisite.

| Gate | Title | Effort | Status | Depends on | Verification gate |
|---|---|---|---|---|---|
| **REL-0** | Strict CI/WPT baseline preserved (no masking, suites re-enabled) | S | `PASSED` (`fd4655eb3b6` / run `45428660357`) | — | Main workflow + strict WPT job conclusions |
| **REL-1** | Runtime / platform validation (Windows + WinUI 3 flagship; Android additive) | L | `NOT_CONFIRMED` | REL-0 | Manual hardware checklist + platform CI artifacts |
| **REL-2** | browserd ABI / lifecycle contract (ownership, threading, teardown, versioning) | M | `NOT_CONFIRMED` | REL-0, W5.4 | `embedding-api.yml` + C ABI lifecycle tests |
| **REL-3** | Real-world compatibility (top sites, WPT deltas, no site-specific hacks) | XL | `IN_PROGRESS` (steps 1–2 prepared; real-site runs `NOT_RUN`) | REL-0, W1 | `WPT_STRICT_BASELINE.md` (run `45396797175`); `SITE_SMOKE_RUNBOOK.md` |
| **REL-4** | Security / stability (sandbox honesty, site isolation, fuzzing, crashes) | XL | `NOT_CONFIRMED` | REL-2, W2.x | `embedding-api.yml` + scheduled fuzzing + audit |
| **REL-5** | Performance (Speedometer, startup, memory, long uptime) | L | `BLOCKED` (measurements `DONE`, threshold gate not run) | REL-0, W5.6/W5.7 | `PERFORMANCE_BASELINE.md`; bencher token + Windows perf + leak evidence |
| **REL-6** | Distribution (signed artifacts, installers, update story) | L | `NOT_CONFIRMED` | REL-1, REL-2, W5.3/W3.4 | Release workflow artifacts from successful runs only |
| **REL-7** | Observability (size, startup, throughput, crash telemetry) | M | `NOT_CONFIRMED` | REL-4 | CI metrics job + opt-in telemetry pipeline |
| **REL-8** | Public-release sign-off (documented go/no-go with all gates evidenced) | S | `BLOCKED` | REL-1 … REL-7 | Signed decision record |

**Flagship / platform rule.** Windows + WinUI 3 is the flagship shell. Android and any
other multiplatform work is additive and must not regress the Windows path. A gate is not
`PASSED` if it only holds on a non-flagship platform.

---

## 4. Gate detail (current state / next action)

### REL-0 — Strict CI/WPT baseline
- **State:** `PASSED` — full Main run `45428660357` on `fd4655eb3b6`, which includes the
  seven WPT suite re-enables from `f5992a160b6`.
- **Evidence:** 194 jobs — 79 `success`, 115 `skipped`, **0 failures and 0 cancellations**;
  20/20 WPT shards `success`; `Process WPT Results` success; `WPT_ALWAYS_SUCCEED_ARG` empty in
  shard logs; sample shard log shows `Filtering 0 unexpected results`. No check run in the Main
  workflow was cancelled.
- **Earlier attempts (superseded, recorded for traceability):** `45413720692` had 78 success /
  115 skipped / 1 cancelled / 0 genuine failures — `MacOS Arm64 … C API` (`138514276569`)
  never got a runner (`runner_id=0`). Its single-job rerun then failed `Result` with
  `No artifacts found matching pattern 'cargo-timings-*'` (rerun/artifact-merge artifact).
  Neither is usable as a green baseline.
- **Scope note:** the separate Embedding API run `45428660374` on the same commit had one
  cancelled macOS C API job and is not part of this Main-run verification; it is tracked under
  REL-2. Current `main` (`687b6ed7d7`) differs from `fd4655eb3b6` only in documentation and
  the analysis script `etc/ci/wpt_pass_rate.py`, so no compiled code changed after the green run.
- **Rule preserved:** no `--always-succeed` on Main; no weakened check; no failed-run install.

### REL-1 — Runtime / platform validation
- **State:** `NOT_CONFIRMED`.
- **Next action:** execute the manual windowed/GPU checklist on real hardware
  (`W3.5` / `W4.5` in `AGENT_BUILD_PLAN.md`); record OS build, GPU, and screenshots.
- **Blocker:** requires a human and a real GPU machine; never CI.

### REL-2 — browserd ABI / lifecycle
- **State:** `FAILED` → `NOT_CONFIRMED`; the first implementation attempt was reverted.
- **Root cause of run `45434483363`:** all three platform C API jobs panicked in
  `servo_builder_set_event_loop_waker` with `misaligned pointer dereference … address 0x1`
  (`ffi/capi/lib.rs:882`). The new C tests passed a deliberately bogus handle expecting
  `SERVO_ERROR_INVALID_ARGUMENT`, but committed `main` only registers/checks `ServoOptions`;
  every other handle kind is still an unchecked raw pointer. The wider registry adoption
  exists in uncommitted local work only, so the tests and matching doc edits asserted
  behaviour that is not on this branch. No checks were weakened to make the tests pass.
- **Decision:** revert the tests and the "all eight handle kinds adopted" documentation.
  The docs are accurate again (`ServoOptions` only); REL-2 stays `NOT_CONFIRMED`.
- **Next action:** land the handle-registry adoption as a **code** change first — register
  in each constructor, check liveness in each entry point, unregister where ownership moves —
  then re-add the lifecycle tests on top of the committed behaviour. Until then, garbage and
  double-freed handles of those types remain undefined behaviour by contract.
- **Note:** the C API version is pre-1.0; versioning rules are in `ABI_STABILITY.md` §2.4.

### REL-3 — Real-world compatibility
- **State:** `IN_PROGRESS` (step 1 done; steps 2–3 pending).
- **Step 1 — WPT baseline: `DONE`.** `WPT_STRICT_BASELINE.md` and `wpt_strict_baseline.json`
  were generated by `etc/ci/wpt_pass_rate.py --reports-dir … --expected-revision … --run-id …`
  from artifact `wpt-full-logs-linux` of strict run `45396797175` at `5461673d1e9`. The script
  refuses to publish reports from mixed or unexpected revisions. All 20 shards report the
  same revision.
  - **Honest headline:** assertion pass rate **88.37%** (3,313/3,749 assertions).
  - **CI-gate view:** 224/407 executed test files have harness status `OK` (55.04%); 182 are
    `SKIP`, 1 `TIMEOUT`. `SKIP`/`NOTRUN` are not passes.
  - **Critical caveat:** 109 harness-`OK` test files still contain `FAIL`/`ERROR` assertions
    (WPT webdriver harness behaviour). Harness `OK` is therefore **not** a conformance pass;
    the two dimensions are reported separately. This is the executed subset, not the full WPT
    tree, and is not to be conflated with `wpt_baseline.json` (static expectations).
- **Step 2 — real-site runbook: `DONE` (preparation); execution `NOT_RUN`.**
  `SITE_SMOKE_RUNBOOK.md` fixes a five-site smoke set, the required evidence (binary from a
  successful CI artifact, run id, artifact sha256, screenshots), and the triage/ticket
  template. `etc/ci/site_smoketest.py` no longer writes a plausible-looking report when no
  binary is available: without `--servo-binary` it exits `2` and writes nothing, and
  `--dry-run` emits explicit `NOT_RUN` rows with a "not test evidence" header.
  `SITE_SMOKETEST_REPORT.md` regenerated as that `NOT_RUN` plan. No site has been exercised,
  so this is **not** a compatibility pass.
- **Next action:** execute the fixed set against a successful CI artifact and record evidence;
  triage every non-`PASS` into `WPT_TRIAGE.md`; then fix engine behaviour, not expectations,
  and re-run WPT to measure the delta.

### REL-4 — Security / stability
- **State:** `NOT_CONFIRMED`.
- **Next action:** read `SECURITY_AUDIT.md` §5 / §8 and `HARDENING_PLAN.md`; close the
  sandbox-honesty and reachable-panic items; verify the scheduled fuzzing workflow actually
  runs with valid CLI syntax.

### REL-5 — Performance
- **State:** `BLOCKED` — measurements are recorded, but no threshold comparison ran.
- **Evidence (`DONE`):** `PERFORMANCE_BASELINE.md` records the real Speedometer score from
  Main run `45428660357` / job `138599279029` (`5.5637 ± 0.1620`, no retries used) and the
  perf-analysis numbers from artifact `14941829764` / job `138599178752` (Layout mean
  `106.8 ms`, Renderer mean `129.6 ms`, FP/FCP mean `1.060 s`, 5 samples of `servo.org`).
  The retry wrapper in `testing_commands.py` retries only `subprocess.TimeoutExpired`, bounded
  at 2, and still fails on malformed results; the threshold was not weakened.
- **Blocker:** `Send results` in `.github/workflows/bencher.yml` failed with
  `error: a value is required for '--token <TOKEN>' but none was supplied`. The step has
  `continue-on-error: true`, so the job stayed green while **no baseline comparison or
  regression detection occurred**. Do not claim a threshold pass, and do not remove
  `continue-on-error` until the token is confirmed or a reviewed visible gate replaces it.
- **Additional gaps:** no long-uptime / repeated load-unload leak evidence; no Windows
  performance evidence (Speedometer is `false` on Windows/macOS/Android/OpenHarmony and runs
  only for `checked-release` on Linux), so the flagship target is not covered.
- **Next action:** configure/review the bencher token, re-run and record a real baseline
  comparison at the same threshold, then add leak and Windows evidence. Do not lower the gate.

### REL-6 — Distribution
- **State:** `NOT_CONFIRMED`.
- **Next action:** produce signed/notarized artifacts from successful CI runs only
  (`W5.3`, `W3.4`); verify a clean-machine install for the flagship Windows target.

### REL-7 — Observability
- **State:** `NOT_CONFIRMED`.
- **Next action:** ensure size / startup / throughput / crash metrics are emitted and
  retained; crash reporting must be opt-in (`FULL_BROWSER_ROADMAP.md` §3.5).

### REL-8 — Public-release sign-off
- **State:** `BLOCKED` until REL-1 … REL-7 are `PASSED`.
- **Next action:** write a signed go/no-go record referencing the run ids and artifacts for
  each gate. Until then, describe browserd as beta / dogfood only.

---

## 5. Change log

| Date | Change | Evidence |
|---|---|---|
| 2026-11-24 | Tracker created. CI/WPT `PASSED` at `5461673d1e9`; re-enable run `45413720692` initially `IN_PROGRESS`; all public-release gates `NOT_CONFIRMED`/`BLOCKED`. | Runs `45396796842`, `45396797175`, `45413720692` |
| 2026-11-24 | Run `45413720692` concluded `failure` solely from one infrastructure-cancelled macOS arm64 C API job (no real failure); job rerun requested; REL-0 set to `FAILED` → rerun `IN_PROGRESS`. | Run `45413720692`, job `138514276569` |
| 2026-11-24 | Attempt 2 of `45413720692` also concluded `failure`: single-job rerun could not merge first-attempt artifacts, so `Result` reported no `cargo-timings-*`. Both attempts recorded as infrastructure, not code; fresh full run `45428660357` started on the docs commit. | Run `45413720692`, check run `Result` |
| 2026-11-24 | REL-2 lifecycle tests on `5881869781f` failed on all 3 platforms (`45434483363`): garbage handle dereferenced at `ffi/capi/lib.rs:882` because registry adoption beyond `ServoOptions` is not in committed code. Reverted tests and over-claiming docs; REL-2 remains `NOT_CONFIRMED`. | Jobs `138588460265/612/962` |
| 2026-11-24 | REL-3 step 1: published per-suite WPT baseline from strict run `45396797175` (`5461673d1e9`). Reports 88.37% assertion pass rate and 55.04% harness-`OK` rate separately, flags 109 harness-`OK` files with failing assertions, and never counts `SKIP` as a pass. REL-3 → `IN_PROGRESS` (steps 2–3 pending). | `WPT_STRICT_BASELINE.md`, `wpt_strict_baseline.json`, artifact `14924931895` |
| 2026-11-24 | REL-0 `PASSED` on full Main run `45428660357` (`fd4655eb3b6`): 194 jobs, 0 failures/cancellations, 20/20 strict WPT shards + `Process WPT Results` success, empty `WPT_ALWAYS_SUCCEED_ARG`, `Filtering 0 unexpected results`. The cancelled macOS C API check came from the separate Embedding API run `45428660374`, not the Main run. | Run `45428660357`; jobs `138599103611`, `138604438839` |
| 2026-11-24 | REL-3 step 2 preparation: fixed five-site smoke set and evidence/triage rules published in `SITE_SMOKE_RUNBOOK.md`. Hardened `site_smoketest.py` so a missing binary is an error (exit 2) and `--dry-run` output is explicitly `NOT_RUN`; regenerated `SITE_SMOKETEST_REPORT.md` as a plan. No site executed — real-site results remain `NOT_RUN`. | `SITE_SMOKE_RUNBOOK.md`, `SITE_SMOKETEST_REPORT.md`; dry-run verified locally |
| 2026-11-24 | REL-5 measurements published in `PERFORMANCE_BASELINE.md` (Speedometer 5.5637 ± 0.1620; Layout mean 106.8 ms, Renderer mean 129.6 ms, FP/FCP mean 1.060 s), but the bencher threshold submission failed on a missing token and was masked by `continue-on-error: true`. No pass claimed; no Windows or leak evidence. REL-5 → `BLOCKED`. | Jobs `138599279029`, `138599178752`; artifact `14941829764` |
