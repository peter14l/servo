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
