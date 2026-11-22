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
#include <iomanip>
#include <limits>
#include <sstream>

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

        m_tabs.push_back(std::move(host));
        m_active = static_cast<uint32_t>(m_tabs.size() - 1);
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i]->Show(i == m_active);
        }
        ApplyViewportToActive();
        return true;
    }

    void WinUIShell::CloseTab(uint32_t index)
    {
        if (index >= m_tabs.size()) return;
        m_tabs.erase(m_tabs.begin() + static_cast<ptrdiff_t>(index));
        if (m_tabs.empty()) {
            m_active = 0;
            return;
        }
        if (m_active >= m_tabs.size()) {
            m_active = static_cast<uint32_t>(m_tabs.size() - 1);
        }
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i]->Show(i == m_active);
        }
        ApplyViewportToActive();
    }

    void WinUIShell::SelectTab(uint32_t index)
    {
        if (index >= m_tabs.size() || index == m_active) return;
        m_active = index;
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            m_tabs[i]->Show(i == m_active);
        }
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

    void WinUIShell::Find(const std::string& text, bool forward, bool matchCase)
    {
        ServoViewHost* view = ActiveView();
        if (!view || !view->GetWebView()) return;
        if (Trim(text).empty()) {
            ClearFind();
            return;
        }
        if (m_findIdentifier < 0 || text != m_findText) {
            m_findIdentifier = servo_webview_find(view->GetWebView(), text.c_str(), forward, matchCase);
            m_findText = text;
        } else {
            servo_webview_find_next(view->GetWebView(), m_findIdentifier, forward);
        }
    }

    void WinUIShell::ClearFind()
    {
        ServoViewHost* view = ActiveView();
        if (view && view->GetWebView() && m_findIdentifier >= 0) {
            servo_webview_find_clear(view->GetWebView(), m_findIdentifier);
        }
        m_findIdentifier = -1;
        m_findText.clear();
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

    void WinUIShell::OnTabFaviconUpdated(ServoViewHost*, HBITMAP, uint32_t, uint32_t)
    {
        // XAML tab headers use native glyphs for now. Favicon bitmaps are not
        // painted through GDI, so there is no old hBitmap to present here.
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
        if (engine.find("google") != std::string::npos) {
            return "https://www.google.com/search?q=" + query;
        }
        if (engine.find("bing") != std::string::npos) {
            return "https://www.bing.com/search?q=" + query;
        }
        return "https://duckduckgo.com/?q=" + query;
    }

    void WinUIShell::NavigateUrl(const std::string& url)
    {
        const std::string clean = Trim(url);
        if (clean.empty()) return;
        const std::string lower = ToLower(clean);

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
            LoadInActiveTab(BrowserData::EnsureBookmarksPageExists());
            return;
        }
        if (lower == "servo://history" || lower == "servo:history" || lower == "about:history" || lower == "history") {
            LoadInActiveTab(BrowserData::EnsureHistoryPageExists());
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

        bool looksLikeUrl = clean.find("://") != std::string::npos ||
            lower.rfind("about:", 0) == 0 || lower.rfind("data:", 0) == 0 ||
            lower.rfind("file:", 0) == 0 || lower.rfind("servo:", 0) == 0 ||
            lower.rfind("view-source:", 0) == 0;
        if (!looksLikeUrl && clean.find(' ') == std::string::npos && clean.find('.') != std::string::npos) {
            looksLikeUrl = true;
        }
        LoadInActiveTab(looksLikeUrl ? clean : SearchUrl(clean));
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
        m_owner.OnStatusTextChanged("DevTools server is disabled in this lightweight build.");
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

    void WinUIShell::HandleNewDownload(ServoDownloadItem* download)
    {
        if (!download) return;
        char url[1024] = {};
        char name[256] = {};
        servo_download_item_get_url(download, url, sizeof(url));
        servo_download_item_get_suggested_filename(download, name, sizeof(name));
        m_owner.OnDownloadStarted(name[0] ? name : url);
    }

    void WinUIShell::HandleDownloadUpdated(ServoDownloadItem* download)
    {
        if (!download) return;
        char url[1024] = {};
        servo_download_item_get_url(download, url, sizeof(url));
        uint64_t received = servo_download_item_get_received_bytes(download);
        uint64_t total = servo_download_item_get_total_bytes(download);
        m_owner.OnDownloadUpdated(url, static_cast<int64_t>(received), static_cast<int64_t>(total));
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
}
