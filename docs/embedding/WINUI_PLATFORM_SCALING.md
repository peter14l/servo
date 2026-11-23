# WinUI Platform Scaling (P3 preparation)

Status: **scaffolding only** · Priority: **Windows-first** · Companion docs: [`WINUI_UIUX_COMPLETION_CHECKLIST.md`](WINUI_UIUX_COMPLETION_CHECKLIST.md), [`ANDROID_EMBEDDING_GUIDE.md`](ANDROID_EMBEDDING_GUIDE.md).

This document records how browser responsibilities are owned today and how future Android/macOS/Linux shells could reuse core state without turning WinUI into a cross-platform UI framework. It does not change the Windows shell.

> [!IMPORTANT]
> The new [`ports/shared/browser_core/browser_core.h`](../../ports/shared/browser_core/browser_core.h) header is documentation/scaffolding only until a platform shell adopts it. It is **not** referenced by any `.vcxproj` or `CMakeLists.txt`, and no existing WinUI or Windows C++ file was modified. WinUI remains the flagship Windows shell; Android keeps its Compose shell.

---

## 1. Current WinUI ownership map

Source inventory at the checklist snapshot (`3ee6c89285d`), cross-checked with graphify. "Legacy" means the file is not compiled into `ServoUI.vcxproj` and therefore does not ship in the WinUI browser.

| Responsibility | Current owner | Notes |
| --- | --- | --- |
| App/window shell, engine init/shutdown, waker window, watchdog, command-line URL handling | `ports/windows/winui/ServoUI/App.xaml.cpp`, `ports/windows/src/browser_runtime.{h,cpp}` | WinUI-specific thread/message plumbing; `BrowserRuntime` holds `HWND` and `Servo*`. |
| Engine host and Servo callbacks (favicons, dialogs, context menu, loads) | `ports/windows/src/servo_view_host.{h,cpp}` | Native child HWND; raw BGRA favicon bridge. |
| Chrome UI: tabs, toolbar, omnibox, find panel, settings pane, internal pages | `ports/windows/winui/ServoUI/MainWindow.xaml`, `MainWindow.xaml.cpp`, `WinUIShell.cpp` | Mica Alt/backdrop, custom title bar, drag regions, per-monitor DPI. |
| Theme resources | `ports/windows/src/fluent_theme.h` | Windows theme/brush helpers. |
| Settings persistence and defaults | `ports/windows/src/browser_settings.{h,cpp}` | Native XAML settings pane is authoritative in WinUI; generated `settings.html` is a fallback from other shells. |
| History, bookmarks, session save/load, generated internal pages | `ports/windows/src/browser_data.{h,cpp}` | `SaveSession`/`LoadSession`, `LoadHistory`, `LoadBookmarks`; generated `servo://` pages. |
| Downloads data and delegate plumbing | `ports/windows/src/download_manager.{h,cpp}`, `ports/windows/src/browser_shell.h` | `IBrowserShell` handles `ServoDownloadItem` callbacks; WinUI exposes status text only today. |
| Engine/shell boundary | `ports/windows/src/browser_shell.h` | Shared by legacy GDI and WinUI shells, but carries `HWND`/`HBITMAP`/`Servo*` types. |
| DevTools backend | `ports/windows/src/browser_runtime.cpp` | Firefox RDP port is reported; token is intentionally never logged or displayed. |
| New tab page | `ports/windows/src/new_tab_page.cpp` | Generated HTML. |
| Packaging/installer | `.github/workflows/windows-browser.yml`, `support/packaging/windows/install.ps1` | Windows-only MSIX-adjacent installer flow. |
| Legacy GDI shell (not in WinUI build) | `ports/windows/src/browser_window.cpp`, `omnibox.cpp`, `find_bar.cpp`, `tab_strip.cpp`, `download_manager.cpp`, `settings_dialog.cpp` | Features that exist only here are gaps for WinUI, not shared code. |
| Android shell | `support/android/` (e.g. `support/android/apk/servoapp/src/main/java/org/servo/servoshell/MainActivity.kt`), `servoview` AAR | Separate Compose UI plus JNI/SurfaceView engine embedding. No shared chrome with WinUI. |

The checklist's gap register (X1–X3) captures the present situation: Android has a separate UI and shared core is not yet reused; WinUI has no cross-platform UI abstraction and should not be forced onto other platforms.

---

## 2. What is deliberately not shared

These stay inside their native shells. The browser core contracts do not abstract them and must not be read as a plan to replace them:

- **XAML chrome** — `MainWindow.xaml`, `MainWindow.xaml.cpp`, custom title bar, Mica Alt/backdrop, theme-aware brushes, drag regions, tab strip visual, settings pane, find panel grid row.
- **Native HWND composition and airspace** — the Servo child HWND, DPI-scaled placement, message-only waker, `PostUiTask`/message pump, watchdog, DirectComposition layering decisions, and the constraint that XAML cannot overlay the native web-content HWND.
- **WinRT/COM/Windows types** — `HWND`, `HBITMAP`, `SoftwareBitmapSource`, COM initialization, Windows packaging and installer flows.
- **Platform UI conventions** — Android Compose navigation/back gesture/safe areas/bottom toolbar/permission prompts; future macOS AppKit or Linux GTK chrome, menus, and window lifecycle.
- **Engine rendering and GPU context** — surfaces, EGL/ANGLE/WebRender setup, sandboxing, and process model. These remain engine/embedder concerns.
- **Credential and secret handling** — the DevTools token stays credential-like: expose it only through the shell's secure connection flow, never through logs or the event sink's diagnostic text.

The shared header only describes plain data and commands: settings key/value pairs, session snapshots, history/bookmark/download records, init/shutdown/navigate commands, and chrome-facing events.

---

## 3. Migration sequence for future shells

Each step is additive. WinUI is never migrated *into* the new abstraction; a future shell adopts the contracts, and existing Windows code keeps building exactly as before.

1. **Adapters first, in the new shell.** Implement `ISettingsStore`, `ISessionCodec`, `IBrowserDataStore`, `IDownloadStore`, `IEngineController`, and `ICoreEventSink` with that platform's native persistence and engine bridge. Keep adapter code inside the new shell's directory.
2. **Wire events to native chrome.** Translate `ICoreEventSink` callbacks into Compose/AppKit/GTK UI updates. Do not attempt to render XAML or reuse WinUI chrome off Windows.
3. **Prove data contracts in isolation.** Test settings round-trips, session serialize/deserialize, history/bookmark CRUD, and download state transitions against each adapter. Core tests must not require a UI toolkit.
4. **Opt in via the new shell's build files only.** Add the header/adapters to that shell's `CMakeLists.txt`/Gradle/native build when it actually adopts them. Do not touch `ServoUI.vcxproj` or existing Windows targets as part of P3 scaffolding.
5. **Android first, then macOS/Linux.** Android can build on the existing `servoview` AAR and Compose shell described in [`ANDROID_EMBEDDING_GUIDE.md`](ANDROID_EMBEDDING_GUIDE.md); macOS and Linux follow the same adapter pattern with their own native shells and platform lifecycle handling.
6. **Optional Windows convergence, later and separately.** Only after a shell is shipping should anyone consider re-expressing existing Windows helpers in terms of these interfaces. That work needs its own runtime verification against the checklist's definition of done and must preserve native HWND behavior.
7. **Track parity using the checklist.** Use the X1–X3 entries and the P0–P2 gap register in [`WINUI_UIUX_COMPLETION_CHECKLIST.md`](WINUI_UIUX_COMPLETION_CHECKLIST.md) as the acceptance reference; do not claim parity from source inventory alone.

---

## 4. Risks and non-goals

### Risks

- **Premature abstraction.** These contracts are derived from observed Windows/Android needs. If a future platform needs a different shape, extend the interface deliberately rather than forcing all shells through Windows semantics.
- **Divergent persistence.** Each adapter chooses its own payload format. A session written by one shell is not promised to load in another; `ISessionCodec` only fixes the C++ data shape.
- **Thread affinity.** WinUI moves engine work to the main HWND thread. Other shells have different lifecycles. The core interfaces start no threads and define no scheduling policy; adapters must document theirs.
- **Partial data today.** WinUI history is capped and has host-side URL fallback; downloads are mostly status text; bookmark CRUD UI is incomplete. Shared records do not imply these features are complete.
- **Legacy duplication.** The GDI shell still holds features absent from WinUI. Treat it as evidence of gaps, not as a second shell to keep alive through the shared core.
- **Credential leakage.** DevTools endpoint/token handling must remain masked; never log or expose the token through generic event text.
- **Build-scope creep.** Adding these headers to Windows or Android build systems before an adapter exists would create unused compile surface. Keep them opt-in.

### Non-goals

- No cross-platform UI toolkit, no shared XAML/Compose/AppKit/GTK chrome, and no replacement of WinUI on Windows.
- No changes to the native HWND composition model or the XAML-overlay constraint.
- No ABI/versioning promise yet; these are C++ source interfaces for in-tree adapters.
- No engine conformance, WPT, sandbox, graphics, or packaging work — see [`PARITY_MATRIX.md`](PARITY_MATRIX.md) and [`FULL_BROWSER_ROADMAP.md`](FULL_BROWSER_ROADMAP.md).
- No iOS commitment; see [`IOS_PORT_SPIKE.md`](IOS_PORT_SPIKE.md).
- No build or runtime verification in this P3 preparation step.

---

## 5. Verification status for this document

| Check | State | Evidence |
| --- | --- | --- |
| Required graphify query before source reads | **PASSED** | `graphify query` run in worktree before first codebase read |
| Guide read first | **PASSED** | `WINUI_UIUX_COMPLETION_CHECKLIST.md` read before drafting |
| Ownership map cross-check | **PASSED** | Checklist baseline/gap register plus bounded source reads of `browser_settings.h`, `browser_data.h`, `browser_runtime.h`, `browser_shell.h` |
| Link existence (checklist, Android guide, header) | **PASSED** | Relative targets exist in the worktree |
| No Windows/WinRT headers in new abstraction | **PASSED** | Header includes only `<cstddef>`, `<cstdint>`, `<functional>`, `<optional>`, `<string>`, `<vector>` |
| New files added to any build system | **NOT_APPLICABLE** | Explicitly deferred until a platform shell adopts the interfaces |
| Build/tests/heavy processes | **NOT_RUN** | Out of scope for this scaffolding task |

This is a living document. Update the ownership map and migration status as adapters land, with runtime evidence, not intention.
