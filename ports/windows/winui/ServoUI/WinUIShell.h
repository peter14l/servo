/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "../../src/browser_shell.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ServoViewHost;
struct Servo;
struct ServoCreateNewWebViewRequest;

namespace winrt::ServoUI::implementation
{
    struct MainWindow;

    // Implements the engine-facing boundary for the WinUI 3 chrome. All Servo
    // WebViews are native child HWNDs of the XAML window; no GDI chrome is used.
    class WinUIShell final : public IBrowserShell
    {
    public:
        WinUIShell(MainWindow& owner, HWND rootHwnd, Servo* servo);
        ~WinUIShell() override;

        bool OpenTab(const std::string& url, ServoCreateNewWebViewRequest* request = nullptr);
        void CloseTab(uint32_t index);
        void SelectTab(uint32_t index);
        size_t TabCount() const { return m_tabs.size(); }
        uint32_t ActiveTabIndex() const { return m_active; }
        ServoViewHost* ActiveView() const;

        void SetViewportBounds(int x, int y, int width, int height);
        void SetNativeViewsVisible(bool visible);
        void Find(const std::string& text, bool forward, bool matchCase, bool wholeWord);
        void Find(const std::string& text, bool forward, bool matchCase) { Find(text, forward, matchCase, false); }
        void ClearFind();

        // Commands called by the WinUI chrome.
        void NavigateStop() override;
        void ZoomReset() override;
        void ReloadIgnoringCache() override;
        void PrintPage() override;
        void RestoreSession() override;
        void ShowDownloads() override;
        void ShowHistory() override;
        void ShowBookmarks() override;
        bool IsBookmarked(std::string const& url) const override;
        bool ToggleBookmark(std::string const& url, std::string const& title) override;
        std::vector<std::wstring> OmniboxSuggestions(std::string const& text) const override;
        std::vector<BrowserDownloadItem> Downloads() const override;
        void OpenDownload(uint64_t id) override;
        void ShowDownloadInFolder(uint64_t id) override;
        void CancelDownload(uint64_t id) override;
        void ClearDownloads() override;
        void ClearBrowsingData(int mask) override;
        std::wstring DefaultBrowserStatus() const override;
        void MakeDefaultBrowser() override;
        void SetPermissionForOrigin(std::string const& origin, int32_t permission, bool allow) override;
        void SetFullscreen(bool fullscreen) override;

        // IBrowserShell
        void OnActiveTabUrlChanged(ServoViewHost* host, const char* url) override;
        void OnActiveTabTitleChanged(ServoViewHost* host, const char* title) override;
        void OnActiveTabLoadStatusChanged(ServoViewHost* host, int32_t status) override;
        void OnActiveTabCrashed(ServoViewHost* host, const char* reason) override;
        void OnStatusTextChanged(ServoViewHost* host, const char* status) override;
        void OnTabFaviconUpdated(ServoViewHost* host, HBITMAP bitmap, uint32_t width, uint32_t height) override;
        void OnFindResult(ServoViewHost* host, int identifier, int count, int activeOrdinal) override;
        void OnNavigationError(ServoViewHost* host, int code, const char* text) override;
        void OnNavigationSecurityChanged(ServoViewHost* host, bool secure, const char* hostName) override;
        void OnZoomChanged(ServoViewHost* host, double zoom) override;
        void RequestPermission(ServoViewHost* host, const char* origin, int32_t permission,
                               void (*callback)(bool, void*), void* callbackUserData,
                               void* userData) override;
        void RequestAuthentication(ServoViewHost* view, const char* host, int32_t port,
                                   const char* realm,
                                   void (*callback)(const char*, const char*, void*),
                                   void* userData) override;
        void RequestPrompt(ServoViewHost* host, const char* message, const char* defaultValue,
                           void (*callback)(const char*, void*), void* userData) override;

        void NavigateUrl(const std::string& url) override;
        void NavigateBack() override;
        void NavigateForward() override;
        void NavigateReload() override;
        void OpenDevTools() override;
        void OpenUrlInNewTab(const std::string& url) override;
        void OpenSettings() override;
        void ZoomIn() override;
        void ZoomOut() override;

        void HandleWebIpcMessage(const std::string& message) override;
        void HandleNewDownload(ServoDownloadItem* download) override;
        void HandleDownloadUpdated(ServoDownloadItem* download) override;

        std::string GetSettingsPageUrl() override;
        bool CreatePopup(Servo* servo, ServoCreateNewWebViewRequest* request) override;
        bool UsesCompositionHosting() const override;

    private:
        size_t IndexFor(ServoViewHost* host) const;
        void ApplyViewVisibility();
        void ApplyViewportToActive();
        void LoadInActiveTab(const std::string& url);
        void ResetFindState();
        void NotifyDownloadItemsChanged();
        BrowserDownloadItem* FindDownload(uint64_t id);
        const BrowserDownloadItem* FindDownload(uint64_t id) const;
        uint64_t TrackDownload(ServoDownloadItem* download);
        static std::string Trim(const std::string& value);
        static std::string SearchUrl(const std::string& text);
        static const char* PermissionName(int32_t permission);

        MainWindow& m_owner;
        HWND m_rootHwnd = nullptr;
        Servo* m_servo = nullptr;
        std::vector<std::unique_ptr<ServoViewHost>> m_tabs;
        uint32_t m_active = 0;
        bool m_nativeViewsVisible = true;
        RECT m_viewport{};
        float m_zoom = 1.0f;
        int m_findIdentifier = -1;
        std::string m_findText;
        ServoViewHost* m_findView = nullptr;
        std::vector<BrowserDownloadItem> m_downloads;
        std::unordered_map<std::string, uint64_t> m_downloadIdsByUrl;
        uint64_t m_nextDownloadId = 1;
        bool m_fullscreen = false;
    };
}
