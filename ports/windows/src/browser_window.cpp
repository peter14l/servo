/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "browser_window.h"
#include "fluent_theme.h"
#include "tab_strip.h"
#include "omnibox.h"
#include "find_bar.h"
#include "download_manager.h"
#include "settings_dialog.h"
#include "new_tab_page.h"
#include "browser_data.h"
#include "password_manager.h"
#include "servo_view_host.h"
#include <windowsx.h>
#include <commdlg.h>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

#ifndef DWMSBT_TABBEDWINDOW
#define DWMSBT_TABBEDWINDOW 4 // Mica Alt
#endif

static const wchar_t* BROWSER_WINDOW_CLASS_NAME = L"ServoBrowserMainWindow";
static const int TAB_STRIP_HEIGHT = 38;
static const int OMNIBOX_HEIGHT = 44;
static const int TOP_CHROME_HEIGHT = TAB_STRIP_HEIGHT + OMNIBOX_HEIGHT; // 82px (Tabs + Omnibox)

void BrowserWindow::RegisterWindowClass(HINSTANCE hInstance) {
    if (!hInstance) {
        hInstance = GetModuleHandleW(nullptr);
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = BrowserWindow::WndProc;
    wc.hInstance = hInstance;
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!hIcon) hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = BROWSER_WINDOW_CLASS_NAME;
    RegisterClassExW(&wc);
}

BrowserWindow* BrowserWindow::Create(struct Servo* servo, const std::string& initialUrl) {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    auto window = new BrowserWindow(servo);

    std::string startUrl = initialUrl.empty() ? NewTabPage::EnsureNewTabPageExists() : initialUrl;

    LogDebug("CreateWindowExW starting for BrowserWindow, hInstance=0x%p...", hInstance);
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        BROWSER_WINDOW_CLASS_NAME,
        L"Servo Browser",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        nullptr,
        nullptr,
        hInstance,
        window
    );
    LogDebug("CreateWindowExW for BrowserWindow returned hwnd=0x%p, GetLastError=%lu", hwnd, GetLastError());

    if (!hwnd) {
        DWORD err = GetLastError();
        LogDebug("BrowserWindow creation failed with error %lu", err);
        wchar_t buf[256];
        swprintf_s(buf, L"Failed to create main browser window (CreateWindowExW error %lu).", err);
        MessageBoxW(nullptr, buf, L"Servo Browser Error", MB_ICONERROR);
        delete window;
        return nullptr;
    }

    window->m_hwnd = hwnd;

    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (hIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
    }

    // Custom-drawn title bar: DWM paints the native system icon and the
    // minimize/maximize/close buttons from these three style bits. Leaving
    // them set is what makes a second set of controls appear on top of ours.
    // Clear ONLY those bits. Keep WS_CAPTION / WS_BORDER / WS_DLGFRAME and
    // WS_THICKFRAME: clearing WS_CAPTION makes User32 fall back to a legacy
    // sizing border that flashes in when switching apps, and it disrupts the
    // Mica Alt / rounded-corner / shadow composition. WM_NCCALCSIZE returns 0
    // so the whole window is still client area. Alt+F4 is handled explicitly
    // in WM_SYSKEYDOWN now that WS_SYSMENU is gone.
    DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~(WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowLongW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    LogDebug("Applying Mica Alt theme...");
    window->ApplyMicaAltTheme();

    LogDebug("Creating child UI components...");
    // Create child UI components
    window->m_tabStrip = std::make_unique<TabStrip>(hwnd, window);
    window->m_omnibox = std::make_unique<OmniboxToolbar>(hwnd, window);
    window->m_findBar = std::make_unique<FindBar>(hwnd, window);
    window->m_downloadManager = std::make_unique<DownloadManager>(hwnd);

    LogDebug("Creating single viewport ServoViewHost with URL %s...", startUrl.c_str());
    window->m_viewHost = std::make_unique<ServoViewHost>(
        hwnd,
        servo,
        window,
        startUrl
    );

    // Sync dark theme to webview now that viewHost is initialized
    window->SyncDarkMode();

    LogDebug("Adding initial tab...");
    // Create initial tab
    window->m_tabStrip->AddTab(startUrl);
    window->LayoutChildren();

    LogDebug("Showing window...");
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);

    // Idle fallback tick timer at 100 ms (10/s instead of the old 33 ms/30/s).
    // Servo's waker drives active processing reactively; this is only a safety
    // net for engine implementations that queue work without waking.
    SetTimer(hwnd, 1, 100, nullptr);

    LogDebug("BrowserWindow::Create completed successfully.");
    return window;
}

BrowserWindow* BrowserWindow::CreateForPopup(struct Servo* servo, struct ServoCreateNewWebViewRequest* request) {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    auto window = new BrowserWindow(servo);

    LogDebug("CreateWindowExW starting for popup window...");
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        BROWSER_WINDOW_CLASS_NAME,
        L"Servo Browser Call",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 720,
        nullptr,
        nullptr,
        hInstance,
        window
    );
    if (!hwnd) {
        LogDebug("CreateWindowExW for popup failed with error %lu", GetLastError());
        // Do not free `request` here: returning nullptr makes
        // OnRequestCreateNew return false, and the Rust side frees the handle
        // in that case. Freeing it here as well would be a double free.
        delete window;
        return nullptr;
    }

    window->m_hwnd = hwnd;
    window->m_isPopup = true;

    // Same custom-chrome style fix as the main window: clear only the bits
    // that make DWM paint native caption buttons, while keeping WS_CAPTION so
    // Mica composition and rounded corners survive activation changes.
    // Alt+F4 is restored explicitly in WM_SYSKEYDOWN.
    DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~(WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowLongW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    window->ApplyMicaAltTheme();

    window->m_tabStrip = std::make_unique<TabStrip>(hwnd, window);
    window->m_omnibox = std::make_unique<OmniboxToolbar>(hwnd, window);
    window->m_findBar = std::make_unique<FindBar>(hwnd, window);
    window->m_downloadManager = std::make_unique<DownloadManager>(hwnd);

    LogDebug("Creating popup viewport ServoViewHost with request 0x%p...", request);
    window->m_viewHost = std::make_unique<ServoViewHost>(
        hwnd,
        servo,
        window,
        request
    );

    // The create request is owned by whichever side consumes it. If the
    // popup constructor could not hand it to a WebView builder, leave it
    // untouched so the Rust callback frees it after we return false.
    if (!window->m_viewHost->WasRequestConsumed()) {
        LogDebug("Popup WebView request was not consumed; denying popup.");
        window->m_viewHost.reset();
        DestroyWindow(hwnd);
        // Leave the shell object allocated: normal shutdown uses ExitProcess
        // without running BrowserWindow destructors, so their UI-teardown path
        // is intentionally not exercised on this failure path either.
        return nullptr;
    }

    window->SyncDarkMode();
    window->m_tabStrip->AddTab("about:blank");
    window->LayoutChildren();

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);

    SetTimer(hwnd, 1, 100, nullptr);
    LogDebug("BrowserWindow::CreateForPopup completed successfully.");
    return window;
}

BrowserWindow::BrowserWindow(struct Servo* servo)
    : m_hwnd(nullptr)
    , m_servo(servo)
    , m_devtoolsPort(0)
    , m_isDark(true)
    , m_isFullscreen(false)
    , m_savedPlacement{ sizeof(WINDOWPLACEMENT) }
{
}

BrowserWindow::~BrowserWindow() = default;

void BrowserWindow::RefreshDwmFrame() {
    if (!m_hwnd) return;
    int backdrop = DWMSBT_TABBEDWINDOW;
    DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    BOOL darkMode = m_isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);
}

void BrowserWindow::ApplyMicaAltTheme() {
    if (!m_hwnd) return;
    LogDebug("Applying Mica Alt theme...");

    // 1. Sync dark mode
    SyncDarkMode();

    // 2. Enable Windows 11 Mica Alt (DWMSBT_TABBEDWINDOW = 4)
    int backdrop = DWMSBT_TABBEDWINDOW;
    HRESULT hr = DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    LogDebug("DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_TABBEDWINDOW) returned 0x%08X", hr);

    // 3. Extend frame into client area for seamless titlebar
    MARGINS margins = { -1 };
    hr = DwmExtendFrameIntoClientArea(m_hwnd, &margins);
    LogDebug("DwmExtendFrameIntoClientArea returned 0x%08X", hr);
}

void BrowserWindow::SyncDarkMode() {
    BOOL darkMode = TRUE; // Default to dark for modern Mica Alt
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    m_isDark = darkMode != FALSE;

    if (m_tabStrip) {
        m_tabStrip->SetDarkMode(m_isDark);
    }
    if (m_omnibox) {
        m_omnibox->SetDarkMode(m_isDark);
    }

    if (m_viewHost && m_viewHost->GetWebView()) {
        servo_webview_notify_theme_change(
            m_viewHost->GetWebView(),
            m_isDark ? SERVO_THEME_DARK : SERVO_THEME_LIGHT
        );
    }
}

void BrowserWindow::LayoutChildren() {
    if (!m_hwnd) return;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int width = rc.right;
    int height = rc.bottom;

    const int VERTICAL_TAB_WIDTH = 220;
    // The vertical tab strip is a floating card: keep a small gap so the
    // Mica Alt backdrop is visible around its rounded edges.
    const int FLOAT_GAP = 8;

    int contentX = 0;
    int contentY = TOP_CHROME_HEIGHT;
    int contentW = width;
    int contentH = std::max(0, height - contentY);

    // Caption button bounds for top bar when in vertical tabs mode
    m_topBarToggleBtnBounds = { 8, 4, 40, TAB_STRIP_HEIGHT - 4 };
    m_topBarCloseBtnBounds = { width - 46, 0, width, TAB_STRIP_HEIGHT };
    m_topBarMaxBtnBounds = { width - 92, 0, width - 46, TAB_STRIP_HEIGHT };
    m_topBarMinBtnBounds = { width - 138, 0, width - 92, TAB_STRIP_HEIGHT };

    if (m_verticalTabs) {
        if (m_tabStrip) {
            m_tabStrip->SetVertical(true);
            m_tabStrip->SetBounds(FLOAT_GAP, TAB_STRIP_HEIGHT + FLOAT_GAP,
                                  VERTICAL_TAB_WIDTH - FLOAT_GAP * 2,
                                  std::max(0, height - TAB_STRIP_HEIGHT - FLOAT_GAP * 2));
        }
        if (m_omnibox) {
            m_omnibox->SetBounds(VERTICAL_TAB_WIDTH, TAB_STRIP_HEIGHT, std::max(0, width - VERTICAL_TAB_WIDTH), OMNIBOX_HEIGHT);
        }
        contentX = VERTICAL_TAB_WIDTH;
        contentY = TOP_CHROME_HEIGHT;
        contentW = std::max(0, width - VERTICAL_TAB_WIDTH);
        contentH = std::max(0, height - contentY);
    } else {
        if (m_tabStrip) {
            m_tabStrip->SetVertical(false);
            m_tabStrip->SetBounds(0, 0, width, TAB_STRIP_HEIGHT);
        }
        if (m_omnibox) {
            m_omnibox->SetBounds(0, TAB_STRIP_HEIGHT, width, OMNIBOX_HEIGHT);
        }
    }

    if (m_viewHost) {
        m_viewHost->SetBounds(contentX, contentY, contentW, contentH);
    }

    if (m_findBar) {
        m_findBar->SetBounds(width - 340, 88, 320, 42);
    }

    if (m_downloadManager) {
        m_downloadManager->SetBounds(width - 380, 88, 360, 240);
    }
}

void BrowserWindow::ToggleVerticalTabs() {
    SetVerticalTabs(!m_verticalTabs);
}

void BrowserWindow::SetVerticalTabs(bool enable) {
    if (m_verticalTabs != enable) {
        m_verticalTabs = enable;
        LayoutChildren();
        if (m_hwnd) {
            InvalidateRect(m_hwnd, nullptr, TRUE);
        }
    }
}

void BrowserWindow::OpenDevTools() {
    if (m_devtoolsPort > 0) {
        std::string devtoolsUrl = "http://localhost:" + std::to_string(m_devtoolsPort);
        if (!m_devtoolsToken.empty()) {
            devtoolsUrl += "/?token=" + m_devtoolsToken;
        }
        if (m_tabStrip) {
            m_tabStrip->AddTab(devtoolsUrl);
        }
    } else {
        MessageBoxW(m_hwnd, L"DevTools server is initializing or listening on local port.", L"DevTools", MB_OK | MB_ICONINFORMATION);
    }
}

void BrowserWindow::SetDevToolsInfo(uint16_t port, const std::string& token) {
    m_devtoolsPort = port;
    m_devtoolsToken = token;
}

void BrowserWindow::HandleNewDownload(struct ServoDownloadItem* download) {
    if (!m_downloadManager || !download) return;
    char urlBuf[1024] = { 0 };
    servo_download_item_get_url(download, urlBuf, sizeof(urlBuf));

    char nameBuf[256] = { 0 };
    servo_download_item_get_suggested_filename(download, nameBuf, sizeof(nameBuf));

    int len = MultiByteToWideChar(CP_UTF8, 0, nameBuf, -1, nullptr, 0);
    std::vector<wchar_t> wname(len);
    MultiByteToWideChar(CP_UTF8, 0, nameBuf, -1, wname.data(), len);

    uint64_t downloadId = m_nextDownloadId++;
    m_downloadIds[std::string(urlBuf)] = downloadId;
    m_downloadManager->AddDownload(downloadId, urlBuf, wname.data());

    // Automatically pop open download manager pane when download starts
    m_downloadManager->Show(true);
}

void BrowserWindow::HandleDownloadUpdated(struct ServoDownloadItem* download) {
    if (!m_downloadManager || !download) return;
    uint64_t recv = servo_download_item_get_received_bytes(download);
    uint64_t total = servo_download_item_get_total_bytes(download);
    int state = servo_download_item_get_state(download);
    char urlBuf[1024] = {};
    servo_download_item_get_url(download, urlBuf, sizeof(urlBuf));
    auto it = m_downloadIds.find(urlBuf);
    if (it == m_downloadIds.end()) {
        LogDebug("[DOWNLOAD] update for unknown download URL '%s'; ignoring.", urlBuf);
        return;
    }
    m_downloadManager->UpdateDownload(it->second, recv, total, state);
}

void BrowserWindow::NavigateUrl(const std::string& url) {
    std::string clean = url;
    while (!clean.empty() && (clean.back() == '/' || clean.back() == ' ')) {
        clean.pop_back();
    }
    if (clean == "servo://native-settings" || clean == "about:native-settings") {
        SettingsDialog::ShowModal(m_hwnd, this);
        return;
    }
    if (clean == "about:settings" || clean == "servo://settings" || clean == "servo:settings" || clean == "edge://settings" || clean == "chrome://settings" || clean == "settings") {
        OpenSettings();
        return;
    }
    if (clean == "servo://bookmarks" || clean == "servo:bookmarks" || clean == "about:bookmarks" || clean == "bookmarks") {
        OpenBookmarks();
        return;
    }
    if (clean == "servo://history" || clean == "servo:history" || clean == "about:history" || clean == "history") {
        OpenHistory();
        return;
    }
    if (clean == "servo://about" || clean == "servo:about" || clean == "about:about" || clean == "servo://version" || clean == "about:version" || clean == "about") {
        OpenAbout();
        return;
    }
    if (clean == "servo://clear-history" || clean == "servo:clear-history") {
        BrowserData::ClearHistory();
        OpenHistory();
        return;
    }
    if (clean == "servo://session-restore" || clean == "servo:session-restore" || clean == "about:session-restore") {
        RestorePreviousSession();
        return;
    }
    if (url.rfind("servo://apply-settings?", 0) == 0 || url.rfind("servo:apply-settings?", 0) == 0) {
        size_t qPos = url.find('?');
        if (qPos != std::string::npos) {
            HandleWebIpcMessage("apply_settings:" + url.substr(qPos + 1));
        }
        return;
    }
    if (clean == "servo://newtab" || clean == "servo:newtab" || clean == "about:newtab" || clean == "newtab") {
        if (m_viewHost) m_viewHost->LoadUrl(NewTabPage::EnsureNewTabPageExists());
        return;
    }
    if (m_viewHost) {
        m_viewHost->LoadUrl(url);
    }
}

void BrowserWindow::ToggleCurrentPageBookmark() {
    if (m_currentUrl.empty()) return;
    bool isBookmarked = BrowserData::ToggleBookmark(m_currentUrl, m_currentTitle);
    if (m_omnibox) {
        m_omnibox->SetBookmarked(isBookmarked);
    }
    LogDebug("[BOOKMARK] URL '%s' is now %s", m_currentUrl.c_str(), isBookmarked ? "bookmarked" : "unbookmarked");
}

void BrowserWindow::OpenBookmarks() {
    std::string bookmarksUrl = BrowserData::EnsureBookmarksPageExists();
    if (m_viewHost) m_viewHost->LoadUrl(bookmarksUrl);
}

void BrowserWindow::OpenHistory() {
    std::string historyUrl = BrowserData::EnsureHistoryPageExists();
    if (m_viewHost) m_viewHost->LoadUrl(historyUrl);
}

void BrowserWindow::OpenAbout() {
    std::string aboutUrl = BrowserData::EnsureAboutPageExists();
    if (m_viewHost) m_viewHost->LoadUrl(aboutUrl);
}

void BrowserWindow::OnStatusTextChanged(ServoViewHost* /*host*/, const char* statusText) {
    if (!statusText || !*statusText) {
        if (m_statusPillHwnd) {
            DestroyWindow(m_statusPillHwnd);
            m_statusPillHwnd = nullptr;
        }
        return;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, statusText, -1, nullptr, 0);
    if (wlen <= 1) return;
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, statusText, -1, wbuf.data(), wlen);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int pillW = (std::min)(static_cast<int>(rc.right - 40), static_cast<int>(wlen * 7 + 24));
    int pillH = 22;
    int pillX = 14;
    int pillY = rc.bottom - pillH - 12;

    if (!m_statusPillHwnd) {
        m_statusPillHwnd = CreateWindowExW(
            WS_EX_TOPMOST, L"STATIC", wbuf.data(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
            pillX, pillY, pillW, pillH,
            m_hwnd, nullptr, GetModuleHandle(nullptr), nullptr
        );
        HFONT hFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SendMessageW(m_statusPillHwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    } else {
        SetWindowPos(m_statusPillHwnd, HWND_TOP, pillX, pillY, pillW, pillH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowTextW(m_statusPillHwnd, wbuf.data());
    }
}

void BrowserWindow::NavigateBack() {
    if (m_viewHost) {
        m_viewHost->GoBack();
    }
}

void BrowserWindow::NavigateForward() {
    if (m_viewHost) {
        m_viewHost->GoForward();
    }
}

void BrowserWindow::NavigateReload() {
    if (m_viewHost) {
        m_viewHost->Reload();
    }
}

void BrowserWindow::NavigateStop() {
    if (m_viewHost) {
        m_viewHost->Stop();
    }
}

int BrowserWindow::FindInPage(const char* text, bool forward, bool matchCase) {
    if (m_viewHost && m_viewHost->GetWebView()) {
        m_findIdentifier = servo_webview_find(m_viewHost->GetWebView(), text, forward, matchCase);
        return m_findIdentifier;
    }
    return -1;
}

int BrowserWindow::FindNext(int identifier, bool forward) {
    if (m_viewHost && m_viewHost->GetWebView()) {
        return servo_webview_find_next(m_viewHost->GetWebView(), identifier, forward);
    }
    return -1;
}

void BrowserWindow::ClearFind(int identifier) {
    if (m_viewHost && m_viewHost->GetWebView()) {
        servo_webview_find_clear(m_viewHost->GetWebView(), identifier);
    }
    if (identifier == m_findIdentifier) {
        m_findIdentifier = -1;
    }
}

void BrowserWindow::OnFindResult(ServoViewHost* /*host*/, int identifier, int count, int activeOrdinal) {
    if (!m_findBar) return;
    // Ignore results from a previous search so the status cannot flicker.
    if (m_findIdentifier >= 0 && identifier != m_findIdentifier) return;
    if (count <= 0) {
        m_findBar->UpdateMatchResult(0, 0);
    } else {
        m_findBar->UpdateMatchResult(activeOrdinal > 0 ? activeOrdinal : 1, count);
    }
}

void BrowserWindow::ZoomIn() {
    if (m_viewHost && m_viewHost->GetWebView()) {
        float currentZoom = servo_webview_page_zoom(m_viewHost->GetWebView());
        float nextZoom = std::min(currentZoom * 1.2f, 5.0f);
        servo_webview_set_page_zoom(m_viewHost->GetWebView(), nextZoom);
    }
}

void BrowserWindow::ZoomOut() {
    if (m_viewHost && m_viewHost->GetWebView()) {
        float currentZoom = servo_webview_page_zoom(m_viewHost->GetWebView());
        float nextZoom = std::max(currentZoom / 1.2f, 0.25f);
        servo_webview_set_page_zoom(m_viewHost->GetWebView(), nextZoom);
    }
}

void BrowserWindow::ZoomReset() {
    if (m_viewHost && m_viewHost->GetWebView()) {
        servo_webview_set_page_zoom(m_viewHost->GetWebView(), 1.0f);
    }
}

void BrowserWindow::ToggleFullscreen() {
    DWORD style = GetWindowLongW(m_hwnd, GWL_STYLE);
    if (!m_isFullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(m_hwnd, &m_savedPlacement) &&
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(m_hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_isFullscreen = true;
        }
    } else {
        SetWindowLongW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(m_hwnd, &m_savedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_isFullscreen = false;
    }
}

void BrowserWindow::ToggleDownloads() {
    if (m_downloadManager) {
        m_downloadManager->Show(!m_downloadManager->IsVisible());
    }
}

void BrowserWindow::OpenSettings() {
    std::string settingsUrl = SettingsDialog::EnsureSettingsPageExists();
    OpenUrlInNewTab(settingsUrl);
}

std::string BrowserWindow::GetSettingsPageUrl() {
    return SettingsDialog::EnsureSettingsPageExists();
}

bool BrowserWindow::CreatePopup(struct Servo* servo, struct ServoCreateNewWebViewRequest* request) {
    return BrowserWindow::CreateForPopup(servo, request) != nullptr;
}

void BrowserWindow::OpenUrlInNewTab(const std::string& url) {
    std::string targetUrl = url.empty() ? NewTabPage::EnsureNewTabPageExists() : url;
    if (m_tabStrip) {
        m_tabStrip->AddTab(targetUrl);
    }
}

void BrowserWindow::RestorePreviousSession() {
    std::vector<std::string> urls;
    int savedActiveIndex = 0;
    if (!BrowserData::LoadSession(urls, savedActiveIndex) || urls.empty()) {
        if (m_tabStrip) {
            m_tabStrip->ReopenClosedTab();
        }
        return;
    }

    if (!m_tabStrip) return;

    bool replaceInitialTab = false;
    if (m_tabStrip->GetTabCount() == 1) {
        auto* tab = m_tabStrip->GetActiveTab();
        if (tab && (tab->url == "about:blank" || tab->url.find("newtab") != std::string::npos || tab->url == "servo://newtab")) {
            replaceInitialTab = true;
        }
    }

    int startIndex = replaceInitialTab ? -1 : m_tabStrip->GetTabCount();
    for (size_t i = 0; i < urls.size(); ++i) {
        m_tabStrip->AddTab(urls[i]);
    }

    if (replaceInitialTab && m_tabStrip->GetTabCount() > 1) {
        m_tabStrip->CloseTab(0);
        if (savedActiveIndex >= 0 && savedActiveIndex < m_tabStrip->GetTabCount()) {
            m_tabStrip->SelectTab(savedActiveIndex);
        }
    } else {
        int targetIdx = (startIndex >= 0) ? (startIndex + savedActiveIndex) : savedActiveIndex;
        if (targetIdx >= 0 && targetIdx < m_tabStrip->GetTabCount()) {
            m_tabStrip->SelectTab(targetIdx);
        }
    }
}

void BrowserWindow::ApplySettings(const BrowserSettings& settings) {
    if (settings.themeMode == 0) {
        m_isDark = false;
    } else if (settings.themeMode == 1) {
        m_isDark = true;
    } else {
        SyncDarkMode();
    }
    if (m_tabStrip) m_tabStrip->SetDarkMode(m_isDark);
    if (m_omnibox) m_omnibox->SetDarkMode(m_isDark);

    SetVerticalTabs(settings.enableVerticalTabs);

    if (m_servo) {
        // Authentic native desktop User-Agent - zero persona spoofing
        const char* uaString = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
        servo_set_preference_string(m_servo, "user_agent", uaString);
        servo_set_preference_bool(m_servo, "dom_indexeddb_enabled", settings.enableIndexedDB);
        servo_set_preference_bool(m_servo, "dom_webgl2_enabled", settings.enableWebGL2);
        servo_set_preference_bool(m_servo, "dom_webgpu_enabled", settings.enableWebGPU);
        servo_set_preference_bool(m_servo, "dom_webrtc_enabled", settings.enableWebRTC);
        servo_set_preference_bool(m_servo, "dom_webrtc_transceiver_enabled", settings.enableWebRTC);
        servo_set_preference_bool(m_servo, "dom_notification_enabled", settings.enableNotifications);
        servo_set_preference_bool(m_servo, "dom_geolocation_enabled", settings.enableGeolocation);
        servo_set_preference_bool(m_servo, "layout_variable_fonts_enabled", settings.enableVariableFonts);
    }
}

void BrowserWindow::HandleWebIpcMessage(const std::string& message) {
    // Log only the command token, never the payload: `prompt_save_password`
    // carries the plaintext password and `request_autofill` the saved username.
    size_t separator = message.find(':');
    std::string command = message.substr(0, separator == std::string::npos ? message.size() : separator);
    if (command.size() > 48) command.resize(48);
    LogDebug("[IPC] Received message: %s", command.c_str());
    if (message.rfind("navigate:", 0) == 0) {
        std::string targetUrl = message.substr(9);
        NavigateUrl(targetUrl);
        return;
    }
    if (message.rfind("apply_settings:", 0) == 0) {
        std::string json = message.substr(15);
        BrowserSettings s = SettingsDialog::LoadSettings();

        auto parseBool = [&](const std::string& key, bool defaultVal) -> bool {
            size_t pos = json.find("\"" + key + "\":");
            if (pos == std::string::npos) pos = json.find(key + "=");
            if (pos != std::string::npos) {
                if (json.find("true", pos) == pos + key.size() + 2 || json.find("true", pos) == pos + key.size() + 3 ||
                    json.find("1", pos) == pos + key.size() + 1 || json.find("1", pos) == pos + key.size() + 2) {
                    return true;
                }
                if (json.find("false", pos) == pos + key.size() + 2 || json.find("false", pos) == pos + key.size() + 3 ||
                    json.find("0", pos) == pos + key.size() + 1 || json.find("0", pos) == pos + key.size() + 2) {
                    return false;
                }
            }
            return defaultVal;
        };

        auto parseInt = [&](const std::string& key, int defaultVal) -> int {
            size_t pos = json.find("\"" + key + "\":");
            if (pos == std::string::npos) pos = json.find(key + "=");
            if (pos != std::string::npos) {
                size_t numStart = pos + key.size() + 1;
                while (numStart < json.size() && (json[numStart] == ':' || json[numStart] == '=' || json[numStart] == ' ' || json[numStart] == '"')) numStart++;
                return std::atoi(json.c_str() + numStart);
            }
            return defaultVal;
        };

        auto parseString = [&](const std::string& key, const std::wstring& defaultVal) -> std::wstring {
            size_t pos = json.find("\"" + key + "\":\"");
            if (pos != std::string::npos) {
                size_t strStart = pos + key.size() + 4;
                size_t strEnd = json.find("\"", strStart);
                if (strEnd != std::string::npos) {
                    std::string valA = json.substr(strStart, strEnd - strStart);
                    int len = MultiByteToWideChar(CP_UTF8, 0, valA.c_str(), -1, nullptr, 0);
                    if (len > 0) {
                        std::vector<wchar_t> wbuf(len);
                        MultiByteToWideChar(CP_UTF8, 0, valA.c_str(), -1, wbuf.data(), len);
                        return wbuf.data();
                    }
                }
            }
            return defaultVal;
        };

        s.themeMode = parseInt("theme", s.themeMode);
        s.searchEngine = parseString("search", s.searchEngine);
        s.homePage = parseString("home", s.homePage);
        s.downloadDir = parseString("dl", s.downloadDir);
        s.enableVerticalTabs = parseBool("verticalTabs", s.enableVerticalTabs);
        s.enableSuggestions = parseBool("sugg", s.enableSuggestions);
        s.enableWebGPU = parseBool("webgpu", s.enableWebGPU);
        s.enableWebGL2 = parseBool("webgl2", s.enableWebGL2);
        s.enableIndexedDB = parseBool("idb", s.enableIndexedDB);
        s.enableWebRTC = parseBool("webrtc", s.enableWebRTC);
        s.enableNotifications = parseBool("notifications", s.enableNotifications);
        s.enableGeolocation = parseBool("geolocation", s.enableGeolocation);
        s.enableVariableFonts = parseBool("varfonts", s.enableVariableFonts);

        SettingsDialog::SaveSettings(s);
        ApplySettings(s);
        return;
    }
    if (message.rfind("prompt_save_password:", 0) == 0) {
        std::string payload = message.substr(21);
        auto extractField = [](const std::string& obj, const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            size_t pos = obj.find(search);
            if (pos == std::string::npos) {
                search = "\"" + key + "\": \"";
                pos = obj.find(search);
            }
            if (pos == std::string::npos) return "";
            size_t start = pos + search.size();
            size_t end = obj.find("\"", start);
            if (end == std::string::npos) return "";
            std::string val = obj.substr(start, end - start);
            std::string out;
            for (size_t i = 0; i < val.size(); ++i) {
                if (val[i] == '\\' && i + 1 < val.size()) {
                    char next = val[++i];
                    if (next == '"') out += '"';
                    else if (next == '\\') out += '\\';
                    else if (next == 'n') out += '\n';
                    else if (next == 'r') out += '\r';
                    else if (next == 't') out += '\t';
                    else out += next;
                } else {
                    out += val[i];
                }
            }
            return out;
        };
        std::string origin = extractField(payload, "origin");
        std::string username = extractField(payload, "username");
        std::string password = extractField(payload, "password");
        if (!origin.empty() && !username.empty() && !password.empty()) {
            PromptSavePassword(origin, username, password);
        }
        return;
    }
    if (message.rfind("request_autofill:", 0) == 0) {
        std::string origin = message.substr(17);
        if (!origin.empty()) {
            RequestAutofill(origin);
        }
        return;
    }
}

void BrowserWindow::PromptSavePassword(const std::string& origin, const std::string& username, const std::string& password) {
    if (origin.empty() || username.empty() || password.empty()) return;
    std::string normOrigin = PasswordManager::NormalizeOrigin(origin);
    if (PasswordManager::HasCredential(normOrigin, username, password)) {
        return;
    }

    int uLen = MultiByteToWideChar(CP_UTF8, 0, username.c_str(), -1, nullptr, 0);
    std::wstring wUser(uLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, username.c_str(), -1, &wUser[0], uLen);
    if (!wUser.empty() && wUser.back() == L'\0') wUser.pop_back();

    int oLen = MultiByteToWideChar(CP_UTF8, 0, normOrigin.c_str(), -1, nullptr, 0);
    std::wstring wOrigin(oLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, normOrigin.c_str(), -1, &wOrigin[0], oLen);
    if (!wOrigin.empty() && wOrigin.back() == L'\0') wOrigin.pop_back();

    std::wstring msg = L"Do you want Servo Browser to save the password for \"" + wUser + L"\" on " + wOrigin + L"?";
    int answer = MessageBoxW(m_hwnd, msg.c_str(), L"Save Password? - Servo Browser", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (answer == IDYES) {
        PasswordManager::SaveCredential(normOrigin, username, password);
        LogDebug("[PASSWORDS] Saved credential for user '%s' on '%s'", username.c_str(), normOrigin.c_str());
    }
}

void BrowserWindow::RequestAutofill(const std::string& origin) {
    if (origin.empty()) return;
    std::string normOrigin = PasswordManager::NormalizeOrigin(origin);
    SavedCredential cred;
    if (PasswordManager::FindCredential(normOrigin, cred)) {
        auto escapeJs = [](const std::string& in) -> std::string {
            std::string out;
            for (char c : in) {
                if (c == '\\') out += "\\\\";
                else if (c == '\'') out += "\\'";
                else if (c == '\"') out += "\\\"";
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else out += c;
            }
            return out;
        };
        std::string js = "(function() {\n"
            "try {\n"
            "    var pwd = document.querySelector('input[type=\"password\"]');\n"
            "    if (!pwd) return;\n"
            "    var allInputs = Array.from(document.querySelectorAll('input'));\n"
            "    var pwdIdx = allInputs.indexOf(pwd);\n"
            "    var user = null;\n"
            "    for (var i = pwdIdx - 1; i >= 0; i--) {\n"
            "        var t = (allInputs[i].type || 'text').toLowerCase();\n"
            "        if (t === 'text' || t === 'email' || t === 'tel') { user = allInputs[i]; break; }\n"
            "    }\n"
            "    if (user && !user.value) {\n"
            "        user.value = '" + escapeJs(cred.username) + "';\n"
            "        user.dispatchEvent(new Event('input', {bubbles: true}));\n"
            "        user.dispatchEvent(new Event('change', {bubbles: true}));\n"
            "    }\n"
            "    if (pwd && !pwd.value) {\n"
            "        pwd.value = '" + escapeJs(cred.password) + "';\n"
            "        pwd.dispatchEvent(new Event('input', {bubbles: true}));\n"
            "        pwd.dispatchEvent(new Event('change', {bubbles: true}));\n"
            "    }\n"
            "} catch(e) {}\n"
            "})();\n";
        if (m_viewHost) {
            m_viewHost->EvaluateJavaScript(js);
        }
    }
}

void BrowserWindow::PrintPage() {
    PRINTDLGW pd = { sizeof(PRINTDLGW) };
    pd.hwndOwner = m_hwnd;
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE;
    if (PrintDlgW(&pd)) {
        if (pd.hDC) DeleteDC(pd.hDC);
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
    }
}

void BrowserWindow::ViewPageSource() {
    if (m_viewHost) {
        std::string srcUrl = "view-source:" + m_viewHost->GetUrl();
        OpenUrlInNewTab(srcUrl);
    }
}

void BrowserWindow::OnActiveTabSwitched(const std::string& url, const std::wstring& title) {
    if (m_viewHost) {
        if (!url.empty()) {
            m_viewHost->LoadUrl(url);
        }
        m_viewHost->SetFocus();
    }
    UpdateActiveTabUI(url, title);
}

void BrowserWindow::OnActiveTabUrlChanged(ServoViewHost* /*host*/, const char* url) {
    if (!url) return;
    if (strcmp(url, "servo://session-restore") == 0 || strcmp(url, "servo:session-restore") == 0) {
        RestorePreviousSession();
        return;
    }
    m_currentUrl = url;
    if (m_omnibox) {
        m_omnibox->SetUrl(url);
        m_omnibox->SetBookmarked(BrowserData::IsBookmarked(url));
    }
    if (m_tabStrip) {
        m_tabStrip->UpdateActiveUrl(url);
    }
    BrowserData::AddHistory(url, m_currentTitle);
    if (m_viewHost && m_omnibox) {
        m_omnibox->UpdateNavigationState(m_viewHost->CanGoBack(), m_viewHost->CanGoForward());
    }
}

void BrowserWindow::OnActiveTabTitleChanged(ServoViewHost* /*host*/, const char* title) {
    if (!title) return;
    m_currentTitle = title;
    BrowserData::AddHistory(m_currentUrl, title);
    int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
    if (len > 0) {
        std::vector<wchar_t> wbuf(len);
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wbuf.data(), len);
        if (m_tabStrip) {
            m_tabStrip->UpdateActiveTitle(wbuf.data());
        }
        std::wstring windowTitle = std::wstring(wbuf.data()) + L" - Servo";
        SetWindowTextW(m_hwnd, windowTitle.c_str());
    }
}

void BrowserWindow::OnActiveTabLoadStatusChanged(ServoViewHost* /*host*/, int32_t status) {
    if (m_omnibox) {
        m_omnibox->SetLoading(status == 0); // 0 = In progress
    }
    if (m_viewHost && m_omnibox) {
        m_omnibox->UpdateNavigationState(m_viewHost->CanGoBack(), m_viewHost->CanGoForward());
    }
}

void BrowserWindow::OnActiveTabCrashed(ServoViewHost* /*host*/, const char* reason) {
    LogDebug("Active tab reported crash: %s", reason ? reason : "unknown");
    std::wstring windowTitle = L"Crashed: ";
    if (reason) {
        int len = MultiByteToWideChar(CP_UTF8, 0, reason, -1, nullptr, 0);
        if (len > 0) {
            std::vector<wchar_t> wbuf(len);
            MultiByteToWideChar(CP_UTF8, 0, reason, -1, wbuf.data(), len);
            windowTitle += wbuf.data();
        }
    }
    windowTitle += L" - Servo";
    SetWindowTextW(m_hwnd, windowTitle.c_str());
    if (m_tabStrip) {
        m_tabStrip->UpdateActiveTitle(L"Tab Crashed");
    }
    if (m_viewHost) {
        std::string crashUrl = BrowserData::EnsureCrashRecoveryPageExists(m_currentUrl, reason ? reason : "Unexpected pipeline error");
        m_viewHost->LoadUrl(crashUrl);
    }
}

void BrowserWindow::OnTabFaviconUpdated(ServoViewHost* /*host*/, HBITMAP hBitmap, uint32_t width, uint32_t height) {
    if (m_tabStrip) {
        m_tabStrip->UpdateTabFavicon(hBitmap, width, height);
    }
}

void BrowserWindow::UpdateActiveTabUI(const std::string& url, const std::wstring& title) {
    m_currentUrl = url;
    if (m_omnibox) {
        m_omnibox->SetUrl(url);
        m_omnibox->SetBookmarked(BrowserData::IsBookmarked(url));
    }
    std::wstring windowTitle = title.empty() ? L"Servo Browser" : (title + L" - Servo");
    SetWindowTextW(m_hwnd, windowTitle.c_str());
    LayoutChildren();
}

LRESULT CALLBACK BrowserWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    BrowserWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<BrowserWindow*>(cs->lpCreateParams);
        if (self) {
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } else {
        self = reinterpret_cast<BrowserWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT BrowserWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCALCSIZE: {
        if (wParam) {
            NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            if (IsZoomed(m_hwnd)) {
                HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(MONITORINFO) };
                if (GetMonitorInfoW(hMon, &mi)) {
                    params->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        }
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rcWindow;
        GetWindowRect(m_hwnd, &rcWindow);

        if (!IsZoomed(m_hwnd)) {
            const int border = 6;
            bool left = pt.x < rcWindow.left + border;
            bool right = pt.x >= rcWindow.right - border;
            bool top = pt.y < rcWindow.top + border;
            bool bottom = pt.y >= rcWindow.bottom - border;

            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        return DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }

    case WM_NCACTIVATE:
        // We paint all chrome ourselves. Returning TRUE tells User32 the
        // non-client area needs no repaint, which is what actually prevents
        // the grey legacy border from flashing in on Alt+Tab. Calling
        // DefWindowProc here (even with lParam = -1) still lets User32 paint
        // the inactive frame on some Windows builds.
        return TRUE;

    case WM_NCPAINT:
        // Suppress default non-client window painting
        return 0;

    case WM_ACTIVATE:
        // DWM can drop the extended frame/backdrop across focus switches.
        // Re-assert it so Mica, rounded corners and the shadow survive Alt+Tab.
        if (LOWORD(wParam) != WA_INACTIVE) {
            RefreshDwmFrame();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        if (m_verticalTabs) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int newHover = 0;
            if (PtInRect(&m_topBarCloseBtnBounds, pt)) newHover = 3;
            else if (PtInRect(&m_topBarMaxBtnBounds, pt)) newHover = 2;
            else if (PtInRect(&m_topBarMinBtnBounds, pt)) newHover = 1;
            else if (PtInRect(&m_topBarToggleBtnBounds, pt)) newHover = 4;

            if (newHover != m_hoveredTopBarBtn) {
                m_hoveredTopBarBtn = newHover;
                RECT topBarRc = { 0, 0, 10000, TAB_STRIP_HEIGHT };
                InvalidateRect(m_hwnd, &topBarRc, FALSE);
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        }
        break;
    }

    case WM_MOUSELEAVE: {
        if (m_hoveredTopBarBtn != 0) {
            m_hoveredTopBarBtn = 0;
            RECT topBarRc = { 0, 0, 10000, TAB_STRIP_HEIGHT };
            InvalidateRect(m_hwnd, &topBarRc, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (m_verticalTabs) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (pt.y < TAB_STRIP_HEIGHT) {
                if (PtInRect(&m_topBarCloseBtnBounds, pt)) {
                    PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
                    return 0;
                }
                if (PtInRect(&m_topBarMaxBtnBounds, pt)) {
                    ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                    return 0;
                }
                if (PtInRect(&m_topBarMinBtnBounds, pt)) {
                    ShowWindow(m_hwnd, SW_MINIMIZE);
                    return 0;
                }
                if (PtInRect(&m_topBarToggleBtnBounds, pt)) {
                    ToggleVerticalTabs();
                    return 0;
                }
                // Click on empty space in titlebar initiates native window drag
                ReleaseCapture();
                SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }
        }
        break;
    }

    case WM_APP_SERVO_WAKE:
    case WM_TIMER:
        if (m_servo) {
            ServoSpinEventLoopOnce(m_servo);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        int width = rc.right;

        auto colors = m_isDark ? fluent::GetDarkColors() : fluent::GetLightColors();

        // Render the custom title bar when vertical tabs are active. This is
        // the only place caption controls are drawn now (the native caption
        // style bits are cleared at creation, so DWM no longer draws a second
        // set on top of ours). Draw into an offscreen DC to avoid the flicker
        // the old direct-to-window rendering produced on activation and hover.
        if (m_verticalTabs && width > 0) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBM = CreateCompatibleBitmap(hdc, width, TAB_STRIP_HEIGHT);
            HGDIOBJ oldBM = SelectObject(memDC, memBM);

            RECT bar = { 0, 0, width, TAB_STRIP_HEIGHT };
            HBRUSH bg = CreateSolidBrush(colors.LayerBackground);
            FillRect(memDC, &bar, bg);
            DeleteObject(bg);
            SetBkMode(memDC, TRANSPARENT);

            HFONT textFont = fluent::CreateFluentFont(13, FW_SEMIBOLD);
            HFONT iconFont = fluent::CreateFluentIconFont(13);
            HFONT captionFont = fluent::CreateCaptionIconFont(10);
            HGDIOBJ oldFont = SelectObject(memDC, textFont);

            auto drawHover = [&](const RECT& r, COLORREF color) {
                HBRUSH hBr = CreateSolidBrush(color);
                HGDIOBJ oldBr = SelectObject(memDC, hBr);
                RoundRect(memDC, r.left, r.top, r.right, r.bottom, 4, 4);
                SelectObject(memDC, oldBr);
                DeleteObject(hBr);
            };

            // Toggle button (left) - matches the sidebar toggle glyph.
            if (m_hoveredTopBarBtn == 4) {
                drawHover(m_topBarToggleBtnBounds, colors.ControlBackgroundHover);
            }
            SelectObject(memDC, iconFont);
            SetTextColor(memDC, (m_hoveredTopBarBtn == 4) ? colors.TextPrimary : colors.Accent);
            RECT toggleRc = m_topBarToggleBtnBounds;
            DrawTextW(memDC, fluent::icons::DockLeft, -1, &toggleRc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Title - stop short of the caption buttons instead of using a
            // hard-coded width that overlapped them on narrow windows.
            SelectObject(memDC, textFont);
            SetTextColor(memDC, colors.TextPrimary);
            RECT titleRc = { m_topBarToggleBtnBounds.right + 4, 0,
                             m_topBarMinBtnBounds.left - 4, TAB_STRIP_HEIGHT };
            DrawTextW(memDC, L"Servo Browser", -1, &titleRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            // Caption buttons (Min, Max, Close) with Fluent hover treatment.
            bool isMaximized = (IsZoomed(m_hwnd) != FALSE);
            auto drawBtn = [&](const RECT& r, const wchar_t* glyph, int id) {
                bool hovered = (m_hoveredTopBarBtn == id);
                if (hovered) {
                    COLORREF bgCol = (id == 3) ? RGB(196, 43, 28)
                                               : colors.ControlBackgroundHover;
                    drawHover(r, bgCol);
                }
                SelectObject(memDC, captionFont);
                SetTextColor(memDC, (hovered && id == 3) ? RGB(255, 255, 255) : colors.TextPrimary);
                RECT br = r;
                DrawTextW(memDC, glyph, -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            };

            drawBtn(m_topBarMinBtnBounds, fluent::icons::ChromeMinimize, 1);
            drawBtn(m_topBarMaxBtnBounds, isMaximized ? fluent::icons::ChromeRestore : fluent::icons::ChromeMaximize, 2);
            drawBtn(m_topBarCloseBtnBounds, fluent::icons::ChromeClose, 3);

            SelectObject(memDC, oldFont);
            DeleteObject(textFont);
            DeleteObject(iconFont);
            DeleteObject(captionFont);

            HPEN sepPen = CreatePen(PS_SOLID, 1, colors.TabBorder);
            HGDIOBJ oldP = SelectObject(memDC, sepPen);
            MoveToEx(memDC, 0, TAB_STRIP_HEIGHT - 1, nullptr);
            LineTo(memDC, width, TAB_STRIP_HEIGHT - 1);
            SelectObject(memDC, oldP);
            DeleteObject(sepPen);

            BitBlt(hdc, 0, 0, width, TAB_STRIP_HEIGHT, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBM);
            DeleteObject(memBM);
            DeleteDC(memDC);
        }

        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        LayoutChildren();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;

    case WM_DPICHANGED: {
        auto* lprc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutChildren();
        return 0;
    }

    case WM_SYSKEYDOWN:
        // Clearing WS_SYSMENU removes the default Alt+F4 handling, so restore
        // it here. Bit 29 of lParam is the context code (1 when ALT is down).
        if (wParam == VK_F4 && (lParam & (1 << 29))) {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // --- Tab Management ---
        if (ctrl && shift && wParam == 'V') {
            ToggleVerticalTabs();
            return 0;
        }
        if (ctrl && shift && wParam == 'T') {
            if (m_tabStrip) m_tabStrip->ReopenClosedTab();
            return 0;
        }
        if (ctrl && (wParam == VK_TAB || wParam == VK_NEXT)) {
            if (m_tabStrip) {
                if (shift) {
                    m_tabStrip->PreviousTab();
                } else {
                    m_tabStrip->NextTab();
                }
            }
            return 0;
        }
        if (ctrl && wParam == VK_PRIOR) {
            if (m_tabStrip) m_tabStrip->PreviousTab();
            return 0;
        }
        if (ctrl && wParam >= '1' && wParam <= '8') {
            if (m_tabStrip) m_tabStrip->SelectTabByIndex(static_cast<int>(wParam - '1'));
            return 0;
        }
        if (ctrl && wParam == '9') {
            if (m_tabStrip && m_tabStrip->GetTabCount() > 0) {
                m_tabStrip->SelectTabByIndex(m_tabStrip->GetTabCount() - 1);
            }
            return 0;
        }
        // Ctrl+Shift+T is handled above by ReopenClosedTab. Session restore
        // remains available from the new-tab page link.
        if (ctrl && wParam == 'T') {
            OpenUrlInNewTab(NewTabPage::EnsureNewTabPageExists());
            return 0;
        }
        if ((ctrl && wParam == 'W') || (ctrl && wParam == VK_F4)) {
            if (m_tabStrip) m_tabStrip->CloseTab(m_tabStrip->GetActiveIndex());
            return 0;
        }

        // --- Omnibox Focus ---
        if ((ctrl && wParam == 'L') || (alt && wParam == 'D') || wParam == VK_F6) {
            if (m_omnibox) m_omnibox->FocusAndSelectAll();
            return 0;
        }
        if (ctrl && (wParam == 'E' || wParam == 'K')) {
            if (m_omnibox) m_omnibox->FocusForSearch();
            return 0;
        }

        // --- Navigation ---
        if (wParam == VK_ESCAPE) {
            if (m_findBar && m_findBar->IsVisible()) {
                m_findBar->Show(false);
            } else {
                NavigateStop();
            }
            return 0;
        }
        if ((ctrl && shift && wParam == 'R') || (ctrl && wParam == VK_F5)) {
            NavigateReload();
            return 0;
        }
        if ((ctrl && wParam == 'R') || wParam == VK_F5) {
            NavigateReload();
            return 0;
        }
        if (alt && wParam == VK_HOME) {
            NavigateUrl(NewTabPage::EnsureNewTabPageExists());
            return 0;
        }
        if (alt && wParam == VK_LEFT) {
            NavigateBack();
            return 0;
        }
        if (alt && wParam == VK_RIGHT) {
            NavigateForward();
            return 0;
        }

        // --- Tools & Overlays ---
        if (ctrl && wParam == 'D') {
            ToggleCurrentPageBookmark();
            return 0;
        }
        if (ctrl && shift && wParam == 'O') {
            OpenBookmarks();
            return 0;
        }
        if (ctrl && wParam == 'H') {
            OpenHistory();
            return 0;
        }
        if (ctrl && wParam == VK_OEM_COMMA) {
            OpenSettings();
            return 0;
        }
        if (ctrl && wParam == 'P') {
            PrintPage();
            return 0;
        }
        if (ctrl && wParam == 'U') {
            ViewPageSource();
            return 0;
        }
        if (ctrl && wParam == 'N') {
            OpenUrlInNewTab(NewTabPage::EnsureNewTabPageExists());
            return 0;
        }
        if (ctrl && wParam == 'F') {
            if (m_findBar) m_findBar->Show(!m_findBar->IsVisible());
            return 0;
        }
        if (ctrl && wParam == 'J') {
            if (m_downloadManager) m_downloadManager->Show(!m_downloadManager->IsVisible());
            return 0;
        }
        if (wParam == VK_F12 || (ctrl && shift && wParam == 'I')) {
            OpenDevTools();
            return 0;
        }
        if (wParam == VK_F11) {
            ToggleFullscreen();
            return 0;
        }

        // --- Zoom Controls ---
        if (ctrl && (wParam == VK_OEM_PLUS || wParam == VK_ADD || wParam == '=')) {
            ZoomIn();
            return 0;
        }
        if (ctrl && (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT || wParam == '-')) {
            ZoomOut();
            return 0;
        }
        if (ctrl && (wParam == '0' || wParam == VK_NUMPAD0)) {
            ZoomReset();
            return 0;
        }

        break;
    }

    case WM_CLOSE:
        LogDebug("BrowserWindow received WM_CLOSE (popup=%d)", m_isPopup ? 1 : 0);
        // Only the main window owns the persisted session. Popups start at
        // about:blank, so saving from one would erase the user's real tabs.
        if (!m_isPopup && m_tabStrip) {
            BrowserData::SaveSession(m_tabStrip->GetAllTabUrls(), m_tabStrip->GetActiveTabIndex());
        }
        break;

    case WM_DESTROY:
        LogDebug("BrowserWindow received WM_DESTROY (popup=%d)", m_isPopup ? 1 : 0);
        KillTimer(m_hwnd, 1);
        // Closing an auxiliary popup must not terminate the whole browser.
        if (!m_isPopup) {
            LogDebug("Main window destroyed, posting quit message...");
            PostQuitMessage(0);
        }
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
