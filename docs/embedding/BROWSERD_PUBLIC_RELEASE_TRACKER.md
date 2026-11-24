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
| Re-enable previously skipped WPT suites | `f5992a160b6d7ca29883258a1cf82107fac0cc4c` | `45413720692` | `FAILED` → rerun `IN_PROGRESS` | 78 success / 115 skipped / **1 cancelled**; no job had a genuine `failure`. The cancelled job is `MacOS Arm64 (Unit Tests, Bencher, C API) / MacOS / Build and test Servo's C API` — it never got a runner (`runner_id=0`, no steps) and was cancelled after 15 min, so GitHub marked the run failed. Job rerun requested; strict WPT results remain clean. |
| Local heavy builds / test suites | — | — | `NOT_RUN` | By design: all builds and tests run in GitHub Actions only. |

Older failures are covered by later fixes and are recorded for traceability, not as open
work: `45384672757` (Main, `Install crown`) and `45384672761` (Lint ktfmt) were resolved
by `903f8325122`, `f0fba21fa6f`, and `5461673d1e9`.

**Overall status: `CI_AND_WPT = PASSED` at `5461673d1e9`; `PUBLIC_RELEASE_READINESS = NOT_CONFIRMED`.**

---

## 3. Master tracker

Effort: **S** a few days · **M** 1–3 weeks · **L** 1–3 months · **XL** quarters+.
"Depends on" refers to gate IDs in this table; upstream embedding tickets are named where
they provide a prerequisite.

| Gate | Title | Effort | Status | Depends on | Verification gate |
|---|---|---|---|---|---|
| **REL-0** | Strict CI/WPT baseline preserved (no masking, suites re-enabled) | S | `FAILED` → rerun `IN_PROGRESS` (`f5992a160b6` / run `45413720692`, cancelled macOS arm64 job) | — | Main workflow + strict WPT job conclusions |
| **REL-1** | Runtime / platform validation (Windows + WinUI 3 flagship; Android additive) | L | `NOT_CONFIRMED` | REL-0 | Manual hardware checklist + platform CI artifacts |
| **REL-2** | browserd ABI / lifecycle contract (ownership, threading, teardown, versioning) | M | `NOT_CONFIRMED` | REL-0, W5.4 | `embedding-api.yml` + C ABI lifecycle tests |
| **REL-3** | Real-world compatibility (top sites, WPT deltas, no site-specific hacks) | XL | `NOT_CONFIRMED` | REL-0, W1 | WPT pass-rate baseline + real-site runbook |
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
- **State:** `FAILED` → rerun `IN_PROGRESS` — run `45413720692` on `f5992a160b6`.
- **Diagnosis:** 78 jobs success, 115 skipped, 1 cancelled, **0 genuine failures**. The sole
  non-success is `MacOS Arm64 (Unit Tests, Bencher, C API) / MacOS / Build and test Servo's
  C API` (job `138514276569`): no steps ran, `runner_id=0`, cancelled after ~15 min waiting
  for a macOS arm64 runner. This is infrastructure churn, not a code regression. A job-level
  rerun has been requested.
- **Next action:** watch the rerun to terminal; confirm all job conclusions, all strict WPT
  shards with the re-enabled suites, `Process WPT Results`, and an empty
  `WPT_ALWAYS_SUCCEED_ARG`.
- **If green:** set `PASSED` with the run id. If the cancellation repeats: record it as a
  runner-availability blocker, not a code failure; do not re-add `--always-succeed`.

### REL-1 — Runtime / platform validation
- **State:** `NOT_CONFIRMED`.
- **Next action:** execute the manual windowed/GPU checklist on real hardware
  (`W3.5` / `W4.5` in `AGENT_BUILD_PLAN.md`); record OS build, GPU, and screenshots.
- **Blocker:** requires a human and a real GPU machine; never CI.

### REL-2 — browserd ABI / lifecycle
- **State:** `NOT_CONFIRMED` (embedding API version is pre-1.0; see `ABI_STABILITY.md`).
- **Next action:** define and test create → use → suspend → resume → destroy sequences for
  every handle browserd touches; verify no panic crosses the FFI boundary.

### REL-3 — Real-world compatibility
- **State:** `NOT_CONFIRMED`.
- **Next action:** publish a WPT baseline with per-suite pass rates, then run a fixed top-site
  list and triage blockers into tickets. Fix engine behaviour; never bulk-edit expectations.

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
