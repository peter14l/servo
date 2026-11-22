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
#include <servo/servo_capi.h>

class ServoViewHost;

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
};
