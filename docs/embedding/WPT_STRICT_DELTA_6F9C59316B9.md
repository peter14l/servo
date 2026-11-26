# Strict WPT delta — `6f9c59316b9` (run `45819179660`)

> **Evidence provenance.** This run publishes `wpt-full-logs-linux`, not per-shard
> `wptreport` JSON. The 20 raw shard logs were converted to wptreport-shaped JSON
> from their actual `suite_start` / `test_end` / `test_status` records, then fed to
> `etc/ci/wpt_pass_rate.py` with `--expected-revision 6f9c59316b9fbcc387de1b47a0c8a583924da480`.
> All 20 reports carried that revision; nothing was inferred or invented. The
> generated JSON is not committed; the command and output are recorded here.

## Comparison with historical baseline `5461673d1e9` / run `45396797175`

| Metric | Baseline | This run (`6f9c59316b9`) | Delta |
|---|---:|---:|---:|
| Assertion pass | 3,313 / 3,749 (88.37%) | 3,313 / 3,749 (88.37%) | **0 assertions, 0.00 pp** |
| Assertion `FAIL` | 351 | 351 | 0 |
| Assertion `ERROR` | 85 | 85 | 0 |
| Harness `OK` | 224 / 407 (55.04%) | 224 / 2,079 (10.77%) | **0 OK files; denominator differs** |
| Harness `OK` with failing assertions | 109 | 109 | 0 |

The harness denominator differs because the historical artifact contained
wptreport JSON with 407 harness results, while this run's raw logs contain 2,079
`test_end` records (mostly `SKIP`, including 1,672 `_webgl` files). The
assertion-level counts are directly comparable and unchanged. `SKIP`/`NOTRUN`
are never passes. No expectations were modified and no always-succeed flag was used.

## Generated report


**Generated:** 2026-11-26T13:33:50.510192+00:00 | **Source:** `wpt_reports`

## Strict CI run baseline (executed tests only)

- **Revision:** `6f9c59316b9fbcc387de1b47a0c8a583924da480`
- **Source run:** `45819179660` (artifact `wpt-full-logs-linux`)
- **Shards reported:** 20
- **Assertion pass rate (headline):** **88.37%** (3,313/3,749)
- **Harness `OK` results:** 224 of 2,079 executed test files (10.77%)
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
| `SKIP` | 1,854 | 89.18% |
| `OK` | 224 | 10.77% |
| `TIMEOUT` | 1 | 0.05% |

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
| `_webgl` | 0 | 1,672 | 0 | 0 | n/a (all skipped) |
| `html` | 0 | 91 | 0 | 0 | n/a (all skipped) |
| `wasm` | 0 | 78 | 0 | 0 | n/a (all skipped) |
| `css` | 0 | 8 | 0 | 0 | n/a (all skipped) |
| `fetch` | 0 | 4 | 0 | 0 | n/a (all skipped) |
| `infrastructure` | 1 | 1 | 1 | 1 | 100.0% |
| `encoding` | 0 | 1 | 0 | 0 | n/a (all skipped) |
