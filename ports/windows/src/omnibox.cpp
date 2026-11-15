/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "omnibox.h"
#include "browser_window.h"
#include "fluent_theme.h"
#include "browser_data.h"
#include <commctrl.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <algorithm>

static const wchar_t* OMNIBOX_CLASS_NAME = L"ServoOmniboxToolbarWindow";
static const wchar_t* SUGGESTIONS_CLASS_NAME = L"ServoOmniboxSuggestionsWindow";
static const UINT_PTR EDIT_SUBCLASS_ID = 1001;
static const UINT_PTR TIMER_PROGRESS_ID = 2001;
static const UINT_PTR TIMER_PROGRESS_DONE_ID = 2002;

struct SiteEntry {
    const wchar_t* key;
    const wchar_t* domain;
    const wchar_t* name;
    const wchar_t* url;
};

static const SiteEntry POPULAR_SITES[] = {
    { L"youtube", L"youtube.com", L"YouTube", L"https://www.youtube.com/" },
    { L"github", L"github.com", L"GitHub", L"https://github.com/" },
    { L"instagram", L"instagram.com", L"Instagram", L"https://www.instagram.com/" },
    { L"reddit", L"reddit.com", L"Reddit", L"https://www.reddit.com/" },
    { L"wikipedia", L"wikipedia.org", L"Wikipedia", L"https://www.wikipedia.org/" },
    { L"google", L"google.com", L"Google", L"https://www.google.com/" },
    { L"duckduckgo", L"duckduckgo.com", L"DuckDuckGo", L"https://duckduckgo.com/" },
    { L"servo", L"servo.org", L"Servo Project", L"https://servo.org/" },
    { L"chatgpt", L"chatgpt.com", L"ChatGPT", L"https://chatgpt.com/" },
    { L"netflix", L"netflix.com", L"Netflix", L"https://www.netflix.com/" },
    { L"amazon", L"amazon.com", L"Amazon", L"https://www.amazon.com/" },
    { L"twitter", L"twitter.com", L"Twitter", L"https://twitter.com/" },
    { L"x", L"x.com", L"X (Twitter)", L"https://x.com/" },
    { L"spotify", L"spotify.com", L"Spotify", L"https://www.spotify.com/" },
    { L"twitch", L"twitch.tv", L"Twitch", L"https://www.twitch.tv/" },
    { L"discord", L"discord.com", L"Discord", L"https://discord.com/" },
    { L"linkedin", L"linkedin.com", L"LinkedIn", L"https://www.linkedin.com/" },
    { L"stackoverflow", L"stackoverflow.com", L"Stack Overflow", L"https://stackoverflow.com/" },
    { L"microsoft", L"microsoft.com", L"Microsoft", L"https://www.microsoft.com/" },
    { L"apple", L"apple.com", L"Apple", L"https://www.apple.com/" },
};

void OmniboxToolbar::RegisterWindowClass(HINSTANCE hInstance) {
    if (!hInstance) {
        hInstance = GetModuleHandleW(nullptr);
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OmniboxToolbar::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = OMNIBOX_CLASS_NAME;
    RegisterClassExW(&wc);

    WNDCLASSEXW swc = {};
    swc.cbSize = sizeof(WNDCLASSEXW);
    swc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    swc.lpfnWndProc = OmniboxToolbar::SuggestionsWndProc;
    swc.hInstance = hInstance;
    swc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    swc.hbrBackground = nullptr;
    swc.lpszClassName = SUGGESTIONS_CLASS_NAME;
    RegisterClassExW(&swc);
}

OmniboxToolbar::OmniboxToolbar(HWND parentHwnd, BrowserWindow* browserWindow)
    : m_hwnd(nullptr)
    , m_browserWindow(browserWindow)
    , m_editHwnd(nullptr)
    , m_suggestionsHwnd(nullptr)
    , m_selectedSuggestion(-1)
    , m_loadProgress(0.0f)
    , m_progressTimerId(0)
    , m_canBack(false)
    , m_canForward(false)
    , m_isLoading(false)
    , m_isDark(true)
    , m_font(nullptr)
    , m_iconFont(nullptr)
    , m_resultFont(nullptr)
    , m_resultBoldFont(nullptr)
    , m_badgeFont(nullptr)
    , m_editBrush(nullptr)
{
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    m_hwnd = CreateWindowExW(
        0,
        OMNIBOX_CLASS_NAME,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 800, 44,
        parentHwnd,
        nullptr,
        hInstance,
        this
    );

    // Create Edit control for address bar
    m_editHwnd = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        0, 0, 400, 24,
        m_hwnd,
        nullptr,
        hInstance,
        nullptr
    );

    SetWindowSubclass(m_editHwnd, EditSubclassProc, EDIT_SUBCLASS_ID, reinterpret_cast<DWORD_PTR>(this));

    // Create Suggestions popup window
    m_suggestionsHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        SUGGESTIONS_CLASS_NAME,
        L"",
        WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
        0, 0, 400, 150,
        m_hwnd,
        nullptr,
        hInstance,
        this
    );

    m_font = fluent::CreateFluentFont(14);
    m_iconFont = fluent::CreateFluentIconFont(14);
    m_resultFont = fluent::CreateFluentFont(13, FW_NORMAL, false, L"Inter");
    m_resultBoldFont = fluent::CreateFluentFont(13, FW_SEMIBOLD, false, L"Inter");
    m_badgeFont = fluent::CreateFluentFont(10, FW_MEDIUM, false, L"Inter");
    auto colors = m_isDark ? fluent::GetDarkColors() : fluent::GetLightColors();
    m_editBrush = CreateSolidBrush(colors.ControlBackground);

    SendMessageW(m_editHwnd, WM_SETFONT, (WPARAM)m_font, TRUE);
    SendMessageW(m_editHwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
}

OmniboxToolbar::~OmniboxToolbar() {
    if (m_progressTimerId) {
        KillTimer(m_hwnd, m_progressTimerId);
    }
    if (m_editHwnd && IsWindow(m_editHwnd)) {
        RemoveWindowSubclass(m_editHwnd, EditSubclassProc, EDIT_SUBCLASS_ID);
    }
    if (m_suggestionsHwnd && IsWindow(m_suggestionsHwnd)) {
        DestroyWindow(m_suggestionsHwnd);
    }
    if (m_font) DeleteObject(m_font);
    if (m_iconFont) DeleteObject(m_iconFont);
    if (m_resultFont) DeleteObject(m_resultFont);
    if (m_resultBoldFont) DeleteObject(m_resultBoldFont);
    if (m_badgeFont) DeleteObject(m_badgeFont);
    if (m_editBrush) DeleteObject(m_editBrush);
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
    }
}

void OmniboxToolbar::SetBounds(int x, int y, int width, int height) {
    if (m_hwnd) {
        SetWindowPos(m_hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutControls();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void OmniboxToolbar::SetUrl(const std::string& url) {
    if (!m_editHwnd) return;
    if (NewTabPage::IsNewTabUrl(url)) {
        SetWindowTextW(m_editHwnd, L"");
        HideSuggestions();
        return;
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (len > 0) {
        std::vector<wchar_t> wbuf(len);
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wbuf.data(), len);
        SetWindowTextW(m_editHwnd, wbuf.data());
    }
    HideSuggestions();
}

void OmniboxToolbar::SetBookmarked(bool bookmarked) {
    if (m_isBookmarked != bookmarked) {
        m_isBookmarked = bookmarked;
        if (m_hwnd) {
            InvalidateRect(m_hwnd, &m_bookmarkBtnBounds, FALSE);
        }
    }
}

void OmniboxToolbar::SetLoading(bool loading) {
    m_isLoading = loading;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, &m_reloadBtnBounds, FALSE);

        if (loading) {
            m_loadProgress = 0.15f;
            SetTimer(m_hwnd, TIMER_PROGRESS_ID, 30, nullptr);
        } else {
            m_loadProgress = 1.0f;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            SetTimer(m_hwnd, TIMER_PROGRESS_DONE_ID, 250, nullptr);
        }
    }
}

void OmniboxToolbar::SetLoadingProgress(float progress) {
    m_loadProgress = std::clamp(progress, 0.0f, 1.0f);
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void OmniboxToolbar::UpdateNavigationState(bool canBack, bool canForward) {
    m_canBack = canBack;
    m_canForward = canForward;
    InvalidateRect(m_hwnd, &m_backBtnBounds, FALSE);
    InvalidateRect(m_hwnd, &m_forwardBtnBounds, FALSE);
}

void OmniboxToolbar::LayoutControls() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    int btnSize = 32;
    int pad = 6;
    int x = 8;
    int y = (rc.bottom - btnSize) / 2;

    // Navigation buttons (Left)
    m_backBtnBounds = { x, y, x + btnSize, y + btnSize };
    x += btnSize + pad;

    m_forwardBtnBounds = { x, y, x + btnSize, y + btnSize };
    x += btnSize + pad;

    m_reloadBtnBounds = { x, y, x + btnSize, y + btnSize };
    x += btnSize + pad;

    m_homeBtnBounds = { x, y, x + btnSize, y + btnSize };
    x += btnSize + pad + 4;

    // Right action buttons (Bookmarks, Downloads & Settings)
    int rx = rc.right - 8 - btnSize;
    m_settingsBtnBounds = { rx, y, rx + btnSize, y + btnSize };
    rx -= (btnSize + pad);
    m_downloadsBtnBounds = { rx, y, rx + btnSize, y + btnSize };
    rx -= (btnSize + pad);
    m_bookmarkBtnBounds = { rx, y, rx + btnSize, y + btnSize };

    // Center Omnibox Pill
    int omniboxRight = rx - pad - 4;
    int omniboxWidth = std::max(100, omniboxRight - x);
    m_omniboxBounds = { x, y, x + omniboxWidth, y + btnSize };

    if (m_editHwnd) {
        int editHeight = 18;
        int editY = y + (btnSize - editHeight) / 2;
        SetWindowPos(m_editHwnd, nullptr, x + 30, editY, omniboxWidth - 38, editHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void OmniboxToolbar::Paint(HDC hdc) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

    auto colors = m_isDark ? fluent::GetDarkColors() : fluent::GetLightColors();

    // Toolbar background
    HBRUSH bgBrush = CreateSolidBrush(colors.LayerBackground);
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    HFONT oldFont = (HFONT)SelectObject(memDC, m_iconFont);
    SetBkMode(memDC, TRANSPARENT);

    auto drawNavButton = [&](const RECT& bounds, const wchar_t* icon, bool enabled, NavButton btnType) {
        bool isHovered = (m_hoveredNavBtn == btnType && enabled);
        bool isPressed = (m_pressedNavBtn == btnType && enabled);

        if (isPressed) {
            HBRUSH hBr = CreateSolidBrush(colors.ControlBackgroundPressed);
            HPEN hPen = CreatePen(PS_SOLID, 1, colors.ControlBorder);
            HGDIOBJ ob = SelectObject(memDC, hBr);
            HGDIOBJ op = SelectObject(memDC, hPen);
            RoundRect(memDC, bounds.left, bounds.top, bounds.right, bounds.bottom, 6, 6);
            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(hBr);
            DeleteObject(hPen);
        } else if (isHovered) {
            HBRUSH hBr = CreateSolidBrush(colors.ControlBackgroundHover);
            HPEN hPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ ob = SelectObject(memDC, hBr);
            HGDIOBJ op = SelectObject(memDC, hPen);
            RoundRect(memDC, bounds.left, bounds.top, bounds.right, bounds.bottom, 6, 6);
            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(hBr);
            DeleteObject(hPen);
        }

        SetTextColor(memDC, enabled ? colors.TextPrimary : colors.TextTertiary);
        RECT r = bounds;
        DrawTextW(memDC, icon, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    // Navigation Controls
    drawNavButton(m_backBtnBounds, fluent::icons::Back, m_canBack, NavButton::Back);
    drawNavButton(m_forwardBtnBounds, fluent::icons::Forward, m_canForward, NavButton::Forward);
    drawNavButton(m_reloadBtnBounds, m_isLoading ? fluent::icons::Stop : fluent::icons::Refresh, true, NavButton::Reload);
    drawNavButton(m_homeBtnBounds, fluent::icons::Home, true, NavButton::Home);

    // Right Controls (Bookmarks, Downloads & Settings)
    const wchar_t* starIcon = m_isBookmarked ? L"\xE735" : L"\xE734";
    if (m_isBookmarked) {
        bool isHovered = (m_hoveredNavBtn == NavButton::Bookmark);
        bool isPressed = (m_pressedNavBtn == NavButton::Bookmark);
        if (isPressed) {
            HBRUSH hBr = CreateSolidBrush(colors.ControlBackgroundPressed);
            HPEN hPen = CreatePen(PS_SOLID, 1, colors.ControlBorder);
            HGDIOBJ ob = SelectObject(memDC, hBr);
            HGDIOBJ op = SelectObject(memDC, hPen);
            RoundRect(memDC, m_bookmarkBtnBounds.left, m_bookmarkBtnBounds.top, m_bookmarkBtnBounds.right, m_bookmarkBtnBounds.bottom, 6, 6);
            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(hBr);
            DeleteObject(hPen);
        } else if (isHovered) {
            HBRUSH hBr = CreateSolidBrush(colors.ControlBackgroundHover);
            HPEN hPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ ob = SelectObject(memDC, hBr);
            HGDIOBJ op = SelectObject(memDC, hPen);
            RoundRect(memDC, m_bookmarkBtnBounds.left, m_bookmarkBtnBounds.top, m_bookmarkBtnBounds.right, m_bookmarkBtnBounds.bottom, 6, 6);
            SelectObject(memDC, ob);
            SelectObject(memDC, op);
            DeleteObject(hBr);
            DeleteObject(hPen);
        }
        SetTextColor(memDC, RGB(255, 185, 0));
        RECT r = m_bookmarkBtnBounds;
        DrawTextW(memDC, starIcon, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        drawNavButton(m_bookmarkBtnBounds, starIcon, true, NavButton::Bookmark);
    }
    drawNavButton(m_downloadsBtnBounds, fluent::icons::Downloads, true, NavButton::Downloads);
    drawNavButton(m_settingsBtnBounds, fluent::icons::Settings, true, NavButton::Settings);

    // Omnibox Frame with Fluent rounded corners
    HBRUSH boxBrush = CreateSolidBrush(colors.ControlBackground);
    HPEN boxPen = CreatePen(PS_SOLID, 1, colors.ControlBorder);
    HBRUSH oldB = (HBRUSH)SelectObject(memDC, boxBrush);
    HPEN oldP = (HPEN)SelectObject(memDC, boxPen);

    RoundRect(memDC, m_omniboxBounds.left, m_omniboxBounds.top, m_omniboxBounds.right, m_omniboxBounds.bottom, 8, 8);

    SelectObject(memDC, oldB);
    SelectObject(memDC, oldP);
    DeleteObject(boxBrush);
    DeleteObject(boxPen);

    // Draw HTTPS Security Lock Icon
    SetTextColor(memDC, colors.TextSecondary);
    RECT lockRect = { m_omniboxBounds.left + 8, m_omniboxBounds.top, m_omniboxBounds.left + 26, m_omniboxBounds.bottom };
    DrawTextW(memDC, fluent::icons::Lock, -1, &lockRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Draw Modern Animated Page Load Progress Bar across bottom of toolbar
    if (m_loadProgress > 0.0f) {
        int barH = 3;
        int barW = static_cast<int>(rc.right * m_loadProgress);
        RECT progRect = { 0, rc.bottom - barH, barW, rc.bottom };
        HBRUSH progBrush = CreateSolidBrush(colors.Accent);
        FillRect(memDC, &progRect, progBrush);
        DeleteObject(progBrush);
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldFont);
    SelectObject(memDC, oldBM);
    DeleteObject(memBM);
    DeleteDC(memDC);
}

// -----------------------------------------------------------------------------
// Interactive Suggestions Dropdown
// -----------------------------------------------------------------------------
void OmniboxToolbar::UpdateSuggestions(const std::wstring& query) {
    m_suggestions.clear();
    m_selectedSuggestion = -1;

    size_t first = query.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        HideSuggestions();
        return;
    }
    size_t last = query.find_last_not_of(L" \t\r\n");
    std::wstring cleanQuery = query.substr(first, (last - first + 1));
    if (cleanQuery.empty()) {
        HideSuggestions();
        return;
    }

    std::wstring lowerQuery = cleanQuery;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);

    // 1. TOP PRIORITY: Website Detection (Rank 0)
    std::vector<OmniboxSuggestion> websiteMatches;
    for (const auto& site : POPULAR_SITES) {
        std::wstring k = site.key;
        std::wstring d = site.domain;
        if (k.rfind(lowerQuery, 0) == 0 || lowerQuery.rfind(k, 0) == 0 ||
            d.rfind(lowerQuery, 0) == 0 || (d.find(lowerQuery) != std::wstring::npos && lowerQuery.length() >= 3)) {
            OmniboxSuggestion item;
            item.type = OmniboxSuggestion::Type::Website;
            item.text = site.name;
            item.targetUrl = site.url;
            item.secondary = std::wstring(site.domain) + L" \x2022 Official Website";
            websiteMatches.push_back(item);
            if (websiteMatches.size() >= 2) break;
        }
    }

    if (lowerQuery.find(L'.') != std::wstring::npos && lowerQuery.find(L' ') == std::wstring::npos) {
        bool alreadyAdded = false;
        for (const auto& w : websiteMatches) {
            if (w.targetUrl.find(lowerQuery) != std::wstring::npos) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            OmniboxSuggestion item;
            item.type = OmniboxSuggestion::Type::Website;
            item.text = cleanQuery;
            item.targetUrl = (lowerQuery.find(L"://") != std::wstring::npos) ? cleanQuery : (L"https://" + cleanQuery);
            item.secondary = L"Direct Website";
            websiteMatches.insert(websiteMatches.begin(), item);
        }
    }

    for (const auto& w : websiteMatches) {
        m_suggestions.push_back(w);
    }

    // Rank 0 pre-selection for instant direct navigation
    if (!websiteMatches.empty()) {
        m_selectedSuggestion = 0;
    }

    // 2. Bookmarks matching query
    std::vector<BookmarkItem> bookmarks = BrowserData::LoadBookmarks();
    for (const auto& bm : bookmarks) {
        int wlenUrl = MultiByteToWideChar(CP_UTF8, 0, bm.url.c_str(), -1, nullptr, 0);
        std::wstring wurl;
        if (wlenUrl > 1) {
            wurl.resize(wlenUrl - 1);
            MultiByteToWideChar(CP_UTF8, 0, bm.url.c_str(), -1, &wurl[0], wlenUrl);
        }

        int wlenTitle = MultiByteToWideChar(CP_UTF8, 0, bm.title.c_str(), -1, nullptr, 0);
        std::wstring wtitle;
        if (wlenTitle > 1) {
            wtitle.resize(wlenTitle - 1);
            MultiByteToWideChar(CP_UTF8, 0, bm.title.c_str(), -1, &wtitle[0], wlenTitle);
        }

        std::wstring lowerUrl = wurl;
        std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::towlower);
        std::wstring lowerTitle = wtitle;
        std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::towlower);

        if ((!lowerUrl.empty() && lowerUrl.find(lowerQuery) != std::wstring::npos) ||
            (!lowerTitle.empty() && lowerTitle.find(lowerQuery) != std::wstring::npos)) {
            OmniboxSuggestion item;
            item.type = OmniboxSuggestion::Type::Bookmark;
            item.text = wtitle.empty() ? wurl : wtitle;
            item.targetUrl = wurl;
            item.secondary = wurl;
            m_suggestions.push_back(item);
            if (m_suggestions.size() >= 5) break;
        }
    }

    // 3. Primary search query
    {
        OmniboxSuggestion item;
        item.type = OmniboxSuggestion::Type::Search;
        item.text = cleanQuery;
        item.targetUrl = cleanQuery;
        item.secondary = L"DuckDuckGo Search";
        m_suggestions.push_back(item);
    }

    // 4. Extended search completions
    if (lowerQuery.find(L'.') == std::wstring::npos) {
        OmniboxSuggestion c1;
        c1.type = OmniboxSuggestion::Type::Search;
        c1.text = cleanQuery + L" official site";
        c1.targetUrl = cleanQuery + L" official site";
        c1.secondary = L"Search suggestion";
        m_suggestions.push_back(c1);

        OmniboxSuggestion c2;
        c2.type = OmniboxSuggestion::Type::Search;
        c2.text = cleanQuery + L" docs";
        c2.targetUrl = cleanQuery + L" docs";
        c2.secondary = L"Search suggestion";
        m_suggestions.push_back(c2);
    }

    if (m_suggestions.empty()) {
        HideSuggestions();
        return;
    }

    POINT pt = { m_omniboxBounds.left, m_omniboxBounds.bottom + 2 };
    ClientToScreen(m_hwnd, &pt);
    int width = m_omniboxBounds.right - m_omniboxBounds.left;
    int itemH = 38;
    int height = std::min(static_cast<int>(m_suggestions.size()) * itemH + 6, 260);

    SetWindowPos(m_suggestionsHwnd, HWND_TOPMOST, pt.x, pt.y, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(m_suggestionsHwnd, nullptr, TRUE);
}

void OmniboxToolbar::HideSuggestions() {
    if (m_suggestionsHwnd && IsWindow(m_suggestionsHwnd)) {
        ShowWindow(m_suggestionsHwnd, SW_HIDE);
    }
    m_selectedSuggestion = -1;
}

bool OmniboxToolbar::IsSuggestionsVisible() const {
    return m_suggestionsHwnd && IsWindowVisible(m_suggestionsHwnd);
}

void OmniboxToolbar::SelectNextSuggestion() {
    if (m_suggestions.empty()) return;
    m_selectedSuggestion = (m_selectedSuggestion + 1) % static_cast<int>(m_suggestions.size());
    InvalidateRect(m_suggestionsHwnd, nullptr, FALSE);
}

void OmniboxToolbar::SelectPreviousSuggestion() {
    if (m_suggestions.empty()) return;
    m_selectedSuggestion = (m_selectedSuggestion - 1 + static_cast<int>(m_suggestions.size())) % static_cast<int>(m_suggestions.size());
    InvalidateRect(m_suggestionsHwnd, nullptr, FALSE);
}

std::wstring OmniboxToolbar::GetSelectedSuggestionText() const {
    if (m_selectedSuggestion >= 0 && m_selectedSuggestion < static_cast<int>(m_suggestions.size())) {
        return m_suggestions[m_selectedSuggestion].targetUrl;
    }
    return L"";
}

void OmniboxToolbar::PaintSuggestions(HDC hdc) {
    RECT rc;
    GetClientRect(m_suggestionsHwnd, &rc);

    auto colors = m_isDark ? fluent::GetDarkColors() : fluent::GetLightColors();

    HBRUSH bg = CreateSolidBrush(colors.CardBackground);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_resultFont ? m_resultFont : m_font);
    HFONT iconFont = m_iconFont;
    SetBkMode(hdc, TRANSPARENT);

    int itemH = 38;
    int y = 3;

    for (int i = 0; i < static_cast<int>(m_suggestions.size()); ++i) {
        RECT itemRc = { 4, y, rc.right - 4, y + itemH };
        bool isSelected = (i == m_selectedSuggestion);
        const auto& item = m_suggestions[i];

        if (isSelected) {
            HBRUSH selBrush = CreateSolidBrush(m_isDark ? RGB(55, 55, 55) : RGB(230, 230, 230));
            FillRect(hdc, &itemRc, selBrush);
            DeleteObject(selBrush);
        }

        // Distinct Icon per suggestion type
        SelectObject(hdc, iconFont);
        SetTextColor(hdc, isSelected ? colors.Accent : colors.TextSecondary);
        RECT iconRc = { itemRc.left + 8, itemRc.top, itemRc.left + 28, itemRc.bottom };

        const wchar_t* iconGlyph = fluent::icons::Search;
        if (item.type == OmniboxSuggestion::Type::Website) iconGlyph = fluent::icons::Globe;
        else if (item.type == OmniboxSuggestion::Type::Bookmark) iconGlyph = fluent::icons::FavoriteStar;
        else if (item.type == OmniboxSuggestion::Type::History) iconGlyph = fluent::icons::History;

        DrawTextW(hdc, iconGlyph, -1, &iconRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Primary title text
        SelectObject(hdc, (isSelected || item.type == OmniboxSuggestion::Type::Website) ? m_resultBoldFont : m_resultFont);
        SetTextColor(hdc, isSelected ? colors.Accent : colors.TextPrimary);
        RECT titleRc = { itemRc.left + 36, itemRc.top + 3, itemRc.right - 88, itemRc.top + 20 };
        DrawTextW(hdc, item.text.c_str(), -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Secondary text / subtitle
        SelectObject(hdc, m_badgeFont ? m_badgeFont : m_font);
        SetTextColor(hdc, colors.TextTertiary);
        RECT subRc = { itemRc.left + 36, itemRc.top + 20, itemRc.right - 88, itemRc.bottom - 2 };
        DrawTextW(hdc, item.secondary.c_str(), -1, &subRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Right-aligned Badge Pill
        const wchar_t* badgeText = L"Search";
        COLORREF badgeBg = m_isDark ? RGB(50, 50, 50) : RGB(235, 235, 235);
        COLORREF badgeFg = colors.TextSecondary;
        if (item.type == OmniboxSuggestion::Type::Website) {
            badgeText = L"Website";
            badgeBg = colors.Accent;
            badgeFg = RGB(255, 255, 255);
        } else if (item.type == OmniboxSuggestion::Type::Bookmark) {
            badgeText = L"Bookmark";
        } else if (item.type == OmniboxSuggestion::Type::History) {
            badgeText = L"History";
        }

        RECT badgeRc = { itemRc.right - 82, itemRc.top + 9, itemRc.right - 8, itemRc.top + 27 };
        HBRUSH bbr = CreateSolidBrush(badgeBg);
        HPEN bpn = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ ob = SelectObject(hdc, bbr);
        HGDIOBJ op = SelectObject(hdc, bpn);
        RoundRect(hdc, badgeRc.left, badgeRc.top, badgeRc.right, badgeRc.bottom, 8, 8);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(bbr);
        DeleteObject(bpn);

        SetTextColor(hdc, badgeFg);
        DrawTextW(hdc, badgeText, -1, &badgeRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        y += itemH;
    }

    SelectObject(hdc, oldFont);
}

LRESULT CALLBACK OmniboxToolbar::SuggestionsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OmniboxToolbar* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<OmniboxToolbar*>(cs->lpCreateParams);
        if (self) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } else {
        self = reinterpret_cast<OmniboxToolbar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        self->PaintSuggestions(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int idx = pt.y / 38;
        if (idx >= 0 && idx < static_cast<int>(self->m_suggestions.size())) {
            self->m_selectedSuggestion = idx;
            std::wstring selText = self->GetSelectedSuggestionText();
            self->HideSuggestions();

            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, selText.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                std::vector<char> utf8Buf(utf8Len);
                WideCharToMultiByte(CP_UTF8, 0, selText.c_str(), -1, utf8Buf.data(), utf8Len, nullptr, nullptr);
                std::string url = NormalizeOmniboxInput(utf8Buf.data());
                if (!url.empty() && self->m_browserWindow) {
                    self->m_browserWindow->NavigateUrl(url);
                }
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int idx = pt.y / 38;
        if (idx >= 0 && idx < static_cast<int>(self->m_suggestions.size()) && idx != self->m_selectedSuggestion) {
            self->m_selectedSuggestion = idx;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Message Handling & Edit Subclassing
// -----------------------------------------------------------------------------
LRESULT CALLBACK OmniboxToolbar::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OmniboxToolbar* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<OmniboxToolbar*>(cs->lpCreateParams);
        if (self) {
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } else {
        self = reinterpret_cast<OmniboxToolbar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OmniboxToolbar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        LayoutControls();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        Paint(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_PROGRESS_ID) {
            if (m_isLoading) {
                m_loadProgress += (0.85f - m_loadProgress) * 0.08f;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        } else if (wParam == TIMER_PROGRESS_DONE_ID) {
            KillTimer(m_hwnd, TIMER_PROGRESS_DONE_ID);
            KillTimer(m_hwnd, TIMER_PROGRESS_ID);
            m_loadProgress = 0.0f;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        NavButton newHover = NavButton::None;

        if (PtInRect(&m_backBtnBounds, pt)) newHover = NavButton::Back;
        else if (PtInRect(&m_forwardBtnBounds, pt)) newHover = NavButton::Forward;
        else if (PtInRect(&m_reloadBtnBounds, pt)) newHover = NavButton::Reload;
        else if (PtInRect(&m_homeBtnBounds, pt)) newHover = NavButton::Home;
        else if (PtInRect(&m_bookmarkBtnBounds, pt)) newHover = NavButton::Bookmark;
        else if (PtInRect(&m_downloadsBtnBounds, pt)) newHover = NavButton::Downloads;
        else if (PtInRect(&m_settingsBtnBounds, pt)) newHover = NavButton::Settings;

        if (newHover != m_hoveredNavBtn) {
            m_hoveredNavBtn = newHover;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (m_hoveredNavBtn != NavButton::None || m_pressedNavBtn != NavButton::None) {
            m_hoveredNavBtn = NavButton::None;
            m_pressedNavBtn = NavButton::None;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        NavButton newPressed = NavButton::None;

        if (PtInRect(&m_backBtnBounds, pt)) newPressed = NavButton::Back;
        else if (PtInRect(&m_forwardBtnBounds, pt)) newPressed = NavButton::Forward;
        else if (PtInRect(&m_reloadBtnBounds, pt)) newPressed = NavButton::Reload;
        else if (PtInRect(&m_homeBtnBounds, pt)) newPressed = NavButton::Home;
        else if (PtInRect(&m_bookmarkBtnBounds, pt)) newPressed = NavButton::Bookmark;
        else if (PtInRect(&m_downloadsBtnBounds, pt)) newPressed = NavButton::Downloads;
        else if (PtInRect(&m_settingsBtnBounds, pt)) newPressed = NavButton::Settings;

        if (newPressed != NavButton::None) {
            m_pressedNavBtn = newPressed;
            SetCapture(m_hwnd);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (GetCapture() == m_hwnd) {
            ReleaseCapture();
        }
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        NavButton pressed = m_pressedNavBtn;
        m_pressedNavBtn = NavButton::None;
        InvalidateRect(m_hwnd, nullptr, FALSE);

        if (m_browserWindow) {
            if (pressed == NavButton::Back && PtInRect(&m_backBtnBounds, pt) && m_canBack) {
                m_browserWindow->NavigateBack();
            } else if (pressed == NavButton::Forward && PtInRect(&m_forwardBtnBounds, pt) && m_canForward) {
                m_browserWindow->NavigateForward();
            } else if (pressed == NavButton::Reload && PtInRect(&m_reloadBtnBounds, pt)) {
                if (m_isLoading) m_browserWindow->NavigateStop();
                else m_browserWindow->NavigateReload();
            } else if (pressed == NavButton::Home && PtInRect(&m_homeBtnBounds, pt)) {
                m_browserWindow->NavigateUrl("https://servo.org");
            } else if (pressed == NavButton::Bookmark && PtInRect(&m_bookmarkBtnBounds, pt)) {
                m_browserWindow->ToggleCurrentPageBookmark();
            } else if (pressed == NavButton::Downloads && PtInRect(&m_downloadsBtnBounds, pt)) {
                m_browserWindow->ToggleDownloads();
            } else if (pressed == NavButton::Settings && PtInRect(&m_settingsBtnBounds, pt)) {
                m_browserWindow->OpenSettings();
            }
        }
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = reinterpret_cast<HDC>(wParam);
        auto colors = m_isDark ? fluent::GetDarkColors() : fluent::GetLightColors();
        SetTextColor(hdcEdit, colors.TextPrimary);
        SetBkColor(hdcEdit, colors.ControlBackground);
        if (!m_editBrush) {
            m_editBrush = CreateSolidBrush(colors.ControlBackground);
        }
        return reinterpret_cast<LRESULT>(m_editBrush);
    }
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OmniboxToolbar::EditSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    auto self = reinterpret_cast<OmniboxToolbar*>(dwRefData);

    if (msg == WM_KEYDOWN) {
        if (wParam == VK_DOWN) {
            if (self && self->IsSuggestionsVisible()) {
                self->SelectNextSuggestion();
                return 0;
            }
        } else if (wParam == VK_UP) {
            if (self && self->IsSuggestionsVisible()) {
                self->SelectPreviousSuggestion();
                return 0;
            }
        } else if (wParam == VK_ESCAPE) {
            if (self && self->IsSuggestionsVisible()) {
                self->HideSuggestions();
                return 0;
            }
        } else if (wParam == VK_RETURN) {
            std::string input;
            if (self && self->IsSuggestionsVisible()) {
                int selIdx = self->m_selectedSuggestion;
                if (selIdx < 0 && !self->GetSuggestions().empty() &&
                    self->GetSuggestions()[0].type == OmniboxSuggestion::Type::Website) {
                    selIdx = 0;
                }
                if (selIdx >= 0 && selIdx < static_cast<int>(self->GetSuggestions().size())) {
                    std::wstring sel = self->GetSuggestions()[selIdx].targetUrl;
                    self->HideSuggestions();
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, sel.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 0) {
                        std::vector<char> utf8Buf(utf8Len);
                        WideCharToMultiByte(CP_UTF8, 0, sel.c_str(), -1, utf8Buf.data(), utf8Len, nullptr, nullptr);
                        input = utf8Buf.data();
                    }
                }
            }
            if (input.empty()) {
                if (self) self->HideSuggestions();
                int len = GetWindowTextLengthW(hwnd);
                if (len > 0) {
                    std::vector<wchar_t> wbuf(len + 1);
                    GetWindowTextW(hwnd, wbuf.data(), len + 1);
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 0) {
                        std::vector<char> utf8Buf(utf8Len);
                        WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, utf8Buf.data(), utf8Len, nullptr, nullptr);
                        input = utf8Buf.data();
                    }
                }
            }

            if (!input.empty()) {
                std::string url = NormalizeOmniboxInput(input);
                if (!url.empty() && self && self->m_browserWindow) {
                    self->m_browserWindow->NavigateUrl(url);
                }
            }
            return 0;
        }
    } else if (msg == WM_KEYUP && wParam != VK_RETURN && wParam != VK_UP && wParam != VK_DOWN && wParam != VK_ESCAPE) {
        if (self) {
            int len = GetWindowTextLengthW(hwnd);
            if (len > 0) {
                std::vector<wchar_t> wbuf(len + 1);
                GetWindowTextW(hwnd, wbuf.data(), len + 1);
                self->UpdateSuggestions(wbuf.data());
            } else {
                self->HideSuggestions();
            }
        }
    } else if (msg == WM_KILLFOCUS) {
        if (self) {
            self->HideSuggestions();
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void OmniboxToolbar::FocusAndSelectAll() {
    if (m_editHwnd) {
        SetFocus(m_editHwnd);
        SendMessageW(m_editHwnd, EM_SETSEL, 0, -1);
    }
}

void OmniboxToolbar::FocusForSearch() {
    if (m_editHwnd) {
        SetFocus(m_editHwnd);
        SetWindowTextW(m_editHwnd, L"? ");
        SendMessageW(m_editHwnd, EM_SETSEL, 2, 2);
    }
}

void OmniboxToolbar::SetDarkMode(bool isDark) {
    m_isDark = isDark;
    if (m_editBrush) {
        DeleteObject(m_editBrush);
        m_editBrush = nullptr;
    }
    auto colors = m_isDark ? fluent::GetDarkColors() : fluent::GetLightColors();
    m_editBrush = CreateSolidBrush(colors.ControlBackground);
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    if (m_editHwnd) {
        InvalidateRect(m_editHwnd, nullptr, TRUE);
    }
}
