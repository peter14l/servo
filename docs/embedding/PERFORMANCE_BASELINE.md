# Performance evidence baseline (REL-5)

Status: **`NOT_CONFIRMED`** — measurements exist, but the threshold gate did not run.
This document records evidence only. It does not claim a performance pass.

## Source

| Item | Value |
|---|---|
| Commit | `fd4655eb3b6b39ad1698aa5571bba45869aa4283` |
| Main run | `45428660357` (`success`, 0 failures/cancellations) |
| Speedometer job | `138599279029` — `Bencher (linux)`, `success` |
| Perf-analysis job | `138599178752` — `Perf analysis`, `success` |
| Perf-analysis artifact | `perf-analysis`, id `14941829764` (40,496 bytes) |
| Profile | `checked-release` on Linux x86_64, GitHub-hosted runner |

## Speedometer (real benchmark, no retries used)

`./mach test-speedometer --bin servo/servoshell --profile checked-release --bmf-output speedometer.json`
completed with no `timed out; retrying` lines in the job log.

| Metric | Value |
|---|---:|
| Score (mean) | **5.5637** |
| Score (delta) | ± 0.1620 |

The retry wrapper in `python/servo/testing_commands.py` retries **only**
`subprocess.TimeoutExpired` (bounded at 2 retries); a malformed result still fails, and
exhausting retries fails the command. No threshold was lowered.

## Perf analysis (trace study, 5 samples of `https://servo.org/`)

From `default/servo.org/servo/summaries.txt` in artifact `14941829764`. Values are wall-clock
durations on a shared CI runner — a trend line, **not** a budget. `0.000ns` means the event was
not observed, not that it is free.

| Event | Min | Mean | Max |
|---|---:|---:|---:|
| Layout (real) | 100.2 ms | 106.8 ms | 118.9 ms |
| ScriptEvaluate (real) | 18.67 ms | 23.85 ms | 30.21 ms |
| ScriptParseHTML (real) | 5.348 ms | 6.086 ms | 6.544 ms |
| Renderer (synthetic) | 123.7 ms | 129.6 ms | 138.6 ms |
| FP / FCP (synthetic) | 302.9 ms | 1.060 s | 3.758 s |

## Why this is not a `PASSED` gate

The `Send results` step in `.github/workflows/bencher.yml` runs:

```text
bencher run --adapter json --file b.json --project ... --token <BENCHER_API_TOKEN> ...
```

In run `45428660357` that command failed:

```text
error: a value is required for '--token <TOKEN>' but none was supplied
```

The step is marked `continue-on-error: true`, so the job stayed green while **no baseline
comparison or regression detection actually ran**. This is a missing secret/configuration on
this fork, not a benchmark result. It must not be described as a green threshold gate.

Additional gaps:

- No long-uptime or repeated load/unload leak results are recorded for this commit.
- No per-platform performance evidence exists: Speedometer runs only for `checked-release`
  on Linux (`linux.yml` sets `speedometer: ${{ inputs.profile == 'checked-release' }}`;
  Windows/macOS/Android/OpenHarmony set it to `false`). This is insufficient for the
  flagship Windows target, so the flagship rule is not met.
- The `observability_baseline.json` numbers are a separate C API trend baseline, not a
  performance budget, and are from an older run (`39307687792`).

## Next action

1. Configure the bencher token for the repository, or choose an explicitly reviewed gate
   mechanism that fails visibly when results cannot be submitted. Do **not** simply delete
   `continue-on-error` without confirming the token, or CI will fail for configuration reasons.
2. Re-run and record a real baseline comparison with the same threshold.
3. Add long-uptime/leak evidence and a flagship Windows measurement, then revisit REL-5.
