# Shared browser core interfaces (P3 scaffolding)

This directory contains a single header-only C++17 contract, [`browser_core.h`](browser_core.h), for the **non-UI** parts of a Servo browser shell: settings, session serialization, history/bookmark records, download records, engine lifecycle commands, and a chrome-facing event sink.

It is **not a cross-platform UI framework**. It defines no windows, views, widgets, layout, theming, input handling, or event loop. There is deliberately no Windows, WinRT, Cocoa, GTK, JNI, or other toolkit header here; only standard C++ types (`std::string`, `std::vector`, `std::optional`, `std::function`, and friends).

## Shell boundaries

- **WinUI 3 remains the flagship Windows shell.** It lives in `ports/windows/winui/ServoUI/` with native XAML chrome and a native child HWND for web content. Nothing in this directory replaces or wraps that UI.
- **Android keeps its Compose shell** under `support/android/`, with Servo exposed through the `servoview` AAR. These interfaces do not introduce a shared chrome layer for Android.
- Future macOS/Linux shells would likewise own their own native chrome (AppKit/GTK or equivalent).

The only thing shared through these contracts is core state and engine-adapter plumbing. Each shell implements the interfaces with its own persistence format, threading model, and native UI bindings.

## What is defined

| Interface / type | Purpose |
| --- | --- |
| `ISettingsStore` | UTF-8 key/value settings for one profile |
| `ISessionCodec` | Serialize/deserialize an opaque session payload |
| `SessionState`, `SessionTab` | Tabs, active index, recently-closed URLs |
| `IBrowserDataStore` | History and bookmark record storage |
| `HistoryRecord`, `BookmarkRecord` | Plain data records |
| `IDownloadStore`, `DownloadRecord`, `DownloadState` | Download record storage and state |
| `IEngineController` | `Init`, `Shutdown`, `Navigate` commands with an optional `CompletionCallback` (`std::function`) |
| `ICoreEventSink` | Events a chrome displays: navigation, title, favicon, status, downloads, DevTools endpoint, errors |
| `BrowserCoreServices` | Non-owning bundle of the above, assembled by a platform shell |

## Adoption status: scaffolding only

These headers are **documentation/scaffolding only until a platform shell adopts them**. They are intentionally **not added to any `.vcxproj` or `CMakeLists.txt`**, and no existing Windows source or project file has been modified to use them. When a shell adopts an adapter, that shell's own build system must opt in explicitly.

For the Windows-first migration rationale, ownership map, and non-goals, see [`docs/embedding/WINUI_PLATFORM_SCALING.md`](../../../docs/embedding/WINUI_PLATFORM_SCALING.md). For the current WinUI gap register, see [`docs/embedding/WINUI_UIUX_COMPLETION_CHECKLIST.md`](../../../docs/embedding/WINUI_UIUX_COMPLETION_CHECKLIST.md). For the existing Android embedding path, see [`docs/embedding/ANDROID_EMBEDDING_GUIDE.md`](../../../docs/embedding/ANDROID_EMBEDDING_GUIDE.md).

All files are Mozilla Public License 2.0, consistent with the repository's C++ headers.
