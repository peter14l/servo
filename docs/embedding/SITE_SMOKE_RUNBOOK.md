# Real-site smoke runbook (REL-3, step 2)

Status: **step 2 executed `PASSED`** for the fixed five-site set; REL-3 itself remains
`IN_PROGRESS` pending step 3 (engine fixes + strict WPT delta). `PLANNED`/`NOT_RUN` rows
are never results. See [`SITE_SMOKETEST_REPORT.md`](SITE_SMOKETEST_REPORT.md) for the
executed run and [`WPT_TRIAGE.md`](WPT_TRIAGE.md) for harness defects found on the way.

This runbook fixes the target set and the evidence required before any REL-3 compatibility
claim can be made. It complements the strictly executed WPT baseline in
[`WPT_STRICT_BASELINE.md`](WPT_STRICT_BASELINE.md), which measures a subset of WPT, not real
sites.

## Fixed target set

The list is fixed for a run. Do not add, remove, or swap targets mid-run; if the list changes,
restart and record a new list version.

| # | Site | Category | Expected features | Failure hints |
|---|---|---|---|---|
| 1 | `https://example.com` | Basic HTML5 / static document | HTML parser, basic CSS, HTTP/1.1 or HTTP/2 | Network/TLS or parser regression |
| 2 | `https://news.ycombinator.com` | Table layout, lightweight dynamic | Cookie storage, table formatting, inline CSS | Table layout or cookie handling |
| 3 | `https://en.wikipedia.org/wiki/Main_Page` | Complex content, media queries, SVG | Responsive layout, Unicode fonts, SVG rendering | CSS/layout, font fallback, SVG |
| 4 | `https://lite.duckduckgo.com` | Forms, query parameters | Form submission, GET/POST, URL encoding | Forms or load navigation |
| 5 | `https://httpbin.org/get` | Network, headers, HTTPS | TLS handshake, `Accept` headers, JSON display | TLS, headers, response handling |

The list is deliberately small and stable: it is a smoke set, not a claim about the top N
sites on the web.

## Preconditions

1. Obtain `servoshell` **only** from a successful GitHub Actions artifact on the commit being
   evaluated. Record the run id, artifact id, artifact sha256, and extraction path.
   Never use output from a failed or cancelled run, and never build locally for this check.
   On Windows, `checked-release-binary-windows` is a WiX installer: launch it and use the
   installed `servoshell.exe`, then confirm its `--version` string contains the source
   commit. Do **not** pass the installer itself as `--servo-binary` (`SMOKE-001`).
2. Record the OS build, network conditions, date/time, and the exact command line.
3. Confirm DNS and TLS access from the runner before interpreting `ERROR` results.
4. Use the same binary for every site in the run.

## Execution

```powershell
python etc/ci/site_smoketest.py `
  --servo-binary <installed-servoshell-from-successful-artifact> `
  --timeout 30 `
  --screenshot-dir docs/embedding/site_smoke_shots `
  --output-markdown docs/embedding/SITE_SMOKETEST_REPORT.md `
  --output-json docs/embedding/site_smoketest.json
```

The harness appends `-f -x --headless -o <screenshot.png> <url>`. `-x` is required: without
it the browser stays open after load and every site looks like `TIMEOUT` (`SMOKE-002`).
`--screenshot-dir` supplies the evidence required for visual targets #2–#5; missing files
downgrade those rows to `NOT_CONFIRMED` (`SMOKE-004`).

`--dry-run` writes an explicit `NOT_RUN` plan for the fixed target set. It is a planning aid,
**never** evidence and never a pass. Without `--servo-binary` (and without `--dry-run`) the
harness exits non-zero and writes no report. A real run also exits non-zero unless **every**
row is `PASS` (`SMOKE-003`); harness exit `0` is necessary but not sufficient for REL-3.

## Interpretation

| Status | Meaning | Counts as pass? |
|---|---|---|
| `PASS` | Clean process exit within timeout, plus a screenshot for visual targets #2–#5 | Yes, for that site only |
| `NOT_CONFIRMED` | Clean exit but required screenshot evidence is missing | No |
| `TIMEOUT` | No completion within the timeout | No |
| `CRASH` | Rust panic or harness exception | No |
| `ERROR` | Non-zero exit without a detected panic | No |
| `NOT_RUN` / `PLANNED` | No binary executed | No |

A clean exit alone does not prove correct rendering. Record screenshots for visual targets
(#2–#5) alongside the status; absence of a screenshot makes the row `NOT_CONFIRMED`, not
`PASS`.

## Triage

Every non-`PASS` row becomes a ticket in [`WPT_TRIAGE.md`](WPT_TRIAGE.md) with:

- site, URL, category, status, timestamp, source run id and artifact sha256;
- root cause category: DNS/TLS, DOM/JS, CSS/layout, WebRender, timeout, or harness;
- the smallest reproducible case and the owning engine component;
- an explicit `BLOCKED` note if the cause could not be isolated.

Fix engine behaviour. Never add a site-specific hack or a special case to make a target pass.

## Current state

Step 2 has been executed against the installed browser from successful CI run
`45428660357` (artifact `14943064230`): all five targets are `PASS` with screenshots,
harness exit `0`. See [`SITE_SMOKETEST_REPORT.md`](SITE_SMOKETEST_REPORT.md) for
provenance, results and screenshots, and [`WPT_TRIAGE.md`](WPT_TRIAGE.md) for the four
resolved harness tickets. This is a smoke result, not a conformance pass; REL-3 step 3
(engine fixes and a strict WPT delta) is still pending.

## Extended compatibility set (`compat-v1`)

`compat-v1` is a **separate, versioned** target list for the bounded compatibility sprint.
It is not a replacement for the fixed five-site smoke set and must not be used to rewrite
that baseline. Every target is visual, so a row without screenshot evidence is
`NOT_CONFIRMED` even if the process exits cleanly.

| # | Site | Category | Expected features |
|---|---|---|---|
| 1 | `https://github.com/servo/servo` | Large dynamic application / SVG icons / code view | SPA navigation, SVG icons, syntax-highlighted code, dynamic panels |
| 2 | `https://www.youtube.com/watch?v=dQw4w9WgXcQ` | Heavy media / canvas / continuous animation | Video metadata, canvas/SVG controls, dynamic thumbnails |
| 3 | `https://threejs.org/examples/webgl_animation_skinning_blending.html` | WebGL animation | WebGL context, canvas animation, continuous rendering |
| 4 | `https://animejs.com/` | JS/CSS/SVG animation showcase | `requestAnimationFrame`, CSS transforms, SVG animation |
| 5 | `https://www.google.com/maps/@37.7749,-122.4194,12z` | WebGL tiles / continuous pan and zoom | Canvas tiles, WebGL, pointer input, dynamic overlays |

Execute from the repository root with the same provenance rules as the fixed smoke set:

```powershell
python etc/ci/site_smoketest.py `
  --servo-binary <installed-servoshell-from-successful-artifact> `
  --site-set compat-v1 `
  --timeout 30 `
  --screenshot-dir docs/embedding/compat_smoke_shots `
  --output-markdown docs/embedding/COMPAT_SMOKE_REPORT.md `
  --output-json docs/embedding/compat_smoke.json
```

Any non-`PASS` row becomes a triage ticket using the same root-cause rules and must be
fixed in engine code, never by a site-specific exception. The result is a compatibility
finding set, not a conformance pass and not a release gate by itself.

For DOM/RAF/screenshot evidence, run the deeper probe:

```powershell
python etc/ci/compat_probe.py `
  --servo-binary <installed-servoshell-from-successful-artifact> `
  --port 7011 `
  --timeout 25 `
  --screenshot-dir docs/embedding/compat_probe_shots `
  --output-json docs/embedding/compat_probe.json `
  --output-markdown docs/embedding/COMPAT_PROBE_REPORT.md `
  --source-run-id <run-id> `
  --artifact-id <artifact-id>
```

`--extra-servo-arg="--prefs-file=<path>"` is available for **diagnostic isolation only**.
It must be recorded in the report provenance and must never be used to make a target pass
by changing shipped defaults or adding a site-specific exception. The executed findings
are in [`COMPAT_TRIAGE_REPORT.md`](COMPAT_TRIAGE_REPORT.md).
