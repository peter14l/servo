# Windows WinUI 3 Shell Migration & Engine Release Plan

Status: **Executable roadmap** · Baseline: `6f9c59316b9` (`private-search-isolation`) · Created: 2026-11-26

This roadmap combines the **WinUI 3 Shell Migration** (Priority 1) with the **Engine Public-Release Readiness** workstreams (Priority 2), following the ground rules in [`BROWSERD_PUBLIC_RELEASE_PLAN.md`](./BROWSERD_PUBLIC_RELEASE_PLAN.md) and [`BROWSERD_PUBLIC_RELEASE_TRACKER.md`](./BROWSERD_PUBLIC_RELEASE_TRACKER.md).

> **Hard Constraints**
> - **No heavy local builds or test suites.** Do not run `mach build`, `cargo build`, `msbuild`, or full test suites locally. All compilation, packaging, and WPT/C API verification run on GitHub Actions.
> - **One workstream per push.** Complete and push Priority 1 (WinUI 3 shell migration) and verify its CI build before moving on to the engine compatibility/release-gate changes.
> - **No site-specific hacks or weakened checks.** Never enable `--always-succeed`, suppress lints, or add site-specific workarounds.
> - **Keep the graph current.** Run `graphify update .` after code changes.

---

## Execution Order Overview

```
Priority 1: WinUI 3 Shell Migration (UI-1 -> UI-5)  -->  Push & CI Green
                                                              |
                                                              v
Priority 2: Engine Release Readiness (REL-0/2 -> REL-3 -> REL-4 -> REL-1/5/6/7 -> REL-8)
```

---

## Priority 1 — WinUI 3 Shell Migration (`ports/windows/`)

**Goal:** Replace the raw Win32 GDI chrome (`ports/windows/src/*.cpp` GDI `RoundRect` / `FillRect` / `EDIT` controls) with a genuine **WinUI 3 + Windows App SDK** shell using [`ports/windows/csharp/ServoCapi.cs`](../../ports/windows/csharp/ServoCapi.cs) and `servo_capi.dll`, while keeping the existing C++ `servo_browser` target intact as a fallback until feature parity is verified.

### UI-1 — Scaffold WinUI 3 Project & `ServoCapi` Host
- **Touch points:**
  - `ports/windows/csharp/ServoBrowser.WinUI.csproj` (new, targeting `net8.0-windows10.0.19041.0`+, `Microsoft.WindowsAppSDK`)
  - `ports/windows/csharp/App.xaml`, `App.xaml.cs`, `MainWindow.xaml`, `MainWindow.xaml.cs`
  - [`ports/windows/csharp/ServoCapi.cs`](../../ports/windows/csharp/ServoCapi.cs) (audit & sync with [`ffi/capi/symbols.txt`](../../ffi/capi/symbols.txt))
- **Steps:**
  1. Create an unpackaged/self-contained-friendly WinUI 3 project in `ports/windows/csharp/` with `UseWinUI = true` and `WindowsPackageType = None` (or MSIX-compatible packaging).
  2. Configure `MicaController` (`MicaKind.BaseAlt`) and `ExtendsContentIntoTitleBar = true` on `MainWindow` so the real Windows 11 Mica Alt backdrop renders without being occluded by opaque child `FillRect`s.
  3. Implement a child `HWND` host (`ServoViewportHost`) that creates a borderless child `WS_CHILD` window, passes it to `servo_rendering_context_create_windows_hwnd`, and forwards DPI-scaled `SizeChanged`, focus, and `WM_PAINT` (`servo_webview_paint` + `servo_webview_present`) events.
- **Acceptance:** Project builds in GitHub Actions (`windows-2022` runner), launches `MainWindow` with Mica Alt, and loads a page inside the hosted Servo viewport.

### UI-2 — Native Fluent TabStrip & Titlebar Integration
- **Touch points:**
  - `ports/windows/csharp/MainWindow.xaml` / `MainWindow.xaml.cs`
  - `ports/windows/csharp/Models/TabItemModel.cs`
- **Steps:**
  1. Use WinUI 3 `TabView` integrated into the custom title bar (`AppWindow.TitleBar`) for native drag-reorder, close/add animations, Mica-blended inactive tabs, and system caption buttons.
  2. Wire `TabView` selection, close, and new-tab actions to `ServoWebView` lifecycle (`servo_webview_builder_create`, `servo_webview_focus`, `servo_webview_free`) and update tab titles, favicons, and loading rings via delegate callbacks.
  3. Add optional vertical-tabs mode using WinUI 3 `NavigationView` (`PaneDisplayMode="Left"`).
- **Acceptance:** Multi-tab creation, switching, closing, and favicon/title updates work smoothly with native WinUI 3 animations and zero airspace clipping over titlebar controls.

### UI-3 — Fluent Omnibox, Navigation Toolbar & Overlays
- **Touch points:**
  - `ports/windows/csharp/Controls/BrowserToolbar.xaml` / `.cs`
  - `ports/windows/csharp/Controls/FindBarOverlay.xaml` / `.cs`
- **Steps:**
  1. Build the navigation bar using `Button` / `AppBarButton` (`Segoe Fluent Icons`) for Back, Forward, Reload/Stop, Home, Bookmarks, Downloads, and Settings.
  2. Replace the Win32 `EDIT` box with a WinUI 3 `AutoSuggestBox` supporting URL/search normalization (percent-encoded queries to `https://duckduckgo.com/html/?q=%s` per [`GEMINI.md`](../../GEMINI.md)), focus glow, security lock indicator, and history/bookmark suggestions.
  3. Implement Find-in-Page (`InfoBar` / floating overlay), Downloads (`Flyout` with `ProgressBar`), and Settings (`ContentDialog` / `NavigationView` page) using native Fluent controls.
- **Acceptance:** All core browser chrome flows (navigation, search, find-in-page, downloads, zoom, dark/light theme sync) work with native Fluent styling.

### UI-4 — Feature Parity & Data Bridge (Bookmarks, History, Passwords, Session)
- **Touch points:**
  - `ports/windows/csharp/Services/` (JSON/SQLite/DPAPI data services matching `browser_data.cpp` and `password_manager.cpp` formats, or a thin C-ABI bridge)
- **Steps:**
  1. Preserve compatibility with existing user profile data (`browser_data`, `password_manager` DPAPI storage, session restore).
  2. Wire JavaScript/prompt dialogs (`alert`, `confirm`, `prompt`, file picker, context menu) to WinUI 3 `ContentDialog` / `MenuFlyout` dispatched on the UI `DispatcherQueue` (never blocking the Servo event-loop spin).
- **Acceptance:** Existing profile data loads cleanly and modal web dialogs do not deadlock Servo's message pump.

### UI-5 — CI Build, Packaging & Push Gate
- **Touch points:**
  - `.github/workflows/windows-browser.yml`
  - `ports/windows/packaging/`
- **Steps:**
  1. Add the WinUI 3 build (`dotnet build` / `dotnet publish`) to `.github/workflows/windows-browser.yml` alongside `servo_capi.dll` and ANGLE DLLs, keeping C++ `servo_browser` intact until the WinUI 3 artifact is hardware-verified.
  2. Update packaging scripts in `ports/windows/packaging/` to bundle the WinUI 3 shell.
  3. Push to `private-search-isolation` and confirm the Windows CI workflow is green before starting Priority 2.

---

## Priority 2 — Engine Release Readiness (`REL-0` … `REL-8`)

Executed sequentially after Priority 1 is pushed and green in CI (one gate per push):

### Phase 2A — Confirm CI & C ABI Baseline (`REL-0`, `REL-2`)
1. Confirm Main + strict WPT pass on the current line (verifying the restored `load_status: Started` and partitioned-cookie fixes from `6f9c59316b9`).
2. Confirm `embedding-api.yml` passes on Linux, Windows, and macOS, including all 6 C ABI lifecycle tests.
3. Pull the fresh Windows CI artifact to verify `044f25042c5` (`SurfaceImportFailed(BadAttribute)` renderer panic fix).

### Phase 2B — Website & Animation Compatibility (`REL-3`)
Target root causes from [`COMPAT_TRIAGE_REPORT.md`](./COMPAT_TRIAGE_REPORT.md) with zero site-specific hacks:
1. **WebGL2 Stabilization (unblocks `three.js`, `Anime.js`, WebGL2 sites):**
   - Audit WebGL2 WPT failures and Windows ANGLE/`surfman` surface import in `components/shared/paint/rendering_context.rs`.
   - Fix remaining context-creation / `BadAttribute` issues and validate WebGL2 in CI before enabling `dom_webgl2_enabled` in `components/config/prefs.rs`.
2. **Google Maps & Complex SPAs:**
   - Re-test Google Maps on the fresh CI artifact (`044f25042c5`).
   - Diagnose why Google Maps produced an empty map DOM (`canvasCount: 0`, `mapNodes: 0`) even with WebGL2 enabled, and implement the missing standards-compliant Web/DOM/Canvas/WebGL behavior.
3. **GitHub & Animation-Heavy Sites:**
   - Re-verify GitHub on the fresh artifact to confirm screenshot/WebRender capture stability.
   - Audit `requestAnimationFrame` cadence, CSS transforms/animations, and the WinUI 3 `servo_webview_paint` / `servo_webview_present` loop for smooth 60+ fps animation rendering.
4. **Strict WPT Conformance Delta:**
   - Compute the strict WPT delta against the `5461673d1e9` baseline (88.37% assertion pass / 55.04% harness-`OK`) and resolve high-impact regressions.

### Phase 2C — Security, Sandboxing & Storage (`REL-4`)
1. **Windows OS Renderer Sandbox:** Implement OS-level content-process sandboxing on Windows (AppContainer / restricted token) beyond in-process mitigations, and keep `servo_capi_is_sandboxing_supported()` truthful.
2. **Site Isolation:** Address cross-origin process partitioning / out-of-process iframes (or formally scope the supported threat model).
3. **CHIPS / Partitioned Cookies:** Fix `components/net/` so stored `Partitioned` cookies are sent on matching requests.
4. **Hygiene:** Keep `check_ffi_panic_boundary.py` (257/257), `cargo deny`, and scheduled fuzzing green.

### Phase 2D — Hardware, Performance, Telemetry & Distribution (`REL-1`, `REL-5`, `REL-6`, `REL-7`)
1. **`REL-1` (Windows + WinUI 3 Hardware Validation):** Execute the manual GPU/hardware checklist on real Windows hardware using green CI artifacts.
2. **`REL-5` (Performance):** Configure the Bencher token (removing masked `continue-on-error: true`), add Windows performance/frame-pacing benchmarks, and run long-uptime leak tests.
3. **`REL-7` (Observability):** Implement opt-in crash telemetry (minidump capture, symbolication, retention policy).
4. **`REL-6` (Distribution):** Produce signed/notarized Windows installers from green CI runs and verify clean-machine install/upgrade.

### Phase 2E — Public Release Sign-off (`REL-8`)
- Record the signed go/no-go decision in [`BROWSERD_PUBLIC_RELEASE_TRACKER.md`](./BROWSERD_PUBLIC_RELEASE_TRACKER.md) once `REL-0` through `REL-7` are `PASSED`, and remove developer-preview caveats.
