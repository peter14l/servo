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
| Fresh full run on docs commit | `fd4655eb3b6b39ad1698aa5571bba45869aa4283` | `45428660357` | `IN_PROGRESS` | Full-run watcher active; supersedes the rerun-artifact failure above. |
| REL-2 lifecycle test attempt | `5881869781f` | `45434483363` | `FAILED` (genuine, reverted) | All three C API jobs panicked at `ffi/capi/lib.rs:882`: `misaligned pointer dereference … address 0x1`. The tests assumed handle-liveness checks that are present in uncommitted local work but **not** in committed `main`, where `servo_builder_set_event_loop_waker` dereferences the garbage handle. Reverted in this change. |
| Local heavy builds / test suites | — | — | `NOT_RUN` | By design: all builds and tests run in GitHub Actions only. |

Older failures are covered by later fixes and are recorded for traceability, not as open
work: `45384672757` (Main, `Install crown`) and `45384672761` (Lint ktfmt) were resolved
by `903f8325122`, `f0fba21fa6f`, and `5461673d1e9`.

**Overall status: `CI_AND_WPT = PASSED` at `5461673d1e9`; re-enable verification not yet
re-confirmed at `f5992a160b6` / `fd4655eb3b6`; `PUBLIC_RELEASE_READINESS = NOT_CONFIRMED`.**

---

## 3. Master tracker

Effort: **S** a few days · **M** 1–3 weeks · **L** 1–3 months · **XL** quarters+.
"Depends on" refers to gate IDs in this table; upstream embedding tickets are named where
they provide a prerequisite.

| Gate | Title | Effort | Status | Depends on | Verification gate |
|---|---|---|---|---|---|
| **REL-0** | Strict CI/WPT baseline preserved (no masking, suites re-enabled) | S | `FAILED` (infrastructure) → fresh run `IN_PROGRESS` (`fd4655eb3b6` / run `45428660357`) | — | Main workflow + strict WPT job conclusions |
| **REL-1** | Runtime / platform validation (Windows + WinUI 3 flagship; Android additive) | L | `NOT_CONFIRMED` | REL-0 | Manual hardware checklist + platform CI artifacts |
| **REL-2** | browserd ABI / lifecycle contract (ownership, threading, teardown, versioning) | M | `NOT_CONFIRMED` | REL-0, W5.4 | `embedding-api.yml` + C ABI lifecycle tests |
| **REL-3** | Real-world compatibility (top sites, WPT deltas, no site-specific hacks) | XL | `IN_PROGRESS` (baseline `DONE`) | REL-0, W1 | `WPT_STRICT_BASELINE.md` (run `45396797175`); real-site runbook pending |
| **REL-4** | Security / stability (sandbox honesty, site isolation, fuzzing, crashes) | XL | `NOT_CONFIRMED` | REL-2, W2.x | `embedding-api.yml` + scheduled fuzzing + audit |
| **REL-5** | Performance (Speedometer, startup, memory, long uptime) | L | `NOT_CONFIRMED` | REL-0, W5.6/W5.7 | Perf analysis job + baseline comparison |
| **REL-6** | Distribution (signed artifacts, installers, update story) | L | `NOT_CONFIRMED` | REL-1, REL-2, W5.3/W3.4 | Release workflow artifacts from successful runs only |
| **REL-7** | Observability (size, startup, throughput, crash telemetry) | M | `NOT_CONFIRMED` | REL-4 | CI metrics job + opt-in telemetry pipeline |
| **REL-8** | Public-release sign-off (documented go/no-go with all gates evidenced) | S | `BLOCKED` | REL-1 … REL-7 | Signed decision record |

**Flagship / platform rule.** Windows + WinUI 3 is the flagship shell. Android and any
other multiplatform work is additive and must not regress the Windows path. A gate is not
`PASSED` if it only holds on a non-flagship platform.

---

## 4. Gate detail (current state / next action)

### REL-0 — Strict CI/WPT baseline
- **State:** `FAILED` (infrastructure) → fresh full run `IN_PROGRESS` — `45428660357` on
  `fd4655eb3b6`.
- **Diagnosis:** run `45413720692` had 78 success, 115 skipped, 1 cancelled and **0 genuine
  failures**; the cancelled `MacOS Arm64 … C API` job (`138514276569`) never got a runner
  (`runner_id=0`). Its single-job rerun then made `Result` fail with
  `No artifacts found matching pattern 'cargo-timings-*'` — a rerun/artifact-merge artifact,
  not a code regression. Neither attempt is usable as a green baseline.
- **Next action:** watch `45428660357` to terminal; confirm all job conclusions, all strict
  WPT shards with the re-enabled suites, `Process WPT Results`, and an empty
  `WPT_ALWAYS_SUCCEED_ARG`.
- **If green:** set `PASSED` with the run id. If the cancellation repeats: record it as a
  runner-availability blocker, not a code failure; do not re-add `--always-succeed`.

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
- **Next action:** run a fixed top-site list and triage blockers into tickets; then fix engine
  behaviour, not expectations, and re-run WPT to measure the delta.

### REL-4 — Security / stability
- **State:** `NOT_CONFIRMED`.
- **Next action:** read `SECURITY_AUDIT.md` §5 / §8 and `HARDENING_PLAN.md`; close the
  sandbox-honesty and reachable-panic items; verify the scheduled fuzzing workflow actually
  runs with valid CLI syntax.

### REL-5 — Performance
- **State:** `NOT_CONFIRMED`.
- **Next action:** keep the real Speedometer benchmark and gate; retry only `TimeoutExpired`.
  Compare against the recorded baseline (`W5.6`), do not weaken the threshold.

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
