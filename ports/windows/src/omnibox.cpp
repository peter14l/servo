/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "omnibox.h"
#include "browser_window.h"
#include "fluent_theme.h"
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

static const wchar_t* POPULAR_DOMAINS[] = {
    L"youtube.com",
    L"instagram.com",
    L"google.com",
    L"github.com",
    L"reddit.com",
    L"wikipedia.org",
    L"twitter.com",
    L"x.com",
    L"netflix.com",
    L"amazon.com",
    L"servo.org",
    L"duckduckgo.com"
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
    int len = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (len > 0) {
        std::vector<wchar_t> wbuf(len);
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wbuf.data(), len);
        SetWindowTextW(m_editHwnd, wbuf.data());
    }
    HideSuggestions();
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

    // Right action buttons (Downloads & Settings)
    int rx = rc.right - 8 - btnSize;
    m_settingsBtnBounds = { rx, y, rx + btnSize, y + btnSize };
    rx -= (btnSize + pad);
    m_downloadsBtnBounds = { rx, y, rx + btnSize, y + btnSize };

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

    auto drawNavButton = [&](const RECT& bounds, const wchar_t* icon, bool enabled) {
        SetTextColor(memDC, enabled ? colors.TextPrimary : colors.TextTertiary);
        RECT r = bounds;
        DrawTextW(memDC, icon, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    // Navigation Controls
    drawNavButton(m_backBtnBounds, fluent::icons::Back, m_canBack);
    drawNavButton(m_forwardBtnBounds, fluent::icons::Forward, m_canForward);
    drawNavButton(m_reloadBtnBounds, m_isLoading ? fluent::icons::Stop : fluent::icons::Refresh, true);
    drawNavButton(m_homeBtnBounds, fluent::icons::Home, true);

    // Right Controls (Downloads & Settings)
    drawNavButton(m_downloadsBtnBounds, fluent::icons::Downloads, true);
    drawNavButton(m_settingsBtnBounds, fluent::icons::Settings, true);

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

    if (query.empty()) {
        HideSuggestions();
        return;
    }

    // 1. Primary search query
    m_suggestions.push_back(L"Search: " + query);

    // 2. Matching popular web domains
    std::wstring lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);

    for (const wchar_t* domain : POPULAR_DOMAINS) {
        std::wstring d(domain);
        if (d.find(lowerQuery) != std::wstring::npos) {
            m_suggestions.push_back(L"https://" + d);
        }
    }

    // 3. Search completions
    if (lowerQuery.find(L'.') == std::wstring::npos) {
        m_suggestions.push_back(query + L" search");
        m_suggestions.push_back(query + L" documentation");
    }

    if (m_suggestions.empty()) {
        HideSuggestions();
        return;
    }

    // Calculate popup position directly below omnibox edit
    POINT pt = { m_omniboxBounds.left, m_omniboxBounds.bottom + 2 };
    ClientToScreen(m_hwnd, &pt);
    int width = m_omniboxBounds.right - m_omniboxBounds.left;
    int itemH = 34;
    int height = std::min(static_cast<int>(m_suggestions.size()) * itemH + 6, 220);

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
        std::wstring text = m_suggestions[m_selectedSuggestion];
        if (text.rfind(L"Search: ", 0) == 0) {
            return text.substr(8);
        }
        return text;
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

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    HFONT iconFont = m_iconFont;
    SetBkMode(hdc, TRANSPARENT);

    int itemH = 34;
    int y = 3;

    for (int i = 0; i < static_cast<int>(m_suggestions.size()); ++i) {
        RECT itemRc = { 3, y, rc.right - 3, y + itemH };

        if (i == m_selectedSuggestion) {
            HBRUSH selBrush = CreateSolidBrush(m_isDark ? RGB(55, 55, 55) : RGB(230, 230, 230));
            FillRect(hdc, &itemRc, selBrush);
            DeleteObject(selBrush);
        }

        // Draw Icon (Search or Globe)
        SelectObject(hdc, iconFont);
        SetTextColor(hdc, colors.TextSecondary);
        RECT iconRc = { itemRc.left + 8, itemRc.top, itemRc.left + 28, itemRc.bottom };
        bool isUrl = (m_suggestions[i].find(L"://") != std::wstring::npos || m_suggestions[i].find(L".") != std::wstring::npos);
        DrawTextW(hdc, isUrl ? fluent::icons::Globe : fluent::icons::Search, -1, &iconRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Draw Text
        SelectObject(hdc, m_font);
        SetTextColor(hdc, (i == m_selectedSuggestion) ? colors.Accent : colors.TextPrimary);
        RECT textRc = { itemRc.left + 36, itemRc.top, itemRc.right - 8, itemRc.bottom };
        DrawTextW(hdc, m_suggestions[i].c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

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
        int idx = pt.y / 34;
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
        int idx = pt.y / 34;
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

    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (m_browserWindow) {
            if (PtInRect(&m_backBtnBounds, pt) && m_canBack) {
                m_browserWindow->NavigateBack();
            } else if (PtInRect(&m_forwardBtnBounds, pt) && m_canForward) {
                m_browserWindow->NavigateForward();
            } else if (PtInRect(&m_reloadBtnBounds, pt)) {
                if (m_isLoading) m_browserWindow->NavigateStop();
                else m_browserWindow->NavigateReload();
            } else if (PtInRect(&m_homeBtnBounds, pt)) {
                m_browserWindow->NavigateUrl("https://servo.org");
            } else if (PtInRect(&m_downloadsBtnBounds, pt)) {
                m_browserWindow->ToggleDownloads();
            } else if (PtInRect(&m_settingsBtnBounds, pt)) {
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
        static HBRUSH s_brush = nullptr;
        if (s_brush) DeleteObject(s_brush);
        s_brush = CreateSolidBrush(colors.ControlBackground);
        return reinterpret_cast<LRESULT>(s_brush);
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
            if (self && self->IsSuggestionsVisible() && self->m_selectedSuggestion >= 0) {
                std::wstring sel = self->GetSelectedSuggestionText();
                self->HideSuggestions();
                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, sel.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (utf8Len > 0) {
                    std::vector<char> utf8Buf(utf8Len);
                    WideCharToMultiByte(CP_UTF8, 0, sel.c_str(), -1, utf8Buf.data(), utf8Len, nullptr, nullptr);
                    input = utf8Buf.data();
                }
            } else {
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
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    if (m_editHwnd) {
        InvalidateRect(m_editHwnd, nullptr, TRUE);
    }
}
