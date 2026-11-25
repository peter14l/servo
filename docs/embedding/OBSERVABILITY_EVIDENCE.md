# Observability evidence baseline (REL-7)

Status: **`BLOCKED`** — the metrics pipeline works, but crash telemetry and opt-in crash
reporting do not exist. This document records evidence, not a gate pass.

## What is emitted and retained (`DONE`)

The `Embedding API (C API)` workflow writes per-platform metric files from the C integration
tests, then aggregates and diffs them against `observability_baseline.json` in an
`Observability report` job. The contract and its limitations are in
[`OBSERVABILITY.md`](OBSERVABILITY.md).

Clean three-platform evidence comes from:

| Item | Value |
|---|---|
| Commit | `ceade171666` (earlier CI-fix commit; metric-collection code is unchanged on current `main`) |
| Workflow run | `45371749676` — `Embedding API (C API)`, `success` |
| C API jobs | Linux `138367467296`, Windows `138367467458`, macOS `138367466666` — all `success` |
| Report job | `138376090700` — `Observability report`, `success` |
| Report artifact | `observability-summary`, id `14910991391` |
| Per-platform artifacts | `observability-metrics-Linux` `14909467977`, `-Windows` `14909383027`, `-macOS` `14910756156` |
| Baseline | `observability_baseline.json`, originally recorded at run `39307687792` |

Report values (single samples on shared GitHub-hosted runners):

| Platform | Engine startup | Load complete | Paint + screenshot | Fixed JS | cdylib size |
|---|---:|---:|---:|---:|---:|
| Linux | 6 ms | 52 ms | 54 ms | 16 ms | 1,210,689 KiB |
| Windows | 16 ms | 78 ms | 31 ms | 15 ms | 223,526 KiB |
| macOS | 16 ms | 257 ms | 23 ms | 116 ms | 365,110 KiB |

The report marks several deltas with `(!)` (>25% from the old baseline: macOS startup
`+128.6%`, macOS load `+35.3%`, macOS JS `+61.1%`, Windows JS `-51.6%`). Per the documented
design, `(!)` is informational and the job stays green: hosted-runner timings are too noisy
for a hard threshold. These numbers are a trend line, **not** a performance budget, and are
not evidence of a size/startup regression pass. `screenshot_ms` has no baseline entry (the
old baseline measured it with a broken timer), so its delta is `-`.

A later run (`45428660374`, commit `fd4655eb3b6`) collected Linux and Windows metrics but its
macOS C API job was cancelled by infrastructure, so its report records the crash proxy as
`abandoned`. It is **not** a clean three-platform observation; it is kept here to explain why
that run cannot sign off this gate.

## What is missing

- **Crash telemetry:** the only recorded crash signal is the coarse `C API job result`
  ("crash proxy"). There is no minidump upload, symbolication, crash counter, or retained
  crash trend. [`FUZZING_AND_CRASH_TELEMETRY.md`](FUZZING_AND_CRASH_TELEMETRY.md) §3 documents
  POSIX backtraces and a Windows exception filter, but ends at embedder hooks — there is no
  telemetry pipeline. [`PARITY_MATRIX.md`](PARITY_MATRIX.md) lists native minidumps + crash
  telemetry as `In Progress`.
- **Opt-in reporting:** no opt-in crash-reporting setting or consent flow exists in the
  committed code; nothing is enabled or uploaded by default, but there is also no pipeline a
  user could opt into. `FULL_BROWSER_ROADMAP.md` §3.5 remains an unmet requirement.
- **Enforced gate:** the observability job reports deltas, exits 0 on measured regressions,
  and is explicitly not a gate. Artifact retention is ordinary CI artifact retention; there
  is no long-lived dashboard or cross-run alerting.

## Next action

1. Implement minidump/backtrace capture with symbols and an explicitly opt-in telemetry
   pipeline (no default upload), with retention/consent documented.
2. Decide whether a quiet, dedicated-runner measurement can support a real size/startup gate;
   do not turn noisy hosted-runner deltas into a failing check.
3. Re-run the three-platform report on the candidate release commit and attach its run id to
   the go/no-go record. Until then REL-7 is `BLOCKED`, and browserd remains beta / dogfood.
