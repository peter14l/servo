# Windows Browser Improvement — Handoff Document

Last updated: 2026-11-20 (session handoff). Keep this document current as work lands.

## 1. Objective and constraints

Improve the native Servo Windows C/C++ browser (`ports/windows`) so it is stable, feature-complete and fast enough for real use, starting with the SXUK student-portal workflow that exposed the original defects.

**User-requested execution order (do not reorder):**

1. **Stability** — crashes/hangs, TLS gateway, IPC, message pump/event-loop re-entry, error handling.
2. **Feature completeness** — missing browser features.
3. **Performance** — startup, page load, scrolling, memory.
4. **Gap audit** — final checklist against a real browser.

**Constraints / decisions already made:**

- Fix problems in the **browser**, not in the SXUK website.
- Builds go through GitHub Actions; download the artifact and install locally.
- **Never log plaintext passwords.** `__servo_ipc__` payloads must never reach `LogDebug`.
- Rust engine / C API changes **are allowed**; new dependencies **are allowed** if justified.
- Keep the user's unrelated working-tree changes untouched (see §7).
- Do not commit `repro.html` (untracked diagnostic file).
- Do not force-push or deploy anywhere except the local install.

## 2. Repository and environment state

| Item | Value |
|---|---|
| Branch | `fix/xpath-crash-and-internal-schemes` |
| Remote | `https://github.com/peter14l/servo.git` (`origin`) |
| Local HEAD at handoff | **not yet pushed** — `b6fea8e7c37` (or later; confirm with `git log -1`); remote branch head is `b21390fda22` |
| Local-only commits | `6003bdcd51d` — handoff doc, select/host hardening, exception-safe event-loop guard; `b6fea8e7c37` — stability audit fixes (popup lifecycle, TLS scoping, request ownership, UTF-8 checks, permissions, ABI, logging) |
| CI run in flight | `44776900341` — *Native Windows Browser Build*, head `b21390fda22` |
| Installed browser | `%LOCALAPPDATA%\Programs\Servo Browser\servo_browser.exe` dated **20 Nov 2026 17:41** — built from head `ffad76cb3de` (all dropdown, IPC, TLS, stability and feature fixes). exe SHA256 `1A1595D5…29DB4` |
| CI runs | `44776900341` (`b21390fda22`) and `44782040555` (`ffad76cb3de`) — both PASSED; both artifacts downloaded and installed in turn |
| CI artifacts | `C:\Users\LOQ\AppData\Local\Temp\opencode\setup\…` (first build) and `…\setup2\…` (latest build), each containing `servo-windows-browser-setup.zip` |
| Log file | `%LOCALAPPDATA%\Programs\Servo Browser\servo_browser.log` |
| Disk | `E:` ~35 MB free; `C:` ~101 GB free — stage artifacts on `C:` |
| Toolchain | Rust 1.97.1, `cbindgen`, MinGW `g++`, `gh`, Python 3.12, `node`, `graphify` |

## 3. What has been completed and verified

| Commit | Change | Evidence |
|---|---|---|
| `5bf4c33c8de` | Schannel TLS gateway: complete responses, `send()` loops, header reads, 502 on failures, timeouts, static cache, graceful close | Pushed; code review |
| `750cbbd75e5` | Native `<select>` menu: resolve host from webview, foreground handling, re-entry guard, cancel keeps selection, context-menu dismissal, compat-script dedupe/AJAX self-heal, IPC payload detected anywhere | Pushed; local `g++ -fsyntax-only` clean, `node --check` clean |
| `2f0ad9af835` | `HandleWebIpcMessage` logs only the command token, never the payload | Pushed; local syntax check clean |
| `b21390fda22` | Split the 16,393-char compat script into two adjacent raw literals (MSVC `C2026`), `FromWebView` falls back to `GetActiveHost()` | Pushed; local syntax check clean, JS byte-identical, `node --check` clean |

**Root cause of the dropdown bug:** embedder-control callbacks receive Rust's responder handle as `user_data`, not the delegate's pointer (`docs/embedding/C_API_GUIDE.md`, "Embedder controls"). The old handler cast it to `ServoViewHost*`, so `m_hwnd` was garbage and `TrackPopupMenuEx` silently failed. Secondary issue: the C API passes `&mut webview` from a **stack-local `WebView` clone** (`ffi/capi/webview_delegate.rs`), so pointer identity against `m_webview` never matches — hence the `GetActiveHost()` fallback.

**Log findings from the user's last run (19:47 / 20:04, 13 Nov):** page reached `attendance-view.aspx`; no `[SELECT]` line (pre-fix binary, no instrumentation); 8 duplicate `prompt_save_password` IPC messages; plaintext password present. The 8 password values were redacted in the installed log by `C:\Users\LOQ\AppData\Local\Temp\opencode\redact-log.ps1`. No crash/hang lines.

**CI blockers already fixed:** runs `43444222058`, `43748389786` failed with `servo_view_host.cpp(363,1): error C2026: string too big` — MSVC's raw-string limit (~16 KB). Run `43443156706` was intentionally cancelled by a newer push.

## 4. Immediate next actions (in this order)

1. ~~Wait for CI `44776900341`.~~ **DONE** — run completed successfully; artifact downloaded, extracted and installed locally. The push restriction is lifted.
2. Download, extract and install (historical recipe):
   ```powershell
   cd E:\servo
   gh run download 44776900341 -n servo-windows-browser-setup -D C:\Users\LOQ\AppData\Local\Temp\opencode\setup
   # extract dist/servo-windows-browser-setup.zip, confirm exit code 0
   # stop any running ServoBrowser process, then:
   powershell -ExecutionPolicy Bypass -File <extracted>\install.ps1
   ```
   Verify `servo_browser.exe` / `servo_capi.dll` timestamps changed and record hashes.
3. Ask the user to restart the browser, open `attendance-view.aspx`, click the Semester dropdown, select a value and press Search.
4. Inspect the log for:
   - `[SELECT] showing menu (options=… selected=… multiple=… thread=…)`
   - `[SELECT] menu closed (confirmed=1 selection=1)`
   - absence of `TrackPopupMenuEx returned 0`; if present, use its `error=`/`owner=` values.
   - no plaintext password and no 8× duplicate `prompt_save_password` lines.
5. Push the locally prepared stability + feature work (§5) and start the next CI build. **Now unblocked.**

Installation verification (PASSED, 20 Nov 2026): both installers exited clean; exe timestamps changed 12 Nov → 20 Nov 17:09 → 20 Nov 17:41; browser launched and reached the new-tab page with `BrowserWindow::Create completed successfully` in the log. No plaintext password in the new log sections.

**User dropdown verification: PASSED (20 Nov 2026).** User reported the Semester dropdown works on the 17:41 build. Log confirms three successful interactions: `[SELECT] showing menu (options=9 selected=1 multiple=0 thread=6216)` followed each time by `[SELECT] menu closed (confirmed=1 selection=1)`. No `TrackPopupMenuEx returned 0`. New IPC log lines contain only `[IPC] Received message: prompt_save_password` (no payload); older entries are redacted. No plaintext password anywhere in the log.

Performance signals observed during verification (62 `[SLOW]`/`[HANG]` lines in the current log):
- `msg=0x8065` (`WM_APP_SERVO_WAKE`) took **1187 ms, 1187 ms and 2343 ms** during attendance page loads — the synchronous `ServoSpinEventLoopOnce` can block the UI thread for >2 s.
- `msg=0x000F` (`WM_PAINT`) repeatedly took **1656–1921 ms** in a burst, and the watchdog logged `[HANG] UI thread recovered after ~1 s`.
- These are the first concrete Phase 3 measurements. A performance audit (explore session `ses_e05e5f906ffenwmWv8Oyk7fkUj`) is running to locate the causes.

## 5. Work plan

### Phase 1 — Stability (audit complete, fixes in local commit — not pushed)

The stability audit (explore session `ses_e062240a5ffekR3LV10autP7RA`) inspected `ports/windows/src` and the C API delegate ownership contracts in `ffi/capi`. Ranked findings and status:

| # | Severity | Finding | Status |
|---|---|---|---|
| 1 | Critical | Popup `WM_DESTROY` called `PostQuitMessage(0)` — closing a `window.open()` popup killed the whole browser | **FIXED** — added `BrowserWindow::m_isPopup`; only the main window quits |
| 2 | Critical | Schannel gateway disabled TLS cert validation globally (`SECURITY_FLAG_IGNORE_*`) | **PARTIALLY FIXED** — ignore flags and TLS 1.1 are now scoped to `sxuk.org` only; every other upstream uses TLS 1.2 with full chain/host/date validation. Residual: SXUK traffic remains unverified by design of the legacy bridge |
| 3 | High | Popup create request double-free: `CreateForPopup` freed it on `CreateWindowExW` failure, returned `nullptr`, then Rust freed it again because the callback returned `false` | **FIXED** — failure paths never free; ownership is either taken by `servo_create_new_webview_builder_create` (returns `true`) or left for Rust (returns `false`). Added `ServoViewHost::WasRequestConsumed()` |
| 4 | High | `notify_load_error` is dead ABI surface — Rust delegate trait never invokes it | **DEFERRED** — needs a Rust trait/C API change or reusing the load-status callback for error feedback |
| 5 | High | Unchecked UTF-8→UTF-16 conversions in the gateway (`&empty_wstring[0]` UB) | **FIXED** — size checks, 400 on malformed input, WinHTTP handles closed on error paths |
| 6 | High | Initial `ServoViewHost` did not check `CreateWindowExW` or WebView build failure | **FIXED** — early return on null HWND, explicit build-failure log, `IsValid()` helper |
| 7 | High | Popup `WM_CLOSE` overwrote the saved session with `about:blank` | **FIXED** — only the main window persists session state |
| 8 | Medium | Multi-select file picker parsed as a single path | **DEFERRED** to feature phase (parse double-NUL format or drop `OFN_ALLOWMULTISELECT`) |
| 9 | Medium | `window.prompt()` returns the default value immediately | **DEFERRED** to feature phase (native input dialog) |
| 10 | Medium | Permission requests silently granted (geolocation, camera, microphone, …) | **FIXED (fail closed)** — denied with a log; real per-origin prompt UI is Phase 2 |
| 11 | Medium | Auth callback `port` is `uint16_t` in C++ but `int32_t` in the generated C header | **FIXED** — `int32_t` plus `%d` log format |
| 12 | Medium | `LogDebug` lazy `FILE*` initialisation outside the logging lock | **FIXED** — `std::call_once` |

Already hardened before the audit: `servo_spin_event_loop` re-entry guard (now exception-safe), IPC payload redaction, Schannel response completeness, embedder-control `user_data` and `FromWebView` host resolution, select-menu host/cursor hardening.

Local verification so far: `g++ -fsyntax-only` clean for `servo_view_host.cpp`, `browser_window.cpp`, `main.cpp`, `schannel_gateway.cpp`; `node --check` clean for the concatenated compat script. The authentication delegate is now assigned directly (no `reinterpret_cast`), so the compiler enforces the ABI signature — this check passed. **Full MSVC build / ctest / real-browser verification: NOT_RUN** — blocked on downloading CI artifact `44776900341`.

Residual risk: if `servo_create_new_webview_builder_create` were to panic *after* consuming the request but before returning, the C++ side would see a null builder and let Rust free the same handle. The Rust implementation consumes only after all fallible registry work, so this is considered very low probability; revisit if popup crashes/double-frees are observed.

Test coverage gaps identified by the audit: no unit tests for real `ServoViewHost`/C API delegate callbacks, popup create-request ownership (success/null context/builder failure/double-free), popup `WM_CLOSE`/`WM_DESTROY`/session save, `notify_load_error` wiring, gateway UTF-8 request parsing and TLS option configuration, auth signature parity, or logging watchdog concurrency. Several existing tests reimplement constants instead of calling production functions.

Acceptance criteria:
- SXUK login → dashboard → attendance → dropdown → search completes without crash/hang across repeated runs.
- No secret material in any log file.
- `ctest --test-dir build-browser -C Release --output-on-failure` passes.
- Failure paths (TLS error, DNS failure, closed socket, invalid IPC JSON) produce visible feedback, not a hung page.

### Phase 2 — Feature completeness (audit complete)

Static audit (explore session `ses_e06173f4effeTeH5R0yyz2XTuX`) of `ports/windows/src` and `ports/windows/tests`. No code was changed and no tests were run by that audit. Full checklist is in the session transcript; the high-value gaps are:

| # | Gap | Evidence | Sketch | Effort |
|---|---|---|---|---|
| 1 | Tabs are metadata, not isolated web contents — switching reloads one shared WebView, losing scroll/DOM/form/per-tab history/zoom | `browser_window.cpp:134-140`, `tab_strip.cpp:164-171`, `browser_window.cpp:905-912` | One `ServoViewHost` per tab, toggle visibility, store per-tab state | L |
| 2 | Settings persistence is not production wiring — search engine, home page, download dir, backdrop, font, suggestions, UA saved/parsed but not consistently honored | `settings_dialog.cpp:619-638,674-682`, `browser_window.cpp:675-687,751-766`, `omnibox.h:143-145`, `omnibox.cpp:843-844` | Single settings controller consumed at startup and runtime | M |
| 3 | DevTools UI cannot reach a server — server disabled at startup, F12 opens a localhost URL or an "initializing" message | `main.cpp:348`, `browser_window.cpp:359-375` | Start server with token support or remove the controls | M |
| 4 | Print is a native no-op — `PrintDlgW` DC freed immediately, no engine print call | `browser_window.cpp:887-895` | Wire engine printing/PDF or remove the command | M–L |
| 5 | Download tracking broken — progress always targets id `1`, directory setting ignored, only first completed item openable, no cancel/pause/remove | `browser_window.cpp:390-402`, `download_manager.cpp:85-120,250-255` | `id -> ActiveDownload` map, apply configured dir, add lifecycle commands | M |
| 6 | Clear browsing data falsely reports success — only clears the settings page `localStorage`, not cookies/cache/history/credentials | `settings_dialog.cpp:498,511,596`, `main.cpp:398-421` | Add IPC clear command for engine + browser stores, report real completion | M |
| 7 | Find-in-page never shows match counts and discards case sensitivity | `find_bar.cpp:111-118,193-217`, `browser_window.cpp:541-559` | Call `UpdateMatchResult` from find results; add case checkbox | S–M |
| 8 | Password/autofill security — DPAPI failure falls back to reversible raw hex; autofill is heuristic JS by DOM order | `password_manager.cpp:54-83`, `browser_window.cpp:841-885` | DPAPI-only/Credential Manager; form-aware field mapping | M–L |
| 9 | HTTP authentication always cancelled — no credential dialog, no saved-credential integration | `servo_view_host.cpp:1680-1696` | Modal auth prompt with host/realm context and callback | M |
| 10 | Context menu ignores engine target info — callback always dismissed, so link/image/media actions impossible | `servo_view_host.cpp:1528-1592` | Build from `ServoContextMenuParams`, map to `SERVO_CONTEXT_MENU_ACTION_*` | M |

Also confirmed present (code exists, runtime unverified): navigation, bookmark/history internal pages, new-tab page, zoom/fullscreen, alert/confirm, select menu, clipboard, IPC bridge, view-source.

Small gaps fixed locally (same stability/feature commit batch, not pushed): removed the unreachable duplicate `Ctrl+Shift+T` session-restore branch; download progress is mapped by URL to the correct row instead of always id `1`; the `on_find_result` delegate is now wired so find-in-page shows real match counts (stale query results are filtered by identifier).

Still open: Mica backdrop setting not applied, suggestions setting not honored, favicons only accept raw RGBA (encoded ICO/PNG/SVG skipped), find-bar match-case toggle, and all ranked gaps 1–6 and 8–10 above.

Test-quality gaps: tab/find/shortcut/zoom unit tests reimplement logic locally instead of calling production code; `BrowserWindow::ApplySettings`/`PromptSavePassword`/`RequestAutofill` are no-op stubs in the test binary; no delegate/callback tests at all; mock Servo API is inert; persistence tests mutate real browser data; `verify_github.ps1` is a smoke test only.

Acceptance: each implemented feature has a manual test path and, where practical, a unit test in `ports/windows/tests/test_browser_unit.cpp`.

### Phase 3 — Performance

Measure before optimizing. Candidate areas: startup/engine-init cost, `servo_spin_event_loop` wake frequency, paint invalidation, scrolling input latency, Schannel gateway caching and memory growth, log volume. Use `%LOCALAPPDATA%\Programs\Servo Browser\servo_browser.log` `[SLOW]`/`[HANG]` lines as existing instrumentation.

Acceptance: before/after numbers for at least startup and page-load latency; no regression in stability tests.

### Phase 4 — Gap audit

Produce a final checklist comparing the browser against a mainstream browser baseline, marking each item as implemented / partial / missing / out of scope, with rationale. Append it to this document.

## 6. Verification recipes (local, no CI)

Regenerate the real C API header after C API changes:
```powershell
cd E:\servo
$out = "C:\Users\LOQ\AppData\Local\Temp\opencode\capi\servo"
New-Item -ItemType Directory -Force -Path $out | Out-Null
cbindgen --crate servo-capi --config ffi/capi/cbindgen.toml --output "$out\servo_capi.h"
```

Syntax-check the changed C++ files (MinGW; MSVC-specific limits are NOT covered):
```powershell
$inc = "C:\Users\LOQ\AppData\Local\Temp\opencode\capi"
$flags = @('-fsyntax-only','-std=c++17','-DUNICODE','-D_UNICODE','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-I','ports/windows/src','-I',$inc)
foreach ($f in @('servo_view_host.cpp','browser_window.cpp','main.cpp','schannel_gateway.cpp')) {
  & g++ @flags "ports/windows/src/$f" 2>&1 | Select-String 'error:'
}
```

Check the compat JavaScript after edits (the first **two** raw literals form one script):
```powershell
python C:\Users\LOQ\AppData\Local\Temp\opencode\check_js.py
```
Helper scripts live in `C:\Users\LOQ\AppData\Local\Temp\opencode\` (`check_js.py`, `split_raw_literal.py`, `redact-log.ps1`, `capture-attendance.ps1`) — temp may be cleaned between sessions; recreate if missing.

**MSVC gotcha:** any raw string literal over ~16 KB fails with `C2026`. Keep each `R"JS(…)JS"` chunk below 16 KB.

## 7. Working-tree hazards — do not touch

- ~148 unrelated modified/deleted files were present at handoff, including large `graphify-out/` snapshot deletions and CRLF→LF normalizations. They appear to be another session's work. **Do not stage, revert or commit them.**
- `E:\servo\repro.html` is untracked; do not commit.
- `git commit --only <paths>` / `git add <specific paths>` only. Never `git add -A`.
- Pushing to the branch cancels the in-flight CI run and restarts the ~30-minute build. Coordinate pushes.

## 8. Project-rule reminders

- `graphify-out/graph.json` exists: run `graphify query "<question>"` before the first grep/search/source read for a codebase question; use `graphify path`/`graphify explain` for focused context. Include this rule in every subagent prompt that explores code. Dirty `graphify-out/` files are expected.
- After modifying code, run `graphify update .` (AST-only) if it does not disturb the existing working-tree state described in §7.
- Evidence-based reporting: state PASSED / FAILED / BLOCKED / NOT_RUN explicitly; never claim a build or test passed without executing it.

## 9. Phase 3 - performance audit (20 Nov 2026)

Baseline from the installed build's log: 62 `[SLOW]`/`[HANG]` lines. `WM_APP_SERVO_WAKE` (`0x8065`) spins took 1187/1187/2343 ms immediately after select interactions; `WM_PAINT` (`0x000F`) repeatedly took 1656-1921 ms in a burst and the watchdog logged `[HANG] UI thread recovered after ~1 s`.

Ranked findings (from explore session `ses_e05e5f906ffenwmWv8Oyk7fkUj`):

1. **HIGH, C++ only** - Modal Win32 UI runs inside the delegate callback, inside `servo_spin_event_loop`, inside the wake handler: `TrackPopupMenuEx` in `servo_view_host.cpp:1896` (select), `MessageBoxW` at `:1489`/`:1499` (alert/confirm), `GetOpenFileNameW` at `:1520`. Log lines 4445-4469 prove the full human interaction time is charged to the wake message. Fix: post a UI task to the main pump and answer the responder there.
2. **HIGH, C++ + Rust** - `WM_PAINT` calls `servo_webview_paint()` + `servo_webview_present()` synchronously (`servo_view_host.cpp:1086-1096`; `ffi/capi/webview.rs:898-899`; `painter.rs:409-445`). Nine consecutive 1.6-1.9 s paints triggered the hang recovery. Fix: paint only when a new frame is ready; remove synchronous `UpdateWindow` from resize.
3. **HIGH, Rust/C API** - `ServoInner::spin_event_loop` (`components/servo/servo.rs:299-330`) drains unbounded queues of paint/embedder/net messages and invokes delegates synchronously. Cap per-spin work and expose a "more work" flag; deferred until C++ fixes are measured.
4. **MED-HIGH, C++ only** - One `PostMessageW` per engine wake, no coalescing (`servo.rs:1293-1300`, `lib.rs:712-719`, `main.cpp:208-214`). Add an interlocked pending flag.
5. **MEDIUM, C++ only** - 33 ms fallback timer = 30 spins/s forever (`browser_window.cpp:156-157,234,1154-1158`). Raise to ~100 ms because wakes drive active work.
6. **MEDIUM, C++ only** - Compat scripts injected 3x per page (`servo_view_host.cpp:631-651`, `:1384-1386`, `:1723-1727`); `setInterval(patchSysDomEvent,50)` has no guard. Deferred: behaviour risk on SXUK.
7. **MED-LOW, C++ only** - Synchronous `vfprintf`+`fflush` under a critical section for every log line (`main.cpp:130-162`), with unbuffered engine stdout/stderr (`:244-264`). Deferred.
8. **MEDIUM, C++ only** - Schannel gateway spawns an unbounded detached thread per request and opens a new session per asset (`schannel_gateway.cpp:582-596`, `:292-302`). Deferred.
9. **LOW-MED, C++ only** - Schannel cache grows to 64 MB with no eviction (`schannel_gateway.cpp:159-195`). Deferred.
10. **LOW, C++ only** - `s_allHosts`/popup `BrowserWindow` objects never freed before `ExitProcess` (`servo_view_host.cpp:671,730,846`, `main.cpp:527`). Deferred.

Existing `[SLOW]`/`[HANG]` instrumentation cannot separate spin/paint/modal time or count wake amplification. Planned counters: wake callbacks/posts/coalesced, spins + spin ms total/max, paints + paint ms total/max, deferred controls + ms total/max, logged every 10 s and at shutdown.

Implementation order in this phase: (1) perf counters + wake coalescing + 100 ms timer, (2) defer modal controls to `WM_APP_RUN_UI_TASK`, (3) frame-driven paint coalescing, (4) CI build + install + re-measure.

**Phase 3 implementation status:** items (1)-(3) landed in `01b900fe011`; local `g++ -fsyntax-only` passes for all changed C++ files. CI run `44790271927` PASSED build, unit tests, packaging and artifact upload; the artifact was downloaded, installed and runtime-verified. See section 11 for measurements and remaining work.

## 10. Phase 4 - final gap audit (static, 20 Nov 2026)

Method: static source inspection against a mainstream desktop-browser baseline. Build/ctest/installer/runtime verification were NOT_RUN in this audit; `IMPLEMENTED` means a production code path exists in source, not that it was runtime-verified. Full audit from explore session `ses_e05d8583bffefH9TDFPcZTWYjR`.

### 10.1 Navigation / session

| Status | Item | Rationale / evidence |
|---|---|---|
| IMPLEMENTED | URL normalization, search fallback | `omnibox.h:132-145`; unit tests `test_browser_unit.cpp:113-140`. |
| IMPLEMENTED | Omnibox suggestions | `omnibox.cpp:404-539,806-852`. |
| IMPLEMENTED | Back/forward/reload/stop | `browser_window.cpp:527-549`. |
| IMPLEMENTED | New tab, bookmarks/history pages, view-source | `browser_window.cpp:415-433,474-479,642-646,923-928`. |
| IMPLEMENTED | Bookmark/history persistence | `browser_data.cpp:96-143,200-271`. |
| IMPLEMENTED | Session save/restore | `browser_data.cpp:654-707`; `browser_window.cpp:649-668,1402-1408`. |
| IMPLEMENTED | Popup lifecycle/ownership | `servo_view_host.cpp:1693-1704`; `browser_window.cpp:181-225,1402-1419`. |
| IMPLEMENTED | Tab strip add/close/select/reopen/context menu | `tab_strip.cpp:87,102,149-171,712-735`. |
| IMPLEMENTED | Find-result wiring and download-ID mapping | `servo_view_host.cpp:1730-1745`; `browser_window.cpp:575-584,380-413`. |
| MISSING | Per-tab WebView isolation | Tabs are metadata over one shared WebView; switching reloads and loses DOM/scroll/form/zoom/history (`browser_window.cpp:134-140,930-938`; `tab_strip.cpp:164-171`). Largest parity gap. |
| PARTIAL | Home button | Hard-coded URL; saved `homePage` not consistently consumed (`omnibox.cpp:843-844`; `settings_dialog.cpp:622-623,675`). |

### 10.2 UI chrome

| Status | Item | Rationale / evidence |
|---|---|---|
| IMPLEMENTED | Per-monitor DPI, custom frame, Mica Alt | `main.cpp:324-329`; `browser_window.cpp:116-125`. |
| IMPLEMENTED | Dark mode, vertical tabs | `browser_window.cpp:687-699`; `tab_strip.cpp:246-248`. |
| IMPLEMENTED | Native alert/confirm via UI task pump | `servo_view_host.cpp:1522-1543`. |
| MISSING | Backdrop setting applied | Loaded/saved but not consumed by `ApplySettings` (`settings_dialog.cpp:636,680`; `browser_window.cpp:687-713`). |
| PARTIAL | Omnibox security/progress | HTTPS lock icon unconditional (`omnibox.cpp:378-390`). |
| PARTIAL | Find bar | No match-case toggle; `matchCase` hard-coded false (`find_bar.cpp:111-118,193-216`). |
| PARTIAL | Downloads pane | Only first completed item openable; no cancel/pause/remove (`download_manager.cpp:85-120,158-204,250-255`). |
| PARTIAL | Settings UI | Values persisted but inconsistently honored (`settings_dialog.cpp:614-699,1017-1053`; `browser_window.cpp:727-792`). |
| MISSING | Clear browsing data | HTML path clears only localStorage; native dialog clears only cache/cookie files (`settings_dialog.cpp:496-498,596,988-1013`). |
| MISSING | Print | `PrintDlgW` opens but DC is freed without engine/PDF printing (`browser_window.cpp:912-921,1356-1359`). |
| MISSING | DevTools UI | Server disabled at startup; F12 falls back to URL/message (`main.cpp:443`; `browser_window.cpp:361-372`). |
| MISSING | Engine-aware context menu | Target params ignored, always answers dismiss (`servo_view_host.cpp:1578-1592,1635-1642`). |
| MISSING | Permission prompt UI | Permissions fail closed safely but no per-origin prompt (`servo_view_host.cpp:1675-1691`). |
| MISSING | HTTP auth dialog | Auth requests logged then cancelled (`servo_view_host.cpp:1747-1763`). |
| MISSING | Real JS prompt | Returns default value immediately (`servo_view_host.cpp:1546-1549`). |
| MISSING | Multi-select file picker parsing | Only one path returned (`servo_view_host.cpp:1551-1576`). |
| PARTIAL | Favicons | Raw RGBA works; encoded ICO/PNG/SVG skipped (`servo_view_host.cpp:1456-1508`; `tab_strip.cpp:207-215`). |

### 10.3 Web platform

| Status | Item | Rationale / evidence |
|---|---|---|
| IMPLEMENTED | Modern web-platform prefs | `main.cpp:440-477`. |
| IMPLEMENTED | Native `<select>` dropdown | `servo_view_host.cpp:2059-2109`. |
| IMPLEMENTED | Web IPC bridge and password-prompt routing | `servo_view_host.cpp:1644-1657`; `browser_window.cpp:715-838,794-830,841-864`. |
| PARTIAL | UA persona wiring | Startup maps saved persona; runtime `ApplySettings` hard-codes Chrome UA (`main.cpp:429-444`; `browser_window.cpp:700-704`). |
| PARTIAL | Autofill | Heuristic JS picks nearest prior text/email/tel input (`browser_window.cpp:866-910`). |
| PARTIAL | Compat scripts | Registered and re-evaluated on load, causing repeated injection (`servo_view_host.cpp:603-656,1407-1415,1771-1775`). |
| MISSING | Bounded compat-script execution | Unguarded 50 ms `setInterval` (`servo_view_host.cpp:265,292`). |
| PARTIAL | Notification/geolocation/WebRTC/WebGPU toggles | Persisted via prefs only (`settings_dialog.cpp:640-666`; `browser_window.cpp:700-711`). |
| MISSING | Load-error UI feedback | `notify_load_error` ABI dead because Rust delegate never invokes it (`servo_view_host.cpp:1418-1429`). |

### 10.4 Networking / security

| Status | Item | Rationale / evidence |
|---|---|---|
| IMPLEMENTED | WebPKI roots, 30 s timeout | `main.cpp:447-448`. |
| IMPLEMENTED | Persistent cookie/cache context | `main.cpp:493-521`. |
| IMPLEMENTED | Localhost-only Schannel bridge, SXUK-only routing | `schannel_gateway.cpp:616-638,657-663`; `main.cpp:541`. |
| PARTIAL | TLS policy | Strict for other hosts; SXUK-only TLS 1.1 + ignore flags (`schannel_gateway.cpp:301-307,342-355`). |
| IMPLEMENTED | Fail-closed request parsing; capped responses | `schannel_gateway.cpp:264-290,438-472`. |
| PARTIAL | Set-Cookie rewriting | `Secure`/`Domain` stripped, `SameSite=None` downgraded (`schannel_gateway.cpp:546-556`). |
| PARTIAL | Gateway cache | 64 MB cap but no eviction (`schannel_gateway.cpp:157-195`). |
| IMPLEMENTED | No plaintext password/IPC payload logging | Only truncated command token logged (`browser_window.cpp:715-721`). |
| PARTIAL | Password storage | DPAPI first, insecure `raw:` hex fallback (`password_manager.cpp:54-84,86-118`). |
| IMPLEMENTED | Password origin normalization | `password_manager.cpp:31-52`. |
| IMPLEMENTED | Permission decisions fail closed | `servo_view_host.cpp:1675-1691`. |
| MISSING | Engine-wide clear browsing data | Same evidence as 10.2. |
| OUT OF SCOPE | Production code signing / auto-update | Packaging uses self-signed dev cert (`packaging/sign_and_package.ps1:16-35,55-57`). |

### 10.5 Stability / performance

| Status | Item | Rationale / evidence |
|---|---|---|
| IMPLEMENTED | Crash diagnostics | `main.cpp:45-130`. |
| IMPLEMENTED | Thread-safe logging | `main.cpp:132-164`. |
| IMPLEMENTED | Hang watchdog + perf counters | `main.cpp:181-232,235-270,607-639`. |
| IMPLEMENTED | Wake coalescing, deferred native controls, 100 ms timer, frame-gated paints | `main.cpp:272-291,293-309,620-630`; `browser_window.cpp:156-159`; `servo_view_host.cpp:1089-1122`. |
| IMPLEMENTED | Event-loop re-entry guard | `browser_window.cpp:37-57`. |
| MISSING | Bounded Rust per-spin queue draining | `components/servo/servo.rs:299-330`. |
| MISSING | Deduped/bounded compat-script execution | Same evidence as 10.3. |
| MISSING | Bounded gateway worker pool | `schannel_gateway.cpp:582-596`. |
| PARTIAL | Gateway cache eviction, sync logging, deterministic shutdown | `schannel_gateway.cpp:157-195`; `main.cpp:132-164,645-647`; `servo_view_host.cpp:673`; `browser_window.cpp:221-223`. |

### 10.6 Packaging / verification

| Status | Item | Rationale / evidence |
|---|---|---|
| IMPLEMENTED | CMake target, unit test target, CI build/test/package/upload | `ports/windows/CMakeLists.txt:16-31,51,79-99,101-119`; `.github/workflows/windows-browser.yml:3-18,146-201`. |
| IMPLEMENTED | Installer/uninstaller basics | `packaging/install.ps1:14-80`; `packaging/uninstall.ps1:6-18`. |
| PARTIAL | Self-signed packaging | Signing failures are warnings (`packaging/sign_and_package.ps1:16-57,59-79`). |
| PARTIAL | Verification harness and unit-test coverage | Smoke test only (`tests/verify_github.ps1:1-105`); tests stub several production paths (`tests/test_browser_unit.cpp:42-50,52-74,217-285,427-462`). |
| MISSING | Direct delegate/C API callback tests | No tests for real callbacks, popup ownership, auth ABI, permissions, find results, download mapping, gateway parsing. |

### 10.7 Highest-value remaining gaps

1. **L** - Per-tab WebView isolation (tabs lose live state on switch).
2. **M-L** - Unified settings wiring + real clear-browsing-data.
3. **M** - Security/privacy dialogs: permission prompt, HTTP auth, real JS prompt, multi-select picker.
4. **M-L** - Secure credential storage + form-aware autofill (remove `raw:` fallback).
5. **M-L** - Remaining performance tail: bounded Rust spins, compat-script dedup, bounded gateway threads, cache eviction.

## 11. Phase 3 runtime verification and adaptive paint throttle (21 Nov 2026)

### 11.1 Verification results from installed `01b900fe011` artifact

| Check | State | Evidence |
|---|---|---|
| CI build/test/package/upload | PASSED | Run `44790271927` for commit `01b900fe011`; artifact downloaded and installed from the extracted inner setup package. |
| Native `<select>` dropdown | PASSED | User reported "Done"; three successful `[SELECT]` pairs in an earlier session and two in the retest; no `TrackPopupMenuEx returned 0`. |
| Responder deferral | PASSED | No post-fix `[SLOW] msg=0x8065` (`WM_APP_SERVO_WAKE`); human interaction appears only as `[SLOW] msg=0x8066` (`WM_APP_RUN_UI_TASK`) plus `[PERF] deferred native control took 1985 ms` and `922 ms`. |
| Wake coalescing / spin bound | PASSED | `[PERF] periodic wake(cb=3826 post=1911 coalesced=1913) spin(count=1913 ms=373 max=16)`. |
| Password logging / IPC payload safety | PASSED | No plaintext password or `__servo_ipc__` payload in `LogDebug`; only benign `prompt_save_password` message name appears. |
| Popup close does not quit browser | PASSED | Verified with the installed artifact during SXUK popup testing. |

Measured paint tail from the same runtime session: `[PERF] paint(count=517 ms=610436 max=2218)` with many 1640-2141 ms paints and 79 `[HANG]` lines. The SXUK attendance page continuously produces new frames, so frame-driven paint coalescing alone cannot keep the UI thread responsive on this software-rendered machine.

### 11.2 Adaptive paint throttle (local, uncommitted at time of writing)

Files: `ports/windows/src/servo_view_host.h`, `ports/windows/src/servo_view_host.cpp`, `ports/windows/src/browser_window.h`, `ports/windows/src/main.cpp`.

Design: after each paint, set `m_paintThrottleMs` to 3x the last paint duration (capped at 5000 ms) for paints >= 400 ms, to the last duration for 100-399 ms paints, and to 0 for fast paints. `OnNewFrameReady` drops frames arriving inside the throttle window and schedules at most one delayed repaint with `SetTimer(hwnd, 2, ...)`; `WM_TIMER` clears the pending flag, marks a frame ready and invalidates the client area. Resize/large-expose paths still set `m_hasNewFrame` directly so user-visible geometry changes are not throttled. A `drop=N` field was added to the 10-second `[PERF]` summary so the next runtime test can prove frames were actually dropped.

Verification state:

- Local `g++ -fsyntax-only -DUNICODE -D_UNICODE`: PASSED for `servo_view_host.cpp`, `browser_window.cpp` and `main.cpp` using the generated C API header at `C:\Users\LOQ\AppData\Local\Temp\opencode\capi`.
- CI: PASSED. Run `44861051417` for commit `b4616899599` completed build, unit tests, packaging, self-signing and artifact upload in 20m40s.
- Artifact: PASSED. Artifact `14690537585` (`servo-windows-browser-setup`) downloaded via the GitHub API, double-expanded, and `install.ps1` installed it to `%LOCALAPPDATA%\Programs\Servo Browser`. Installed `servo_browser.exe` SHA256: `8C5A2BB6FE1769BFAAB0AD60835CE8D76C3559DBABEB84DF953D4D1C40BC5E8D`.
- Runtime re-measurement on the SXUK attendance page: PASSED. See section 11.3.

### 11.3 Final runtime verification of adaptive throttle (21 Nov 2026)

User retested login -> attendance page -> semester dropdown with the installed `b4616899599` artifact.

| Check | State | Evidence |
|---|---|---|
| Dropdown still works | PASSED | 2 `[SELECT] showing menu` and 2 `[SELECT] menu closed (confirmed=1 selection=1)` pairs; `popup_failure=0`. |
| Deferral still works | PASSED | `slow_wake_8065=0`; 2 `[SLOW] msg=0x8066` plus `[PERF] deferred native control took 1015 ms` and `1078 ms`. |
| Password/IPC payload safety | PASSED | `plaintext_password=0`, `servo_ipc_payload=0`; only benign `[IPC] Received message: prompt_save_password` lines. |
| Browser survives interaction | PASSED | Process still alive with title `View Attendance - Servo` after the retest. |
| Paint storm fixed | PASSED | Final summary: `[PERF] periodic wake(cb=3791 post=2247 coalesced=1542) spin(count=2249 ms=295 max=47) paint(count=3510 ms=2851 max=766) control(count=2 ms=2093 max=1078) drop=66`. Compare old build: `paint(count=517 ms=610436 max=2218)` and 79 `[HANG]` lines. New run: `hang=0`. |

The max paint duration dropped from 2218 ms to 766 ms, total paint time from 610436 ms to 2851 ms, and `[HANG]` from 79 to 0. The only remaining non-zero slow messages are the intentionally deferred native controls (`msg=0x8066`), which no longer block engine spin/wake dispatch. Remaining gaps are unchanged from section 10.7; performance tail work can now focus on the next tier (bounded Rust spins, compat-script dedup, bounded gateway threads, cache eviction) rather than emergency UI-thread stalls.

## 12. Embedding C API cross-platform failures (21 Nov 2026)

Failing CI: run `44861051245` (Embedding API C API) failed all three `Build and test servo-capi-tests` jobs, while the Windows browser build for the same commit passed. Failure log saved locally at `C:\Users\LOQ\AppData\Local\Temp\opencode\capi-failed.log`.

### 12.1 Root causes

1. **All platforms — `test_rendering_context_create_window_returns_null_in_headless` panics.** The C test passes fake native handles and expects `NULL`. `WindowRenderingContext::new_with_optional_refresh_driver` and `set_window` called `.expect("Failed to create native widget")` on `create_native_widget_from_window_handle`. On macOS/Windows this surfman error is `IncompatibleNativeWidget`, so the process panicked inside the FFI call instead of returning an error. Linux happened to return `Err` for the fake handles but still had the same panic hazard.
2. **Linux — `test_webview_custom_scheme` crashes the style/layout pipeline.** Backtrace: layout thread -> `ServoLayoutNode::text_node_needs_caret_placeholder` -> `LayoutDom<Node>::text_node_needs_caret_placeholder` -> `Node::is_editable_or_editing_host` -> `Node::inclusive_ancestors` -> `DomRoot::from_ref`, which asserts `thread_state::get().is_script()`. The layout thread was calling a script-thread-only ancestor-rooting API just to check editability of a plain text node.

### 12.2 Fixes

- `components/shared/paint/rendering_context.rs`: replaced both native-widget `.expect(...)` calls with `?`, so invalid/unsupported handles propagate as `Error` and the C API returns `NULL` as documented.
- `components/script/dom/node/layout_dom.rs`: added `LayoutDom::is_editable_or_editing_host_for_layout()`, which walks `composed_parent_node_ref()` and reads `ContentEditable`/editing-host state through layout-safe accessors. `text_node_paints_caret` and `text_node_needs_caret_placeholder` now use that helper instead of `Node::is_editable_or_editing_host`.

Existing C tests are the regression tests: `test_rendering_context_create_window_returns_null_in_headless` and `test_webview_custom_scheme`.

### 12.3 Verification state

- Local `cargo check -p servo-script -p servo-paint-api`: BLOCKED by local environment, not by source errors. `aws-lc-sys` initially lacked NASM on PATH; after adding it, `mozjs_sys` required MozTools/MozillaBuild, a separate unrelated WebIDL file `MediaKeys.webidl` had a syntax error, and the E: drive filled during compilation. Regenerable `target/debug/build`, `target/debug/incremental` and `target/debug/deps` caches were removed to recover disk space; no source files were removed.
- `graphify update .`: PASSED after the Rust edits.
- CI for these C API fixes: NOT_RUN at time of writing. Next step is to commit code plus docs, push, and re-run the Embedding API C API workflow on Linux/macOS/Windows plus the Windows browser workflow.
