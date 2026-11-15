# GEMINI.md — Developer & AI Agent Guide for Servo (Windows / WinUI 3 Port)

This guide provides operational rules, architectural facts, and constraints for Google Antigravity and Gemini CLI agents working on the Servo codebase.

---

## 1. Operating Ground Rules & Constraints

### ⚠️ The Golden Rule: No Heavy Local Builds
- **Do NOT run full Servo or Cargo engine builds locally** (`mach build`, `cargo build -p servo`, etc.).
- Compiling the complete engine (`mozjs`, `webrender`, `surfman`, 400+ workspace crates) requires **30+ GB of free disk space** and substantial CPU time. The developer environment has strict disk space limitations.
- **Build Strategy**:
  1. Local agents perform edits, update FFI bindings, edit UI components, and maintain unit tests.
  2. Full compilation, binary linking, ANGLE bundling, and installer packaging are **delegated to GitHub Actions** runners (`.github/workflows/windows-browser.yml` and `.github/workflows/capi-artifacts.yml`).
  3. Pushing changes touching `ports/windows/**` or `ffi/capi/**` automatically triggers the Windows CI workflow on GitHub Actions `windows-2022` runners.

### Knowledge Graph Integration (`graphify`)
- This repository has a knowledge graph generated at `graphify-out/` (`graph.json`, `GRAPH_REPORT.md`).
- **Rule**: When `graphify-out/graph.json` exists, run `graphify query "<question>"` before running raw file greps or searching source trees. Use `graphify path "<A>" "<B>"` for tracing relationships and `graphify explain "<concept>"` for focused architecture concepts.
- **Rule**: After modifying code, run `graphify update .` to keep the graph current (AST-only, zero API cost).
- A committed `graphify-out/graph.json` is rewritten whole on every update; large diffs in PRs to this file are normal.

---

## 2. Codebase Architecture & Subsystems

```
┌──────────────────────────────────────────────────────────────────┐
│  ports/windows/src/ (Native Windows 11 Shell)                    │
│  - BrowserWindow: Win32 frame, Mica Alt, theme synchronization   │
│  - ServoViewHost: Child HWND host for WebView viewport           │
│  - TabStrip: Edge-to-edge tab view in non-client caption area    │
│  - OmniboxToolbar: Navigation controls and URL/search normalizer │
├──────────────────────────────────────────────────────────────────┤
│  ffi/capi/ (servo-capi C ABI)                                    │
│  - lib.rs, webview.rs, rendering_context.rs                      │
│  - Exports servo_capi.dll, servo_capi.lib, servo_capi.h          │
├──────────────────────────────────────────────────────────────────┤
│  components/servo/ (Servo Core Engine API)                       │
│  - webview.rs: High-level WebView object & delegation            │
│  - servo.rs: Servo engine lifecycle and event loop               │
├──────────────────────────────────────────────────────────────────┤
│  components/paint/ & components/shared/paint/                    │
│  - paint.rs, painter.rs: WebRender rendering                     │
│  - rendering_context.rs: surfman-managed OpenGL / GLES / D3D11   │
└──────────────────────────────────────────────────────────────────┘
```

### Critical Subsystem Details

1. **`ffi/capi/` (`servo-capi`)**:
   - C ABI bridge providing `servo_builder_*`, `servo_options_*`, `servo_rendering_context_*`, and `servo_webview_*`.
   - Exported symbols are tracked in `ffi/capi/symbols.txt` (must remain strictly sorted alphabetically).
   - C++ RAII helpers live in `ffi/capi/servo_capi.hpp`.
   - C# P/Invoke bindings live in `ports/windows/csharp/ServoCapi.cs`.

2. **`ports/windows/` (Native Browser)**:
   - Built via CMake (`ports/windows/CMakeLists.txt`).
   - Fast, standalone unit test suite located at `ports/windows/tests/test_browser_unit.cpp` (mock headers in `tests/mock_servo/`).
   - Packaging scripts in `ports/windows/packaging/` (`sign_and_package.ps1`, `install.ps1`).

---

## 3. Important Architectural Invariants

### 1. Paint and Present Separation
- `servo_webview_paint(webview)` renders the WebRender draw commands into the internal backbuffer managed by `surfman`.
- `servo_webview_present(webview)` (or `servo_rendering_context_present(context)`) **must be called immediately after paint** in `WM_PAINT` to swap the backbuffer to the physical screen. Without calling `present()`, the window displays a blank white pane.

### 2. Omnibox & Search Normalization
- Servo enforces WHATWG URL parsing via Rust's `url::Url::parse`. Raw query strings with unencoded spaces cause `Url::parse` to fail with `ServoError::InvalidState` (`-3`).
- Search queries must always be percent-encoded (`UrlEncode`) before calling `servo_webview_load`.
- Targeted search endpoint is `https://duckduckgo.com/html/?q=%s` (static server-side rendered HTML), matching the official `servoshell` configuration.

### 3. Native Win32 Window Handles
- Prefer using `servo_rendering_context_create_windows_hwnd(hwnd, hinstance, width, height)` instead of constructing fake C `raw_window_handle` structs. This ensures Rust constructs `RawWindowHandle::Win32` using exact rustc compiler layout.
- `servo_webview_builder_create` and `servo_create_new_webview_builder_create` take ownership of the `RenderingContext` pointer (`Box::from_raw`). Embedders must null out their local context pointer after passing to avoid dangling pointers or double frees.

### 4. SpiderMonkey GC Heap & Memory Guardrails
- **Heap Limit Range**: `js_mem_max` is validated in `script_runtime.rs` via `in_range(pref!(js_mem_max), 1, 0x1000)`. Setting values above 4096 MB causes the check to return `None` and fall back to `u32::MAX` (unbounded 4 GB heap).
- **Session History Cache**: `session_history_max_length` defaults to 20 full document cache entries. For SPAs like Instagram, set this preference to `5` in `main.cpp` to prevent multi-GB RAM retention of stale DOM trees and pipeline closures.

### 5. Contenteditable & Rich Editor Invariants (Lexical / Draft.js)
- **Mouse Focus**: Modern rich text editors call `event.preventDefault()` on `mousedown` to manage selection programmatically. In `document_event_handler.rs`, mouse focus and selection collapse steps must never be suppressed when `node.is_editable_or_editing_host()` is true, even if `result` was canceled.
- **InputEvent.data**: Rich editors inspect `event.data` on `beforeinput` (e.g. `insertText`). In `execcommands.rs`, always populate `InputEvent::new` with `Some(value.clone())` instead of `None` (`null` in JS), or editors will drop user input.

### 6. Reverse Flexbox (`column-reverse`) Scroll Origins
- Chat message histories (Instagram DMs, Discord, Slack) use `display: flex; flex-direction: column-reverse;`.
- According to CSSOM View, the scroll origin of `column-reverse` is at the physical bottom (newest items). In `components/layout/style_ext.rs`, check `Display::from(self.get_box().display)` for `DisplayInside::Flex` and invert `downward` for `ColumnReverse` so initial views anchor to latest messages.

### 7. Color Emoji Rendering on Windows
- WebRender's DirectWrite backend (`dwrote`) rasterizes glyphs via `IDWriteGlyphRunAnalysis::CreateAlphaTexture`, which produces only 1-channel alpha masks. It does not translate `COLR`/`CPAL` sub-layers in `Segoe UI Emoji`, causing emojis to render as white/black wireframe outlines tinted by CSS `color`.
- Prioritize embedded color bitmap strike fonts (`Twemoji Mozilla`, `Noto Color Emoji`, `EmojiOne Color`) ahead of `Segoe UI Emoji` in `components/fonts/platform/windows/font_list.rs` fallback chains so WebRender renders full RGBA color via `FontInstanceFlags::EMBEDDED_BITMAPS`.

### 8. YouTube WebComponents & ShadyCSS Invariants
- YouTube's `custom-elements-es5-adapter.js` and Polymer components check `if (window.ShadyCSS)` and invoke `window.ShadyCSS.prepareTemplate(template, name)` and `window.ShadyCSS.styleElement(element)`. When `window.ShadyCSS` is defined without these methods, custom elements throw `TypeError` during construction, aborting `<ytd-video-renderer>` creation and causing YouTube search results to render as a blank canvas.
- A getter/setter trap on `window.ShadyCSS` in `servo_view_host.cpp` guarantees that `prepareTemplate`, `styleElement`, `styleSubtree`, and related methods remain valid no-op functions.

### 9. Instagram DM Scope Isolation & Dummy Video Ticker
- **Click & Keyboard Hooks**: Click listeners intended to focus the message composer must NEVER match `[role="main"]`. In Instagram, the entire feed lives under `<main role="main">`, so querying `[contenteditable="true"]` within `[role="main"]` steals clicks from all feed posts, stories, reels, and video controls. Scope click hooks strictly to `location.pathname.startsWith('/direct/')` and target the actual composer container (`[data-lexical-editor="true"]`).
- **MutationObserver Layout Thrashing**: Querying `scrollHeight` and `clientHeight` across every mutated node on the main feed forces synchronous reflows and freezes the browser. Restrict DOM observation strictly to `/direct/t/` chat threads.
- **Video Playback Ticker**: In `components/media/backends/dummy/lib.rs`, the player must emit periodic `PositionChanged` events (advancing `currentTime`) and `VideoFrameUpdated` events so web players (Instagram, YouTube) do not consider media stalled or unrendered. Provide a `MediaSource` stub returning `isTypeSupported: () => false` so players cleanly fallback to progressive MP4.

---

## 4. Useful Commands

```powershell
# Knowledge graph query
graphify query "ServoViewHost BrowserWindow"
graphify query "servo_webview_present"

# Update knowledge graph after code edits
graphify update .

# Git status check
git status --short

# Push changes to trigger GitHub Actions build
git push origin embedding/cef-parity
```
