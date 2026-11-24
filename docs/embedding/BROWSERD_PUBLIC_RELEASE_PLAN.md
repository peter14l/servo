# browserd — Public Release Readiness Implementation Plan

Status: **Executable work plan** · Baseline: `5461673d1e9` · Written: 2026-11-24

This document turns the gates in
[`BROWSERD_PUBLIC_RELEASE_TRACKER.md`](./BROWSERD_PUBLIC_RELEASE_TRACKER.md) into
self-contained tickets that an AI coding session can execute from a cold start and advance
to a verified state. It follows the discipline of
[`AGENT_BUILD_PLAN.md`](./AGENT_BUILD_PLAN.md) (tactical tickets, evidence, honest states)
and [`WINDOWS_BROWSER_PLAN.md`](./WINDOWS_BROWSER_PLAN.md) (flagship-shell tracker).

**Scope:** the gates between "green CI/WPT" and "safe for public browserd use"
(REL-0 … REL-8 in the tracker).

**Explicitly out of scope here:** re-doing the embedding API work already tracked in
`AGENT_BUILD_PLAN.md` W2–W6; the multi-year Blink/CEF parity goal in
[`FULL_BROWSER_ROADMAP.md`](./FULL_BROWSER_ROADMAP.md); and any local full build. This plan
*references* those workstreams as prerequisites instead of duplicating them.

> **Green CI + strict WPT = beta / dogfood readiness, not public-release sign-off.**
> No ticket in this plan may be marked complete on the strength of a green CI run alone
> when its acceptance criteria require hardware, a human decision, or real-world evidence.

---

## 0. How to use this document

1. Read §1 (Ground rules) in full before touching anything. It contains the hard
   constraints that cause most tickets to fail if ignored.
2. Open the tracker and pick **one** gate whose status is `NOT_RUN`, `NOT_CONFIRMED`, or
   `FAILED` and whose dependencies are satisfied. Do not run two gates in one session —
   the verification gate is per-push and interleaving makes failures unattributable.
3. Each ticket in §3 is self-contained: *State → Prerequisites → Touch points → Steps →
   Acceptance → Tests → Gotchas*. Where a step says "verify", actually verify and record
   the run id or artifact.
4. Line numbers and run ids drift. Re-locate symbols with `git grep` before editing, and
   re-check the tracker for the current baseline commit.
5. When the ticket's Acceptance and Tests are all green, stop, update the tracker, and
   report using the template in §4. Do not "improve" adjacent work.

### Status vocabulary

Use exactly the tracker's states from `BROWSERD_PUBLIC_RELEASE_TRACKER.md` §1:
`PASSED`, `FAILED`, `BLOCKED`, `NOT_RUN`, `IN_PROGRESS`, `DONE`, `NOT_CONFIRMED`.

---

## 1. Ground rules (mandatory)

### 1.1 Repository and tooling facts

| Fact | Value |
|---|---|
| Repo | `E:\servo`, remote `https://github.com/peter14l/servo.git` |
| Working tree | `E:\servo` contains un-committed user work — never stage or commit it |
| Isolated worktree | `C:\Users\LOQ\AppData\Local\Temp\opencode\servo-ci-fix` on `fix/ci-jobs` |
| Last fully verified clean Main/WPT | `fd4655eb3b6`, Main run `45428660357` (includes WPT suite re-enables); 0 failures/cancellations |
| Earlier clean baseline (pre-re-enable) | `5461673d1e9`, runs `45396796842` / `45396797175` |
| Current pushed `main` | `687b6ed7d7` (revert + WPT baseline docs); differs from the verified commit only in docs and `etc/ci/wpt_pass_rate.py` |
| Flagship shell | Windows + WinUI 3; Android/multiplatform is additive only |
| `gh` CLI | authenticated as `peter14l` |

Never stage or commit `graphify-out/` or `.project/`. A dirty `graphify-out/` after hooks
is expected; it is not a reason to skip the graph. For codebase questions, run
`graphify query "<question>"` before raw grep/source reads, and `graphify update .` after
code changes.

### 1.2 Build and verification reality — read twice

**Do not attempt a local full build, local test suite, cargo/msbuild/mach invocation, or
installer execution.** Disk is the binding constraint; the full workspace does not fit.
All builds, binaries, and artifacts come from GitHub Actions.

| What | Where it runs |
|---|---|
| Full build + C API tests + WPT | CI only (`linux.yml`, `linux-wpt.yml`, `windows.yml`, `mac*.yml`) |
| Windowed / GPU / real-hardware behaviour | Human + real GPU machine, never CI |
| Artifact install / upgrade verification | Clean VM from a **successful** CI run only |

Install only from successful CI artifacts. Never install output from a failed run, and never
downgrade a dependency or toolchain without explicit user approval.

### 1.3 Non-negotiable rules

1. **Strict WPT gating.** `--always-succeed` is permitted **only** for the scheduled
   `wpt-sync-from-upstream` workflow. Any occurrence in a Main/WPT verification is a
   `FAILED` gate. Disabled/removed suites stay disabled until strict results are clean,
   then are re-enabled by an explicit commit (as in `f5992a160b6`).
2. **No forced passes.** Do not weaken a check to make it green. `ktfmt` is satisfied by
   matching formatter output (`kotlinLangStyle()` + `COMPLETE`), not by relaxing the check.
   `deny.toml` ignores/skips and `./graphify` tidy exclusions are narrow, recorded debt —
   not a template for new suppressions.
3. **Honest capability claims.** Never log or document a capability that does not exist
   (the false "sandbox active" log in `components/servo/servo.rs` is the anti-pattern).
4. **Flagship-first.** A gate does not pass if it only holds on Android or another
   secondary target.
5. **One ticket, one push.** Never interleave workstreams in a single verification cycle.
6. **Evidence or it did not happen.** Every `PASSED` needs a run id, log excerpt, artifact,
   or signed decision record. An unexecuted check is `NOT_RUN`.
7. **Rollback is documented before it is needed** (§5).

### 1.4 Verification commands

```powershell
# Current run state
gh run view <run-id> --json status,conclusion,headSha,url
gh run list -R peter14l/servo -L 8

# Job-level conclusions (job logs are NOT in the run log)
$jobs = @(); $page = 1
do {
  $raw = gh api "repos/peter14l/servo/actions/runs/<run-id>/jobs?per_page=100&page=$page"
  $parsed = $raw | ConvertFrom-Json
  $jobs += $parsed.jobs; $page++
} while ($parsed.jobs.Count -eq 100)
$jobs | Group-Object conclusion | Select-Object Name,Count
$jobs | Where-Object { $_.conclusion -notin @('success','skipped') } |
  Select-Object name,status,conclusion
```

For strict WPT, additionally confirm: all WPT shards `success`, `Process WPT Results`
`success`, `WPT_ALWAYS_SUCCEED_ARG` empty, and a sample shard log showing
`Filtering 0 unexpected results`.

---

## 2. Gate sequence (waves)

Each wave is independently shippable. Do not start a wave before the previous one is
green. REL-0 is the gate already in progress; REL-8 is the decision record, not code.

| Wave | Gates | Why this order |
|---|---|---|
| **0 — Baseline** | REL-0 | Nothing else is trustworthy until the strict, unmasked CI/WPT baseline (including re-enabled suites) is green. |
| **1 — Contract** | REL-2 | browserd's ABI/lifecycle is the interface every other gate depends on; fix it before validating real hardware or distribution. |
| **2 — Evidence** | REL-1, REL-3, REL-5, REL-7 | Hardware, real-world, performance, and observability evidence can then be gathered in parallel against a stable contract. |
| **3 — Hardening & distribution** | REL-4, REL-6 | Security depth and signed packaging depend on the evidence above; ship only from successful artifacts. |
| **4 — Sign-off** | REL-8 | A documented go/no-go only after REL-1 … REL-7 are `PASSED`. |

---

## 3. Gate tickets

### REL-0 — Preserve the strict CI/WPT baseline

- **State:** `PASSED` — Main run `45428660357` on `fd4655eb3b6` (tracker §2/§3).
- **Prerequisites:** none.
- **Touch points:** `.github/workflows/linux-wpt.yml`, `.github/workflows/linux.yml`,
  `tests/wpt/include.ini`.
- **Diagnosis (superseded attempts):** run `45413720692` had 78 success / 115 skipped /
  1 cancelled / 0 genuine failures — the macOS arm64 C API job never got a runner. Its
  single-job rerun then failed `Result` with `No artifacts found matching pattern
  'cargo-timings-*'` because a one-job rerun cannot merge first-attempt artifacts. A fresh
  full run was required; neither attempt is a usable baseline.
- **Steps (all executed):**
  1. Watched the fresh full run `45428660357` to terminal: 194 jobs, 79 `success`,
     115 `skipped`, **0 failures / 0 cancellations**.
  2. Confirmed all WPT shards (20/20) `success` and `Process WPT Results` `success`.
  3. Confirmed `WPT_ALWAYS_SUCCEED_ARG` empty in shard logs and `Filtering 0 unexpected
     results` in a sample shard log. Static check: the flag is set only when
     `inputs.wpt-sync-from-upstream` is true in `linux-wpt.yml`.
  4. Recorded `PASSED` with the run id; no masking argument was restored.
- **Acceptance:** Main run `success`/no failures; strict WPT clean with the seven
  re-enabled suites; no masking argument — **met**. Note: a separate cancelled macOS C API
  check from the Embedding API run `45428660374` is not part of this Main-run verification and
  is tracked under REL-2.
- **Tests:** §1.4 verification commands, recorded in the tracker change log.
- **Gotchas:** a `cancelled` job with no steps means it never got a runner — confirm
  whether it is infrastructure churn or a real regression before ignoring it. Do not treat
  a run as green while any check run is `cancelled`.

### REL-1 — Runtime / platform validation

- **State:** `NOT_CONFIRMED`.
- **Prerequisites:** REL-0 `PASSED`.
- **Touch points:** manual checklist derived from `W3.5` / `W4.5` in
  `AGENT_BUILD_PLAN.md`; `docs/embedding/COMPATIBILITY.md`.
- **Steps:**
  1. Install the flagship Windows + WinUI 3 shell from a **successful** CI artifact.
  2. Exercise windowed rendering, resize, DPI change, input, IME, clipboard, dialogs,
     downloads, multi-tab/popup lifecycle on real Windows 11 hardware.
  3. Record OS build, GPU/driver, screenshots, and any crash.
  4. Repeat the smoke set on Android and one non-flagship desktop target (additive only).
- **Acceptance:** flagship checklist fully executed with no blocker; secondary targets
  either pass or are explicitly documented as limited.
- **Tests:** manual runbook with evidence attached to the tracker.
- **Gotchas:** cannot be automated; `BLOCKED` until a human and GPU are available.

### REL-2 — browserd ABI / lifecycle contract

- **State:** `NOT_CONFIRMED`.
- **Prerequisites:** REL-0; `W5.4` (ABI versioning) from `AGENT_BUILD_PLAN.md`.
- **Touch points:** `ffi/capi/*`, `tests/capi/c/*`, `docs/embedding/ABI_STABILITY.md`.
- **Steps:**
  1. Enumerate every handle browserd touches; for each, state ownership and thread.
  2. Add C lifecycle tests: create → use → suspend → resume → destroy, plus double-free /
     use-after-free / bad-argument cases.
  3. Verify no panic crosses the FFI boundary (`catch_unwind` where needed).
  4. Confirm versioning rules for pre-1.0 changes are documented.
- **Acceptance:** lifecycle tests pass in `embedding-api.yml` on all platforms; contract
  documented and linked from `ABI_STABILITY.md`.
- **Tests:** `tests/capi/c/test_integration.c` additions + CI run id.
- **Gotchas:** generated header is never hand-edited; `symbols.txt` is append-only.

### REL-3 — Real-world compatibility

- **State:** `IN_PROGRESS` (step 1 `DONE`; steps 2–3 pending).
- **Prerequisites:** REL-0; W1 (WPT) workstream.
- **Touch points:** WPT baseline report, real-site runbook, `docs/embedding/PARITY_MATRIX.md`.
- **Steps:**
  1. `DONE` — Publish a per-suite WPT pass-rate baseline from the strict run:
     `docs/embedding/WPT_STRICT_BASELINE.md` + `wpt_strict_baseline.json`, generated from run
     `45396797175` / revision `5461673d1e9` via `etc/ci/wpt_pass_rate.py`. Reports assertion
     pass rate (88.37%) and harness `OK` rate (55.04%) separately; `SKIP` is not a pass, and
     the 109 harness-`OK` files with failing assertions are called out explicitly.
  2. `DONE` (preparation) — fixed the smoke set and evidence rules in
     `docs/embedding/SITE_SMOKE_RUNBOOK.md`; `etc/ci/site_smoketest.py` now refuses to emit a
     report without a binary (exit 2) and labels `--dry-run` output `NOT_RUN`.
     **Execution is `NOT_RUN`**; `SITE_SMOKETEST_REPORT.md` contains no results.
  3. Fix engine behaviour, never expectations; re-run WPT to measure delta.
- **Acceptance:** baseline published with run id (`DONE` for step 1); blocker list has owners;
  no unexplained site-specific hacks. Overall gate still `NOT_CONFIRMED` until steps 2–3.
- **Tests:** strict WPT delta + real-site results in the tracker.
- **Gotchas:** W1 is measured in years; this gate tracks progress honestly, it is not a
  single-session "completion".

### REL-4 — Security / stability

- **State:** `NOT_CONFIRMED`.
- **Prerequisites:** REL-2; `W2.x` security tickets.
- **Touch points:** `SECURITY_AUDIT.md` §5/§8, `HARDENING_PLAN.md`,
  `.github/workflows/fuzzing.yml`.
- **Steps:**
  1. Close sandbox-honesty and reachable-panic items from `AGENT_BUILD_PLAN.md`.
  2. Verify the scheduled fuzzing workflow runs with valid CLI syntax (the known CLI
     syntax fix is out of scope unless the user approves — record it as `BLOCKED`).
  3. Confirm hang monitor, crash handling, and CSP/COOP/CORP/XCTO/TLS review evidence.
- **Acceptance:** audit items have evidence; fuzzing job green; no false capability claims.
- **Tests:** `embedding-api.yml` + scheduled fuzzing run id.
- **Gotchas:** do not claim sandboxing where none is applied.

### REL-5 — Performance

- **State:** `BLOCKED` (measurements `DONE`; threshold gate and platform coverage pending).
- **Prerequisites:** REL-0; `W5.6`/`W5.7`.
- **Touch points:** perf analysis job, `benchmark-servo.sh` (fetched in CI, not vendored),
  Speedometer gate, `.github/workflows/bencher.yml`, `docs/embedding/PERFORMANCE_BASELINE.md`.
- **Steps:**
  1. `DONE` — keep the real benchmark and threshold; retry **only** `TimeoutExpired` (bounded
     at 2 in `testing_commands.py`; malformed results still fail). No retry was used in run
     `45428660357`; Speedometer scored `5.5637 ± 0.1620`. Perf-analysis numbers and artifact
     provenance are in `PERFORMANCE_BASELINE.md`.
  2. `BLOCKED` — compare against the recorded baseline: `Send results` failed with a missing
     `--token` value while `continue-on-error: true` kept the job green, so **no comparison
     ran**. Configure/confirm the token (or a reviewed visible gate) first; do not simply
     delete `continue-on-error` and break CI on configuration. Investigate regressions rather
     than lowering the gate.
  3. `NOT_RUN` — confirm long-uptime / repeated load-unload leak results and add flagship
     Windows performance evidence (Speedometer is Linux `checked-release` only).
- **Acceptance:** perf job green on its real threshold **and** a baseline comparison recorded
  — **not met**. No pass is claimed for this gate.
- **Tests:** perf analysis job id + numbers (`138599178752`, artifact `14941829764`),
  Speedometer job `138599279029`.
- **Gotchas:** do not reintroduce stale flags (e.g. `--print-pwm`) into benchmarks; a green
  job with a masked submission error is not a green threshold.

### REL-6 — Distribution

- **State:** `NOT_CONFIRMED`.
- **Prerequisites:** REL-1, REL-2; `W5.3`/`W3.4`.
- **Touch points:** release workflows, installer/signing scripts.
- **Steps:**
  1. Produce signed/notarized artifacts from a **successful** CI run only.
  2. Verify a clean-machine install and upgrade of the flagship Windows package.
  3. Document support cadence and rollback to the previous known-good artifact.
- **Acceptance:** install succeeds on a clean VM; artifacts trace to a successful run id.
- **Tests:** artifact hash + run id + clean-VM result.
- **Gotchas:** never install or ship failed-run output; no downgrade without approval.

### REL-7 — Observability

- **State:** `NOT_CONFIRMED`.
- **Prerequisites:** REL-4.
- **Touch points:** CI metrics job, opt-in telemetry pipeline.
- **Steps:**
  1. Emit and retain size, startup, throughput, and crash metrics.
  2. Make crash reporting opt-in; symbolicate crashes.
  3. Confirm metrics are available for each shipped platform.
- **Acceptance:** metrics present and linked from the tracker.
- **Tests:** CI metrics job id + sample payload.
- **Gotchas:** no silent telemetry; privacy posture must match `SECURITY.md`.

### REL-8 — Public-release sign-off

- **State:** `BLOCKED` until REL-1 … REL-7 are `PASSED`.
- **Prerequisites:** all of REL-1 … REL-7.
- **Touch points:** tracker §5 change log, a signed decision record.
- **Steps:**
  1. Collect run ids / artifacts for every gate.
  2. Write a go/no-go record: what passed, what is deferred, known limitations.
  3. Update this tracker to `PASSED` only when the record is signed.
- **Acceptance:** signed record exists; no gate relies on `DONE`-without-`PASSED`.
- **Tests:** record review.
- **Gotchas:** until this gate passes, describe browserd as beta / dogfood only.

---

## 4. Reporting template (end of every ticket)

```markdown
## <GATE-ID> — <title>
- commits: <short-sha> (<subject>)
- CI: run <id> on <sha> → <conclusion>; relevant jobs: <names>
- local gates run: <commands + result>   # normally "none — CI only"
- acceptance: item-by-item with evidence (file:line or run id)
- state changed: from <old> to <new> in BROWSERD_PUBLIC_RELEASE_TRACKER.md
- not done / deliberately deferred: <list>
- new risks found: <list>
```

Be conservative: an item you could not execute is `NOT_RUN`, never `done`.

---

## 5. Rollback procedures

| Situation | Rollback |
|---|---|
| A CI fix regresses Main/WPT | Revert the offending commit on `fix/ci-jobs`, push, re-run strict Main/WPT; restore the last green baseline `5461673d1e9` if needed. |
| A re-enabled WPT suite fails | Keep the suite disabled with a tracked ticket; **do not** restore `--always-succeed`; re-enable only after strict results are clean. |
| Formatter/lint gate fails | Revert to the exact formatter output; never weaken the check. |
| Dependency/`deny.toml` change causes breakage | Revert the narrow ignore/skip; do not broadly suppress findings. |
| Artifact fails clean-machine install | Withdraw that artifact; install the previous known-good **successful** run only; no downgrade without approval. |
| Security capability claim found false | Correct the log/doc immediately, mark REL-4 `FAILED`, and re-verify. |
| Public sign-off discovered to be premature | Set REL-8 back to `BLOCKED` and downgrade external messaging to beta/dogfood. |

The escape hatch for the embedding work remains `git reset --hard 13cad2c3a45` on the
embedding branch (see `AGENT_BUILD_PLAN.md` §1.1); do not apply it to the dirty `E:\servo`
worktree without explicit approval.

---

## 6. Risks

| Risk | Mitigation |
|---|---|
| "Green CI" is mistaken for public readiness | Tracker separates `CI_AND_WPT = PASSED` from `PUBLIC_RELEASE_READINESS = NOT_CONFIRMED`; REL-8 requires a signed record. |
| WPT masking returns | Strict gating in `linux-wpt.yml`; `--always-succeed` only on scheduled sync. |
| Hardware-gated gates stall | Mark `BLOCKED` with the specific hardware/human need; do not fake evidence. |
| W1/parity is multi-year | Track honest deltas; do not promise completion in one session. |
| Flagship regresses while adding platforms | Windows + WinUI 3 first; secondary targets additive and separately evidenced. |
| Suppressions become habit | `deny.toml` / tidy exclusions must stay narrow and be recorded as debt. |
| Artifact trust | Install only from successful runs; record hashes and run ids. |

---

## 7. Related documents

- [`BROWSERD_PUBLIC_RELEASE_TRACKER.md`](./BROWSERD_PUBLIC_RELEASE_TRACKER.md) — status tracker.
- [`AGENT_BUILD_PLAN.md`](./AGENT_BUILD_PLAN.md) — W2–W6 embedding tickets (prerequisites).
- [`WINDOWS_BROWSER_PLAN.md`](./WINDOWS_BROWSER_PLAN.md) — flagship Windows shell plan.
- [`PRODUCTION_READINESS.md`](./PRODUCTION_READINESS.md) / [`PRODUCTION_REPORT.md`](../../PRODUCTION_REPORT.md) — strategic assessment.
- [`FULL_BROWSER_ROADMAP.md`](./FULL_BROWSER_ROADMAP.md) — long-horizon full-browser milestones.
- [`ABI_STABILITY.md`](./ABI_STABILITY.md), [`SECURITY_AUDIT.md`](./SECURITY_AUDIT.md), [`HARDENING_PLAN.md`](./HARDENING_PLAN.md) — gate evidence sources.
