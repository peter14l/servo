# Real-World Website Conformance Smoke Report

**Result:** the fixed five-site smoke set was **executed** and all five rows are `PASS`
under the harness definition (clean process exit within 30 s **and** a screenshot for
visual targets). This is a smoke check, **not** full WPT conformance and **not** a REL-3
sign-off; step 3 (engine fixes and a strict WPT delta) remains pending.

## Provenance

| Item | Value |
|---|---|
| Executed at (UTC) | `2026-11-24 18:41:32Z` |
| Source CI run | `45428660357` (`Main`, **completed / success**) |
| Source commit | `fd4655eb3b6b39ad1698aa5571bba45869aa4283` (the strict green baseline) |
| CI artifact | `14943064230` — `checked-release-binary-windows`, not expired |
| Artifact zip sha256 | `AC621331B0AA7CDDB25F8E752ECA5CBAF1A773F9FABC0DA97EC8269E58028E17` |
| WiX bootstrapper sha256 | `626B6C77C942BBB5F29013748162F415A96815362A837CF2E717F2ECAC188F9B` |
| Installed binary | `C:\Program Files\Servo\Servo Tech Demo\servoshell.exe` |
| Installed binary sha256 | `E0E28202B914ECDC0EC1E352D69FDEE76AE25A307C59B2551316A512415D7314` |
| Binary self-reported version | `Servo 0.7.0-fd4655eb3` (matches the source commit) |
| Install metadata | `Servo Tech Demo` `0.7.0`, install date `20261125`, from the WiX package cache |
| Host OS | Windows 11 Home Insider Preview, build `26320.0` |
| Harness exit code | `0` (required condition: every row `PASS`) |

The artifact named `checked-release-binary-windows` is a **WiX Burn installer**, not a
bare browser executable. The installer was launched and completed locally on the user's
machine; the resulting `servoshell.exe` reports the exact green commit in its version
string, which is the provenance link used for this run. No locally built binary was used.

## Exact command

```powershell
python etc/ci/site_smoketest.py `
  --servo-binary "C:\Program Files\Servo\Servo Tech Demo\servoshell.exe" `
  --timeout 30 `
  --screenshot-dir docs/embedding/site_smoke_shots `
  --output-markdown docs/embedding/SITE_SMOKETEST_REPORT.md `
  --output-json docs/embedding/site_smoketest.json
```

The harness invokes each target as
`servoshell.exe -f -x --headless -o <screenshot.png> <url>`:
`-f` fails hard on thread failure, `-x` exits after load, and `-o` writes the screenshot.
Without `-x`, the browser stays open after load and every site falsely appears as
`TIMEOUT`. No timeout was raised to force a pass.

## Results

| Site | Category | Status | Time | Screenshot | Root Cause |
|---|---|---:|---|---|---|
| [Example Domain](https://example.com) | Basic HTML5 / Static Document | **PASS** | 2.88s | [01-example-domain.png](site_smoke_shots/01-example-domain.png) | Clean exit |
| [Hacker News](https://news.ycombinator.com) | Table Layout / Lightweight Dynamic | **PASS** | 4.92s | [02-hacker-news.png](site_smoke_shots/02-hacker-news.png) | Clean exit |
| [Wikipedia Main Page](https://en.wikipedia.org/wiki/Main_Page) | Complex Content / Media Queries / SVG | **PASS** | 10.12s | [03-wikipedia-main-page.png](site_smoke_shots/03-wikipedia-main-page.png) | Clean exit |
| [DuckDuckGo Lite](https://lite.duckduckgo.com) | Forms / Query Parameters | **PASS** | 2.94s | [04-duckduckgo-lite.png](site_smoke_shots/04-duckduckgo-lite.png) | Clean exit |
| [Httpbin JSON](https://httpbin.org/get) | Network / Headers / HTTPS | **PASS** | 3.94s | [05-httpbin-json.png](site_smoke_shots/05-httpbin-json.png) | Clean exit |

Screenshot files are 1280×740 PNGs; sizes: 112,689 / 227,169 / 325,730 / 26,420 /
69,128 bytes respectively. Machine-readable results are in
[`site_smoketest.json`](site_smoketest.json).

## Interpretation and caveats

- `PASS` here means **clean exit plus screenshot evidence** for the visual targets. It
  does not prove pixel-perfect rendering, feature parity, or a WPT conformance delta.
- The five targets are the fixed set from [`SITE_SMOKE_RUNBOOK.md`](SITE_SMOKE_RUNBOOK.md);
  they are a smoke set, not a "top N sites" claim.
- An earlier attempt produced five `TIMEOUT` rows because the harness invoked the WiX
  installer instead of the browser and omitted `-x`. Those rows are **invalid
  provenance**, were discarded, and are recorded as resolved harness tickets in
  [`WPT_TRIAGE.md`](WPT_TRIAGE.md). They are not engine failures and not site passes.
- No site-specific hack, expectation edit, weakened check, or forced pass was used.
- REL-3 remains `IN_PROGRESS`: step 2 is now `DONE`, while step 3 (fix engine behaviour,
  re-run strict WPT, measure the delta) is still pending.

## Triage

There are no non-`PASS` rows in this executed run, so no new site tickets were opened.
Harness defects found before this run are tracked in [`WPT_TRIAGE.md`](WPT_TRIAGE.md).
