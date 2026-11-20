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

Performance signal observed during verification: `[SLOW] msg=0x8065 took 1187 ms` and `took 2343 ms` during attendance page loads (`0x8065` = `WM_APP_SERVO_WAKE`). This is the first concrete Phase 3 measurement — the synchronous `ServoSpinEventLoopOnce` can block the UI thread for >2 s on navigation.

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
