# WPT Conformance Pass-Rate Baseline Report

**Generated:** 2026-11-24T16:59:18.202511+00:00 | **Source:** `wpt_reports`

## Strict CI run baseline (executed tests only)

- **Revision:** `5461673d1e96d5db8ab22020f96112b51a0ae42c`
- **Source run:** `45396797175` (artifact `wpt-full-logs-linux`)
- **Shards reported:** 20
- **Assertion pass rate (headline):** **88.37%** (3,313/3,749)
- **Harness `OK` results:** 224 of 407 executed test files (55.04%)
- **Harness `OK` results containing failing assertions:** 109

The two figures measure different things and must not be conflated.
The WPT webdriver harness can mark a test file `OK` even when some of
its assertions `FAIL` or `ERROR`, so harness `OK` is **not** a conformance
pass. The assertion-level rate is the conformance signal; the harness
rate is what strict CI gates on after expectation metadata is applied.

`SKIP`/`NOTRUN` are not passes in either dimension. This baseline covers
only the tests the strict CI run actually executed, not the full WPT tree;
`wpt_baseline.json` is the static full-tree expectations baseline and the
two must not be conflated either.

### Harness outcomes (per test file — CI gate view)

| Status | Count | Percentage |
|---|---:|---:|
| `OK` | 224 | 55.04% |
| `SKIP` | 182 | 44.72% |
| `TIMEOUT` | 1 | 0.25% |

### Assertion outcomes (per subtest — conformance view)

| Status | Count | Percentage |
|---|---:|---:|
| `PASS` | 3,313 | 88.37% |
| `FAIL` | 351 | 9.36% |
| `ERROR` | 85 | 2.27% |

### Per-suite pass rate

| Suite | Harness `OK` | Files | Assertion pass | Assertions | Rate |
|---|---:|---:|---:|---:|---:|
| `webdriver` | 223 | 224 | 3,312 | 3,748 | 88.37% |
| `html` | 0 | 91 | 0 | 0 | n/a (all skipped) |
| `wasm` | 0 | 78 | 0 | 0 | n/a (all skipped) |
| `css` | 0 | 8 | 0 | 0 | n/a (all skipped) |
| `fetch` | 0 | 4 | 0 | 0 | n/a (all skipped) |
| `infrastructure` | 1 | 1 | 1 | 1 | 100.0% |
| `encoding` | 0 | 1 | 0 | 0 | n/a (all skipped) |
