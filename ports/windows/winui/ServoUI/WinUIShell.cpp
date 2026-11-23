/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "pch.h"
#include "WinUIShell.h"
#include "MainWindow.xaml.h"

#include "../../src/servo_view_host.h"
#include "../../src/browser_runtime.h"
#include "../../src/browser_settings.h"
#include "../../src/browser_data.h"
#include "../../src/new_tab_page.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <shellapi.h>

namespace winrt::ServoUI::implementation
{
    namespace
    {
        constexpr size_t kNoTab = std::numeric_limits<size_t>::max();

        std::string Utf8(std::wstring const& value) {
            if (value.empty()) return {};
            int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (length <= 1) return {};
            std::string result(static_cast<size_t>(length - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], length, nullptr, nullptr);
            return result;
        }

        std::string ToLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string PercentEncode(std::string const& value) {
            static const char* hex = "0123456789ABCDEF";
            std::ostringstream out;
            for (unsigned char c : value) {
                bool safe = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
                if (safe) {
                    out << static_cast<char>(c);
                } else {
                    out << '%' << hex[(c >> 4) & 0x0F] << hex[c & 0x0F];
                }
            }
            return out.str();
        }

        bool IsBareHost(std::string const& value) {
            if (value.find('\\') != std::string::npos || value.find(' ') != std::string::npos ||
                value.find("://") != std::string::npos) {
                return false;
            }
            size_t dot = value.find('.');
            return dot != std::string::npos && dot > 0 && dot + 1 < value.size();
        }

        std::wstring Wide(std::string const& value) {
            if (value.empty()) return {};
            int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
            if (length <= 1) return {};
            std::wstring result(static_cast<size_t>(length - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &result[0], length);
            return result;
        }

        // Never feed a raw URL to ShellExecute: only a basename derived from
        // the download metadata is combined with the configured directory.
        std::string SanitizedFileName(std::string const& suggested, std::string const& url) {
            size_t first = 0;
            while (first < suggested.size() &&
                   std::isspace(static_cast<unsigned char>(suggested[first]))) ++first;
            size_t last = suggested.size();
            while (last > first && std::isspace(static_cast<unsigned char>(suggested[last - 1]))) --last;
            std::string source = suggested.substr(first, last - first);
            if (source.empty()) {
                source = url;
                size_t query = source.find_first_of("?#");
                if (query != std::string::npos) source.resize(query);
                size_t slash = source.find_last_of("/\\");
                if (slash != std::string::npos) source = source.substr(slash + 1);
            }
            std::string result;
            result.reserve(source.size());
            for (unsigned char c : source) {
                bool unsafe = c <= 0x1F || c == '/' || c == '\\' || c == '<' || c == '>' ||
                    c == ':' || c == '"' || c == '|' || c == '?' || c == '*';
                result += unsafe ? '_' : static_cast<char>(c);
            }
            size_t trimFirst = result.find_first_not_of(" .");
            if (trimFirst == std::string::npos) return "download";
            size_t trimLast = result.find_last_not_of(" .");
            result = result.substr(trimFirst, trimLast - trimFirst + 1);
            if (result.size() > 180) result.resize(180);
            return result;
        }

        std::string DownloadDirectoryUtf8() {
            BrowserSettings settings = LoadBrowserSettings();
            std::wstring dir = settings.downloadDir;
            if (dir.empty()) {
                wchar_t profile[MAX_PATH] = {};
                DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
                if (length > 0 && length < MAX_PATH) dir = std::wstring(profile) + L"\\Downloads";
            }
            if (dir.empty()) dir = L".";
            while (dir.size() > 1 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
            return Utf8(dir);
        }

        std::string DownloadPathFor(std::string const& fileName) {
            return DownloadDirectoryUtf8() + "\\" + fileName;
        }

        // The UI agent owns these MainWindow methods. The fallback overloads
        // keep this bridge buildable before that header lands and fail closed
        // for the user-prompt callbacks instead of leaking/unblocking a request.
        template <typename Owner>
        void CallSecurityChanged(Owner& owner, bool secure, std::wstring const& host, ...) {
            (void)owner; (void)secure; (void)host;
        }
        template <typename Owner>
        auto CallSecurityChanged(Owner& owner, bool secure, std::wstring const& host, int)
            -> decltype(owner.OnSecurityStateChanged(secure, host), void()) {
            owner.OnSecurityStateChanged(secure, host);
        }

        template <typename Owner>
        void CallDownloadItems(Owner& owner, std::vector<BrowserDownloadItem> const& items, ...) {
            (void)owner; (void)items;
        }
        template <typename Owner>
        auto CallDownloadItems(Owner& owner, std::vector<BrowserDownloadItem> const& items, int)
            -> decltype(owner.OnDownloadItemsChanged(items), void()) {
            owner.OnDownloadItemsChanged(items);
        }

        template <typename Owner>
        void CallZoomChanged(Owner& owner, double zoom, ...) { (void)owner; (void)zoom; }
        template <typename Owner>
        auto CallZoomChanged(Owner& owner, double zoom, int)
            -> decltype(owner.OnZoomChanged(zoom), void()) {
            owner.OnZoomChanged(zoom);
        }

        template <typename Owner>
        void CallFullscreenChanged(Owner& owner, bool fullscreen, ...) { (void)owner; (void)fullscreen; }
        template <typename Owner>
        auto CallFullscreenChanged(Owner& owner, bool fullscreen, int)
            -> decltype(owner.OnFullscreenChanged(fullscreen), void()) {
            owner.OnFullscreenChanged(fullscreen);
        }

        template <typename Owner>
        void CallNavigationError(Owner& owner, int code, std::wstring const& text, ...) {
            (void)owner; (void)code; (void)text;
        }
        template <typename Owner>
        auto CallNavigationError(Owner& owner, int code, std::wstring const& text, int)
            -> decltype(owner.OnNavigationError(code, text), void()) {
            owner.OnNavigationError(code, text);
        }

        template <typename Owner>
        void CallPermission(Owner& owner, std::wstring const& origin, std::wstring const& name,
                            std::function<void(bool)> const& callback, ...) {
            (void)owner; (void)origin; (void)name;
            LogDebug("[PERMISSION] No permission UI in this shell; denying request.");
            callback(false);
        }
        template <typename Owner>
        auto CallPermission(Owner& owner, std::wstring const& origin, std::wstring const& name,
                            std::function<void(bool)> const& callback, int)
            -> decltype(owner.OnPermissionRequested(origin, name, callback), void()) {
            owner.OnPermissionRequested(origin, name, callback);
        }

        template <typename Owner>
        void CallAuthentication(Owner& owner, std::wstring const& origin, std::wstring const& host,
                                std::function<void(std::wstring, std::wstring)> const& callback, ...) {
            (void)owner; (void)origin; (void)host;
            LogDebug("[AUTH] No authentication UI in this shell; cancelling request.");
            callback(std::wstring(), std::wstring());
        }
        template <typename Owner>
        auto CallAuthentication(Owner& owner, std::wstring const& origin, std::wstring const& host,
                                std::function<void(std::wstring, std::wstring)> const& callback, int)
            -> decltype(owner.OnAuthenticationRequested(origin, host, callback), void()) {
            owner.OnAuthenticationRequested(origin, host, callback);
        }

        template <typename Owner>
        void CallPrompt(Owner& owner, std::wstring const& message, std::wstring const& defaultValue,
                        std::function<void(std::wstring)> const& callback, ...) {
            (void)owner; (void)message;
            LogDebug("[PROMPT] No JavaScript prompt UI in this shell; using default value.");
            callback(defaultValue);
        }
        template <typename Owner>
        auto CallPrompt(Owner& owner, std::wstring const& message, std::wstring const& defaultValue,
                        std::function<void(std::wstring)> const& callback, int)
            -> decltype(owner.OnPromptRequested(message, defaultValue, callback), void()) {
            owner.OnPromptRequested(message, defaultValue, callback);
        }

        // BrowserData gains this generated-page helper in the shared workstream.
        template <typename Data>
        auto DownloadsPageFor(int) -> decltype(Data::EnsureDownloadsPageExists()) {
            return Data::EnsureDownloadsPageExists();
        }
        template <typename Data>
        std::string DownloadsPageFor(...) { return {}; }

        // SitePermission helpers are added in browser_settings.h by the shared
        // workstream; without them the reset is logged as unavailable.
        auto ResetSitePermissions(int)
            -> decltype(SaveSitePermissions(std::vector<SitePermission>{}), void()) {
            SaveSitePermissions(std::vector<SitePermission>{});
        }
        void ResetSitePermissions(...) {
            LogDebug("[DATA] Site-permission reset is not available in this settings build.");
        }

        struct PrintContext {
            MainWindow* owner;
            std::string requestedPath;
        };

        void OnPrintFinished(bool success, const char* outputPath, void* userData) {
            std::unique_ptr<PrintContext> context(static_cast<PrintContext*>(userData));
            if (!context) return;
            MainWindow* owner = context->owner;
            std::string message = success
                ? "Print to PDF saved to " + std::string(outputPath ? outputPath : context->requestedPath)
                : "Print to PDF failed";
            PostUiTask([owner, message]() { owner->OnStatusTextChanged(message); });
        }
    }

    WinUIShell::WinUIShell(MainWindow& owner, HWND rootHwnd, Servo* servo)
        : m_owner(owner)
        , m_rootHwnd(rootHwnd)
        , m_servo(servo)
    {
    }

    WinUIShell::~WinUIShell()
    {
        m_tabs.clear();
    }

    ServoViewHost* WinUIShell::ActiveView() const
    {
        if (m_tabs.empty() || m_active >= m_tabs.size()) return nullptr;
        return m_tabs[m_active].get();
    }

    size_t WinUIShell::IndexFor(ServoViewHost* host) const
    {
        if (!host) return kNoTab;
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i].get() == host) return i;
        }
        return kNoTab;
    }

    void WinUIShell::ApplyViewVisibility()
    {
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i]->Show(m_nativeViewsVisible && i == m_active);
        }
    }

    void WinUIShell::SetNativeViewsVisible(bool visible)
    {
        if (m_nativeViewsVisible == visible) return;
        m_nativeViewsVisible = visible;
        ApplyViewVisibility();
        if (visible) {
            ApplyViewportToActive();
        }
    }

    bool WinUIShell::OpenTab(const std::string& url, ServoCreateNewWebViewRequest* request)
    {
        if (!m_rootHwnd || !m_servo) return false;

        std::unique_ptr<ServoViewHost> host;
        if (request) {
            host = std::make_unique<ServoViewHost>(m_rootHwnd, m_servo, this, request);
        } else {
            std::string target = url.empty() ? NewTabPage::EnsureNewTabPageExists() : url;
            host = std::make_unique<ServoViewHost>(m_rootHwnd, m_servo, this, target);
        }
        if (!host || !host->IsValid()) return false;

        ResetFindState();
        m_tabs.push_back(std::move(host));
        m_active = static_cast<uint32_t>(m_tabs.size() - 1);
        ApplyViewVisibility();
        ApplyViewportToActive();
        return true;
    }

    void WinUIShell::CloseTab(uint32_t index)
    {
        if (index >= m_tabs.size()) return;
        ResetFindState();
        m_tabs.erase(m_tabs.begin() + static_cast<ptrdiff_t>(index));
        if (m_tabs.empty()) {
            m_active = 0;
            return;
        }
        if (m_active >= m_tabs.size()) {
            m_active = static_cast<uint32_t>(m_tabs.size() - 1);
        }
        ApplyViewVisibility();
        ApplyViewportToActive();
    }

    void WinUIShell::SelectTab(uint32_t index)
    {
        if (index >= m_tabs.size() || index == m_active) return;
        ResetFindState();
        m_active = index;
        ApplyViewVisibility();
        ApplyViewportToActive();
        if (ServoViewHost* view = ActiveView()) {
            view->SetFocus();
        }
    }

    void WinUIShell::SetViewportBounds(int x, int y, int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        m_viewport = { x, y, x + width, y + height };
        ApplyViewportToActive();
    }

    void WinUIShell::ApplyViewportToActive()
    {
        ServoViewHost* view = ActiveView();
        if (!view || IsRectEmpty(&m_viewport)) return;
        view->SetBounds(m_viewport.left, m_viewport.top,
                        m_viewport.right - m_viewport.left,
                        m_viewport.bottom - m_viewport.top);
    }

    void WinUIShell::ResetFindState()
    {
        m_findIdentifier = -1;
        m_findText.clear();
        m_findView = nullptr;
    }

    void WinUIShell::Find(const std::string& text, bool forward, bool matchCase, bool wholeWord)
    {
        ServoViewHost* view = ActiveView();
        if (!view || !view->GetWebView()) return;
        if (Trim(text).empty()) {
            ClearFind();
            return;
        }
        // ServoViewHost has a four-argument Find overload, but its wholeWord
        // parameter is also ignored because servo_webview_find() only accepts
        // (text, forward, matchCase). Passing \b would be literal text. Keep
        // the direct C call here so this shell retains the find identifier
        // needed for find-next/find-clear.
        (void)wholeWord;
        if (m_findView != view) ResetFindState();
        if (m_findIdentifier < 0 || text != m_findText) {
            m_findIdentifier = servo_webview_find(view->GetWebView(), text.c_str(), forward, matchCase);
            m_findText = text;
            m_findView = view;
        } else {
            servo_webview_find_next(view->GetWebView(), m_findIdentifier, forward);
        }
    }

    void WinUIShell::ClearFind()
    {
        if (m_findView && m_findView->GetWebView() && m_findIdentifier >= 0) {
            servo_webview_find_clear(m_findView->GetWebView(), m_findIdentifier);
        }
        ResetFindState();
    }

    void WinUIShell::OnActiveTabUrlChanged(ServoViewHost* host, const char* url)
    {
        if (!url) return;
        const size_t index = IndexFor(host);
        if (index != kNoTab) {
            m_owner.OnUrlChanged(static_cast<uint32_t>(index), url);
        }
    }

    void WinUIShell::OnActiveTabTitleChanged(ServoViewHost* host, const char* title)
    {
        if (!title) return;
        const size_t index = IndexFor(host);
        if (index != kNoTab) {
            m_owner.OnTitleChanged(static_cast<uint32_t>(index), title);
        }
    }

    void WinUIShell::OnActiveTabLoadStatusChanged(ServoViewHost* host, int32_t status)
    {
        const size_t index = IndexFor(host);
        if (index != kNoTab) {
            m_owner.OnLoadStatusChanged(static_cast<uint32_t>(index), status);
        }
    }

    void WinUIShell::OnActiveTabCrashed(ServoViewHost* host, const char* reason)
    {
        const size_t index = IndexFor(host);
        if (index != kNoTab) {
            m_owner.OnCrashed(static_cast<uint32_t>(index), reason ? reason : "Unknown error");
        }
    }

    void WinUIShell::OnStatusTextChanged(ServoViewHost* host, const char* status)
    {
        if (IndexFor(host) != m_active || !status) return;
        m_owner.OnStatusTextChanged(status);
    }

    void WinUIShell::OnTabFaviconUpdated(ServoViewHost* host, HBITMAP hBitmap,
                                         uint32_t width, uint32_t height)
    {
        const size_t index = IndexFor(host);
        if (hBitmap && index != kNoTab && width > 0 && height > 0) {
            BITMAP bm{};
            if (GetObjectW(hBitmap, sizeof(bm), &bm) == sizeof(bm) && bm.bmBits &&
                static_cast<uint32_t>(bm.bmWidth) >= width) {
                const int rawHeight = bm.bmHeight;
                const int bitmapHeight = rawHeight >= 0 ? rawHeight : -rawHeight;
                if (static_cast<uint32_t>(bitmapHeight) >= height) {
                    const size_t srcStride = static_cast<size_t>(bm.bmWidthBytes);
                    const size_t dstStride = static_cast<size_t>(width) * 4;
                    if (srcStride >= dstStride) {
                        std::vector<uint8_t> pixels(dstStride * height);
                        const auto* src = static_cast<const uint8_t*>(bm.bmBits);
                        for (uint32_t row = 0; row < height; ++row) {
                            std::memcpy(pixels.data() + row * dstStride,
                                        src + row * srcStride, dstStride);
                        }
                        auto* owner = &m_owner;
                        const uint32_t tabIndex = static_cast<uint32_t>(index);
                        PostUiTask([owner, tabIndex, pixels = std::move(pixels), width, height]() {
                            owner->OnTabFavicon(tabIndex, pixels, width, height);
                        });
                    }
                }
            }
        }
        // Ownership of the HBITMAP is transferred to the shell.
        if (hBitmap) DeleteObject(hBitmap);
    }

    void WinUIShell::OnFindResult(ServoViewHost* host, int identifier, int count, int activeOrdinal)
    {
        if (identifier != m_findIdentifier || IndexFor(host) != m_active) return;
        m_owner.OnFindResult(identifier, count, activeOrdinal);
    }

    void WinUIShell::LoadInActiveTab(const std::string& url)
    {
        if (ServoViewHost* view = ActiveView()) {
            view->LoadUrl(url);
        }
    }

    std::string WinUIShell::Trim(const std::string& value)
    {
        size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
        size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
        return value.substr(first, last - first);
    }

    std::string WinUIShell::SearchUrl(const std::string& text)
    {
        BrowserSettings settings = LoadBrowserSettings();
        std::string engine = ToLower(Utf8(settings.searchEngine));
        std::string query = PercentEncode(Trim(text));
        if (engine == "custom") {
            std::string custom = Trim(Utf8(settings.customSearchEngine));
            if (!custom.empty()) {
                // The settings UI documents {q}; %s is accepted for templates
                // imported from older profiles and common search-engine docs.
                static const char* const markers[] = { "{q}", "%s" };
                bool substituted = false;
                for (const char* marker : markers) {
                    const std::string needle(marker);
                    size_t pos = custom.find(needle);
                    while (pos != std::string::npos) {
                        custom.replace(pos, needle.size(), query);
                        pos = custom.find(needle, pos + query.size());
                        substituted = true;
                    }
                }
                return substituted ? custom : custom + query;
            }
            engine = "duckduckgo";
        }
        if (engine.find("google") != std::string::npos) {
            return "https://www.google.com/search?q=" + query;
        }
        if (engine.find("bing") != std::string::npos) {
            return "https://www.bing.com/search?q=" + query;
        }
        if (engine.find("ecosia") != std::string::npos) {
            return "https://www.ecosia.org/search?q=" + query;
        }
        return "https://duckduckgo.com/?q=" + query;
    }

    void WinUIShell::NavigateUrl(const std::string& url)
    {
        const std::string clean = Trim(url);
        if (clean.empty()) return;
        const std::string lower = ToLower(clean);

        if (lower == "servo://downloads" || lower == "chrome://downloads" ||
            lower == "about:downloads" || lower == "downloads") {
            ShowDownloads();
            return;
        }
        if (lower == "servo://clear-history" || lower == "about:clear-history") {
            ClearBrowsingData(1); // BrowserClearDataMask::History
            return;
        }
        if (lower == "servo://session-restore" || lower == "about:session-restore") {
            RestoreSession();
            return;
        }
        if (lower == "servo://native-settings" || lower == "about:native-settings") {
            m_owner.OpenSettingsPane();
            return;
        }
        if (lower == "servo://settings" || lower == "servo:settings" || lower == "about:settings" ||
            lower == "chrome://settings" || lower == "edge://settings" || lower == "settings") {
            m_owner.OpenSettingsPane();
            return;
        }
        if (lower == "servo://bookmarks" || lower == "servo:bookmarks" || lower == "about:bookmarks" || lower == "bookmarks") {
            ShowBookmarks();
            return;
        }
        if (lower == "servo://history" || lower == "servo:history" || lower == "about:history" || lower == "history") {
            ShowHistory();
            return;
        }
        if (lower == "servo://about" || lower == "servo:about" || lower == "about:about" ||
            lower == "servo://version" || lower == "about:version" || lower == "about") {
            LoadInActiveTab(BrowserData::EnsureAboutPageExists());
            return;
        }
        if (lower == "servo://newtab" || lower == "servo:newtab" || lower == "about:newtab" || lower == "newtab") {
            LoadInActiveTab(NewTabPage::EnsureNewTabPageExists());
            return;
        }
        if (lower.rfind("servo://apply-settings?", 0) == 0 || lower.rfind("servo:apply-settings?", 0) == 0) {
            size_t question = clean.find('?');
            if (question != std::string::npos) {
                HandleWebIpcMessage("apply_settings:" + clean.substr(question + 1));
            }
            return;
        }

        if (lower.rfind("view-source:", 0) == 0) {
            if (ServoViewHost* view = ActiveView()) view->ViewPageSource();
            return;
        }

        bool hasScheme = clean.find("://") != std::string::npos ||
            lower.rfind("about:", 0) == 0 || lower.rfind("data:", 0) == 0 ||
            lower.rfind("file:", 0) == 0 || lower.rfind("servo:", 0) == 0;
        if (!hasScheme && IsBareHost(clean)) {
            LoadInActiveTab("https://" + clean);
        } else if (hasScheme) {
            LoadInActiveTab(clean);
        } else {
            LoadInActiveTab(SearchUrl(clean));
        }
    }

    void WinUIShell::NavigateBack()
    {
        if (ServoViewHost* view = ActiveView()) view->GoBack();
    }

    void WinUIShell::NavigateForward()
    {
        if (ServoViewHost* view = ActiveView()) view->GoForward();
    }

    void WinUIShell::NavigateReload()
    {
        if (ServoViewHost* view = ActiveView()) view->Reload();
    }

    void WinUIShell::OpenDevTools()
    {
        const uint16_t port = BrowserRuntime::Instance().DevToolsPort();
        if (port == 0) {
            m_owner.OnStatusTextChanged("DevTools server is starting...");
            return;
        }
        m_owner.OnStatusTextChanged(
            "DevTools server listening on 127.0.0.1:" + std::to_string(port) +
            " (Firefox RDP client required).");
    }

    void WinUIShell::OpenUrlInNewTab(const std::string& url)
    {
        m_owner.OpenUrlInNewTabFromShell(url);
    }

    void WinUIShell::OpenSettings()
    {
        m_owner.OpenSettingsFromShell();
    }

    void WinUIShell::ZoomIn()
    {
        ServoViewHost* view = ActiveView();
        if (!view || !view->GetWebView()) return;
        float current = servo_webview_page_zoom(view->GetWebView());
        m_zoom = std::min(current * 1.2f, 5.0f);
        servo_webview_set_page_zoom(view->GetWebView(), m_zoom);
    }

    void WinUIShell::ZoomOut()
    {
        ServoViewHost* view = ActiveView();
        if (!view || !view->GetWebView()) return;
        float current = servo_webview_page_zoom(view->GetWebView());
        m_zoom = std::max(current / 1.2f, 0.25f);
        servo_webview_set_page_zoom(view->GetWebView(), m_zoom);
    }

    void WinUIShell::HandleWebIpcMessage(const std::string& message)
    {
        // Log only the command token. Several IPC messages carry passwords,
        // usernames, or raw __servo_ipc__ payloads.
        size_t separator = message.find(':');
        std::string command = message.substr(0, separator == std::string::npos ? message.size() : separator);
        if (command.size() > 48) command.resize(48);
        LogDebug("[IPC] Received message: %s", command.c_str());

        if (command == "navigate" && separator != std::string::npos) {
            NavigateUrl(message.substr(separator + 1));
            return;
        }
        if (command == "prompt_save_password" || command == "request_autofill") {
            return;
        }
        m_owner.HandleWebIpcMessage(message);
    }

    void WinUIShell::NavigateStop()
    {
        if (ServoViewHost* view = ActiveView()) view->Stop();
    }

    void WinUIShell::ZoomReset()
    {
        ServoViewHost* view = ActiveView();
        if (!view || !view->GetWebView()) return;
        m_zoom = 1.0f;
        servo_webview_set_page_zoom(view->GetWebView(), m_zoom);
        CallZoomChanged(m_owner, 1.0, 0);
    }

    void WinUIShell::ReloadIgnoringCache()
    {
        // The C API has no "reload ignoring cache" flag. Reload normally and
        // say so instead of claiming a cache bypass that did not happen.
        if (ServoViewHost* view = ActiveView()) view->Reload();
        m_owner.OnStatusTextChanged("Reloaded (cache bypass is not available in the current C API).");
    }

    void WinUIShell::PrintPage()
    {
        ServoViewHost* view = ActiveView();
        if (!view || !view->GetWebView()) {
            m_owner.OnStatusTextChanged("Print to PDF needs an active page.");
            return;
        }
        std::string title = view->GetTitle();
        if (title.empty()) title = view->GetUrl();
        if (title.empty()) title = "page";
        std::string fileName = SanitizedFileName(title + ".pdf", view->GetUrl());
        std::string path = DownloadPathFor(fileName);
        auto* context = new PrintContext{ &m_owner, path };
        const int result = servo_webview_print_to_pdf(view->GetWebView(), path.c_str(), nullptr,
                                                      &OnPrintFinished, context);
        if (result != SERVO_ERROR_OK) {
            delete context;
            m_owner.OnStatusTextChanged("Print to PDF is not available in the current C API.");
        }
    }

    void WinUIShell::RestoreSession()
    {
        std::vector<std::string> urls;
        int active = 0;
        if (!BrowserData::LoadSession(urls, active) || urls.empty()) {
            m_owner.OnStatusTextChanged("No saved session to restore.");
            return;
        }
        for (const std::string& url : urls) {
            if (!url.empty()) m_owner.OpenUrlInNewTabFromShell(url);
        }
        if (active > 0 && static_cast<size_t>(active) < TabCount()) {
            SelectTab(static_cast<uint32_t>(active));
        }
        m_owner.OnStatusTextChanged("Session restored.");
    }

    void WinUIShell::ShowDownloads()
    {
        const std::string page = DownloadsPageFor<BrowserData>(0);
        if (page.empty()) {
            m_owner.OnStatusTextChanged("Downloads page is unavailable in this build.");
            return;
        }
        LoadInActiveTab(page);
    }

    void WinUIShell::ShowHistory()
    {
        LoadInActiveTab(BrowserData::EnsureHistoryPageExists());
    }

    void WinUIShell::ShowBookmarks()
    {
        LoadInActiveTab(BrowserData::EnsureBookmarksPageExists());
    }

    bool WinUIShell::IsBookmarked(std::string const& url) const
    {
        return BrowserData::IsBookmarked(url);
    }

    bool WinUIShell::ToggleBookmark(std::string const& url, std::string const& title)
    {
        const bool bookmarked = BrowserData::ToggleBookmark(url, title);
        m_owner.OnStatusTextChanged(bookmarked ? "Bookmark added." : "Bookmark removed.");
        return bookmarked;
    }

    std::vector<std::wstring> WinUIShell::OmniboxSuggestions(std::string const& text) const
    {
        std::vector<std::wstring> result;
        const std::string query = Trim(text);
        if (query.empty() || !LoadBrowserSettings().enableSuggestions) return result;

        std::vector<std::string> candidates;
        const std::string needle = ToLower(query);
        for (const HistoryItem& item : BrowserData::LoadHistory()) {
            if (ToLower(item.url).find(needle) != std::string::npos ||
                ToLower(item.title).find(needle) != std::string::npos) {
                candidates.push_back(item.url);
            }
        }
        // Static engine suggestion: the canonical search URL for the selected
        // engine, computed from the same table used by the omnibox itself.
        candidates.push_back(SearchUrl(query));
        if (IsBareHost(query)) candidates.push_back("https://" + query);

        std::unordered_set<std::string> seen;
        for (const std::string& candidate : candidates) {
            if (candidate.empty() || !seen.insert(candidate).second) continue;
            result.push_back(Wide(candidate));
            if (result.size() >= 8) break;
        }
        return result;
    }

    BrowserDownloadItem* WinUIShell::FindDownload(uint64_t id)
    {
        for (BrowserDownloadItem& item : m_downloads) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    const BrowserDownloadItem* WinUIShell::FindDownload(uint64_t id) const
    {
        for (const BrowserDownloadItem& item : m_downloads) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    void WinUIShell::NotifyDownloadItemsChanged()
    {
        CallDownloadItems(m_owner, m_downloads, 0);
    }

    uint64_t WinUIShell::TrackDownload(ServoDownloadItem* download)
    {
        char url[2048] = {};
        char name[512] = {};
        servo_download_item_get_url(download, url, sizeof(url));
        servo_download_item_get_suggested_filename(download, name, sizeof(name));
        const std::string fileName = SanitizedFileName(name, url);
        BrowserDownloadItem item{};
        item.id = m_nextDownloadId++;
        item.url = url;
        item.fileName = fileName;
        item.path = DownloadPathFor(fileName);
        item.received = 0;
        item.total = 0;
        item.state = 0; // SERVO_DOWNLOAD_IN_PROGRESS / BrowserDownloadState::Active
        m_downloads.push_back(item);
        m_downloadIdsByUrl[item.url] = item.id;
        NotifyDownloadItemsChanged();
        return item.id;
    }

    void WinUIShell::HandleNewDownload(ServoDownloadItem* download)
    {
        if (!download) return;
        const uint64_t id = TrackDownload(download);
        const BrowserDownloadItem* item = FindDownload(id);
        m_owner.OnDownloadStarted(item ? item->fileName : "Download started");
    }

    void WinUIShell::HandleDownloadUpdated(ServoDownloadItem* download)
    {
        if (!download) return;
        char url[2048] = {};
        servo_download_item_get_url(download, url, sizeof(url));
        auto known = m_downloadIdsByUrl.find(url);
        const uint64_t id = known != m_downloadIdsByUrl.end() ? known->second : TrackDownload(download);
        BrowserDownloadItem* item = FindDownload(id);
        if (!item) return;

        const uint64_t received = servo_download_item_get_received_bytes(download);
        const uint64_t total = servo_download_item_get_total_bytes(download);
        const int64_t maxValue = std::numeric_limits<int64_t>::max();
        item->received = static_cast<int64_t>(std::min<uint64_t>(received, static_cast<uint64_t>(maxValue)));
        item->total = static_cast<int64_t>(std::min<uint64_t>(total, static_cast<uint64_t>(maxValue)));
        // Servo reports 0=in progress, 1=complete, 2=cancelled, 3=interrupted;
        // the shell contract uses the same numbers (3 is shown as "failed").
        item->state = std::clamp(servo_download_item_get_state(download), 0, 3);
        m_owner.OnDownloadUpdated(item->url, item->received, item->total);
        NotifyDownloadItemsChanged();
    }

    std::vector<BrowserDownloadItem> WinUIShell::Downloads() const
    {
        return m_downloads;
    }

    void WinUIShell::OpenDownload(uint64_t id)
    {
        const BrowserDownloadItem* item = FindDownload(id);
        if (!item || item->path.empty()) {
            m_owner.OnStatusTextChanged("Download has no local file yet.");
            return;
        }
        const std::wstring path = Wide(item->path);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            m_owner.OnStatusTextChanged("The downloaded file is no longer available.");
            return;
        }
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            m_owner.OnStatusTextChanged("Could not open the downloaded file.");
        }
    }

    void WinUIShell::ShowDownloadInFolder(uint64_t id)
    {
        const BrowserDownloadItem* item = FindDownload(id);
        if (!item || item->path.empty()) {
            m_owner.OnStatusTextChanged("Download has no local file yet.");
            return;
        }
        const std::wstring args = L"/select,\"" + Wide(item->path) + L"\"";
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(),
                                               nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            m_owner.OnStatusTextChanged("Could not open the download folder.");
        }
    }

    void WinUIShell::CancelDownload(uint64_t id)
    {
        BrowserDownloadItem* item = FindDownload(id);
        if (!item || item->state != 0) return;
        // ServoDownloadItem handles are owned by the engine and recreated for
        // every callback, so retaining one here would be a use-after-free.
        // Cancellation itself is currently unsupported by the C API; reflect
        // the user's action in the local list without claiming engine success.
        item->state = 2; // BrowserDownloadState::Canceled
        NotifyDownloadItemsChanged();
        m_owner.OnStatusTextChanged("Download cancelled locally (engine cancel is not supported by the current C API).");
    }

    void WinUIShell::ClearDownloads()
    {
        m_downloads.clear();
        m_downloadIdsByUrl.clear();
        NotifyDownloadItemsChanged();
    }

    void WinUIShell::ClearBrowsingData(int mask)
    {
        // Use the shared helper: it clears local history/permissions and calls
        // the runtime hook that enumerates sites before clearing storage. The
        // old null-list site-data call cleared nothing, so it must not remain.
        const ClearBrowsingDataResult result = BrowserSettings::ClearBrowsingData(mask);

        static const char* const kNames[] = {
            "history", "cookies", "cache", "site data", "passwords"
        };

        std::ostringstream message;
        if (result.clearedMask == 0) {
            message << "No browsing data was cleared.";
        } else {
            message << "Cleared: ";
            bool first = true;
            for (int bit = 0; bit < 5; ++bit) {
                if (!(result.clearedMask & (1 << bit))) continue;
                if (!first) message << ", ";
                first = false;
                message << kNames[bit];
            }
            message << ".";
        }
        if (!result.details.empty()) {
            message << ' ' << result.details;
        }
        m_owner.OnStatusTextChanged(message.str());
    }

    std::wstring WinUIShell::DefaultBrowserStatus() const
    {
        constexpr wchar_t key[] =
            L"SOFTWARE\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\UserChoice";
        wchar_t progId[256] = {};
        DWORD size = sizeof(progId);
        if (RegGetValueW(HKEY_CURRENT_USER, key, L"ProgId", RRF_RT_REG_SZ, nullptr,
                         progId, &size) != ERROR_SUCCESS) {
            return L"Default browser: unknown";
        }
        std::wstring id(progId);
        std::wstring lower;
        for (wchar_t c : id) lower += static_cast<wchar_t>(std::towlower(c));
        if (lower.find(L"servo") != std::wstring::npos) return L"Servo Browser is the default browser";
        return L"Default browser: " + id;
    }

    void WinUIShell::MakeDefaultBrowser()
    {
        // UserChoice is protected by a hash; always hand off to the OS flow.
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps",
                                               nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            m_owner.OnStatusTextChanged("Could not open Windows default-apps settings.");
        }
    }

    void WinUIShell::SetPermissionForOrigin(std::string const& origin, int32_t permission, bool allow)
    {
        SetSitePermission(origin, permission, allow);
        m_owner.OnStatusTextChanged(std::string("Permission updated for ") +
                                    (origin.empty() ? "this site" : origin) + ".");
    }

    void WinUIShell::SetFullscreen(bool fullscreen)
    {
        ServoViewHost* view = ActiveView();
        if (view && view->GetWebView()) {
            servo_webview_set_fullscreen(view->GetWebView(), fullscreen);
        }
        m_fullscreen = fullscreen;
        CallFullscreenChanged(m_owner, fullscreen, 0);
    }

    void WinUIShell::OnNavigationError(ServoViewHost*, int code, const char* text)
    {
        // ServoViewHost owns the error interstitial (BrowserData::EnsureErrorPageExists).
        // The shell surfaces the banner and status so the page is not loaded twice.
        const std::string reason = text ? text : "Navigation failed";
        CallNavigationError(m_owner, code, Wide(reason));
        m_owner.OnStatusTextChanged(("Navigation failed: " + reason).c_str());
    }

    void WinUIShell::OnNavigationSecurityChanged(ServoViewHost*, bool secure, const char* hostName)
    {
        CallSecurityChanged(m_owner, secure, Wide(hostName ? hostName : ""));
    }

    void WinUIShell::OnZoomChanged(ServoViewHost*, double zoom)
    {
        m_zoom = static_cast<float>(std::clamp(zoom, 0.25, 5.0));
        CallZoomChanged(m_owner, zoom, 0);
    }

    const char* WinUIShell::PermissionName(int32_t permission)
    {
        // ServoPermissionType (SERVO_PERMISSION_*): 0 geolocation,
        // 1 notifications, 2 push, 3 midi, 4 camera, 5 microphone, 6 speaker,
        // 7 device info, 8 background sync, 9 bluetooth, 10 persistent storage,
        // 11 screen wake lock, 12 gamepad. Group the capture devices as
        // "media"; keep everything else deliberately vague for the prompt.
        switch (permission) {
        case 0: return "geolocation";
        case 1: return "notifications";
        case 4: case 5: case 6: return "media";
        default: return "this site";
        }
    }

    void WinUIShell::RequestPermission(ServoViewHost*, const char* origin, int32_t permission,
                                       void (*callback)(bool, void*), void* callbackUserData,
                                       void* userData)
    {
        (void)userData; // delegate user data; the wrapped callback carries ours
        if (!callback) return;
        auto done = std::make_shared<bool>(false);
        std::function<void(bool)> wrapped = [callback, callbackUserData, done](bool allow) {
            if (*done) return;
            *done = true;
            callback(allow, callbackUserData);
        };
        const std::wstring originWide = Wide(origin ? origin : "");
        const std::wstring nameWide = Wide(PermissionName(permission));
        MainWindow* owner = &m_owner;
        PostUiTask([owner, originWide, nameWide, wrapped]() {
            CallPermission(*owner, originWide, nameWide, wrapped, 0);
        });
    }

    void WinUIShell::RequestAuthentication(ServoViewHost*, const char* host, int32_t port,
                                           const char* realm,
                                           void (*callback)(const char*, const char*, void*),
                                           void* userData)
    {
        if (!callback) return;
        auto done = std::make_shared<bool>(false);
        std::function<void(std::wstring, std::wstring)> wrapped =
            [callback, userData, done](std::wstring username, std::wstring password) {
                if (*done) return;
                *done = true;
                const std::string userUtf8 = Utf8(username);
                const std::string passwordUtf8 = Utf8(password);
                callback(userUtf8.c_str(), passwordUtf8.c_str(), userData);
            };
        std::string authority = host ? host : "";
        authority += ":" + std::to_string(port);
        const std::wstring originWide = Wide(authority);
        const std::wstring realmWide = Wide(realm ? realm : "Authentication required");
        MainWindow* owner = &m_owner;
        PostUiTask([owner, originWide, realmWide, wrapped]() {
            CallAuthentication(*owner, originWide, realmWide, wrapped, 0);
        });
    }

    void WinUIShell::RequestPrompt(ServoViewHost*, const char* message, const char* defaultValue,
                                   void (*callback)(const char*, void*), void* userData)
    {
        if (!callback) return;
        auto done = std::make_shared<bool>(false);
        std::function<void(std::wstring)> wrapped = [callback, userData, done](std::wstring answer) {
            if (*done) return;
            *done = true;
            const std::string answerUtf8 = Utf8(answer);
            callback(answerUtf8.c_str(), userData);
        };
        const std::wstring messageWide = Wide(message ? message : "");
        const std::wstring defaultWide = Wide(defaultValue ? defaultValue : "");
        MainWindow* owner = &m_owner;
        PostUiTask([owner, messageWide, defaultWide, wrapped]() {
            CallPrompt(*owner, messageWide, defaultWide, wrapped, 0);
        });
    }

    std::string WinUIShell::GetSettingsPageUrl()
    {
        // The native XAML pane is opened by the shell; leave the current page
        // loaded instead of navigating to an HTML settings imitation.
        m_owner.OpenSettingsPane();
        ServoViewHost* view = ActiveView();
        std::string current = view ? view->GetUrl() : std::string();
        return current.empty() ? "about:blank" : current;
    }

    bool WinUIShell::CreatePopup(Servo*, ServoCreateNewWebViewRequest* request)
    {
        if (!request) return false;
        return m_owner.OpenPopupTab(request);
    }

    bool WinUIShell::UsesCompositionHosting() const
    {
        // WinUI 3 paints XAML through a composition surface that covers
        // ordinary child HWNDs. Layered child windows are composited above it.
        return true;
    }
}
