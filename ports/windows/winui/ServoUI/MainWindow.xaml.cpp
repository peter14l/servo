/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "pch.h"
#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "WinUIShell.h"

#include "../../src/browser_runtime.h"
#include "../../src/browser_settings.h"
#include "../../src/browser_data.h"
#include "../../src/servo_view_host.h"

// Local includes only: pch.h is owned by another workstream and must not be
// changed here. These headers back the new settings persistence and clipboard
// commands without adding a package dependency.
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;

namespace winrt::ServoUI::implementation
{
    namespace
    {
        namespace muxc = Microsoft::UI::Xaml::Controls;

        bool KeyDown(int virtualKey)
        {
            return (GetKeyState(virtualKey) & 0x8000) != 0;
        }

        std::wstring Wide(std::string const& value)
        {
            if (value.empty()) return {};
            int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
            if (length <= 1) return {};
            std::wstring result(static_cast<size_t>(length - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &result[0], length);
            return result;
        }

        std::wstring WideText(hstring const& value)
        {
            std::wstring_view view(value);
            return std::wstring(view);
        }

        std::string Utf8(hstring const& value)
        {
            std::wstring_view view(value);
            if (view.empty()) return {};
            int length = WideCharToMultiByte(CP_UTF8, 0, view.data(), -1, nullptr, 0, nullptr, nullptr);
            if (length <= 1) return {};
            std::string result(static_cast<size_t>(length - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, view.data(), -1, &result[0], length, nullptr, nullptr);
            return result;
        }

        bool JsonBool(std::string const& json, const char* key, bool fallback)
        {
            std::string quoted = std::string("\"") + key + "\":";
            size_t pos = json.find(quoted);
            if (pos == std::string::npos) {
                quoted = std::string(key) + "=";
                pos = json.find(quoted);
            }
            if (pos == std::string::npos) return fallback;
            size_t start = pos + quoted.size();
            while (start < json.size() && (json[start] == ' ' || json[start] == '\"')) ++start;
            if (json.compare(start, 4, "true") == 0 || json.compare(start, 1, "1") == 0) return true;
            if (json.compare(start, 5, "false") == 0 || json.compare(start, 1, "0") == 0) return false;
            return fallback;
        }

        std::wstring JsonString(std::string const& json, const char* key, std::wstring const& fallback)
        {
            std::string quoted = std::string("\"") + key + "\":\"";
            size_t pos = json.find(quoted);
            if (pos == std::string::npos) return fallback;
            size_t start = pos + quoted.size();
            size_t end = json.find('\"', start);
            if (end == std::string::npos) return fallback;
            return Wide(json.substr(start, end - start));
        }

        int SearchEngineIndex(std::wstring const& value)
        {
            if (value == L"Google") return 1;
            if (value == L"Bing") return 2;
            if (value == L"Ecosia") return 3;
            return 0;
        }

        std::wstring SearchEngineForIndex(int index)
        {
            switch (index) {
            case 1: return L"Google";
            case 2: return L"Bing";
            case 3: return L"Ecosia";
            default: return L"DuckDuckGo";
            }
        }

        std::wstring UserAgentForIndex(int index)
        {
            switch (index) {
            case 1: return L"Chrome 131 (Desktop)";
            case 2: return L"Safari 18 (macOS)";
            default: return L"Firefox 132 (Desktop) [Recommended]";
            }
        }

        int UserAgentIndex(std::wstring const& value)
        {
            if (value.find(L"Chrome") != std::wstring::npos) return 1;
            if (value.find(L"Safari") != std::wstring::npos) return 2;
            return 0;
        }

        // ServoPermissionType ids accepted by SetPermissionForOrigin.
        // -1 means WinUIShell only supplied a vague label ("media" or "this
        // site"), so no precise per-site rule can be persisted from the prompt.
        int32_t PermissionKind(std::wstring const& name)
        {
            std::wstring lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::tolower(c)); });
            if (lower == L"geolocation") return 0;
            if (lower == L"notifications") return 1;
            if (lower == L"camera") return 4;
            if (lower == L"microphone") return 5;
            return -1;
        }

        std::string UrlEncode(std::string const& value)
        {
            static const char* hex = "0123456789ABCDEF";
            std::string result;
            result.reserve(value.size() * 3);
            for (unsigned char c : value) {
                bool unreserved = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
                if (unreserved) {
                    result.push_back(static_cast<char>(c));
                } else {
                    result.push_back('%');
                    result.push_back(hex[(c >> 4) & 0x0F]);
                    result.push_back(hex[c & 0x0F]);
                }
            }
            return result;
        }

        std::wstring HostFromUrl(std::string const& url)
        {
            size_t scheme = url.find("://");
            size_t start = scheme == std::string::npos ? 0 : scheme + 3;
            size_t end = url.find_first_of("/?#", start);
            std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
            size_t at = host.find('@');
            if (at != std::string::npos) host.erase(0, at + 1);
            size_t port = host.rfind(':');
            if (port != std::string::npos && host.find(']') == std::string::npos) host.resize(port);
            return Wide(host);
        }

        bool IsLikelyUrlOrHost(std::string const& text)
        {
            if (text.empty() || text.find(' ') != std::string::npos) return false;
            std::string lower = text;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("://") != std::string::npos) return true;
            return lower.find('.') != std::string::npos || lower.rfind("about:", 0) == 0 ||
                   lower.rfind("data:", 0) == 0 || lower.rfind("file:", 0) == 0 ||
                   lower.rfind("servo:", 0) == 0 || lower.rfind("view-source:", 0) == 0;
        }

        std::wstring FormatBytes(int64_t bytes)
        {
            if (bytes < 0) return L"";
            if (bytes < 1024) return std::to_wstring(bytes) + L" B";
            if (bytes < 1024 * 1024) return std::to_wstring(bytes / 1024) + L" KB";
            if (bytes < 1024LL * 1024 * 1024) return std::to_wstring(bytes / (1024 * 1024)) + L" MB";
            return std::to_wstring(bytes / (1024LL * 1024 * 1024)) + L" GB";
        }

        bool ToggleOn(Microsoft::UI::Xaml::Controls::Primitives::ToggleButton const& button)
        {
            auto value = button.IsChecked();
            return value != nullptr && value.Value();
        }

        bool SwitchOn(Microsoft::UI::Xaml::Controls::ToggleSwitch const& toggle)
        {
            return toggle.IsOn();
        }

        std::wstring ReadLocalSetting(std::wstring const& key, std::wstring const& fallback)
        {
            try {
                auto values = Windows::Storage::ApplicationData::Current().LocalSettings().Values();
                hstring name(key);
                if (values.HasKey(name)) return WideText(unbox_value<hstring>(values.Lookup(name)));
            } catch (...) {
            }
            return fallback;
        }

        void WriteLocalSetting(std::wstring const& key, std::wstring const& value)
        {
            try {
                auto values = Windows::Storage::ApplicationData::Current().LocalSettings().Values();
                values.Insert(hstring(key), box_value(hstring(value)));
            } catch (...) {
            }
        }

        void ApplyBrowserSettings(BrowserSettings const& settings)
        {
            SaveBrowserSettings(settings);
            Servo* servo = BrowserRuntime::Instance().GetServo();
            if (!servo) return;
            servo_set_preference_bool(servo, "dom_webgl2_enabled", settings.enableWebGL2);
            servo_set_preference_bool(servo, "dom_webgpu_enabled", settings.enableWebGPU);
            servo_set_preference_bool(servo, "dom_indexeddb_enabled", settings.enableIndexedDB);
            servo_set_preference_bool(servo, "dom_webrtc_enabled", settings.enableWebRTC);
            servo_set_preference_bool(servo, "dom_webrtc_transceiver_enabled", settings.enableWebRTC);
            servo_set_preference_bool(servo, "dom_notification_enabled", settings.enableNotifications);
            servo_set_preference_bool(servo, "dom_geolocation_enabled", settings.enableGeolocation);
            servo_set_preference_bool(servo, "layout_variable_fonts_enabled", settings.enableVariableFonts);
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
    }

    MainWindow::~MainWindow()
    {
        try {
            if (m_rootLayoutUpdatedToken.value != 0) {
                RootGrid().LayoutUpdated(m_rootLayoutUpdatedToken);
                m_rootLayoutUpdatedToken = {};
            }
        } catch (...) {
        }
        m_shell.reset();
    }

    void MainWindow::ShutdownEngine()
    {
        if (m_engineShutdown) return;
        m_engineShutdown = true;

        // WebViews must die before Servo: their destructors send CloseWebView.
        m_shell.reset();
        BrowserRuntime::Instance().Shutdown();
    }

    HWND MainWindow::GetXamlHwnd()
    {
        if (m_xamlHwnd && IsWindow(m_xamlHwnd)) return m_xamlHwnd;
        try {
            winrt::ServoUI::MainWindow thisWindow = *this;
            winrt::com_ptr<IWindowNative> native = thisWindow.as<IWindowNative>();
            HWND hwnd = nullptr;
            if (native) {
                native->get_WindowHandle(&hwnd);
            }
            m_xamlHwnd = hwnd;
        } catch (...) {
            m_xamlHwnd = nullptr;
        }
        return m_xamlHwnd;
    }

    void MainWindow::EnsureShell()
    {
        if (m_shell) return;
        HWND hwnd = GetXamlHwnd();
        Servo* servo = BrowserRuntime::Instance().GetServo();
        if (!hwnd || !servo) {
            LogDebug("WinUI shell cannot start: hwnd=0x%p servo=0x%p", hwnd, servo);
            return;
        }
        m_shell = std::make_unique<WinUIShell>(*this, hwnd, servo);
    }

    void MainWindow::ConfigureTitleBar()
    {
        if (!m_xamlHwnd) return;
        try {
            auto windowId = Microsoft::UI::GetWindowIdFromWindow(m_xamlHwnd);
            auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
            try {
                wchar_t modulePath[MAX_PATH] = {};
                DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
                if (length > 0 && length < MAX_PATH) {
                    std::wstring iconPath(modulePath);
                    size_t separator = iconPath.find_last_of(L"\\/");
                    if (separator != std::wstring::npos) iconPath.resize(separator + 1);
                    iconPath += L"app_logo.ico";
                    if (GetFileAttributesW(iconPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        appWindow.SetIcon(winrt::hstring(iconPath));
                    }
                }
            } catch (...) {
                LogDebug("Could not apply the taskbar icon; using the embedded executable icon.");
            }
            auto titleBar = appWindow.TitleBar();
            if (!Microsoft::UI::Windowing::AppWindowTitleBar::IsCustomizationSupported()) return;
            titleBar.ExtendsContentIntoTitleBar(true);
            titleBar.ButtonBackgroundColor(Windows::UI::Colors::Transparent());
            titleBar.ButtonInactiveBackgroundColor(Windows::UI::Colors::Transparent());
            bool lightTheme = RootGrid().ActualTheme() == Microsoft::UI::Xaml::ElementTheme::Light;
            auto glyphColor = lightTheme ? Windows::UI::Colors::Black() : Windows::UI::Colors::White();
            titleBar.ButtonHoverBackgroundColor(lightTheme
                ? Windows::UI::ColorHelper::FromArgb(20, 0, 0, 0)
                : Windows::UI::ColorHelper::FromArgb(32, 255, 255, 255));
            titleBar.ButtonPressedBackgroundColor(lightTheme
                ? Windows::UI::ColorHelper::FromArgb(36, 0, 0, 0)
                : Windows::UI::ColorHelper::FromArgb(48, 255, 255, 255));
            titleBar.ButtonForegroundColor(glyphColor);
            titleBar.ButtonInactiveForegroundColor(lightTheme
                ? Windows::UI::ColorHelper::FromArgb(120, 0, 0, 0)
                : Windows::UI::ColorHelper::FromArgb(160, 255, 255, 255));

            // ExtendsContentIntoTitleBar means our XAML occupies the same pixels
            // as the system caption buttons. Reserve their measured inset so the
            // new-tab button and drag region never sit underneath Close.
            double rightInset = titleBar.RightInset();
            Microsoft::UI::Xaml::Thickness titlePadding{};
            titlePadding.Left = 8.0;
            titlePadding.Top = 0.0;
            titlePadding.Right = rightInset;
            titlePadding.Bottom = 0.0;
            TitleBarArea().Padding(titlePadding);
            UpdateDragRegions();
        } catch (...) {
            LogDebug("Custom title bar setup failed; falling back to the system title bar.");
        }
    }

    void MainWindow::UpdateDragRegions()
    {
        if (!m_xamlHwnd || !m_loaded) return;
        try {
            auto windowId = Microsoft::UI::GetWindowIdFromWindow(m_xamlHwnd);
            auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
            auto titleBar = appWindow.TitleBar();
            if (!Microsoft::UI::Windowing::AppWindowTitleBar::IsCustomizationSupported() ||
                !titleBar.ExtendsContentIntoTitleBar()) {
                return;
            }

            float scale = 1.0f;
            UINT dpi = GetDpiForWindow(m_xamlHwnd);
            if (dpi > 0) scale = static_cast<float>(dpi) / 96.0f;

            // The caption-button inset can change with DPI or window size, so
            // keep the reserved padding current on every drag-region refresh.
            auto padding = TitleBarArea().Padding();
            double rightInset = titleBar.RightInset();
            if (std::abs(padding.Right - rightInset) > 0.5) {
                Microsoft::UI::Xaml::Thickness updated = padding;
                updated.Right = rightInset;
                TitleBarArea().Padding(updated);
                padding = updated;
            }

            // Only the empty part of the title bar is a drag region. The tabs
            // toggle, tab strip, new-tab button, and caption buttons stay
            // interactive.
            double titleBarWidth = TitleBarArea().ActualWidth();
            if (titleBarWidth <= 0) return;
            double toggleWidth = VerticalTabsToggle().ActualWidth();
            double newTabButtonWidth = AddTabButton().ActualWidth();
            double available = titleBarWidth - padding.Left - padding.Right -
                               toggleWidth - newTabButtonWidth - 12.0;

            int x = static_cast<int>(std::lround((padding.Left + toggleWidth + 6.0) * scale));
            int width = static_cast<int>(std::lround(std::max(0.0, available) * scale));
            int height = static_cast<int>(std::lround(TitleBarArea().ActualHeight() * scale));
            if (width <= 0 || height <= 0) return;

            Windows::Graphics::RectInt32 dragRect{};
            dragRect.X = x;
            dragRect.Y = 0;
            dragRect.Width = width;
            dragRect.Height = height;
            std::vector<Windows::Graphics::RectInt32> rects{ dragRect };
            titleBar.SetDragRectangles(rects);
        } catch (...) {
            // A failed drag-region update must never break the browser UI.
        }
    }

    void MainWindow::LayoutViewport()
    {
        if (!m_shell || !m_loaded || !m_xamlHwnd) return;

        try {
            auto transform = ViewportArea().TransformToVisual(RootGrid());
            auto point = transform.TransformPoint(Windows::Foundation::Point{ 0.0f, 0.0f });
            double width = ViewportArea().ActualWidth();
            double height = ViewportArea().ActualHeight();
            if (width <= 0 || height <= 0) return;

            float scale = 1.0f;
            UINT dpi = GetDpiForWindow(m_xamlHwnd);
            if (dpi > 0) scale = static_cast<float>(dpi) / 96.0f;

            int x = static_cast<int>(std::lround(point.X * scale));
            int y = static_cast<int>(std::lround(point.Y * scale));
            int cx = static_cast<int>(std::lround(width * scale));
            int cy = static_cast<int>(std::lround(height * scale));
            m_shell->SetViewportBounds(x, y, cx, cy);
            UpdateDragRegions();
        } catch (...) {
            LogDebug("WinUI viewport layout failed.");
        }
    }

    void MainWindow::ActivateAsInitial(hstring const& url)
    {
        m_initialUrl = Utf8(url);
        Activate();
        if (m_loaded) {
            EnsureShell();
            if (m_shell->TabCount() == 0) AddTab(m_initialUrl);
        }
    }

    void MainWindow::ActivateWithRequest(ServoCreateNewWebViewRequest* request)
    {
        if (!request) return;
        Activate();
        if (m_loaded) {
            AddTab(std::string(), request);
        } else {
            m_pendingRequest = request;
        }
    }

    bool MainWindow::OpenPopupTab(ServoCreateNewWebViewRequest* request)
    {
        if (!request) return false;
        if (!m_loaded) {
            m_pendingRequest = request;
            return true;
        }
        EnsureShell();
        if (!m_shell) return false;
        AddTab(std::string(), request);
        return true;
    }

    void MainWindow::OpenUrlInNewTabFromShell(std::string const& url)
    {
        AddTab(url);
    }

    void MainWindow::OpenSettingsFromShell()
    {
        OpenSettingsPane();
    }

    void MainWindow::AddTab(std::string url, ServoCreateNewWebViewRequest* request)
    {
        EnsureShell();
        if (!m_shell) {
            UpdateStatus(L"Servo engine is unavailable.");
            return;
        }
        if (!m_shell->OpenTab(url, request)) {
            UpdateStatus(L"Failed to create a new tab.");
            return;
        }

        ServoViewHost* view = m_shell->ActiveView();
        std::string actualUrl = request ? std::string() : (view ? view->GetUrl() : url);
        std::wstring title = request ? L"Popup" : (actualUrl.empty() ? L"New tab" : Wide(actualUrl));

        auto item = muxc::TabViewItem{};
        const uint32_t newIndex = static_cast<uint32_t>(m_tabFaviconSources.size());
        m_tabFaviconSources.emplace_back();
        item.Header(CreateTabHeader(newIndex, title));
        item.MinWidth(120);
        item.MaxWidth(260);
        item.IsClosable(true);
        item.ContextFlyout(RootGrid().Resources().Lookup(box_value(L"TabContextMenu")).as<muxc::MenuFlyout>());
        item.ContextRequested([this](auto const& sender, auto const&) {
            m_contextTabIndex = TabIndexFromContextMenu(sender);
        });

        m_updatingTabs = true;
        Tabs().TabItems().Append(item);
        Tabs().SelectedItem(item);
        m_updatingTabs = false;

        m_tabUrls.push_back(actualUrl);
        m_tabTitles.push_back(title);
        m_tabPinned.push_back(false);
        m_tabMuted.push_back(false);
        m_activeTab = static_cast<uint32_t>(m_tabUrls.size() - 1);
        RefreshVerticalTabs();
        LayoutViewport();
        UpdateActiveTab(m_activeTab);
        UpdateBookmarkState();
    }

    void MainWindow::CloseTab(uint32_t index)
    {
        if (index >= m_tabUrls.size()) return;
        if (!m_shell) return;

        m_shell->CloseTab(index);
        if (!m_tabUrls[index].empty()) {
            m_recentlyClosedUrls.push_back(m_tabUrls[index]);
            if (m_recentlyClosedUrls.size() > 32) m_recentlyClosedUrls.erase(m_recentlyClosedUrls.begin());
        }
        auto items = Tabs().TabItems();
        if (index < items.Size()) items.RemoveAt(index);
        m_tabUrls.erase(m_tabUrls.begin() + static_cast<ptrdiff_t>(index));
        m_tabTitles.erase(m_tabTitles.begin() + static_cast<ptrdiff_t>(index));
        if (index < m_tabPinned.size()) m_tabPinned.erase(m_tabPinned.begin() + static_cast<ptrdiff_t>(index));
        if (index < m_tabMuted.size()) m_tabMuted.erase(m_tabMuted.begin() + static_cast<ptrdiff_t>(index));
        if (index < m_tabFaviconSources.size()) {
            m_tabFaviconSources.erase(m_tabFaviconSources.begin() + static_cast<ptrdiff_t>(index));
        }

        uint32_t active = m_shell->ActiveTabIndex();
        m_updatingTabs = true;
        if (items.Size() > 0 && active < items.Size()) {
            Tabs().SelectedIndex(static_cast<int32_t>(active));
        }
        m_updatingTabs = false;
        m_activeTab = active;
        RefreshVerticalTabs();
        LayoutViewport();
        UpdateActiveTab(active);
    }

    void MainWindow::SelectTab(uint32_t index)
    {
        if (index >= m_tabUrls.size() || !m_shell) return;
        m_shell->SelectTab(index);
        m_activeTab = index;
        RefreshVerticalTabs();
        LayoutViewport();
        UpdateActiveTab(index);
    }

    void MainWindow::UpdateActiveTab(uint32_t index)
    {
        if (index >= m_tabUrls.size()) return;
        m_activeTab = index;
        Omnibox().Text(hstring(Wide(m_tabUrls[index])));
        std::wstring title = m_tabTitles[index];
        Title(hstring(title.empty() ? L"Servo" : title + L" - Servo"));
        UpdateStatus(L"");
        UpdateNavigationButtons();
        UpdateBookmarkState();
        ErrorBanner().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        HideSuggestions();
        SecurityInfoPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void MainWindow::UpdateStatus(std::wstring const& status)
    {
        try {
            StatusText().Text(hstring(status));
        } catch (...) {
        }
    }

    void MainWindow::UpdateNavigationButtons()
    {
        try {
            ServoViewHost* view = m_shell ? m_shell->ActiveView() : nullptr;
            BackButton().IsEnabled(view && view->CanGoBack());
            ForwardButton().IsEnabled(view && view->CanGoForward());
        } catch (...) {
        }
    }

    void MainWindow::SetStopReloadState(bool loading)
    {
        m_loading = loading;
        try {
            auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;
            auto visible = Microsoft::UI::Xaml::Visibility::Visible;
            ReloadButton().Visibility(loading ? collapsed : visible);
            StopButton().Visibility(loading ? visible : collapsed);
        } catch (...) {
        }
    }

    void MainWindow::UpdateBookmarkState()
    {
        try {
            if (!m_shell || m_activeTab >= m_tabUrls.size() || m_tabUrls[m_activeTab].empty()) return;
            bool bookmarked = m_shell->IsBookmarked(m_tabUrls[m_activeTab]);
            auto visible = Microsoft::UI::Xaml::Visibility::Visible;
            auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;
            BookmarkOutlinePath().Visibility(bookmarked ? collapsed : visible);
            BookmarkFilledPath().Visibility(bookmarked ? visible : collapsed);
        } catch (...) {
        }
    }

    Windows::Foundation::IInspectable MainWindow::CreateTabHeader(
        uint32_t index, std::wstring const& title)
    {
        auto panel = muxc::StackPanel{};
        panel.Orientation(muxc::Orientation::Horizontal);
        panel.Spacing(8);

        if (index < m_tabFaviconSources.size() && m_tabFaviconSources[index]) {
            auto icon = muxc::Image{};
            icon.Width(16);
            icon.Height(16);
            icon.Source(m_tabFaviconSources[index]);
            panel.Children().Append(icon);
        }

        std::wstring decorated = title.empty() ? L"New tab" : title;
        if (index < m_tabPinned.size() && m_tabPinned[index]) decorated = L"[P] " + decorated;
        if (index < m_tabMuted.size() && m_tabMuted[index]) decorated = L"[M] " + decorated;

        auto label = muxc::TextBlock{};
        label.Text(hstring(decorated));
        panel.Children().Append(label);
        return panel;
    }

    void MainWindow::SetTabHeader(uint32_t index, std::wstring const& header)
    {
        if (index >= m_tabTitles.size()) return;
        try {
            auto items = Tabs().TabItems();
            if (index >= items.Size()) return;
            auto item = items.GetAt(index).as<muxc::TabViewItem>();
            item.Header(CreateTabHeader(index, header));
            RefreshVerticalTabs();
        } catch (...) {
        }
    }

    void MainWindow::LoadInActiveTab(std::string const& url)
    {
        if (!m_shell) return;
        m_shell->NavigateUrl(url);
    }

    void MainWindow::OpenSettingsPane()
    {
        PopulateSettingsPane();
        SettingsStatus().Text(hstring(L""));
        // XAML overlays cannot draw above the native Servo child HWND.
        if (m_shell) m_shell->SetNativeViewsVisible(false);
        SettingsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
    }

    void MainWindow::PopulateSettingsPane()
    {
        BrowserSettings settings = LoadBrowserSettings();
        SettingSearchEngine().SelectedIndex(SearchEngineIndex(settings.searchEngine));
        if (settings.searchEngine == L"Custom") SettingSearchEngine().SelectedIndex(4);
        SettingHomePage().Text(hstring(settings.homePage));
        SettingDownloadDir().Text(hstring(settings.downloadDir));
        SettingTheme().SelectedIndex(settings.themeMode >= 0 && settings.themeMode <= 2 ? settings.themeMode : 2);
        SettingBackdrop().SelectedIndex(settings.backdropType >= 0 && settings.backdropType <= 2 ? settings.backdropType : 0);
        SettingVerticalTabs().IsOn(settings.enableVerticalTabs);
        SettingSuggestions().IsOn(settings.enableSuggestions);
        SettingUserAgent().SelectedIndex(UserAgentIndex(settings.userAgentPersona));
        SettingNotifications().IsOn(settings.enableNotifications);
        SettingGeolocation().IsOn(settings.enableGeolocation);
        SettingWebGL2().IsOn(settings.enableWebGL2);
        SettingWebGPU().IsOn(settings.enableWebGPU);
        SettingIndexedDB().IsOn(settings.enableIndexedDB);
        SettingWebRTC().IsOn(settings.enableWebRTC);
        SettingVariableFonts().IsOn(settings.enableVariableFonts);

        bool custom = SettingSearchEngine().SelectedIndex() == 4;
        SettingCustomSearch().Visibility(custom ? Microsoft::UI::Xaml::Visibility::Visible
                                                : Microsoft::UI::Xaml::Visibility::Collapsed);
        CustomSearchNote().Visibility(custom ? Microsoft::UI::Xaml::Visibility::Visible
                                             : Microsoft::UI::Xaml::Visibility::Collapsed);
        // Prefer the shared BrowserSettings value; the WinRT local setting is a
        // compatibility fallback for profiles written by older builds.
        m_customSearchTemplate = settings.customSearchEngine.empty()
            ? ReadLocalSetting(L"CustomSearchTemplate", L"")
            : settings.customSearchEngine;
        SettingCustomSearch().Text(hstring(m_customSearchTemplate));

        int scale = settings.textScale > 0 ? settings.textScale : 100;
        if (scale == 100) {
            try {
                scale = std::clamp(std::stoi(ReadLocalSetting(L"TextScale", L"100")), 100, 150);
            } catch (...) {
                scale = 100;
            }
        }
        scale = std::clamp(scale, 100, 150);
        SettingTextScale().Value(static_cast<double>(scale));
        TextScaleValue().Text(hstring(std::to_wstring(scale) + L"%"));
        SettingReduceMotion().IsOn(settings.reduceMotion || ReadLocalSetting(L"ReduceMotion", L"0") == L"1");

        std::wstring origin = m_activeTab < m_tabUrls.size()
            ? HostFromUrl(m_tabUrls[m_activeTab]) : std::wstring();
        PermissionOrigin().Text(hstring(origin.empty() ? L"No active site" : origin));
        RefreshDefaultBrowserStatus();
    }

    void MainWindow::ApplySettingsFromPane()
    {
        BrowserSettings settings = LoadBrowserSettings();
        const int engineIndex = SettingSearchEngine().SelectedIndex();
        settings.searchEngine = engineIndex == 4 ? std::wstring(L"Custom") : SearchEngineForIndex(engineIndex);
        m_customSearchTemplate = WideText(SettingCustomSearch().Text());
        settings.customSearchEngine = m_customSearchTemplate;
        settings.textScale = std::clamp(static_cast<int>(SettingTextScale().Value()), 100, 150);
        settings.reduceMotion = SwitchOn(SettingReduceMotion());
        // Keep the WinRT copy in sync for UI code that still reads it directly.
        WriteLocalSetting(L"CustomSearchTemplate", m_customSearchTemplate);
        WriteLocalSetting(L"TextScale", std::to_wstring(settings.textScale));
        WriteLocalSetting(L"ReduceMotion", settings.reduceMotion ? L"1" : L"0");
        settings.homePage = WideText(SettingHomePage().Text());
        if (settings.homePage.empty()) settings.homePage = L"https://servo.org";
        auto downloadDir = WideText(SettingDownloadDir().Text());
        if (!downloadDir.empty()) settings.downloadDir = downloadDir;
        settings.themeMode = SettingTheme().SelectedIndex();
        settings.backdropType = SettingBackdrop().SelectedIndex();
        settings.enableVerticalTabs = SettingVerticalTabs().IsOn();
        settings.enableSuggestions = SettingSuggestions().IsOn();
        settings.userAgentPersona = UserAgentForIndex(SettingUserAgent().SelectedIndex());
        settings.enableNotifications = SettingNotifications().IsOn();
        settings.enableGeolocation = SettingGeolocation().IsOn();
        settings.enableWebGL2 = SettingWebGL2().IsOn();
        settings.enableWebGPU = SettingWebGPU().IsOn();
        settings.enableIndexedDB = SettingIndexedDB().IsOn();
        settings.enableWebRTC = SettingWebRTC().IsOn();
        settings.enableVariableFonts = SettingVariableFonts().IsOn();

        ApplyBrowserSettings(settings);
        ApplyTheme(settings.themeMode);
        ApplyVerticalTabs(settings.enableVerticalTabs);
        UpdateStatus(L"Settings saved.");
    }

    void MainWindow::ApplyTheme(int themeMode)
    {
        using Microsoft::UI::Xaml::ElementTheme;
        ElementTheme theme = ElementTheme::Default;
        if (themeMode == 0) theme = ElementTheme::Light;
        else if (themeMode == 1) theme = ElementTheme::Dark;
        RootGrid().RequestedTheme(theme);
    }

    void MainWindow::ApplyVerticalTabs(bool enable)
    {
        try {
            BrowserSettings settings = LoadBrowserSettings();
            const int width = std::clamp(settings.verticalTabsWidth, 100, 420);

            VerticalTabsPanel().Visibility(enable ? Microsoft::UI::Xaml::Visibility::Visible
                                                  : Microsoft::UI::Xaml::Visibility::Collapsed);
            VerticalTabsSplitter().Visibility(enable ? Microsoft::UI::Xaml::Visibility::Visible
                                                     : Microsoft::UI::Xaml::Visibility::Collapsed);
            VerticalTabsColumn().MinWidth(enable ? 100.0 : 0.0);
            VerticalTabsColumn().MaxWidth(enable ? 420.0 : 0.0);

            Microsoft::UI::Xaml::GridLength railWidth{};
            railWidth.Value = enable ? static_cast<double>(width) : 0.0;
            railWidth.GridUnitType = Microsoft::UI::Xaml::GridUnitType::Pixel;
            VerticalTabsColumn().Width(railWidth);

            Microsoft::UI::Xaml::GridLength splitterWidth{};
            splitterWidth.Value = 1.0;
            splitterWidth.GridUnitType = enable ? Microsoft::UI::Xaml::GridUnitType::Auto
                                                : Microsoft::UI::Xaml::GridUnitType::Pixel;
            VerticalTabsSplitterColumn().Width(splitterWidth);

            Tabs().Visibility(enable ? Microsoft::UI::Xaml::Visibility::Collapsed
                                     : Microsoft::UI::Xaml::Visibility::Visible);
            RefreshVerticalTabs();
            LayoutViewport();
            UpdateDragRegions();
        } catch (...) {
            LogDebug("Could not apply vertical tab layout.");
        }
    }

    void MainWindow::SetVerticalTabsColumnWidth(int width)
    {
        const int clamped = std::clamp(width, 100, 420);
        Microsoft::UI::Xaml::GridLength railWidth{};
        railWidth.Value = static_cast<double>(clamped);
        railWidth.GridUnitType = Microsoft::UI::Xaml::GridUnitType::Pixel;
        VerticalTabsColumn().Width(railWidth);
        LayoutViewport();
    }

    void MainWindow::PersistVerticalTabsWidth()
    {
        const int width = std::clamp(
            static_cast<int>(std::lround(VerticalTabsColumn().Width().Value)), 100, 420);
        BrowserSettings settings = LoadBrowserSettings();
        settings.verticalTabsWidth = width;
        SaveBrowserSettings(settings);
    }

    void MainWindow::VerticalTabsSplitterPointerPressed(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        try {
            m_resizingVerticalTabs = true;
            m_resizePointerId = args.Pointer().PointerId();
            m_resizeStartX = args.GetCurrentPoint(RootGrid()).Position().X;
            m_resizeStartWidth = static_cast<int>(std::lround(VerticalTabsColumn().Width().Value));
            VerticalTabsSplitter().CapturePointer(args.Pointer());
            args.Handled(true);
        } catch (...) {
            m_resizingVerticalTabs = false;
        }
    }

    void MainWindow::VerticalTabsSplitterPointerMoved(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!m_resizingVerticalTabs || args.Pointer().PointerId() != m_resizePointerId) return;
        try {
            const double currentX = args.GetCurrentPoint(RootGrid()).Position().X;
            const int delta = static_cast<int>(std::lround(currentX - m_resizeStartX));
            SetVerticalTabsColumnWidth(m_resizeStartWidth + delta);
            args.Handled(true);
        } catch (...) {
        }
    }

    void MainWindow::VerticalTabsSplitterPointerReleased(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!m_resizingVerticalTabs || args.Pointer().PointerId() != m_resizePointerId) return;
        try {
            VerticalTabsSplitter().ReleasePointerCapture(args.Pointer());
            PersistVerticalTabsWidth();
            args.Handled(true);
        } catch (...) {
        }
        m_resizingVerticalTabs = false;
        m_resizePointerId = 0;
    }

    void MainWindow::RefreshVerticalTabs()
    {
        try {
            m_updatingTabs = true;
            auto items = VerticalTabsList().Items();
            items.Clear();
            for (size_t i = 0; i < m_tabTitles.size(); ++i) {
                auto item = muxc::ListViewItem{};
                item.Content(CreateTabHeader(static_cast<uint32_t>(i), m_tabTitles[i]));
                item.ContextFlyout(RootGrid().Resources().Lookup(box_value(L"TabContextMenu")).as<muxc::MenuFlyout>());
                item.ContextRequested([this](auto const& sender, auto const&) {
                    m_contextTabIndex = TabIndexFromContextMenu(sender);
                });
                items.Append(item);
            }
            if (!m_tabTitles.empty() && m_activeTab < m_tabTitles.size()) {
                VerticalTabsList().SelectedIndex(static_cast<int32_t>(m_activeTab));
            }
            m_updatingTabs = false;
        } catch (...) {
            m_updatingTabs = false;
        }
    }

    void MainWindow::ClearHistoryClicked(IInspectable const&, RoutedEventArgs const&)
    {
        BrowserData::ClearHistory();
        UpdateStatus(L"Browsing history cleared.");
    }

    void MainWindow::ResetSettingsClicked(IInspectable const&, RoutedEventArgs const&)
    {
        BrowserSettings defaults;
        ApplyBrowserSettings(defaults);
        PopulateSettingsPane();
        ApplyTheme(defaults.themeMode);
        ApplyVerticalTabs(defaults.enableVerticalTabs);
        SettingsStatus().Text(hstring(L"Defaults restored"));
        UpdateStatus(L"Default settings restored.");
    }

    void MainWindow::RootLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        m_loaded = true;
        m_xamlHwnd = GetXamlHwnd();
        m_customSearchTemplate = ReadLocalSetting(L"CustomSearchTemplate", L"");
        BrowserSettings settings = LoadBrowserSettings();
        ApplyTheme(settings.themeMode);
        ConfigureTitleBar();
        ApplyVerticalTabs(settings.enableVerticalTabs);
        EnsureShell();

        if (m_pendingRequest) {
            ServoCreateNewWebViewRequest* request = m_pendingRequest;
            m_pendingRequest = nullptr;
            AddTab(std::string(), request);
        } else if (m_shell && m_shell->TabCount() == 0) {
            AddTab(m_initialUrl);
        }
        LayoutViewport();

        // XAML reports the final viewport geometry one layout pass after Loaded.
        // Reapply it once so the native child HWND does not stay blank until a
        // resize forces a repaint.
        if (m_rootLayoutUpdatedToken.value == 0) {
            m_rootLayoutUpdatedToken = RootGrid().LayoutUpdated(
                [this](IInspectable const&, IInspectable const&) {
                    LayoutViewport();
                    if (m_rootLayoutUpdatedToken.value != 0) {
                        try {
                            RootGrid().LayoutUpdated(m_rootLayoutUpdatedToken);
                        } catch (...) {
                        }
                        m_rootLayoutUpdatedToken = {};
                    }
                });
        }
    }

    void MainWindow::RootKeyDown(IInspectable const&, KeyRoutedEventArgs const& args)
    {
        using Windows::System::VirtualKey;

        auto key = args.Key();
        bool ctrl = KeyDown(VK_CONTROL);
        bool alt = KeyDown(VK_MENU);
        bool shift = KeyDown(VK_SHIFT);
        auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;

        if (key == VirtualKey::Escape) {
            bool consumed = false;
            if (SuggestionsPanel().Visibility() != collapsed) {
                HideSuggestions();
                consumed = true;
            }
            if (m_fullscreen) {
                ToggleFullscreen();
                consumed = true;
            }
            if (DownloadsPanel().Visibility() != collapsed) {
                DownloadsPanel().Visibility(collapsed);
                LayoutViewport();
                consumed = true;
            }
            if (TabSearchPanel().Visibility() != collapsed) {
                TabSearchPanel().Visibility(collapsed);
                LayoutViewport();
                consumed = true;
            }
            if (FindPanel().Visibility() != collapsed) {
                FindCloseClicked(nullptr, RoutedEventArgs{});
                consumed = true;
            }
            if (SettingsPanel().Visibility() != collapsed) {
                SettingsPanel().Visibility(collapsed);
                if (m_shell) m_shell->SetNativeViewsVisible(true);
                ApplySettingsFromPane();
                consumed = true;
            }
            if (!consumed && m_loading && m_shell) {
                m_shell->NavigateStop();
                consumed = true;
            }
            if (consumed) args.Handled(true);
            return;
        }

        if (ctrl && shift && key == VirtualKey::T) {
            RestoreRecentlyClosedTab();
            args.Handled(true);
        } else if (ctrl && shift && key == VirtualKey::R) {
            if (m_shell) m_shell->ReloadIgnoringCache();
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::T) {
            AddTab(std::string());
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::W || (ctrl && key == VirtualKey::F4)) {
            if (!m_tabUrls.empty()) CloseTab(m_activeTab);
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::Tab) {
            if (!m_tabUrls.empty()) {
                int next = (static_cast<int>(m_activeTab) + (shift ? -1 : 1) + static_cast<int>(m_tabUrls.size()))
                    % static_cast<int>(m_tabUrls.size());
                SelectTab(static_cast<uint32_t>(next));
            }
            args.Handled(true);
        } else if (ctrl && key >= VirtualKey::Number1 && key <= VirtualKey::Number9) {
            int digit = static_cast<int>(key) - static_cast<int>(VirtualKey::Number0);
            if (digit > 0 && static_cast<size_t>(digit) <= m_tabUrls.size()) {
                SelectTab(static_cast<uint32_t>(digit - 1));
            }
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::D) {
            BookmarkClicked(nullptr, RoutedEventArgs{});
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::H) {
            LoadInActiveTab("servo://history");
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::J) {
            DownloadsClicked(nullptr, RoutedEventArgs{});
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::P) {
            if (m_shell) m_shell->PrintPage();
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::L || (ctrl && key == VirtualKey::E) || (alt && key == VirtualKey::D)) {
            Omnibox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
            Omnibox().SelectAll();
            args.Handled(true);
        } else if (ctrl && static_cast<int32_t>(key) == 0xBC) { // Ctrl+, (VK_OEM_COMMA)
            OpenSettingsPane();
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::F) {
            FindOpenClicked(nullptr, RoutedEventArgs{});
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::U) {
            if (m_shell && m_shell->ActiveView()) {
                m_shell->ActiveView()->ViewPageSource();
            }
            args.Handled(true);
        } else if (key == VirtualKey::F12 || (ctrl && shift && key == VirtualKey::I)) {
            if (m_shell) m_shell->OpenDevTools();
            args.Handled(true);
        } else if (key == VirtualKey::F11) {
            ToggleFullscreen();
            args.Handled(true);
        } else if (key == VirtualKey::F6) {
            CycleFocus(!shift);
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::R) {
            if (m_shell) m_shell->NavigateReload();
            args.Handled(true);
        } else if (key == VirtualKey::F5) {
            if (m_shell) m_shell->NavigateReload();
            args.Handled(true);
        } else if (ctrl && (key == VirtualKey::Add || KeyDown(VK_OEM_PLUS))) {
            if (m_shell) m_shell->ZoomIn();
            args.Handled(true);
        } else if (ctrl && (key == VirtualKey::Subtract || KeyDown(VK_OEM_MINUS))) {
            if (m_shell) m_shell->ZoomOut();
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::Number0) {
            if (m_shell) m_shell->ZoomReset();
            args.Handled(true);
        } else if (alt && key == VirtualKey::Left) {
            if (m_shell) m_shell->NavigateBack();
            args.Handled(true);
        } else if (alt && key == VirtualKey::Right) {
            if (m_shell) m_shell->NavigateForward();
            args.Handled(true);
        }
    }

    void MainWindow::ViewportSizeChanged(IInspectable const&, SizeChangedEventArgs const&)
    {
        LayoutViewport();
    }

    void MainWindow::OmniboxKeyDown(IInspectable const&, KeyRoutedEventArgs const& args)
    {
        using Windows::System::VirtualKey;
        auto visible = Microsoft::UI::Xaml::Visibility::Visible;

        if (args.Key() == VirtualKey::Down && SuggestionsPanel().Visibility() == visible &&
            !m_suggestions.empty()) {
            m_suggestionIndex = std::min(m_suggestionIndex + 1, static_cast<int>(m_suggestions.size()) - 1);
            SuggestionsList().SelectedIndex(m_suggestionIndex);
            args.Handled(true);
        } else if (args.Key() == VirtualKey::Up && SuggestionsPanel().Visibility() == visible &&
                   !m_suggestions.empty()) {
            m_suggestionIndex = std::max(m_suggestionIndex - 1, 0);
            SuggestionsList().SelectedIndex(m_suggestionIndex);
            args.Handled(true);
        } else if (args.Key() == VirtualKey::Escape) {
            if (SuggestionsPanel().Visibility() == visible) {
                HideSuggestions();
            } else if (m_activeTab < m_tabUrls.size()) {
                m_updatingOmnibox = true;
                Omnibox().Text(hstring(Wide(m_tabUrls[m_activeTab])));
                m_updatingOmnibox = false;
            }
            args.Handled(true);
        } else if (args.Key() == VirtualKey::Enter) {
            std::wstring chosen = WideText(Omnibox().Text());
            if (SuggestionsPanel().Visibility() == visible && m_suggestionIndex >= 0 &&
                static_cast<size_t>(m_suggestionIndex) < m_suggestions.size()) {
                chosen = m_suggestions[static_cast<size_t>(m_suggestionIndex)];
            }
            HideSuggestions();
            if (!chosen.empty()) SubmitOmnibox(chosen);
            args.Handled(true);
        }
    }

    void MainWindow::OmniboxTextChanged(IInspectable const&,
                                        Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
    {
        if (!m_updatingOmnibox) PopulateSuggestions();
    }

    void MainWindow::HideSuggestions()
    {
        try {
            if (SuggestionsPanel().Visibility() == Microsoft::UI::Xaml::Visibility::Collapsed) return;
            SuggestionsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            LayoutViewport();
        } catch (...) {
        }
        m_suggestions.clear();
        m_suggestionIndex = -1;
    }

    void MainWindow::PopulateSuggestions()
    {
        std::vector<std::wstring> suggestions;
        try {
            BrowserSettings settings = LoadBrowserSettings();
            std::string text = to_string(Omnibox().Text());
            if (settings.enableSuggestions && m_shell && !text.empty()) {
                suggestions = m_shell->OmniboxSuggestions(text);
            }
        } catch (...) {
        }

        bool wasVisible = SuggestionsPanel().Visibility() == Microsoft::UI::Xaml::Visibility::Visible;
        m_suggestions = std::move(suggestions);
        m_suggestionIndex = -1;
        try {
            auto items = SuggestionsList().Items();
            items.Clear();
            for (auto const& value : m_suggestions) {
                auto block = muxc::TextBlock{};
                block.Text(hstring(value));
                items.Append(block);
            }
            bool show = !m_suggestions.empty();
            SuggestionsPanel().Visibility(show ? Microsoft::UI::Xaml::Visibility::Visible
                                               : Microsoft::UI::Xaml::Visibility::Collapsed);
            if (show != wasVisible) LayoutViewport();
        } catch (...) {
        }
    }

    void MainWindow::SuggestionSelectionChanged(IInspectable const&,
                                                muxc::SelectionChangedEventArgs const&)
    {
        try {
            int index = SuggestionsList().SelectedIndex();
            if (index < 0 || static_cast<size_t>(index) >= m_suggestions.size()) return;
            m_suggestionIndex = index;
            m_updatingOmnibox = true;
            Omnibox().Text(hstring(m_suggestions[static_cast<size_t>(index)]));
            m_updatingOmnibox = false;
            Omnibox().SelectAll();
        } catch (...) {
        }
    }

    void MainWindow::SubmitOmnibox(std::wstring const& text)
    {
        std::string utf8 = Utf8(text);
        BrowserSettings settings = LoadBrowserSettings();
        if (settings.searchEngine == L"Custom" && !IsLikelyUrlOrHost(utf8) &&
            !m_customSearchTemplate.empty()) {
            std::string query = UrlEncode(utf8);
            std::string narrow = Utf8(m_customSearchTemplate);
            size_t placeholder = narrow.find("{q}");
            std::string target = placeholder == std::string::npos
                ? narrow + query
                : narrow.substr(0, placeholder) + query + narrow.substr(placeholder + 3);
            LoadInActiveTab(target);
            return;
        }
        LoadInActiveTab(utf8);
    }

    void MainWindow::BackClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_shell) m_shell->NavigateBack();
    }

    void MainWindow::ForwardClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_shell) m_shell->NavigateForward();
    }

    void MainWindow::ReloadClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_shell) m_shell->NavigateReload();
    }

    void MainWindow::StopClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_shell) m_shell->NavigateStop();
    }

    void MainWindow::SecurityChipClicked(IInspectable const&, RoutedEventArgs const&)
    {
        auto visible = Microsoft::UI::Xaml::Visibility::Visible;
        auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;
        bool show = SecurityInfoPanel().Visibility() != visible;
        SecurityInfoPanel().Visibility(show ? visible : collapsed);
        LayoutViewport();
    }

    void MainWindow::SecurityInfoCloseClicked(IInspectable const&, RoutedEventArgs const&)
    {
        SecurityInfoPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        LayoutViewport();
    }

    void MainWindow::BookmarkClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_shell || m_activeTab >= m_tabUrls.size()) return;
        std::string url = m_tabUrls[m_activeTab];
        if (url.empty()) {
            UpdateStatus(L"Nothing to bookmark on this tab.");
            return;
        }
        std::wstring title = m_tabTitles[m_activeTab];
        bool nowBookmarked = m_shell->ToggleBookmark(url, Utf8(title));
        UpdateBookmarkState();
        UpdateStatus(nowBookmarked ? L"Bookmark added." : L"Bookmark removed.");
    }

    void MainWindow::DownloadsClicked(IInspectable const&, RoutedEventArgs const&)
    {
        auto visible = Microsoft::UI::Xaml::Visibility::Visible;
        if (DownloadsPanel().Visibility() == visible) {
            DownloadsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        } else {
            HideSuggestions();
            TabSearchPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            PopulateDownloads();
            DownloadsPanel().Visibility(visible);
        }
        LayoutViewport();
    }

    void MainWindow::DownloadsCloseClicked(IInspectable const&, RoutedEventArgs const&)
    {
        DownloadsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        LayoutViewport();
    }

    void MainWindow::ClearDownloadsClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_shell) return;
        m_shell->ClearDownloads();
        PopulateDownloads();
        UpdateStatus(L"Download list cleared.");
    }

    void MainWindow::ErrorRetryClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ErrorBanner().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        LayoutViewport();
        if (m_shell) m_shell->NavigateReload();
    }

    void MainWindow::ErrorDismissClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ErrorBanner().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        LayoutViewport();
    }

    void MainWindow::ZoomResetClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_shell) m_shell->ZoomReset();
        ZoomBubble().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        m_zoomTimer.Stop();
        LayoutViewport();
    }

    void MainWindow::ZoomBubbleCloseClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ZoomBubble().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        m_zoomTimer.Stop();
        LayoutViewport();
    }

    void MainWindow::FullscreenClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ToggleFullscreen();
    }

    void MainWindow::HomeClicked(IInspectable const&, RoutedEventArgs const&)
    {
        BrowserSettings settings = LoadBrowserSettings();
        LoadInActiveTab(to_string(hstring(settings.homePage)));
    }

    void MainWindow::AddTabClicked(IInspectable const&, RoutedEventArgs const&)
    {
        AddTab(std::string());
    }

    void MainWindow::VerticalTabsToggleClicked(IInspectable const&, RoutedEventArgs const&)
    {
        BrowserSettings settings = LoadBrowserSettings();
        settings.enableVerticalTabs = !settings.enableVerticalTabs;
        ApplyBrowserSettings(settings);
        ApplyVerticalTabs(settings.enableVerticalTabs);
        SettingVerticalTabs().IsOn(settings.enableVerticalTabs);
        UpdateStatus(settings.enableVerticalTabs ? L"Vertical tabs on." : L"Vertical tabs off.");
    }

    void MainWindow::VerticalTabsSelectionChanged(IInspectable const&,
                                                  muxc::SelectionChangedEventArgs const&)
    {
        if (m_updatingTabs) return;
        int32_t index = VerticalTabsList().SelectedIndex();
        if (index >= 0) SelectTab(static_cast<uint32_t>(index));
    }

    void MainWindow::SettingsClicked(IInspectable const&, RoutedEventArgs const&)
    {
        OpenSettingsPane();
    }

    void MainWindow::TabsSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_updatingTabs) return;
        int32_t index = Tabs().SelectedIndex();
        if (index >= 0 && static_cast<uint32_t>(index) < m_tabUrls.size()) {
            SelectTab(static_cast<uint32_t>(index));
        }
    }

    void MainWindow::TabsTabCloseRequested(muxc::TabView const&, muxc::TabViewTabCloseRequestedEventArgs const& args)
    {
        try {
            auto tab = args.Tab();
            auto items = Tabs().TabItems();
            for (uint32_t i = 0; i < items.Size(); ++i) {
                auto candidate = items.GetAt(i).as<muxc::TabViewItem>();
                if (candidate == tab) {
                    CloseTab(i);
                    break;
                }
            }
        } catch (...) {
        }
    }

    uint32_t MainWindow::TabIndexFromContextMenu(IInspectable const& sender)
    {
        try {
            auto horizontal = Tabs().TabItems();
            for (uint32_t i = 0; i < horizontal.Size(); ++i) {
                if (horizontal.GetAt(i) == sender) return i;
            }
            auto vertical = VerticalTabsList().Items();
            for (uint32_t i = 0; i < vertical.Size(); ++i) {
                if (vertical.GetAt(i) == sender) return i;
            }
        } catch (...) {
        }
        return m_contextTabIndex < m_tabUrls.size() ? m_contextTabIndex : m_activeTab;
    }

    void MainWindow::TabContextRequested(IInspectable const& sender,
                                         Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        m_contextTabIndex = index < m_tabUrls.size() ? index : m_activeTab;
    }

    void MainWindow::TabContextCloseClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index < m_tabUrls.size()) CloseTab(index);
    }

    void MainWindow::TabContextCloseLeftClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index >= m_tabUrls.size()) return;
        for (int i = static_cast<int>(index) - 1; i >= 0; --i) {
            CloseTab(static_cast<uint32_t>(i));
        }
    }

    void MainWindow::TabContextPinClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index >= m_tabPinned.size()) return;
        m_tabPinned[index] = !m_tabPinned[index];
        SetTabHeader(index, m_tabTitles[index]);
        UpdateStatus(m_tabPinned[index] ? L"Tab pinned." : L"Tab unpinned.");
    }

    void MainWindow::TabContextMuteClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index >= m_tabMuted.size()) return;
        m_tabMuted[index] = !m_tabMuted[index];
        SetTabHeader(index, m_tabTitles[index]);
        UpdateStatus(m_tabMuted[index] ? L"Tab muted." : L"Tab unmuted.");
    }

    void MainWindow::TabContextCloseOthersClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t keep = TabIndexFromContextMenu(sender);
        if (keep >= m_tabUrls.size()) return;
        for (int i = static_cast<int>(m_tabUrls.size()) - 1; i >= 0; --i) {
            if (static_cast<uint32_t>(i) != keep) CloseTab(static_cast<uint32_t>(i));
        }
    }

    void MainWindow::TabContextCloseRightClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index >= m_tabUrls.size()) return;
        for (int i = static_cast<int>(m_tabUrls.size()) - 1; i > static_cast<int>(index); --i) {
            CloseTab(static_cast<uint32_t>(i));
        }
    }

    void MainWindow::TabContextDuplicateClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index < m_tabUrls.size()) AddTab(m_tabUrls[index]);
    }

    void MainWindow::TabContextReloadClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index >= m_tabUrls.size()) return;
        SelectTab(index);
        if (m_shell) m_shell->NavigateReload();
    }

    void MainWindow::TabContextReloadIgnoringCacheClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index >= m_tabUrls.size()) return;
        SelectTab(index);
        if (m_shell) m_shell->ReloadIgnoringCache();
    }

    void MainWindow::TabContextCopyUrlClicked(IInspectable const& sender, RoutedEventArgs const&)
    {
        uint32_t index = TabIndexFromContextMenu(sender);
        if (index >= m_tabUrls.size()) return;
        try {
            Windows::ApplicationModel::DataTransfer::Clipboard::SetText(hstring(Wide(m_tabUrls[index])));
            UpdateStatus(L"URL copied to clipboard.");
        } catch (...) {
            UpdateStatus(L"Could not copy the URL.");
        }
    }

    void MainWindow::TabContextRestoreClosedClicked(IInspectable const&, RoutedEventArgs const&)
    {
        RestoreRecentlyClosedTab();
    }

    void MainWindow::TabContextTabSearchClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ShowTabSearch();
    }

    void MainWindow::AppRestoreTabClicked(IInspectable const&, RoutedEventArgs const&)
    {
        RestoreRecentlyClosedTab();
    }

    void MainWindow::AppTabSearchClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ShowTabSearch();
    }

    void MainWindow::AppHistoryClicked(IInspectable const&, RoutedEventArgs const&)
    {
        LoadInActiveTab("servo://history");
    }

    void MainWindow::AppBookmarksClicked(IInspectable const&, RoutedEventArgs const&)
    {
        LoadInActiveTab("servo://bookmarks");
    }

    void MainWindow::AppDownloadsClicked(IInspectable const&, RoutedEventArgs const&)
    {
        DownloadsClicked(nullptr, RoutedEventArgs{});
    }

    void MainWindow::AppPrintClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_shell) m_shell->PrintPage();
    }

    void MainWindow::AppFullscreenClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ToggleFullscreen();
    }

    void MainWindow::FindOpenClicked(IInspectable const&, RoutedEventArgs const&)
    {
        FindPanel().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
        FindCount().Text(hstring(L""));
        LayoutViewport();
        FindBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
        FindBox().SelectAll();
    }

    void MainWindow::FindBoxKeyDown(IInspectable const& sender, KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter && m_shell) {
            if (KeyDown(VK_SHIFT)) FindPreviousClicked(sender, RoutedEventArgs{});
            else FindNextClicked(sender, RoutedEventArgs{});
            args.Handled(true);
        } else if (args.Key() == Windows::System::VirtualKey::Escape) {
            FindCloseClicked(nullptr, RoutedEventArgs{});
            args.Handled(true);
        }
    }

    void MainWindow::FindBoxTextChanged(IInspectable const&,
                                        Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
    {
        if (!m_shell) return;
        std::string text = to_string(FindBox().Text());
        if (text.empty()) {
            FindCount().Text(hstring(L""));
            m_shell->ClearFind();
            return;
        }
        m_shell->Find(text, true, ToggleOn(FindMatchCase()), ToggleOn(FindWholeWord()));
    }

    void MainWindow::FindOptionClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_shell) return;
        std::string text = to_string(FindBox().Text());
        if (text.empty()) return;
        m_shell->Find(text, true, ToggleOn(FindMatchCase()), ToggleOn(FindWholeWord()));
    }

    void MainWindow::FindPreviousClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_shell) return;
        std::string text = to_string(FindBox().Text());
        if (text.empty()) {
            FindCount().Text(hstring(L""));
            m_shell->ClearFind();
            return;
        }
        m_shell->Find(text, false, ToggleOn(FindMatchCase()), ToggleOn(FindWholeWord()));
        FindBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::FindNextClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_shell) return;
        std::string text = to_string(FindBox().Text());
        if (text.empty()) {
            FindCount().Text(hstring(L""));
            m_shell->ClearFind();
            return;
        }
        m_shell->Find(text, true, ToggleOn(FindMatchCase()), ToggleOn(FindWholeWord()));
        FindBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::FindCloseClicked(IInspectable const&, RoutedEventArgs const&)
    {
        FindPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        FindCount().Text(hstring(L""));
        LayoutViewport();
        if (m_shell) m_shell->ClearFind();
    }

    void MainWindow::SettingsSaveClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ApplySettingsFromPane();
        SettingsStatus().Text(hstring(L"Saved"));
    }

    void MainWindow::SettingsCloseClicked(IInspectable const&, RoutedEventArgs const&)
    {
        SettingsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        if (m_shell) m_shell->SetNativeViewsVisible(true);
        ApplySettingsFromPane();
    }

    void MainWindow::SettingsSearchTextChanged(IInspectable const&,
                                               Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
    {
        std::wstring query = WideText(SettingsSearch().Text());
        std::transform(query.begin(), query.end(), query.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::tolower(c)); });
        const wchar_t* cards[] = {
            L"SettingsCardStart", L"SettingsCardAppearance", L"SettingsCardPrivacy",
            L"SettingsCardExperiments", L"SettingsCardStandards", L"SettingsCardData",
            L"SettingsCardAbout"
        };
        auto visible = Microsoft::UI::Xaml::Visibility::Visible;
        auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;
        try {
            for (auto name : cards) {
                auto element = RootGrid().FindName(hstring(name)).as<muxc::Border>();
                std::wstring tag = element.Tag() != nullptr
                    ? WideText(unbox_value<hstring>(element.Tag())) : std::wstring();
                std::transform(tag.begin(), tag.end(), tag.begin(),
                               [](wchar_t c) { return static_cast<wchar_t>(std::tolower(c)); });
                bool match = query.empty() || tag.find(query) != std::wstring::npos;
                element.Visibility(match ? visible : collapsed);
            }
        } catch (...) {
        }
    }

    void MainWindow::ClearDataClicked(IInspectable const&, RoutedEventArgs const&)
    {
        int mask = 0;
        auto history = ClearHistoryCheck().IsChecked();
        auto cookies = ClearCookiesCheck().IsChecked();
        auto cache = ClearCacheCheck().IsChecked();
        auto siteData = ClearSiteDataCheck().IsChecked();
        auto passwords = ClearPasswordsCheck().IsChecked();
        if (history != nullptr && history.Value()) mask |= BrowserClearHistory;
        if (cookies != nullptr && cookies.Value()) mask |= BrowserClearCookies;
        if (cache != nullptr && cache.Value()) mask |= BrowserClearCache;
        if (siteData != nullptr && siteData.Value()) mask |= BrowserClearSiteData;
        if (passwords != nullptr && passwords.Value()) mask |= BrowserClearPasswords;
        if (!mask || !m_shell) {
            UpdateStatus(L"Select at least one data type to clear.");
            return;
        }
        m_shell->ClearBrowsingData(mask);
        SettingsStatus().Text(hstring(L"Selected data cleared"));
        UpdateStatus(L"Selected browsing data cleared.");
    }

    void MainWindow::PermissionNotificationsChanged(IInspectable const&, muxc::SelectionChangedEventArgs const&)
    {
        if (m_activeTab >= m_tabUrls.size()) return;
        ApplyPermissionChoice(Utf8(HostFromUrl(m_tabUrls[m_activeTab])), 1,
                              PermissionNotifications().SelectedIndex());
    }

    void MainWindow::PermissionGeolocationChanged(IInspectable const&, muxc::SelectionChangedEventArgs const&)
    {
        if (m_activeTab >= m_tabUrls.size()) return;
        ApplyPermissionChoice(Utf8(HostFromUrl(m_tabUrls[m_activeTab])), 0,
                              PermissionGeolocation().SelectedIndex());
    }

    void MainWindow::CheckDefaultBrowserClicked(IInspectable const&, RoutedEventArgs const&)
    {
        RefreshDefaultBrowserStatus();
    }

    void MainWindow::MakeDefaultBrowserClicked(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_shell) return;
        m_shell->MakeDefaultBrowser();
        RefreshDefaultBrowserStatus();
    }

    void MainWindow::OnUrlChanged(uint32_t index, std::string const& url)
    {
        if (index >= m_tabUrls.size()) return;
        m_tabUrls[index] = url;
        if (index == m_activeTab) {
            m_updatingOmnibox = true;
            Omnibox().Text(hstring(Wide(url)));
            m_updatingOmnibox = false;
            UpdateNavigationButtons();
            UpdateBookmarkState();
            HideSuggestions();
            std::wstring host = HostFromUrl(url);
            if (!host.empty()) {
                try {
                    PermissionOrigin().Text(hstring(host));
                } catch (...) {
                }
            }
        }
    }

    void MainWindow::OnTitleChanged(uint32_t index, std::string const& title)
    {
        if (index >= m_tabTitles.size()) return;
        std::wstring wide = Wide(title);
        m_tabTitles[index] = wide;
        SetTabHeader(index, wide);
        if (index == m_activeTab) {
            Title(hstring(wide.empty() ? L"Servo" : wide + L" - Servo"));
            if (!m_tabUrls[index].empty()) BrowserData::AddHistory(m_tabUrls[index], title);
        }
    }

    void MainWindow::OnLoadStatusChanged(uint32_t index, int32_t status)
    {
        if (index != m_activeTab) return;
        bool loading = status == 0;
        SetStopReloadState(loading);
        UpdateStatus(loading ? L"Loading..." : L"");
        UpdateNavigationButtons();
        if (!loading) UpdateBookmarkState();
    }

    void MainWindow::OnStatusTextChanged(std::string const& status)
    {
        UpdateStatus(Wide(status));
    }

    void MainWindow::OnCrashed(uint32_t index, char const* reason)
    {
        std::string text = reason ? reason : "Unknown error";
        LogDebug("WinUI tab %u crashed: %s", index, text.c_str());
        if (index < m_tabTitles.size()) {
            m_tabTitles[index] = L"Tab Crashed";
            SetTabHeader(index, L"Tab Crashed");
        }
        UpdateStatus(L"Tab crashed.");
        if (m_shell && index < m_tabUrls.size()) {
            m_shell->NavigateUrl(BrowserData::EnsureCrashRecoveryPageExists(m_tabUrls[index], text));
        }
    }

    void MainWindow::OnFindResult(int, int count, int activeOrdinal)
    {
        std::wstring summary = count <= 0
            ? std::wstring(L"No matches")
            : std::to_wstring(count) + L" matches (match " + std::to_wstring(activeOrdinal + 1) + L")";
        try {
            FindCount().Text(hstring(summary));
        } catch (...) {
        }
        UpdateStatus(summary);
    }

    void MainWindow::OnTabFavicon(uint32_t index, std::vector<uint8_t> const& pixels,
                                  uint32_t width, uint32_t height)
    {
        try {
            if (index >= m_tabUrls.size() || width == 0 || height == 0) return;
            const size_t required = static_cast<size_t>(width) * height * 4;
            if (pixels.size() < required) return;
            if (m_tabFaviconSources.size() < m_tabUrls.size()) {
                m_tabFaviconSources.resize(m_tabUrls.size());
            }

            auto buffer = Windows::Storage::Streams::Buffer(static_cast<uint32_t>(pixels.size()));
            buffer.Length(static_cast<uint32_t>(pixels.size()));
            std::memcpy(buffer.data(), pixels.data(), pixels.size());

            auto bitmap = Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
                buffer,
                Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                static_cast<int32_t>(width),
                static_cast<int32_t>(height),
                Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);
            auto source = Microsoft::UI::Xaml::Media::Imaging::SoftwareBitmapSource{};
            (void)source.SetBitmapAsync(bitmap);
            m_tabFaviconSources[index] = source;
            SetTabHeader(index, m_tabTitles[index]);
        } catch (...) {
            LogDebug("Could not display tab favicon.");
        }
    }

    void MainWindow::OnDownloadStarted(std::string const& url)
    {
        UpdateStatus(L"Download started: " + Wide(url));
    }

    void MainWindow::OnDownloadUpdated(std::string const& url, int64_t received, int64_t total)
    {
        std::wstring status = L"Downloading ";
        status += Wide(url);
        status += L" - ";
        status += std::to_wstring(received);
        if (total > 0) {
            status += L" / ";
            status += std::to_wstring(total);
        }
        UpdateStatus(status);
    }

    void MainWindow::HandleWebIpcMessage(std::string const& message)
    {
        const std::string prefix = "apply_settings:";
        if (message.rfind(prefix, 0) != 0) return;

        std::string json = message.substr(prefix.size());
        BrowserSettings settings = LoadBrowserSettings();
        settings.searchEngine = JsonString(json, "search", settings.searchEngine);
        settings.homePage = JsonString(json, "home", settings.homePage);
        settings.downloadDir = JsonString(json, "dl", settings.downloadDir);
        settings.enableWebGL2 = JsonBool(json, "webgl2", settings.enableWebGL2);
        settings.enableWebGPU = JsonBool(json, "webgpu", settings.enableWebGPU);
        settings.enableIndexedDB = JsonBool(json, "idb", settings.enableIndexedDB);
        settings.enableWebRTC = JsonBool(json, "webrtc", settings.enableWebRTC);
        settings.enableNotifications = JsonBool(json, "notifications", settings.enableNotifications);
        settings.enableGeolocation = JsonBool(json, "geolocation", settings.enableGeolocation);
        settings.enableVariableFonts = JsonBool(json, "varfonts", settings.enableVariableFonts);
        ApplyBrowserSettings(settings);
        ApplyTheme(settings.themeMode);
        ApplyVerticalTabs(settings.enableVerticalTabs);
        PopulateSettingsPane();
    }

    void MainWindow::OnSecurityStateChanged(bool secure, std::wstring const& host)
    {
        m_secure = secure;
        m_securityHost = host;
        try {
            auto visible = Microsoft::UI::Xaml::Visibility::Visible;
            auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;
            SecuritySecurePath().Visibility(secure ? visible : collapsed);
            SecurityWarningPath().Visibility(secure ? collapsed : visible);
            SecurityHost().Text(hstring(host.empty() ? (secure ? L"Secure" : L"Not secure") : host));
            SecurityInfoText().Text(hstring(secure
                ? L"Connection to " + (host.empty() ? L"this site" : host) + L" is secure."
                : L"Connection to " + (host.empty() ? L"this site" : host) +
                  L" is not secure. Avoid entering sensitive information."));
            muxc::ToolTipService::SetToolTip(SecurityChip(),
                box_value(hstring(secure ? L"Secure connection" : L"Connection is not secure")));
        } catch (...) {
        }
    }

    void MainWindow::OnDownloadItemsChanged(std::vector<BrowserDownloadItem> const& items)
    {
        try {
            auto list = DownloadsList().Items();
            list.Clear();
            int active = 0;
            for (auto const& item : items) {
                if (item.state == 0) ++active;

                auto row = muxc::StackPanel{};
                row.Spacing(4);

                auto title = muxc::TextBlock{};
                title.Text(hstring(Wide(item.fileName.empty() ? item.url : item.fileName)));
                title.FontSize(12);
                title.TextTrimming(Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
                row.Children().Append(title);

                auto progress = muxc::ProgressBar{};
                progress.Minimum(0.0);
                progress.Maximum(100.0);
                if (item.state == 0) {
                    progress.IsIndeterminate(item.total <= 0);
                    progress.Value(item.total > 0
                        ? static_cast<double>(std::min<int64_t>(100, item.received * 100 / item.total))
                        : 0.0);
                } else {
                    progress.IsIndeterminate(false);
                    progress.Value(item.state == 1 ? 100.0 : 0.0);
                }
                row.Children().Append(progress);

                std::wstring stateText = L"Active";
                if (item.state == 1) stateText = L"Complete";
                else if (item.state == 2) stateText = L"Canceled";
                else if (item.state == 3) stateText = L"Failed";
                auto meta = muxc::TextBlock{};
                meta.Text(hstring(FormatBytes(item.received) + L" / " + FormatBytes(item.total) + L" • " + stateText));
                meta.FontSize(11);
                row.Children().Append(meta);

                auto actions = muxc::StackPanel{};
                actions.Orientation(muxc::Orientation::Horizontal);
                actions.Spacing(6);

                auto open = muxc::Button{};
                open.Content(box_value(hstring(L"Open")));
                open.IsEnabled(item.state == 1);
                uint64_t id = item.id;
                open.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                    HandleDownloadAction(id, 0);
                });
                actions.Children().Append(open);

                auto show = muxc::Button{};
                show.Content(box_value(hstring(L"Show in folder")));
                show.IsEnabled(item.state == 1);
                show.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                    HandleDownloadAction(id, 1);
                });
                actions.Children().Append(show);

                auto cancel = muxc::Button{};
                cancel.Content(box_value(hstring(L"Cancel")));
                cancel.IsEnabled(item.state == 0);
                cancel.Click([this, id](IInspectable const&, RoutedEventArgs const&) {
                    HandleDownloadAction(id, 2);
                });
                actions.Children().Append(cancel);
                row.Children().Append(actions);

                auto container = muxc::ListViewItem{};
                container.Content(row);
                list.Append(container);
            }

            DownloadBadge().Text(active > 0 ? std::to_wstring(active) : L"");
            DownloadStatusText().Text(hstring(std::to_wstring(items.size()) + L" item(s), " +
                                              std::to_wstring(active) + L" active"));
        } catch (...) {
            LogDebug("Could not render the download list.");
        }
    }

    void MainWindow::HandleDownloadAction(uint64_t id, int action)
    {
        if (!m_shell) return;
        try {
            if (action == 0) m_shell->OpenDownload(id);
            else if (action == 1) m_shell->ShowDownloadInFolder(id);
            else if (action == 2) {
                m_shell->CancelDownload(id);
                OnDownloadItemsChanged(m_shell->Downloads());
            }
        } catch (...) {
            UpdateStatus(L"Download action failed.");
        }
    }

    void MainWindow::PopulateDownloads()
    {
        if (!m_shell) return;
        OnDownloadItemsChanged(m_shell->Downloads());
    }

    void MainWindow::OnZoomChanged(double zoom)
    {
        int percent = static_cast<int>(std::lround(zoom * 100.0));
        try {
            ZoomText().Text(hstring(std::to_wstring(percent) + L"%"));
            bool wasVisible = ZoomBubble().Visibility() == Microsoft::UI::Xaml::Visibility::Visible;
            ZoomBubble().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
            if (!wasVisible) LayoutViewport();

            m_zoomTimer.Interval(std::chrono::milliseconds(1800));
            m_zoomTimer.Tick([this](IInspectable const&, IInspectable const&) {
                m_zoomTimer.Stop();
                ZoomBubble().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
                LayoutViewport();
            });
            m_zoomTimer.Start();
        } catch (...) {
        }
    }

    void MainWindow::OnFullscreenChanged(bool fullscreen)
    {
        m_fullscreen = fullscreen;
        try {
            auto chrome = fullscreen ? Microsoft::UI::Xaml::Visibility::Collapsed
                                     : Microsoft::UI::Xaml::Visibility::Visible;
            TitleBarArea().Visibility(chrome);
            ToolbarArea().Visibility(chrome);
            if (fullscreen) ChromePanelsStack().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            else ChromePanelsStack().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
            FullscreenEnterPath().Visibility(chrome);
            FullscreenExitPath().Visibility(fullscreen ? Microsoft::UI::Xaml::Visibility::Visible
                                                       : Microsoft::UI::Xaml::Visibility::Collapsed);
            LayoutViewport();
            UpdateDragRegions();
        } catch (...) {
        }
    }

    void MainWindow::ToggleFullscreen()
    {
        if (!m_xamlHwnd) return;
        try {
            if (SettingsPanel().Visibility() == Microsoft::UI::Xaml::Visibility::Visible) {
                SettingsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
                if (m_shell) m_shell->SetNativeViewsVisible(true);
                ApplySettingsFromPane();
            }
            auto windowId = Microsoft::UI::GetWindowIdFromWindow(m_xamlHwnd);
            auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
            bool next = !m_fullscreen;
            appWindow.SetPresenter(next
                ? Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen
                : Microsoft::UI::Windowing::AppWindowPresenterKind::Default);
            OnFullscreenChanged(next);
        } catch (...) {
            UpdateStatus(L"Fullscreen is unavailable on this window.");
        }
    }

    void MainWindow::ShowErrorBanner(int errorCode, std::wstring const& text)
    {
        std::wstring message = L"Navigation failed";
        if (errorCode != 0) message += L" (" + std::to_wstring(errorCode) + L")";
        message += text.empty() ? L"." : L": " + text;
        try {
            ErrorText().Text(hstring(message));
            ErrorBanner().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
            LayoutViewport();
        } catch (...) {
        }
        UpdateStatus(message);
    }

    void MainWindow::OnNavigationError(int errorCode, std::wstring const& errorText)
    {
        ShowErrorBanner(errorCode, errorText);
    }

    void MainWindow::ShowTabSearch()
    {
        try {
            HideSuggestions();
            DownloadsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            TabSearchBox().Text(hstring(L""));
            PopulateTabSearch();
            TabSearchPanel().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
            LayoutViewport();
            TabSearchBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
        } catch (...) {
        }
    }

    void MainWindow::PopulateTabSearch()
    {
        try {
            std::wstring query = WideText(TabSearchBox().Text());
            std::transform(query.begin(), query.end(), query.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::tolower(c)); });
            auto items = TabSearchList().Items();
            items.Clear();
            for (size_t i = 0; i < m_tabTitles.size(); ++i) {
                std::wstring lower = m_tabTitles[i];
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](wchar_t c) { return static_cast<wchar_t>(std::tolower(c)); });
                if (!query.empty() && lower.find(query) == std::wstring::npos) continue;
                auto block = muxc::TextBlock{};
                block.Text(hstring(std::to_wstring(i + 1) + L". " +
                                   (m_tabTitles[i].empty() ? L"New tab" : m_tabTitles[i])));
                auto item = muxc::ListViewItem{};
                item.Content(block);
                item.Tag(box_value(static_cast<uint32_t>(i)));
                items.Append(item);
            }
        } catch (...) {
        }
    }

    void MainWindow::TabSearchBoxTextChanged(IInspectable const&,
                                             Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
    {
        PopulateTabSearch();
    }

    void MainWindow::TabSearchSelectionChanged(IInspectable const&, muxc::SelectionChangedEventArgs const&)
    {
        try {
            int index = TabSearchList().SelectedIndex();
            if (index < 0) return;
            auto item = TabSearchList().Items().GetAt(static_cast<uint32_t>(index)).as<muxc::ListViewItem>();
            uint32_t tab = unbox_value<uint32_t>(item.Tag());
            TabSearchPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            LayoutViewport();
            SelectTab(tab);
        } catch (...) {
        }
    }

    void MainWindow::RestoreRecentlyClosedTab()
    {
        if (!m_recentlyClosedUrls.empty()) {
            std::string url = m_recentlyClosedUrls.back();
            m_recentlyClosedUrls.pop_back();
            OpenUrlInNewTabFromShell(url);
            UpdateStatus(L"Closed tab restored.");
        } else if (m_shell) {
            m_shell->RestoreSession();
            UpdateStatus(L"Previous session restored.");
        } else {
            UpdateStatus(L"No recently closed tabs.");
        }
    }

    void MainWindow::CycleFocus(bool forward)
    {
        try {
            if (SettingsPanel().Visibility() == Microsoft::UI::Xaml::Visibility::Visible) {
                SettingsSearch().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
                return;
            }
            std::vector<Windows::Foundation::IInspectable> targets;
            if (FindPanel().Visibility() == Microsoft::UI::Xaml::Visibility::Visible) {
                targets.push_back(FindBox());
            }
            targets.push_back(Omnibox());
            targets.push_back(BackButton());
            targets.push_back(ReloadButton());
            targets.push_back(BookmarkButton());
            targets.push_back(DownloadsButton());
            targets.push_back(FindButton());
            targets.push_back(FullscreenButton());
            targets.push_back(SettingsButton());
            targets.push_back(AppButton());
            const size_t webIndex = targets.size(); // sentinel: focus the native view

            auto focused = Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement(
                RootGrid().XamlRoot());
            int current = -1;
            for (size_t i = 0; i < targets.size(); ++i) {
                if (targets[i] == focused) {
                    current = static_cast<int>(i);
                    break;
                }
            }
            int next;
            if (current < 0) next = forward ? 0 : static_cast<int>(webIndex);
            else next = (current + (forward ? 1 : -1) + static_cast<int>(webIndex) + 1)
                % static_cast<int>(webIndex + 1);

            if (static_cast<size_t>(next) == webIndex) {
                if (m_shell && m_shell->ActiveView()) m_shell->ActiveView()->SetFocus();
            } else {
                targets[static_cast<size_t>(next)].as<muxc::Control>()
                    .Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
            }
        } catch (...) {
        }
    }

    void MainWindow::RefreshDefaultBrowserStatus()
    {
        try {
            std::wstring status = m_shell ? Wide(m_shell->DefaultBrowserStatus()) : std::wstring();
            DefaultBrowserStatus().Text(hstring(status.empty() ? L"Unknown" : status));
        } catch (...) {
            DefaultBrowserStatus().Text(hstring(L"Unknown"));
        }
    }

    void MainWindow::ApplyPermissionChoice(std::string const& origin, int32_t permission, int choice)
    {
        if (!m_shell || origin.empty()) return;
        if (choice == 0) {
            UpdateStatus(L"Ask is the default; the site will prompt when needed.");
            return;
        }
        m_shell->SetPermissionForOrigin(origin, permission, choice == 1);
        UpdateStatus(choice == 1 ? L"Permission allowed for this site."
                                 : L"Permission blocked for this site.");
    }

    void MainWindow::OnPermissionRequested(std::wstring const& origin, std::wstring const& permission,
                                           std::function<void(bool)> callback)
    {
        auto dialog = muxc::ContentDialog{};
        m_activeDialog = dialog;
        try {
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Title(box_value(hstring(L"Permission request")));

            auto body = muxc::StackPanel{};
            body.Spacing(8);
            auto line = muxc::TextBlock{};
            line.Text(hstring(permission + L" wants to use " + origin + L"."));
            line.TextWrapping(Microsoft::UI::Xaml::TextWrapping::Wrap);
            body.Children().Append(line);
            auto hint = muxc::TextBlock{};
            hint.Text(hstring(L"Allow once, Block this site, or Deny for now."));
            hint.FontSize(12);
            body.Children().Append(hint);
            dialog.Content(body);
            dialog.PrimaryButtonText(hstring(L"Allow"));
            dialog.SecondaryButtonText(hstring(L"Block"));
            dialog.CloseButtonText(hstring(L"Deny"));
            dialog.DefaultButton(muxc::ContentDialogButton::Primary);

            auto operation = dialog.ShowAsync();
            operation.Completed([this, callback, origin, permission](
                Windows::Foundation::IAsyncOperation<muxc::ContentDialogResult> const& op,
                Windows::Foundation::AsyncStatus status) {
                if (status != Windows::Foundation::AsyncStatus::Completed) return;
                auto result = op.GetResults();
                bool allowed = result == muxc::ContentDialogResult::Primary;
                if (result == muxc::ContentDialogResult::Secondary) {
                    try {
                        int32_t kind = PermissionKind(permission);
                        if (m_shell && !origin.empty() && kind >= 0) {
                            m_shell->SetPermissionForOrigin(Utf8(origin), kind, false);
                        }
                    } catch (...) {
                    }
                }
                if (callback) callback(allowed);
            });
        } catch (...) {
            if (callback) callback(false);
        }
    }

    void MainWindow::OnAuthenticationRequested(std::wstring const& host, std::wstring const& realm,
                                               std::function<void(std::wstring, std::wstring)> callback)
    {
        auto dialog = muxc::ContentDialog{};
        m_activeDialog = dialog;
        try {
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Title(box_value(hstring(L"Sign in")));
            auto body = muxc::StackPanel{};
            body.Spacing(8);
            auto info = muxc::TextBlock{};
            info.Text(hstring(L"Enter credentials for " + host +
                              (realm.empty() ? L"." : L" — " + realm)));
            info.TextWrapping(Microsoft::UI::Xaml::TextWrapping::Wrap);
            body.Children().Append(info);
            auto user = muxc::TextBox{};
            user.PlaceholderText(hstring(L"Username"));
            body.Children().Append(user);
            auto password = muxc::PasswordBox{};
            password.PlaceholderText(hstring(L"Password"));
            body.Children().Append(password);
            dialog.Content(body);
            dialog.PrimaryButtonText(hstring(L"Sign in"));
            dialog.CloseButtonText(hstring(L"Cancel"));
            dialog.DefaultButton(muxc::ContentDialogButton::Primary);

            auto operation = dialog.ShowAsync();
            operation.Completed([callback, user, password](
                Windows::Foundation::IAsyncOperation<muxc::ContentDialogResult> const& op,
                Windows::Foundation::AsyncStatus status) {
                if (status != Windows::Foundation::AsyncStatus::Completed) return;
                auto result = op.GetResults();
                if (result == muxc::ContentDialogResult::Primary && callback) {
                    callback(WideText(user.Text()), WideText(password.Password()));
                } else if (callback) {
                    callback(std::wstring(), std::wstring());
                }
            });
        } catch (...) {
            if (callback) callback(std::wstring(), std::wstring());
        }
    }

    void MainWindow::OnPromptRequested(std::wstring const& message, std::wstring const& defaultValue,
                                       std::function<void(std::wstring)> callback)
    {
        auto dialog = muxc::ContentDialog{};
        m_activeDialog = dialog;
        try {
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Title(box_value(hstring(L"Prompt")));
            auto body = muxc::StackPanel{};
            body.Spacing(8);
            auto label = muxc::TextBlock{};
            label.Text(hstring(message));
            label.TextWrapping(Microsoft::UI::Xaml::TextWrapping::Wrap);
            body.Children().Append(label);
            auto input = muxc::TextBox{};
            input.Text(hstring(defaultValue));
            body.Children().Append(input);
            dialog.Content(body);
            dialog.PrimaryButtonText(hstring(L"OK"));
            dialog.CloseButtonText(hstring(L"Cancel"));
            dialog.DefaultButton(muxc::ContentDialogButton::Primary);

            auto operation = dialog.ShowAsync();
            operation.Completed([callback, input](
                Windows::Foundation::IAsyncOperation<muxc::ContentDialogResult> const& op,
                Windows::Foundation::AsyncStatus status) {
                if (status != Windows::Foundation::AsyncStatus::Completed) return;
                auto result = op.GetResults();
                if (callback) {
                    callback(result == muxc::ContentDialogResult::Primary ? WideText(input.Text()) : std::wstring());
                }
            });
        } catch (...) {
            if (callback) callback(std::wstring());
        }
    }
}
