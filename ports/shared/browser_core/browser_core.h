/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

// Platform-neutral contracts for the non-UI parts of a Servo browser shell.
//
// This header is deliberately C++17 + standard library only. It does not
// include Windows.h, WinRT, Cocoa, GTK, JNI, or any UI toolkit header. A
// platform shell implements these interfaces and owns the platform-specific
// persistence, threading, and rendering details. See README.md in this
// directory for the scope boundaries.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace servo::browser_core {

// ---------------------------------------------------------------------------
// Settings key/value store
// ---------------------------------------------------------------------------

// Persistent settings for one browser profile. Keys are opaque UTF-8 strings;
// each shell decides how they map to its native settings surface. Values are
// stored as strings so adapters can choose their own encoding for booleans and
// numbers.
class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;

    virtual std::optional<std::string> Get(const std::string& key) const = 0;
    virtual void Set(const std::string& key, const std::string& value) = 0;
    virtual void Remove(const std::string& key) = 0;
    virtual std::vector<std::string> Keys() const = 0;
};

// ---------------------------------------------------------------------------
// Session serialization
// ---------------------------------------------------------------------------

struct SessionTab {
    std::string url;
    std::string title;
};

struct SessionState {
    std::vector<SessionTab> tabs;
    int active_tab_index = -1;
    std::vector<std::string> recently_closed_urls; // newest first
};

// Converts a session snapshot to/from an opaque payload. The payload format is
// owned by the adopting shell (JSON, plist, SQLite blob, and so on); this
// interface only fixes the C++ shape crossing the boundary.
class ISessionCodec {
public:
    virtual ~ISessionCodec() = default;

    virtual std::string Serialize(const SessionState& state) const = 0;
    virtual std::optional<SessionState> Deserialize(const std::string& payload) const = 0;
};

// ---------------------------------------------------------------------------
// History and bookmark records
// ---------------------------------------------------------------------------

struct HistoryRecord {
    std::string url;
    std::string title;
    std::uint64_t visited_at_ms = 0; // Unix epoch, UTC
    std::uint32_t visit_count = 0;
};

struct BookmarkRecord {
    std::string id;       // stable, opaque identifier
    std::string parent_id; // empty for the root folder
    std::string url;
    std::string title;
    std::uint64_t created_at_ms = 0; // Unix epoch, UTC
};

// Record storage only. Sorting, filtering, import/export, and UI presentation
// remain the responsibility of each platform shell.
class IBrowserDataStore {
public:
    virtual ~IBrowserDataStore() = default;

    virtual void AddHistory(const HistoryRecord& record) = 0;
    virtual void RemoveHistory(const std::string& url) = 0;
    virtual std::vector<HistoryRecord> GetHistory(std::size_t limit) const = 0;
    virtual void ClearHistory() = 0;

    virtual void AddBookmark(const BookmarkRecord& record) = 0;
    virtual void RemoveBookmark(const std::string& id) = 0;
    virtual std::vector<BookmarkRecord> GetBookmarks() const = 0;
};

// ---------------------------------------------------------------------------
// Download records
// ---------------------------------------------------------------------------

enum class DownloadState {
    Pending,
    InProgress,
    Paused,
    Completed,
    Failed,
    Canceled,
};

struct DownloadRecord {
    std::string id;         // stable, opaque identifier
    std::string url;
    std::string target_path; // platform-native path, carried as UTF-8 text
    std::string mime_type;
    DownloadState state = DownloadState::Pending;
    std::uint64_t received_bytes = 0;
    std::uint64_t total_bytes = 0; // 0 means unknown
    std::uint64_t started_at_ms = 0;
    std::uint64_t finished_at_ms = 0;
    std::string error;      // empty unless state is Failed
};

class IDownloadStore {
public:
    virtual ~IDownloadStore() = default;

    virtual void Upsert(const DownloadRecord& record) = 0;
    virtual std::optional<DownloadRecord> Get(const std::string& id) const = 0;
    virtual std::vector<DownloadRecord> List(std::size_t limit) const = 0;
};

// ---------------------------------------------------------------------------
// Engine lifecycle commands
// ---------------------------------------------------------------------------

struct EngineInitParams {
    std::string user_data_dir; // profile/storage root chosen by the shell
    std::string initial_url;   // empty means the shell's own new-tab page
};

enum class NavigationDisposition {
    CurrentTab,
    NewTab,
    NewWindow,
};

struct NavigateCommand {
    std::string url;
    NavigationDisposition disposition = NavigationDisposition::CurrentTab;
};

// Optional asynchronous acknowledgement. Shells that already report the same
// result through ICoreEventSink may pass no callback.
using CompletionCallback = std::function<void(bool success, const std::string& detail)>;

class IEngineController {
public:
    virtual ~IEngineController() = default;

    virtual bool Init(const EngineInitParams& params) = 0;
    virtual void Shutdown() = 0;
    virtual void Navigate(const NavigateCommand& command,
                          CompletionCallback on_complete = {}) = 0;
};

// ---------------------------------------------------------------------------
// Chrome-facing event sink
// ---------------------------------------------------------------------------

struct FaviconPixels {
    std::vector<std::uint8_t> rgba; // straight RGBA, top-to-bottom rows
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// The engine adapter pushes events here; the shell translates them into its
// native UI updates. Every method has an empty default so a shell implements
// only the events it displays.
class ICoreEventSink {
public:
    virtual ~ICoreEventSink() = default;

    virtual void OnInitialized() {}
    virtual void OnShutdown() {}

    virtual void OnNavigationStarted(const std::string& url) { (void)url; }
    virtual void OnNavigationCompleted(const std::string& url, bool success)
    {
        (void)url;
        (void)success;
    }
    virtual void OnTitleChanged(const std::string& title) { (void)title; }
    virtual void OnFaviconChanged(const std::string& url, const FaviconPixels& pixels)
    {
        (void)url;
        (void)pixels;
    }
    virtual void OnStatusTextChanged(const std::string& text) { (void)text; }
    virtual void OnDownloadChanged(const DownloadRecord& record) { (void)record; }

    // Credential-like value: the shell must never log or display the token.
    virtual void OnDevToolsEndpoint(std::uint16_t port, const std::string& token)
    {
        (void)port;
        (void)token;
    }

    virtual void OnError(const std::string& operation, const std::string& message)
    {
        (void)operation;
        (void)message;
    }
};

// ---------------------------------------------------------------------------
// Service bundle
// ---------------------------------------------------------------------------

// Non-owning references assembled by a platform shell. Lifetime and thread
// affinity are defined by that shell; no interface here starts threads.
struct BrowserCoreServices {
    ISettingsStore* settings = nullptr;
    ISessionCodec* session = nullptr;
    IBrowserDataStore* data = nullptr;
    IDownloadStore* downloads = nullptr;
    IEngineController* engine = nullptr;
    ICoreEventSink* events = nullptr;
};

} // namespace servo::browser_core
