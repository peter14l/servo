/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <servo/servo_capi.h>

class ServoViewHost;

// Shared download record. Both the legacy GDI shell and the WinUI shell own a
// list of these; keeping the shape here avoids two divergent download models.
struct BrowserDownloadItem {
    uint64_t id = 0;
    std::string url;
    std::string fileName;
    std::string path;
    int64_t received = 0;
    int64_t total = 0;
    int32_t state = 0; // 0 active, 1 complete, 2 canceled, 3 failed
};

// Categories accepted by IBrowserShell::ClearBrowsingData and by the shared
// BrowserSettings::ClearBrowsingData helper. Keep the bit values stable: the
// WinUI shell parses the same numbers from generated-page routes.
enum BrowserClearDataMask {
    BrowserClearHistory = 1,
    BrowserClearCookies = 2,
    BrowserClearCache = 4,
    BrowserClearSiteData = 8,
    BrowserClearPasswords = 16
};

// Boundary between the Servo engine host and whatever chrome hosts it.
// The legacy GDI BrowserWindow and the WinUI 3 shell both implement this so
// ServoViewHost can stay shared and engine-only.
class IBrowserShell {
public:
    virtual ~IBrowserShell() = default;

    // Navigation / tab state reported by the WebView.
    virtual void OnActiveTabUrlChanged(ServoViewHost* host, const char* url) = 0;
    virtual void OnActiveTabTitleChanged(ServoViewHost* host, const char* title) = 0;
    virtual void OnActiveTabLoadStatusChanged(ServoViewHost* host, int32_t status) = 0;
    virtual void OnActiveTabCrashed(ServoViewHost* host, const char* reason) = 0;
    virtual void OnStatusTextChanged(ServoViewHost* host, const char* status) = 0;
    virtual void OnTabFaviconUpdated(ServoViewHost* host, HBITMAP bitmap, uint32_t width, uint32_t height) = 0;
    virtual void OnFindResult(ServoViewHost* host, int identifier, int count, int activeOrdinal) = 0;

    // Commands issued from the page or the view's context menu.
    virtual void NavigateUrl(const std::string& url) = 0;
    virtual void NavigateBack() = 0;
    virtual void NavigateForward() = 0;
    virtual void NavigateReload() = 0;
    virtual void OpenDevTools() = 0;
    virtual void OpenUrlInNewTab(const std::string& url) = 0;
    virtual void OpenSettings() = 0;
    virtual void ZoomIn() = 0;
    virtual void ZoomOut() = 0;

    // Services the engine asks the browser to perform.
    virtual void HandleWebIpcMessage(const std::string& message) = 0;
    virtual void HandleNewDownload(struct ServoDownloadItem* download) = 0;
    virtual void HandleDownloadUpdated(struct ServoDownloadItem* download) = 0;

    // Internal pages and popups are supplied by the shell so the engine host
    // does not need to know how the chrome is built.
    virtual std::string GetSettingsPageUrl() = 0;
    virtual bool CreatePopup(struct Servo* servo, struct ServoCreateNewWebViewRequest* request) = 0;

    // Shells that render chrome through DirectComposition need their native
    // child windows layered; otherwise the composition surface covers them.
    virtual bool UsesCompositionHosting() const { return false; }

    // Optional P0-P3 surface. These default to no-ops / fail-closed values so
    // the legacy GDI shell keeps compiling and never pretends a feature exists.
    virtual void OnNavigationError(ServoViewHost*, int, const char*) {}
    virtual void OnNavigationSecurityChanged(ServoViewHost*, bool, const char*) {}
    virtual void OnZoomChanged(ServoViewHost*, double) {}

    virtual void NavigateStop() {}
    virtual void ReloadIgnoringCache() {}
    virtual void ZoomReset() {}
    virtual void PrintPage() {}
    virtual void SetFullscreen(bool) {}
    virtual void RestoreSession() {}

    virtual void ShowDownloads() {}
    virtual void ShowHistory() {}
    virtual void ShowBookmarks() {}
    virtual bool IsBookmarked(const std::string&) const { return false; }
    virtual bool ToggleBookmark(const std::string&, const std::string&) { return false; }
    virtual std::vector<std::wstring> OmniboxSuggestions(const std::string&) const { return {}; }

    virtual std::vector<BrowserDownloadItem> Downloads() const { return {}; }
    virtual void OpenDownload(uint64_t) {}
    virtual void ShowDownloadInFolder(uint64_t) {}
    virtual void CancelDownload(uint64_t) {}
    virtual void ClearDownloads() {}
    virtual void ClearBrowsingData(int) {}

    virtual std::wstring DefaultBrowserStatus() const { return L""; }
    virtual void MakeDefaultBrowser() {}
    virtual void SetPermissionForOrigin(const std::string&, int32_t, bool) {}

    // Modality requests fail closed: a denied permission, empty credentials
    // and the JavaScript prompt's default value. Wrapping shells must override
    // these to present native UI; the engine must never block on a no-op.
    virtual void RequestPermission(ServoViewHost*, const char*, int32_t,
                                   void (*callback)(bool, void*), void* callbackUserData,
                                   void*) {
        if (callback) callback(false, callbackUserData);
    }
    virtual void RequestAuthentication(ServoViewHost*, const char*, int32_t, const char*,
                                       void (*callback)(const char*, const char*, void*),
                                       void* userData) {
        if (callback) callback("", "", userData);
    }
    virtual void RequestPrompt(ServoViewHost*, const char*, const char* defaultValue,
                               void (*callback)(const char*, void*), void* userData) {
        if (callback) callback(defaultValue ? defaultValue : "", userData);
    }
};
