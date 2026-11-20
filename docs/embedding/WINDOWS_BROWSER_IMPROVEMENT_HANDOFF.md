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
| Local HEAD at handoff | **not yet pushed** — `git log -1` for the current hash; remote branch head is `b21390fda22` (split compat script + `FromWebView` fallback) |
| Local-only commit | Handoff doc, select/host hardening, exception-safe event-loop guard |
| CI run in flight | `44776900341` — *Native Windows Browser Build*, head `b21390fda22` |
| Installed browser | `%LOCALAPPDATA%\Programs\Servo Browser\servo_browser.exe` dated **12 Nov 2026 01:30** — **pre-fix build** |
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

1. **Wait for CI `44776900341`.** Do not push anything until its artifact is downloaded — a push cancels the run (`concurrency.cancel-in-progress: true`).
2. Download, extract and install:
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
5. Only then push the locally prepared stability work (§5) and start the next CI build.

## 5. Work plan

### Phase 1 — Stability (in progress)

A background `explore` subagent (session `ses_e062240a5ffekR3LV10autP7RA`) is auditing `ports/windows/src` and its C API interaction. Insert its ranked findings here when it reports; then implement Critical/High items first.

Known starting points / already hardened:
- `servo_spin_event_loop` re-entry (`ServoSpinEventLoopOnce` in `browser_window.cpp`).
- `HandleWebIpcMessage` payload redaction.
- Schannel gateway response completeness.
- Embedder-control `user_data` and `FromWebView` host resolution.

Acceptance criteria:
- SXUK login → dashboard → attendance → dropdown → search completes without crash/hang across repeated runs.
- No secret material in any log file.
- `ctest --test-dir build-browser -C Release --output-on-failure` passes.
- Failure paths (TLS error, DNS failure, closed socket, invalid IPC JSON) produce visible feedback, not a hung page.

### Phase 2 — Feature completeness

Audit and implement the highest-value missing browser features. Candidates to confirm against the code first: history UI, bookmarks UI, session/tab restore, downloads UI, full context-menu actions, keyboard shortcuts, find-in-page polish, zoom, print, form autofill, spellcheck, privacy/clear-data, profile settings. Do not assume any of these are missing — inspect `ports/windows/src` first.

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
foreach ($f in @('servo_view_host.cpp','browser_window.cpp','main.cpp')) {
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
