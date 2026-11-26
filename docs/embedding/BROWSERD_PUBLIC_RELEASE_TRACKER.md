# browserd — Public Release Readiness Progress Tracker

Status: **Living tracker** · Baseline: `5461673d1e9` (CI/WPT PASSED) · Written: 2026-11-24 · Last gate attempt: 2026-11-26

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

## Developer-preview caveats (required on every preview build)

Until REL-1 … REL-8 are `PASSED`, a build from this branch is **developer preview /
dogfood only**. Release notes, installer, and about screen must state all of the
following verbatim in substance:

- **Unsupported platforms.** There is no iOS port and no iOS CI. Android and
  OpenHarmony are experimental. Windows, Android, iOS, OpenHarmony, and all
  ARM/ARM64 and RISC-V targets have **no OS renderer sandbox**.
- **No sandbox by default.** The content-process sandbox is off by default and
  functional only on Linux x86_64 and macOS x86_64. Windows applies in-process
  mitigation policies (DEP, ASLR, strict handle checking, extension-point
  disable, Win32k lockdown, Job Object containment); these are **not** an OS
  sandbox. Query `servo_capi_is_sandboxing_supported()` at runtime; do not assume.
- **No site isolation.** Event loops are pooled by registrable domain. There is no
  per-origin/per-site process partitioning and no out-of-process iframes.
- **Storage limitations.** `Partitioned` (CHIPS) cookies are stored but never
  sent. Do not rely on partitioned-cookie behaviour.
- **Conformance is partial.** The executed strict-WPT subset reports 88.37%
  assertion passes and 55.04% harness-`OK` files; `SKIP` is not a pass. This is
  not browser-ready conformance.
- **Not a public release.** REL-8 has no signed go/no-go decision. Do not describe
  any preview build as production-ready or as a replacement for Blink/WebKit/CEF.

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
| **REL-3** | Real-world compatibility (top sites, WPT deltas, no site-specific hacks) | XL | `IN_PROGRESS` (steps 1–2 `DONE`; smoke `PASSED`; WPT delta pending) | REL-0, W1 | `WPT_STRICT_BASELINE.md` (run `45396797175`); `SITE_SMOKETEST_REPORT.md` (run `45428660357`); `WPT_TRIAGE.md` |
| **REL-4** | Security / stability (sandbox honesty, site isolation, fuzzing, crashes) | XL | `FAILED` (FFI panic boundary + `cargo deny` bans) | REL-2, W2.x | `embedding-api.yml` + scheduled fuzzing + audit |
| **REL-5** | Performance (Speedometer, startup, memory, long uptime) | L | `BLOCKED` (measurements `DONE`, threshold gate not run) | REL-0, W5.6/W5.7 | `PERFORMANCE_BASELINE.md`; bencher token + Windows perf + leak evidence |
| **REL-6** | Distribution (signed artifacts, installers, update story) | L | `NOT_CONFIRMED` | REL-1, REL-2, W5.3/W3.4 | Release workflow artifacts from successful runs only |
| **REL-7** | Observability (size, startup, throughput, crash telemetry) | M | `BLOCKED` (metrics `DONE`; crash telemetry missing) | REL-4 | `OBSERVABILITY_EVIDENCE.md`; opt-in crash pipeline |
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
  REL-2. Current `main` (`3721340f05`) differs from `fd4655eb3b6` only in documentation,
  analysis/smoke scripts and release evidence, so no compiled code changed after the green run.
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
- **State:** `IN_PROGRESS` (steps 1 and 2 `DONE`; step 3 pending). The smoke set passed;
  this is not a full conformance pass and does not close the gate.
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
- **Step 2 — real-site smoke: `DONE` (executed `PASSED`).**
  `SITE_SMOKE_RUNBOOK.md` fixes the five-site set and evidence rules. The binary came from
  successful Main run `45428660357` / artifact `14943064230`; the Windows artifact is a WiX
  installer, so the installed `servoshell.exe` was used and verified via its version string
  `Servo 0.7.0-fd4655eb3`. All five targets returned `PASS` in 2.88–10.12 s with five
  1280×740 screenshots in `docs/embedding/site_smoke_shots/`; harness exit `0`. Full
  provenance (hashes, command, host OS) is in `SITE_SMOKETEST_REPORT.md` and
  `site_smoketest.json`. An earlier all-`TIMEOUT` attempt against the installer itself was
  discarded as invalid provenance and recorded as four resolved harness tickets in
  `WPT_TRIAGE.md` (wrong artifact type, missing `-x`, always-zero exit, missing screenshots).
  No timeout was raised, no check weakened, and no site-specific hack added.
- **Next action:** fix engine behaviour, never expectations; re-run strict WPT and record the
  delta. REL-3 becomes `PASSED` only after that delta is measured with no unexplained hacks.

### REL-4 — Security / stability
- **State:** `FAILED` on this release candidate. Two locally executable subchecks found
  genuine defects; neither was suppressed.
- **Attempted 2026-11-26:** `python etc/ci/check_ffi_panic_boundary.py` exited `1` with
  **16 of 257** exported `extern "C"` functions unwrapped (a Rust panic could cross the FFI
  boundary): `servo_capi_version`, `servo_capi_get_version`,
  `servo_capi_platform_name`, `servo_capi_is_multiprocess_enabled`,
  `servo_capi_is_sandboxing_supported`, 10 `servo_web_resource_*` getters/setters and
  `servo_web_resource_load_get_request`. `cargo deny check` exited `1` with
  `advisories ok, bans FAILED, licenses ok, sources ok`: two `mach2` versions (`0.4.3`
  and `0.7.0`) are present in this branch's `Cargo.lock`, introduced by the local
  `minidump-writer 0.10.2` / `crash-context 0.6.3` crash-handler work; the origin lock has
  only `mach2 0.7.0`.
- **Next action:** wrap the 16 FFI entry points in `ServoError::catch_ffi*`, then resolve
  the duplicate `mach2` by upgrading/aligning the minidump stack (requires a real build).
  Read `SECURITY_AUDIT.md` §5 / §8 and `HARDENING_PLAN.md`; close the sandbox-honesty and
  reachable-panic items; verify the scheduled fuzzing workflow actually runs with valid CLI
  syntax. No local build was attempted after the operator instruction not to run heavy work.

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
- **State:** `BLOCKED` — metrics pipeline `DONE`, crash telemetry and opt-in reporting absent.
- **Evidence (`DONE`):** `OBSERVABILITY_EVIDENCE.md` records a clean three-platform report from
  run `45371749676` / commit `ceade17166` (all three C API jobs `success`, report job
  `138376090700`, artifact `14910991391`): Linux startup 6 ms / load 52 ms / JS 16 ms /
  cdylib 1,210,689 KiB; Windows 16 / 78 / 15 ms / 223,526 KiB; macOS 16 / 257 / 116 ms /
  365,110 KiB. Metric-collection code is unchanged on current `main`. Deltas >25% are flagged
  informationally; the job is a reporter, not a gate, and the numbers are single hosted-runner
  samples, not a budget.
- **Blocker:** only a coarse "C API job result" crash proxy is recorded. No minidumps,
  symbolication, crash counter/trend, or opt-in crash-reporting pipeline exists
  (`FUZZING_AND_CRASH_TELEMETRY.md` §3 ends at embedder hooks; `PARITY_MATRIX.md` marks native
  telemetry `In Progress`; `FULL_BROWSER_ROADMAP.md` §3.5 is unmet).
- **Next action:** implement opt-in crash capture/telemetry with symbols and documented
  retention; decide whether dedicated-runner measurements can support a real gate; re-run the
  three-platform report on the release candidate and attach the run id.

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
| 2026-11-24 | REL-7 evidence published in `OBSERVABILITY_EVIDENCE.md`: clean three-platform metrics report from run `45371749676` (`ceade17166`, artifact `14910991391`); no crash telemetry or opt-in pipeline. REL-7 → `BLOCKED`. | Run `45371749676`; job `138376090700` |
| 2026-11-25 | REL-3 step 2 executed `PASSED`: fixed five-site smoke run against the installed browser from successful run `45428660357` / artifact `14943064230` (binary reports `Servo 0.7.0-fd4655eb3`). All five sites `PASS` with five 1280×740 screenshots and harness exit `0`. Four harness/provenance defects found en route (`SMOKE-001`–`004`) were fixed and recorded; the invalid all-`TIMEOUT` installer run was discarded. REL-3 stays `IN_PROGRESS` pending the strict WPT delta. | `SITE_SMOKETEST_REPORT.md`, `site_smoketest.json`, `site_smoke_shots/`, `WPT_TRIAGE.md` |
| 2026-11-26 | Release-candidate capture: committed all tracked + untracked work (`cd73716ed87`), merged `origin/private-search-isolation` by merge commit (`5eb4b20f987`, no rebase/force), and reconciled C API version/preview caveats (`45c72d20628`). No public-release sign-off. | Local `git` history |
| 2026-11-26 | Gate attempts on the release candidate: tidy defects repaired (CRLF, empty placeholders, scratch tools, license/EOF issues); `cargo deny check` `FAILED` on duplicate `mach2`; FFI panic-boundary check `FAILED` on 16 unwrapped entry points; local C API test `BLOCKED` (no `cargo-c`), CI C API `PASSED` at `bff51f6786c` / run `45648587705`. REL-4 set to `FAILED`. | See §6 |
| 2026-11-26 | Current-branch CI evidence reviewed: Main `45648587994` (`bff51f6786c`) `FAILED` only at artifact merge (`No artifacts found matching pattern 'cargo-timings-*'`); Main `45638582953` (`dd200ffad2c`) `FAILED` at tidy before repair. Post-repair CI is pending push and is **not** claimed green. | Runs `45648587994`, `45638582953`; job `139341442557` |
| 2026-11-26 | Tidy repair verified at file level: 139 changed files scanned with 0 file-level errors after CRLF/license/EOF/scratch repairs. `uv run --frozen ruff check` `PASSED` (`[]`), WPT `flake8` `PASSED`, co-author check `PASSED`. Full `mach test-tidy` stayed hung in post-scan tools and was stopped; `pyrefly` still reports 5 platform-specific `resource` errors on Windows. No local formatting subchecks were reached. | Local tidy file-level runner; `ruff`, `flake8`, co-author and `pyrefly` outputs; full tidy shell `sh_215b6a54f001eTI4THIiTYXd5z` |

---

## 6. Release-gate attempt matrix — 2026-11-26 release candidate

This section records **attempts**, not wishes. The candidate is the local branch
`private-search-isolation` after commits `cd73716ed87`, `5eb4b20f987` and `45c72d20628`,
plus the tidy-repair commit made after these checks. Heavy local builds/tests were
explicitly forbidden by the operator, so build-dependent gates were attempted only where a
CI artifact or an already-generated report existed.

| Gate / check | State | Evidence | Honest conclusion |
|---|---|---|---|
| REL-0 strict CI/WPT baseline | `PASSED` (historical) / `NOT_RUN` for this exact RC | `fd4655eb3b6` / run `45428660357`; latest branch runs `45648587994` and `45638582953` failed before this RC | The old baseline remains valid for its commit. The post-repair RC has not been verified in CI; do not carry the old green run forward as if it were this commit. |
| REL-1 runtime / GPU hardware | `BLOCKED` | No human/GPU checklist artifact | Requires real hardware and a signed manual runbook; cannot be faked by CI or a local lint. |
| REL-2 C ABI / lifecycle | `NOT_CONFIRMED` | CI run `45648587705` at `bff51f6786c`: `C API (Linux)`, `C API (Windows)`, `C API (macOS)` all `success`; local `cargo test -p servo-capi-tests --locked` `BLOCKED` | Three-platform compile/test success is real, but the reverted lifecycle tests were not re-landed. Local test needs `cargo-c`; the pinned install timed out after 3600 s. No heavy retry was made after the operator instruction. |
| REL-2 local C API tooling | `FAILED` → `BLOCKED` | `cargo test -p servo-capi-tests --locked` first failed: `no such command: cinstall`; `tests/capi/build.rs:111` panicked. `cargo install cargo-c --version 0.10.23 --locked` timed out; no `cinstall` command installed. | Tooling failure, not a code pass. `cargo install cargo-cinstall` was a wrong crate name and is not usable evidence. |
| REL-3 real-world / WPT deltas | `IN_PROGRESS` | `docs/embedding/wpt_strict_baseline.json` from `45396797175` / revision `5461673d1e9`: 3,313/3,749 assertions (88.37%), 224/407 harness `OK` (55.04%), 109 harness-`OK` files with failing assertions; five-site smoke `PASSED` in `SITE_SMOKETEST_REPORT.md` | Pass-rate calculator was already executed against the strict artifact and is not re-run locally. Strict WPT delta on this RC is `NOT_RUN`; conformance remains partial. |
| REL-4 FFI panic boundary | `FAILED` | `python etc/ci/check_ffi_panic_boundary.py` exit `1`; 16/257 exported `extern "C"` functions unwrapped | Genuine finding. No panic may cross the C boundary; no suppression applied. |
| REL-4 dependency policy | `FAILED` | `cargo-deny 0.22.1`; `cargo deny check` exit `1`: `advisories ok, bans FAILED, licenses ok, sources ok`; `cargo-deny --format=json --all-features check` exit `2` with `code: duplicate` for `mach2 0.4.3` + `0.7` | Genuine finding introduced by the local crash-handler dependency stack; no new ignore/skip added. |
| REL-4 tidy / hygiene | `FAILED` locally, `BLOCKED` for full gate | `.\mach test-tidy` needed `PYTHONIOENCODING=utf-8`; first real run exposed CRLF, empty placeholders, scratch TOML, license and EOF defects. After repair, the file-level scanner checked 139 changed files with 0 errors; `ruff check` and WPT `flake8` exited `0`; co-author check exited `0`. `pyrefly check` still reports 5 `missing-attribute` errors in `python/wpt/run.py` because Windows has no `resource` module. Full `mach test-tidy` reached 100% file scan, then hung in post-scan tools for ~48 min and was stopped before formatting checks. | Real defects were fixed; the remaining local failure is platform-specific type checking plus a hung post-scan tool. Tidy is not `PASSED`; CI on Linux remains the authority for this gate. |
| REL-5 performance | `BLOCKED` | `PERFORMANCE_BASELINE.md`; bencher `Send results` failed on missing `--token` under `continue-on-error: true`; no Windows or leak evidence | No threshold comparison occurred. A green job with a masked submission error is not a pass. |
| REL-6 signed distribution | `NOT_CONFIRMED` | No signed/notarized artifact or clean-VM install record for this RC | Distribution cannot be claimed from a developer-preview branch. |
| REL-7 observability / telemetry | `BLOCKED` | Three-platform metrics from run `45371749676` / artifact `14910991391`; no opt-in crash pipeline; local crash-handler work is incomplete and breaks dependency policy | Size/startup reporting exists; crash telemetry does not. |
| REL-8 public sign-off | `BLOCKED` | REL-1 … REL-7 not all `PASSED`; `PUBLIC_RELEASE_READINESS = NOT_CONFIRMED` | This branch is developer preview / dogfood only. Do not tag, announce, or describe it as a public release. |

**Overall for this RC:** `PUBLIC_RELEASE_READINESS = NOT_CONFIRMED`; multiple subchecks are
`FAILED`. Pushing the branch publishes the honest preview state; it does not constitute a
release approval.
