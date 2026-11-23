/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "../../src/browser_shell.h"
#include <cstdint>
#include <memory>
#include <string>
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
        void Find(const std::string& text, bool forward, bool matchCase);
        void ClearFind();

        // IBrowserShell
        void OnActiveTabUrlChanged(ServoViewHost* host, const char* url) override;
        void OnActiveTabTitleChanged(ServoViewHost* host, const char* title) override;
        void OnActiveTabLoadStatusChanged(ServoViewHost* host, int32_t status) override;
        void OnActiveTabCrashed(ServoViewHost* host, const char* reason) override;
        void OnStatusTextChanged(ServoViewHost* host, const char* status) override;
        void OnTabFaviconUpdated(ServoViewHost* host, HBITMAP bitmap, uint32_t width, uint32_t height) override;
        void OnFindResult(ServoViewHost* host, int identifier, int count, int activeOrdinal) override;

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
        static std::string Trim(const std::string& value);
        static std::string SearchUrl(const std::string& text);

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
    };
}
