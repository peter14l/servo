# WinUI Browser UI/UX Completion Checklist

Status: **Living document** · Priority: **Windows-first** · Snapshot: commit `3ee6c89285d` · Companion docs: [`FULL_BROWSER_ROADMAP.md`](FULL_BROWSER_ROADMAP.md), [`PARITY_MATRIX.md`](PARITY_MATRIX.md), [`WINDOWS_BROWSER_PLAN.md`](WINDOWS_BROWSER_PLAN.md), [`WINDOWS_BROWSER_IMPROVEMENT_HANDOFF.md`](WINDOWS_BROWSER_IMPROVEMENT_HANDOFF.md), [`WINUI_PLATFORM_SCALING.md`](WINUI_PLATFORM_SCALING.md), [`UPDATE_AND_FEEDBACK.md`](../../support/packaging/windows/UPDATE_AND_FEEDBACK.md).

> [!IMPORTANT]
> **Current batch:** P0–P3 source work is complete in this worktree
> (see [§2.1](#21-implemented-in-this-batch)). The gap-register marks below still
> describe the `3ee6c89285d` snapshot; they are **not** upgraded to **I**/**P** on
> intention alone. A post-push CI build of this batch is **PENDING**, and the
> interactive smoke test is **NOT_RUN**.

---

## 1. Purpose and scope

This document answers one question: **what is actually left on the UI/UX side before the WinUI browser feels like a complete, feature-rich Chrome/Edge-class browser?**

It covers only the native WinUI 3 shell in `ports/windows/winui/ServoUI/` plus the shared C++ helpers it compiles from `ports/windows/src/`. It is not an engine-conformance document. For engine, sandbox, WPT, and C API parity, see [`PARITY_MATRIX.md`](PARITY_MATRIX.md) and [`FULL_BROWSER_ROADMAP.md`](FULL_BROWSER_ROADMAP.md).

"Complete like Chrome/Edge" is interpreted as:

- a user can create, switch, restore, and understand tabs;
- navigation is predictable and shows security/loading state;
- common browser features are discoverable in the chrome;
- settings actually affect runtime behavior;
- downloads, history, bookmarks, find, and DevTools have usable UX;
- security/privacy dialogs and error states are explicit;
- keyboard, accessibility, DPI, and multi-monitor behavior are dependable;
- the installed build is signed, updatable, and diagnosable.

> [!IMPORTANT]
> The repository still contains a legacy GDI shell (`browser_window.cpp`, `omnibox.cpp`, `find_bar.cpp`, `tab_strip.cpp`, `download_manager.cpp`, `settings_dialog.cpp`, and related files). Features that exist **only** there are counted as gaps for WinUI, because that code is not in `ServoUI.vcxproj` and therefore does not ship in the WinUI browser.

### Status legend

| Mark | Meaning |
| --- | --- |
| **I** | Implemented in source in the WinUI shell. Source-inventory only unless stated otherwise. |
| **P** | Partial: data/API/UI fragment exists, but the user-visible flow is incomplete or unwired. |
| **M** | Missing: no WinUI code path was found. |
| **U** | Unverified: code exists, but runtime behavior or wiring could not be confirmed from source. |
| **E** | Implemented elsewhere (legacy GDI shell or Android shell), not in WinUI. |

---

## 2. Current baseline: what is already in WinUI

The WinUI shell is not an empty prototype. The following are implemented in source at this snapshot.

| Area | Current state | Evidence |
| --- | --- | --- |
| App/window shell | Per-monitor DPI v2, engine init/shutdown, message-only waker window, watchdog, command-line URL handling | `winui/ServoUI/App.xaml.cpp`, `src/browser_runtime.cpp` |
| Visual system | Mica Alt backdrop, custom title bar, caption-button inset, drag regions, app icon, theme-aware brushes | `winui/ServoUI/MainWindow.xaml`, `MainWindow.xaml.cpp:202-335` |
| Tabs | Horizontal `TabView`, add/close/select, per-tab title/URL state, rich context menu, vertical rail with drag-resize and persisted width | `MainWindow.xaml:27-66,148-166`, `MainWindow.xaml.cpp:381-419,592-712,960-996` |
| Navigation toolbar | Back, Forward, Reload, Home, omnibox, status text, Find, Settings | `MainWindow.xaml:82-114` |
| URL handling | URL detection, bare-host `https://` prefixing, DDG/Google/Bing/Ecosia search, internal `servo://`/`about:`/`chrome://settings` routes, View Source | `WinUIShell.cpp:306-393`, `src/browser_settings.cpp` |
| Page features | Find panel with live search/next/previous/count, zoom in/out/reset, generated New Tab page | `MainWindow.xaml:117-143`, `MainWindow.xaml.cpp:777-842,998-1054`, `src/new_tab_page.cpp` |
| Internal pages | Bookmarks, History, About, crash recovery generated HTML with search and footer navigation | `src/browser_data.cpp:421-651` |
| Settings | Native XAML pane with General/Appearance/Privacy/Experiments/Standards/Data/About sections, save/reset/clear-history, persistence | `MainWindow.xaml:172-328`, `MainWindow.xaml.cpp:527-581,714-729` |
| Favicons | Engine callback → raw BGRA → `SoftwareBitmapSource` → tab header cache | `src/servo_view_host.cpp:2064-2125`, `WinUIShell.cpp:251-282`, `MainWindow.xaml.cpp:1129-1156` |
| Dialogs/context menu | Native alert/confirm/file picker and web-content context menu with Back/Forward/Reload/Copy/Select All/View Source/Inspect/Settings | `src/servo_view_host.cpp:2137-2245` |
| DevTools backend | Firefox RDP server enabled, port cached, F12/Ctrl+Shift+I reports endpoint | `src/browser_runtime.cpp:220-223,342-345,445-452` |
| Packaging | CI build, self-signed installable artifact, installer/uninstaller, shortcuts, icon | `.github/workflows/windows-browser.yml`, `support/packaging/windows/Install-ServoBrowser.ps1` |

**Verification note:** at this snapshot, CI build/install/launch passed, but an interactive UI test suite and manual feature-by-feature runtime verification were **NOT_RUN**. Items marked **I** mean "implemented in source", not "runtime-verified".

### 2.1 Implemented in this batch

This batch adds source work across P0–P3. Treat every entry below as
**source-inventory only** until the post-push CI build and an interactive smoke test
have actually run; none of it is a runtime verification claim.

| Priority | Area being implemented in this batch | Where to look |
| --- | --- | --- |
| P0 | Navigation/chrome: Stop control, security chip + site-info panel, bookmark action, downloads button/panel, error banner with retry | `ports/windows/winui/ServoUI/MainWindow.xaml` (`StopButton`, `SecurityChip`, `BookmarkButton`, `DownloadsButton`, `ErrorBanner`, …) |
| P0 | Omnibox suggestions, find options (match case / whole word), zoom bubble, tab search | `MainWindow.xaml` (`SuggestionsPanel`, `FindMatchCase`, `FindWholeWord`, `ZoomBubble`, `TabSearchPanel`) |
| P1 | Fullscreen control, settings search, accessibility/automation names, permission and private-mode affordances | `MainWindow.xaml`, `WinUIShell.h` |
| P0–P1 | Shared shell contract additions (download records, clear-data categories, navigation-error/security/zoom callbacks, stop/reload/fullscreen/print/session actions) and the engine callbacks that feed them | `ports/windows/src/browser_shell.h`, `servo_view_host.{h,cpp}`, `WinUIShell.cpp` |
| P2 | More personalization/privacy settings (backdrop, text scale, reduced motion, notifications/geolocation, default-browser entry) and engine-wide clear-browsing-data helpers | `MainWindow.xaml` settings cards, `ports/windows/src/browser_settings.{h,cpp}` |
| P3 | Shared browser-core contracts and platform-scaling write-up; **scaffolding only**, not adopted by any shell or project file | [`WINUI_PLATFORM_SCALING.md`](WINUI_PLATFORM_SCALING.md), `ports/shared/browser_core/` |
| CI/packaging/docs | Source-only CI gates, artifact SHA256 + manifest, default-browser capability metadata, Start Menu utilities, diagnostics template, manual update/feedback doc | `.github/workflows/windows-browser.yml`, `support/packaging/windows/`, [`UPDATE_AND_FEEDBACK.md`](../../support/packaging/windows/UPDATE_AND_FEEDBACK.md) |

The update/feedback work is intentionally labelled **scaffolding**: there is no
auto-updater and no in-product crash reporter in this batch. See §3.11 and
[`UPDATE_AND_FEEDBACK.md`](../../support/packaging/windows/UPDATE_AND_FEEDBACK.md).

---

## 3. Gap register

### 3.1 Tabs and window management

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| T1 | **U** | Per-tab live WebView state is not evidenced in WinUI. Tabs currently track title/URL/favicon; source inventory did not confirm that each tab owns a live WebView. | Switching tabs preserves DOM, scroll, form state, zoom, and per-tab history. If the shell still uses one shared view, this is the largest single parity gap. |
| T2 | **M** | Recently-closed list and `Ctrl+Shift+T` are advertised by generated About/New Tab pages but not wired in WinUI. | Reopening a closed tab works; session save/load is callable from WinUI; advertised shortcuts match reality. |
| T3 | **M** | `Ctrl+Tab`, `Ctrl+Shift+Tab`, and `Ctrl+1..9` are not implemented in `RootKeyDown`. | Standard tab cycling and direct tab selection work even when focus is in the web content. |
| T4 | **M** | Tab pinning, grouping, muting, drag-reorder, detach-to-window, tab search, and hover previews are absent. | At minimum pinning, mute indicator, drag-reorder, and tab search exist with context-menu/keyboard access. |
| T5 | **P** | Web popups always become tabs in the same window. | Popups respect a documented policy: tab by default, with a UI choice to open in a new window where appropriate. |
| T6 | **M** | Multiple windows, window size/position persistence, snaplayout integration, virtual-desktop behavior, and minimize-to-tray are absent. | New-window command works; window bounds restore; DPI/monitor changes are covered by tests. |
| T7 | **I** | Per-monitor DPI v2, DPI-scaled native HWND placement, title-bar drag regions, and custom frame fallback are implemented. | Preserve this behavior when adding fullscreen/multi-window support. |

### 3.2 Navigation and omnibox

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| N1 | **P** | The "suggestions" setting is persisted and displayed, but the WinUI omnibox is a plain `TextBox` with no suggestion flyout. | History and search suggestions appear, are keyboard-navigable, respect the setting, and insert/replace text correctly. |
| N2 | **P** | No Stop/cancel navigation button. `ServoViewHost::Stop()` exists but the WinUI toolbar never calls it. | Reload changes to Stop during load; Stop cancels navigation and updates status. |
| N3 | **M** | No lock icon, site-info chip, certificate details popup, or connection-security indicator. | Secure/insecure/extension-validated states are visible, clickable, and accurate; certificate errors have an interstitial. |
| N4 | **M** | No navigation error page. `OnLoadError` only logs and updates status text. | DNS/TLS/HTTP/offline failures show a useful page with retry and details. |
| N5 | **M** | No bookmark star and no `Ctrl+D` in WinUI, although the bookmarks page tells users to use them. | Add/edit/remove bookmark for the current URL; `Ctrl+D` toggles it; star reflects state. |
| N6 | **M** | No downloads button, reading list, translate, share, profile/avatar, or extensions button in the toolbar. | Core toolbar buttons are present with tooltips, keyboard access, and real panels/flows. |
| N7 | **U** | Home button exists; whether its target follows the saved `homePage` setting needs runtime verification. | Home navigates to the configured URL and the setting is editable in WinUI settings. |
| N8 | **M** | No print/PDF or "install site" UI. | `Ctrl+P` opens print preview/PDF export; installable sites expose an install affordance where supported. |

### 3.3 Find, page UI, and context menus

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| F1 | **P** | Find panel has live search, next, Shift+Enter previous, count, and Escape clear, but `matchCase` is hard-coded `false` and there is no whole-word/Previous button. | Match case, whole word, previous/next buttons, active-match highlight, and result count all work and persist per tab. |
| F2 | **P** | Find is a separate XAML grid row because XAML cannot paint over the native Servo child HWND. | Keep the constraint documented; improve visual continuity rather than pretending it is an overlay. |
| F3 | **M** | No fullscreen mode (`F11`) in WinUI. | `F11` toggles fullscreen; chrome hides/shows predictably; Escape/F11 exits. |
| F4 | **P** | Zoom shortcuts exist, but there is no visible zoom level, reset control, or zoom bubble. | Zoom level is displayed temporarily and reset is discoverable. |
| F5 | **P** | Web context menu exists but is not target-aware; link/image/media context actions are limited. | Right-click on link, image, text, selection, and page shows appropriate actions: open in new tab/window, copy link/image, save image, search selection. |
| F6 | **M** | No reading mode, translate UI, or page-action menu. | Optional but expected in a modern browser: translate prompt/action and readable view with clear entry/exit. |

### 3.4 Downloads

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| D1 | **P** | Downloads are surfaced only as status-text start/progress messages. | A downloads button opens a panel with current and recent items, progress, state, and timestamps. |
| D2 | **M** | No completion toast, "show in folder", open file, pause/resume, cancel, retry, or remove-from-list UI. | Each download item exposes the actions its state supports; opening a downloaded file works. |
| D3 | **M** | `downloadDir` is persisted and editable, but no WinUI code path was found that passes it to Servo or the download delegate. | Changing the download directory affects real downloads immediately. |
| D4 | **M** | No dangerous-file warning, mixed-content download warning, or blocked-download explanation. | Unsafe downloads are blocked or confirmed with an explicit dialog. |

### 3.5 Settings and personalization

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| S1 | **P** | Settings pane exists, but not every value is honored: backdrop choice is stored but not applied, suggestions toggle has no behavior, UA persona is startup-only. | Every setting either affects runtime state immediately or is clearly marked as requiring restart and actually applies after restart. |
| S2 | **M** | Clear browsing data only clears history. No cache/cookies/site-data/password/passwords UI. | Users can choose data types and time range; operation reports completion and is engine-wide. |
| S3 | **M** | No settings search, import/export, custom search-engine entry, or "make default browser" control. | Common settings are findable; default-browser state is shown; import/export has a clear flow. |
| S4 | **M** | No per-site permissions/content settings UI. | Users can inspect and reset permissions/cookies/data per origin. |
| S5 | **M** | Password save/autofill IPC is ignored in WinUI and `password_manager.cpp` is not compiled into the shell. | Save-password prompt, autofill suggestions, and a password-manager entry exist with secure storage. |
| S6 | **M** | No accessibility settings page. | Text scaling, reduced motion, high-contrast behavior, and screen-reader options are exposed. |
| S7 | **P** | Theme Light/Dark/System works; backdrop and accent customization are incomplete. | Theme, accent, backdrop, and chrome density selections are applied consistently. |

### 3.6 Security and privacy UX

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| P1 | **M** | Per-origin permission prompts do not exist; requests fail closed. | Camera, microphone, geolocation, notifications, and clipboard prompts show origin, permission, Allow/Deny/Block, and persist decisions. |
| P2 | **M** | HTTP auth requests are cancelled with no UI. | Basic/digest auth dialog with realm, origin, username/password, and cancel. |
| P3 | **M** | JavaScript `prompt()` returns the default value immediately. | A real modal prompt with input, OK/Cancel, and correct return value. |
| P4 | **M** | No Safe Browsing/tracking-protection warning UI or HTTPS-only mode. | Blocked/interstitial pages explain the risk and offer safe overrides where appropriate. |
| P5 | **M** | No incognito/private window or profile UI. | Private mode is visually distinct, isolated, and discoverable from the menu/keyboard. |
| P6 | **P** | Global notification/geolocation toggles exist in settings, but there is no site-level control. | Global defaults plus per-origin exceptions are manageable. |

### 3.7 History, bookmarks, and profiles

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| H1 | **I** | Generated History and Bookmarks pages exist with client-side search and reverse-chronological lists. | Preserve and wire them into keyboard shortcuts and UI buttons. |
| H2 | **P** | Bookmark data APIs exist, but there is no add/edit/delete/folders UI, no star, and no `Ctrl+D`. | Full bookmark CRUD, folders, import/export, and a bookmarks bar or manager. |
| H3 | **P** | History page has a "Clear History" link to `servo://clear-history`, but no WinUI route handling was found. | The link works or is removed; per-entry deletion and date grouping are available. |
| H4 | **M** | No profiles, sync, or recently-visited omnibox dropdown. | Profiles are switchable; sync is either implemented or explicitly out of scope; omnibox uses history safely. |

### 3.8 DevTools

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| V1 | **P** | Servo's Firefox RDP server is enabled and F12 reports the endpoint, but there is no in-window frontend. | F12 opens a usable docked or windowed DevTools UI. |
| V2 | **M** | No console, inspector, network panel, sources, performance, or device emulation UI. | Core panels open against the active tab; closing DevTools restores layout. |
| V3 | **P** | "Inspect Element" calls the generic `OpenDevTools()` status path; no element targeting was found. | Right-click → Inspect selects the clicked node in the DOM tree. |
| V4 | **M** | DevTools token is intentionally never displayed. | Keep it hidden from logs/UI; provide a secure connection flow if a frontend is added. |

### 3.9 Keyboard shortcuts and focus

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| K1 | **I** | `Ctrl+T/W/L/F/U`, `Ctrl+R`, `F5`, `F12`, `Ctrl+Shift+I`, `Ctrl+±/0`, `Alt+Left/Right`, Escape, omnibox Enter/Escape are implemented. | Keep these covered by automated or recorded smoke tests. |
| K2 | **M** | Missing `Ctrl+D`, `Ctrl+H`, `Ctrl+J`, `Ctrl+Shift+T`, `Ctrl+Tab`, `Ctrl+1..9`, `F11`, `Ctrl+,`, `Alt+D`, `Ctrl+Shift+R`, `Ctrl+P`, `Ctrl+F4`. | Implement the mainstream set or document deliberate deviations. |
| K3 | **P** | `F6` is a stub. | `F6`/`Shift+F6` cycles chrome → web content → find/settings as appropriate. |
| K4 | **U** | Shortcuts are attached to `RootGrid`; forwarding from the native child HWND to XAML `KeyDown` was not runtime-verified. | Shortcuts work consistently whether focus is on chrome, omnibox, find, or web content. |

### 3.10 Accessibility and internationalization

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| A1 | **P** | Theme resources, tooltips, per-monitor DPI, and some explicit focus calls exist. | Every chrome control has an accessible name, role, state, and keyboard behavior. |
| A2 | **M** | No `AutomationProperties`, custom AutomationPeers, UIA integration, or screen-reader announcements found. | Chrome and web content are reachable and operable with Narrator/NVDA; state changes are announced. |
| A3 | **M** | No high-contrast, reduced-motion, or text-scaling handling found. | System accessibility preferences are respected. |
| A4 | **M** | No UI localization or RTL layout work found. | Strings are resource-based; layouts mirror correctly in RTL locales. |
| A5 | **M** | AccessKit tree updates are an engine/embedder concern; no WinUI-side adapter/action forwarding was found. | See [`ACCESSIBILITY_AND_PRINTING.md`](ACCESSIBILITY_AND_PRINTING.md); expose and runtime-verify tree updates and actions. |

### 3.11 Distribution, updates, and diagnostics

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| R1 | **P** | Installer is self-signed and CI-packaged. | Production signing, MSIX/Store-ready packaging, and a documented update path exist. |
| R2 | **M** | No auto-update UX. | Update check, download/install progress, release notes, and rollback path. |
| R3 | **M** | No in-product feedback/crash-report UI. | Opt-in crash reporting with symbolication plus a feedback command. |
| R4 | **M** | No "make default browser" control. | Settings shows current default status and can invoke the OS flow. |

**Batch note (R1–R3):** the installer now declares StartMenuInternet/URL/file-association
capability metadata and creates a redacted feedback template, and CI emits a SHA256 plus
a manifest for manual verification. `UserChoice` is deliberately never written. These
are registration/scaffolding changes only: production signing, MSIX/Store packaging, a
real updater (R2) and in-product crash reporting (R3) are still **not implemented**.
The manual path and redaction rules are in
[`UPDATE_AND_FEEDBACK.md`](../../support/packaging/windows/UPDATE_AND_FEEDBACK.md).

### 3.12 Future platform scaling

| ID | Status | Gap | What "done" looks like |
| --- | --- | --- | --- |
| X1 | **E** | Android has a separate Compose UI and Servo integration under `support/android/`, with no shared chrome code. | Shared core (settings/session/history/downloads/engine bridge) is reused; platform shells stay thin. |
| X2 | **M** | WinUI chrome has no shared UI abstraction for future platforms. | Do not force WinUI onto Android; keep Windows native while extracting non-UI logic behind stable interfaces. |
| X3 | **M** | No mobile-specific patterns in WinUI (not applicable to Windows, but relevant to future shells). | Android shell handles back gesture, lifecycle, permissions, safe areas, and bottom toolbar; see [`ANDROID_EMBEDDING_GUIDE.md`](ANDROID_EMBEDDING_GUIDE.md). |

---

## 4. Known caveats that must not be misrepresented

1. **DevTools is Firefox RDP only.** There is no in-window frontend. F12 reports a port and requires an external RDP client. See `src/browser_runtime.cpp:342-345`.
2. **The DevTools token is a credential.** It is intentionally not logged or displayed. Keep it that way.
3. **Native XAML settings is authoritative in WinUI.** Generated `settings.html` is a legacy/other-shell fallback, not the WinUI settings surface. See `WinUIShell.cpp:474-482`.
4. **XAML cannot overlay the native Servo child HWND.** The find panel is a separate grid row and settings hides native views while open. Do not design UI that assumes true overlay composition.
5. **Favicons accept only raw RGBA with sufficient bytes.** Encoded ICO/PNG/SVG is skipped by the current bridge.
6. **Permissions/auth/prompt paths fail closed or are stubbed.** Do not claim real prompts until P1/P2/P3 above are implemented.
7. **Some settings require restart.** UA persona is startup-only; backdrop is stored but currently not applied at all.
8. **Session history is capped at 5**, with host-side URL fallback because Servo history is sometimes empty for host-driven loads.

---

## 5. Prioritized roadmap

### P0 — Credible daily driver

These close the gap between "impressive prototype" and "browser I can recommend".

1. **Verify and fix tab state isolation (T1).** If tabs share one WebView, give each tab a live view or an equivalent state snapshot/restore model.
2. **Wire session restore and advertised shortcuts (T2, T3, K2).** Remove or implement every shortcut/page claim.
3. **Complete omnibox basics (N1, N2, N5).** Suggestions, Stop, bookmark star with `Ctrl+D`.
4. **Build a real downloads panel (D1-D4).** Include directory setting, show-in-folder, cancel, and completion state.
5. **Add security/privacy dialogs (P1-P3).** Permissions, HTTP auth, real JS prompt.
6. **Make settings honest (S1, S2).** Every setting works or says why not; clear browsing data is engine-wide.
7. **Add error and security states (N3, N4).** Lock/site-info chip, cert/details popup, navigation error page.
8. **Complete shortcut and focus coverage (K2-K4).** Verify forwarding from web content to chrome.

**Exit criteria:** a user can browse, switch tabs without losing state, bookmark, download, find, configure settings, recover from errors, and use standard shortcuts without hitting a stub.

### P1 — Feature-rich browser

1. Fullscreen, visible zoom control, target-aware context menu (F3-F5).
2. History/bookmark managers with CRUD and import/export (H2, H3).
3. DevTools frontend with element targeting (V1-V3).
4. Print/PDF UI and site-install affordance (N8).
5. Multi-window, window bounds, snaplayout basics (T6).
6. Password manager/autofill UX with secure storage (S5).
7. Complete find options (F1).
8. Settings search, per-site permissions, default browser (S3, S4, R4).

**Exit criteria:** most actions a user expects from Chrome/Edge chrome are present, discoverable, and reversible.

### P2 — Chrome/Edge polish

1. Tab pinning, grouping, mute indicators, drag-reorder, detach, tab search, hover previews (T4).
2. Profiles, private mode, sync or explicit sync deferral (P5, H4).
3. Themes/accent/backdrop personalization that actually applies (S7).
4. Translate, reading mode, reading list, share (F6, N6).
5. Accessibility parity: AutomationProperties, UIA, high contrast, reduced motion, text scaling, localization/RTL (A1-A5).
6. Completion toast, download safety UI, update UX, crash feedback (D4, R2, R3).

### P3 — Platform scaling preparation

1. Extract shared core interfaces for settings, session, history, downloads, and engine lifecycle.
2. Keep WinUI as the Windows shell; add Android/macOS/Linux shells against the same core.
3. Reuse [`ANDROID_EMBEDDING_GUIDE.md`](ANDROID_EMBEDDING_GUIDE.md) work without making WinUI a cross-platform UI framework.

---

## 6. Definition of done for any UI/UX item

An item in this checklist is not done until all of the following are true:

- [ ] The flow works in a **CI-built, installed artifact**, not only in source.
- [ ] The feature is reachable by mouse **and** keyboard, with a clear tooltip/accessible name.
- [ ] Success, loading, empty, error, and disabled states are designed.
- [ ] Settings affecting the feature are persisted and actually applied, or clearly marked restart-required.
- [ ] State survives tab switches and app restarts where users expect it to.
- [ ] The implementation respects the native-HWND airspace constraint.
- [ ] Evidence is recorded: CI run, manual smoke steps, and screenshots/log excerpts where useful.
- [ ] No advertised shortcut, button, or setting lies about what it does.

---

## 7. Verification status of this document

| Check | State | Evidence |
| --- | --- | --- |
| CI build of prior snapshot (`3ee6c89285d`) | **PASSED** | Run `45124120503`; historical evidence only |
| Artifact install of prior snapshot | **PASSED** | `servo-windows-browser-setup` installed to `%LOCALAPPDATA%\Programs\Servo Browser` |
| Installed app launch of prior snapshot | **PASSED** | `ServoUI.exe` process observed after install/relaunch |
| CI build of **this batch** | **PENDING** | Main agent pushes after parallel agents finish; no run exists yet |
| Artifact install / launch of **this batch** | **PENDING** | Depends on the CI build above |
| Static source inventory | **PASSED** | Graphify query plus bounded source reads; implemented/partial/missing claims have file evidence above |
| Source-only CI gates (diff/XML/forbidden patterns/secrets) | **NOT_RUN** | Added in this batch; they execute on the first post-push CI run |
| Interactive UI test suite | **NOT_RUN** | No automated UI test suite exists in this flow |
| Feature-by-feature manual smoke test | **NOT_RUN** | Not performed for this document |
| Installer execution | **NOT_RUN** | PowerShell syntax review only; installer was not executed |
| Local build/tests | **NOT_RUN** | CI-only build policy |

---

## 8. Out of scope

- Web platform conformance and WPT pass rates: [`PARITY_MATRIX.md`](PARITY_MATRIX.md)
- Sandbox, process model, and security internals: [`SECURITY_AUDIT.md`](SECURITY_AUDIT.md), [`WINDOWS_SANDBOX_DESIGN.md`](WINDOWS_SANDBOX_DESIGN.md)
- iOS feasibility: [`IOS_PORT_SPIKE.md`](IOS_PORT_SPIKE.md)
- Android engine embedding details: [`ANDROID_EMBEDDING_GUIDE.md`](ANDROID_EMBEDDING_GUIDE.md)
- Extension API design: [`EXTENSIONS_DESIGN.md`](EXTENSIONS_DESIGN.md)

This is a living checklist. Update the status marks with evidence as features land; do not mark an item **I** or **P** based on intention alone.
