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

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
            auto titleBar = appWindow.TitleBar();
            if (!Microsoft::UI::Windowing::AppWindowTitleBar::IsCustomizationSupported()) return;
            titleBar.ExtendsContentIntoTitleBar(true);
            titleBar.ButtonBackgroundColor(Windows::UI::Colors::Transparent());
            titleBar.ButtonInactiveBackgroundColor(Windows::UI::Colors::Transparent());
            titleBar.ButtonHoverBackgroundColor(Windows::UI::ColorHelper::FromArgb(32, 255, 255, 255));
            titleBar.ButtonPressedBackgroundColor(Windows::UI::ColorHelper::FromArgb(48, 255, 255, 255));
            titleBar.ButtonForegroundColor(Windows::UI::Colors::White());
            titleBar.ButtonInactiveForegroundColor(Windows::UI::ColorHelper::FromArgb(160, 255, 255, 255));
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

            // Only the empty part of the tab strip is a drag region. Tabs, the
            // new-tab button, and the caption buttons stay interactive.
            double tabStripWidth = Tabs().ActualWidth();
            double titleBarWidth = TitleBarArea().ActualWidth();
            if (tabStripWidth <= 0 || titleBarWidth <= 0) return;
            double leftPadding = TitleBarArea().Padding().Left;
            double newTabButtonWidth = 34.0;

            int x = static_cast<int>(std::lround(leftPadding * scale));
            int width = static_cast<int>(std::lround(
                std::max(0.0, tabStripWidth - newTabButtonWidth) * scale));
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
        item.Header(box_value(hstring(title)));

        m_updatingTabs = true;
        Tabs().TabItems().Append(item);
        Tabs().SelectedItem(item);
        m_updatingTabs = false;

        m_tabUrls.push_back(actualUrl);
        m_tabTitles.push_back(title);
        m_activeTab = static_cast<uint32_t>(m_tabUrls.size() - 1);
        LayoutViewport();
        UpdateActiveTab(m_activeTab);
    }

    void MainWindow::CloseTab(uint32_t index)
    {
        if (index >= m_tabUrls.size()) return;
        if (!m_shell) return;

        m_shell->CloseTab(index);
        auto items = Tabs().TabItems();
        if (index < items.Size()) items.RemoveAt(index);
        m_tabUrls.erase(m_tabUrls.begin() + static_cast<ptrdiff_t>(index));
        m_tabTitles.erase(m_tabTitles.begin() + static_cast<ptrdiff_t>(index));

        uint32_t active = m_shell->ActiveTabIndex();
        m_updatingTabs = true;
        if (items.Size() > 0 && active < items.Size()) {
            Tabs().SelectedIndex(static_cast<int32_t>(active));
        }
        m_updatingTabs = false;
        m_activeTab = active;
        LayoutViewport();
        UpdateActiveTab(active);
    }

    void MainWindow::SelectTab(uint32_t index)
    {
        if (index >= m_tabUrls.size() || !m_shell) return;
        m_shell->SelectTab(index);
        m_activeTab = index;
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
    }

    void MainWindow::UpdateStatus(std::wstring const& status)
    {
        try {
            StatusText().Text(hstring(status));
        } catch (...) {
        }
    }

    void MainWindow::SetTabHeader(uint32_t index, std::wstring const& header)
    {
        if (index >= m_tabTitles.size()) return;
        try {
            auto items = Tabs().TabItems();
            if (index >= items.Size()) return;
            auto item = items.GetAt(index).as<muxc::TabViewItem>();
            item.Header(box_value(hstring(header)));
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
        // XAML overlays cannot draw above the native Servo child HWND.
        if (m_shell) m_shell->SetNativeViewsVisible(false);
        SettingsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
    }

    void MainWindow::PopulateSettingsPane()
    {
        BrowserSettings settings = LoadBrowserSettings();
        SettingWebGL2().IsOn(settings.enableWebGL2);
        SettingWebGPU().IsOn(settings.enableWebGPU);
        SettingIndexedDB().IsOn(settings.enableIndexedDB);
        SettingWebRTC().IsOn(settings.enableWebRTC);
        SettingNotifications().IsOn(settings.enableNotifications);
        SettingGeolocation().IsOn(settings.enableGeolocation);
        SettingVariableFonts().IsOn(settings.enableVariableFonts);
    }

    void MainWindow::ApplySettingsFromPane()
    {
        BrowserSettings settings = LoadBrowserSettings();
        settings.enableWebGL2 = SettingWebGL2().IsOn();
        settings.enableWebGPU = SettingWebGPU().IsOn();
        settings.enableIndexedDB = SettingIndexedDB().IsOn();
        settings.enableWebRTC = SettingWebRTC().IsOn();
        settings.enableNotifications = SettingNotifications().IsOn();
        settings.enableGeolocation = SettingGeolocation().IsOn();
        settings.enableVariableFonts = SettingVariableFonts().IsOn();
        ApplyBrowserSettings(settings);
        UpdateStatus(L"Settings saved.");
    }

    void MainWindow::RootLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        m_loaded = true;
        m_xamlHwnd = GetXamlHwnd();
        ConfigureTitleBar();
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

        if (key == VirtualKey::Escape) {
            bool consumed = false;
            if (FindPanel().Visibility() == Microsoft::UI::Xaml::Visibility::Visible) {
                FindPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
                LayoutViewport();
                if (m_shell) m_shell->ClearFind();
                consumed = true;
            }
            if (SettingsPanel().Visibility() == Microsoft::UI::Xaml::Visibility::Visible) {
                SettingsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
                if (m_shell) m_shell->SetNativeViewsVisible(true);
                ApplySettingsFromPane();
                consumed = true;
            }
            if (consumed) args.Handled(true);
            return;
        }

        if (ctrl && key == VirtualKey::T) {
            AddTab(std::string());
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::W) {
            if (!m_tabUrls.empty()) CloseTab(m_activeTab);
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::L) {
            Omnibox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
            Omnibox().SelectAll();
            args.Handled(true);
        } else if (ctrl && key == VirtualKey::F) {
            FindOpenClicked(nullptr, RoutedEventArgs{});
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
            if (m_shell && m_shell->ActiveView()) {
                servo_webview_set_page_zoom(m_shell->ActiveView()->GetWebView(), 1.0f);
            }
            args.Handled(true);
        } else if (alt && key == VirtualKey::Left) {
            if (m_shell) m_shell->NavigateBack();
            args.Handled(true);
        } else if (alt && key == VirtualKey::Right) {
            if (m_shell) m_shell->NavigateForward();
            args.Handled(true);
        } else if (!shift && key == VirtualKey::F6) {
            if (m_shell) m_shell->ActiveView();
            args.Handled(true);
        }
    }

    void MainWindow::ViewportSizeChanged(IInspectable const&, SizeChangedEventArgs const&)
    {
        LayoutViewport();
    }

    void MainWindow::OmniboxKeyDown(IInspectable const&, KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter) {
            LoadInActiveTab(to_string(Omnibox().Text()));
            args.Handled(true);
        } else if (args.Key() == Windows::System::VirtualKey::Escape) {
            if (m_activeTab < m_tabUrls.size()) {
                Omnibox().Text(hstring(Wide(m_tabUrls[m_activeTab])));
            }
            args.Handled(true);
        }
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

    void MainWindow::HomeClicked(IInspectable const&, RoutedEventArgs const&)
    {
        BrowserSettings settings = LoadBrowserSettings();
        LoadInActiveTab(to_string(hstring(settings.homePage)));
    }

    void MainWindow::AddTabClicked(IInspectable const&, RoutedEventArgs const&)
    {
        AddTab(std::string());
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

    void MainWindow::FindOpenClicked(IInspectable const&, RoutedEventArgs const&)
    {
        FindPanel().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
        LayoutViewport();
        FindBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
        FindBox().SelectAll();
    }

    void MainWindow::FindBoxKeyDown(IInspectable const&, KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter && m_shell) {
            bool shift = KeyDown(VK_SHIFT);
            m_shell->Find(to_string(FindBox().Text()), !shift, false);
            args.Handled(true);
        } else if (args.Key() == Windows::System::VirtualKey::Escape) {
            FindPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            LayoutViewport();
            if (m_shell) m_shell->ClearFind();
            args.Handled(true);
        }
    }

    void MainWindow::FindCloseClicked(IInspectable const&, RoutedEventArgs const&)
    {
        FindPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        LayoutViewport();
        if (m_shell) m_shell->ClearFind();
    }

    void MainWindow::SettingsCloseClicked(IInspectable const&, RoutedEventArgs const&)
    {
        SettingsPanel().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        if (m_shell) m_shell->SetNativeViewsVisible(true);
        ApplySettingsFromPane();
    }

    void MainWindow::OnUrlChanged(uint32_t index, std::string const& url)
    {
        if (index >= m_tabUrls.size()) return;
        m_tabUrls[index] = url;
        if (index == m_activeTab) Omnibox().Text(hstring(Wide(url)));
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
        UpdateStatus(status == 0 ? L"Loading..." : L"");
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
        if (count <= 0) {
            UpdateStatus(L"No matches");
        } else {
            UpdateStatus(std::to_wstring(count) + L" matches (match " + std::to_wstring(activeOrdinal + 1) + L")");
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
        PopulateSettingsPane();
    }
}
