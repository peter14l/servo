/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <functional>
#include <string>
#include <memory>
#include <map>
#include <servo/servo_capi.h>
#include "browser_shell.h"
#include "browser_runtime.h"

// Deferred native controls (select/alert/confirm/file picker) run on
// WM_APP_RUN_UI_TASK instead of inside the Servo event-loop spin, so user
// interaction time is not charged to WM_APP_SERVO_WAKE. Both messages and the
// shared engine runtime are declared in browser_runtime.h.

class TabStrip;
class OmniboxToolbar;
class FindBar;
class ServoViewHost;

class BrowserWindow : public IBrowserShell {
public:
    static void RegisterWindowClass(HINSTANCE hInstance);
    static BrowserWindow* Create(struct Servo* servo, const std::string& initialUrl = "");
    static BrowserWindow* CreateForPopup(struct Servo* servo, struct ServoCreateNewWebViewRequest* request);

    BrowserWindow(struct Servo* servo);
    ~BrowserWindow();

    HWND GetHwnd() const { return m_hwnd; }
    struct Servo* GetServo() const { return m_servo; }
    ServoViewHost* GetViewHost() const { return m_viewHost.get(); }

    void NavigateUrl(const std::string& url) override;
    void NavigateBack() override;
    void NavigateForward() override;
    void NavigateReload() override;
    void NavigateStop();

    int FindInPage(const char* text, bool forward, bool matchCase);
    int FindNext(int identifier, bool forward);
    void ClearFind(int identifier);
    void OnFindResult(ServoViewHost* host, int identifier, int count, int activeOrdinal) override;

    void OnActiveTabSwitched(const std::string& url, const std::wstring& title);
    void OnActiveTabUrlChanged(ServoViewHost* host, const char* url) override;
    void OnActiveTabTitleChanged(ServoViewHost* host, const char* title) override;
    void OnActiveTabLoadStatusChanged(ServoViewHost* host, int32_t status) override;
    void OnActiveTabCrashed(ServoViewHost* host, const char* reason) override;
    void OnTabFaviconUpdated(ServoViewHost* host, HBITMAP hBitmap, uint32_t width, uint32_t height) override;
    void UpdateActiveTabUI(const std::string& url, const std::wstring& title);

    void OpenDevTools() override;
    void SetDevToolsInfo(uint16_t port, const std::string& token);
    void HandleNewDownload(struct ServoDownloadItem* download) override;
    void HandleDownloadUpdated(struct ServoDownloadItem* download) override;
    void ToggleDownloads();
    void ToggleCurrentPageBookmark();
    void OpenBookmarks();
    void OpenHistory();
    void OpenAbout();
    void OnStatusTextChanged(ServoViewHost* host, const char* statusText) override;
    void OpenSettings() override;
    void OpenUrlInNewTab(const std::string& url) override;
    void RestorePreviousSession();
    void ApplySettings(const struct BrowserSettings& settings);
    void HandleWebIpcMessage(const std::string& message) override;

    // IBrowserShell: internal pages and popup creation.
    std::string GetSettingsPageUrl() override;
    bool CreatePopup(struct Servo* servo, struct ServoCreateNewWebViewRequest* request) override;
    void PromptSavePassword(const std::string& origin, const std::string& username, const std::string& password);
    void RequestAutofill(const std::string& origin);
    void ToggleVerticalTabs();
    void SetVerticalTabs(bool enable);
    bool IsVerticalTabs() const { return m_verticalTabs; }
    void PrintPage();
    void ViewPageSource();

    void ZoomIn() override;
    void ZoomOut() override;
    void ZoomReset();
    void ToggleFullscreen();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void LayoutChildren();
    void ApplyMicaAltTheme();
    void RefreshDwmFrame();
    void SyncDarkMode();

    HWND m_hwnd;
    struct Servo* m_servo;

    std::unique_ptr<ServoViewHost> m_viewHost;
    std::unique_ptr<TabStrip> m_tabStrip;
    std::unique_ptr<OmniboxToolbar> m_omnibox;
    std::unique_ptr<FindBar> m_findBar;
    std::unique_ptr<class DownloadManager> m_downloadManager;

    // Servo allocates a fresh ServoDownloadItem for every callback, so map by
    // URL rather than pointer to keep progress on the correct download row.
    std::map<std::string, uint64_t> m_downloadIds;
    uint64_t m_nextDownloadId = 1;

    int m_findIdentifier = -1;

    uint16_t m_devtoolsPort;
    std::string m_devtoolsToken;
    std::string m_currentUrl;
    std::string m_currentTitle;
    HWND m_statusPillHwnd = nullptr;
    bool m_isDark;
    bool m_isFullscreen;
    bool m_verticalTabs = false;
    // Popups created by window.open() must not quit the app or overwrite the
    // main window's saved session when they are closed.
    bool m_isPopup = false;
    RECT m_topBarToggleBtnBounds = {};
    RECT m_topBarMinBtnBounds = {};
    RECT m_topBarMaxBtnBounds = {};
    RECT m_topBarCloseBtnBounds = {};
    int m_hoveredTopBarBtn = 0;
    WINDOWPLACEMENT m_savedPlacement;
};

