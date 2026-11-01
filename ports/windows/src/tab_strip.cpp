/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "tab_strip.h"
#include "browser_window.h"
#include "servo_view_host.h"
#include "fluent_theme.h"
#include <windowsx.h>
#include <algorithm>

static const wchar_t* TAB_STRIP_CLASS_NAME = L"ServoTabStripWindow";
static const int TAB_HEIGHT = 36;
static const int TAB_MAX_WIDTH = 220;
static const int TAB_MIN_WIDTH = 100;
static const int ADD_BTN_WIDTH = 34;

void TabStrip::RegisterWindowClass(HINSTANCE hInstance) {
    if (!hInstance) {
        hInstance = GetModuleHandleW(nullptr);
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = TabStrip::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = TAB_STRIP_CLASS_NAME;
    RegisterClassExW(&wc);
}

TabStrip::TabStrip(HWND parentHwnd, BrowserWindow* browserWindow)
    : m_hwnd(nullptr)
    , m_browserWindow(browserWindow)
    , m_activeIndex(-1)
    , m_nextTabId(1)
    , m_hoveredIndex(-1)
    , m_hoveredCaptionButton(CAPTION_NONE)
    , m_font(nullptr)
    , m_iconFont(nullptr)
    , m_captionFont(nullptr)
    , m_isDark(true)
{
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    m_hwnd = CreateWindowExW(
        0,
        TAB_STRIP_CLASS_NAME,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 800, TAB_HEIGHT,
        parentHwnd,
        nullptr,
        hInstance,
        this
    );

    m_font = fluent::CreateFluentFont(13);
    m_iconFont = fluent::CreateFluentIconFont(11);
    m_captionFont = fluent::CreateCaptionIconFont(10);
}

TabStrip::~TabStrip() {
    if (m_font) {
        DeleteObject(m_font);
    }
    if (m_iconFont) {
        DeleteObject(m_iconFont);
    }
    if (m_captionFont) {
        DeleteObject(m_captionFont);
    }
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
    }
}

void TabStrip::SetBounds(int x, int y, int width, int height) {
    if (m_hwnd) {
        SetWindowPos(m_hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutTabs();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

int TabStrip::AddTab(const std::string& url) {
    auto tab = std::make_unique<BrowserTab>();
    tab->id = m_nextTabId++;
    tab->title = L"New Tab";
    tab->url = url.empty() ? "https://servo.org" : url;

    // Create the ServoViewHost for this tab
    tab->viewHost = std::make_unique<ServoViewHost>(
        m_browserWindow->GetHwnd(),
        m_browserWindow->GetServo(),
        m_browserWindow,
        tab->url
    );

    int newIndex = static_cast<int>(m_tabs.size());
    m_tabs.push_back(std::move(tab));

    SelectTab(newIndex);
    LayoutTabs();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return newIndex;
}

void TabStrip::CloseTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;

    // Record in history for Ctrl+Shift+T (capped to 25 items)
    if (m_closedHistory.size() >= 25) {
        m_closedHistory.erase(m_closedHistory.begin());
    }
    m_closedHistory.push_back({ m_tabs[index]->url, m_tabs[index]->title });

    bool wasActive = (index == m_activeIndex);
    m_tabs[index]->viewHost->Show(false);
    m_tabs.erase(m_tabs.begin() + index);

    if (m_tabs.empty()) {
        // Closing last tab closes the browser
        PostMessage(m_browserWindow->GetHwnd(), WM_CLOSE, 0, 0);
        return;
    }

    if (wasActive) {
        int nextActive = std::min(index, static_cast<int>(m_tabs.size()) - 1);
        SelectTab(nextActive);
    } else if (m_activeIndex > index) {
        m_activeIndex--;
    }

    LayoutTabs();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TabStrip::NextTab() {
    if (m_tabs.empty()) return;
    int next = (m_activeIndex + 1) % static_cast<int>(m_tabs.size());
    SelectTab(next);
}

void TabStrip::PreviousTab() {
    if (m_tabs.empty()) return;
    int prev = (m_activeIndex - 1 + static_cast<int>(m_tabs.size())) % static_cast<int>(m_tabs.size());
    SelectTab(prev);
}

void TabStrip::SelectTabByIndex(int index) {
    if (index >= 0 && index < static_cast<int>(m_tabs.size())) {
        SelectTab(index);
    }
}

void TabStrip::ReopenClosedTab() {
    if (!m_closedHistory.empty()) {
        auto lastClosed = m_closedHistory.back();
        m_closedHistory.pop_back();
        AddTab(lastClosed.first);
    }
}

void TabStrip::SetDarkMode(bool isDark) {
    m_isDark = isDark;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void TabStrip::SelectTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;

    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
        m_tabs[m_activeIndex]->viewHost->Show(false);
    }

    m_activeIndex = index;
    auto* activeTab = m_tabs[m_activeIndex].get();
    activeTab->viewHost->Show(true);
    activeTab->viewHost->SetFocus();

    m_browserWindow->UpdateActiveTabUI(activeTab->url, activeTab->title);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

BrowserTab* TabStrip::GetActiveTab() {
    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_tabs.size())) {
        return m_tabs[m_activeIndex].get();
    }
    return nullptr;
}

void TabStrip::UpdateActiveTitle(const std::wstring& title) {
    auto* tab = GetActiveTab();
    if (tab) {
        tab->title = title.empty() ? L"Untitled" : title;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void TabStrip::UpdateActiveUrl(const std::string& url) {
    auto* tab = GetActiveTab();
    if (tab) {
        tab->url = url;
    }
}

void TabStrip::UpdateTabFavicon(ServoViewHost* host, HBITMAP hBitmap, uint32_t width, uint32_t height) {
    for (auto& tab : m_tabs) {
        if (tab->viewHost.get() == host) {
            if (tab->faviconBitmap) {
                DeleteObject(tab->faviconBitmap);
            }
            tab->faviconBitmap = hBitmap;
            tab->faviconWidth = width;
            tab->faviconHeight = height;
            if (m_hwnd) {
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            break;
        }
    }
}

void TabStrip::CloseOtherTabs(int keepIndex) {
    if (keepIndex < 0 || keepIndex >= static_cast<int>(m_tabs.size())) return;
    int cur = static_cast<int>(m_tabs.size()) - 1;
    while (cur >= 0) {
        if (cur != keepIndex) {
            CloseTab(cur);
            if (cur < keepIndex) keepIndex--;
        }
        cur--;
    }
}

void TabStrip::CloseTabsToRight(int fromIndex) {
    if (fromIndex < 0 || fromIndex >= static_cast<int>(m_tabs.size())) return;
    while (static_cast<int>(m_tabs.size()) > fromIndex + 1) {
        CloseTab(static_cast<int>(m_tabs.size()) - 1);
    }
}

void TabStrip::DuplicateTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
    AddTab(m_tabs[index]->url);
}

void TabStrip::LayoutTabs() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int count = static_cast<int>(m_tabs.size());

    // Windows 11 Fluent Caption Buttons (right-aligned: Minimize, Maximize/Restore, Close)
    const int btnW = 46;
    const int btnH = rc.bottom; // 38px
    m_closeButtonBounds = { rc.right - btnW, 0, rc.right, btnH };
    m_maxButtonBounds = { rc.right - btnW * 2, 0, rc.right - btnW, btnH };
    m_minButtonBounds = { rc.right - btnW * 3, 0, rc.right - btnW * 2, btnH };

    int availableWidth = rc.right - (btnW * 3) - 16;
    if (count == 0) return;

    int tabWidth = (availableWidth - ADD_BTN_WIDTH) / count;
    tabWidth = std::clamp(tabWidth, TAB_MIN_WIDTH, TAB_MAX_WIDTH);

    int curX = 8;
    for (int i = 0; i < count; ++i) {
        // Tab sits from y=4 to y=35 (31px high)
        m_tabs[i]->bounds = { curX, 4, curX + tabWidth - 2, 35 };
        // Close button: 16x16, vertically centered in 31px tab
        m_tabs[i]->closeButtonBounds = {
            m_tabs[i]->bounds.right - 24, 11,
            m_tabs[i]->bounds.right - 6, 27
        };
        curX += tabWidth;
    }

    // Add Tab button '+' (26x26, vertically centered)
    m_addTabButtonBounds = { curX + 4, 6, curX + 32, 33 };
}

void TabStrip::Paint(HDC hdc) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    // Double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

    auto colors = m_isDark ? fluent::GetDarkColors() : fluent::GetLightColors();

    // Fill background with Mica-blend color
    HBRUSH bgBrush = CreateSolidBrush(colors.LayerBackground);
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Bottom dividing line across tab strip separating from Omnibox
    HPEN sepPen = CreatePen(PS_SOLID, 1, colors.TabBorder);
    HPEN oldSP = (HPEN)SelectObject(memDC, sepPen);
    MoveToEx(memDC, 0, rc.bottom - 1, nullptr);
    LineTo(memDC, rc.right, rc.bottom - 1);
    SelectObject(memDC, oldSP);
    DeleteObject(sepPen);

    HFONT oldFont = (HFONT)SelectObject(memDC, m_font);
    SetBkMode(memDC, TRANSPARENT);

    // Render Tabs with WinUI 3 TabView geometry
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        bool isActive = (static_cast<int>(i) == m_activeIndex);
        bool isHovered = (static_cast<int>(i) == m_hoveredIndex);
        const auto& tab = m_tabs[i];

        // Draw subtle vertical divider between inactive tabs
        if (i > 0 && !isActive && static_cast<int>(i - 1) != m_activeIndex) {
            HPEN divPen = CreatePen(PS_SOLID, 1, colors.TabBorder);
            HPEN oldDP = (HPEN)SelectObject(memDC, divPen);
            MoveToEx(memDC, tab->bounds.left - 1, tab->bounds.top + 7, nullptr);
            LineTo(memDC, tab->bounds.left - 1, tab->bounds.bottom - 7);
            SelectObject(memDC, oldDP);
            DeleteObject(divPen);
        }

        // Tab surface (active card or hovered card)
        if (isActive || isHovered) {
            COLORREF tabBg = isActive ? colors.TabBackgroundActive : colors.TabBackgroundHover;
            HBRUSH tabBrush = CreateSolidBrush(tabBg);
            HPEN tabPen = CreatePen(PS_SOLID, 1, colors.TabBorder);
            HBRUSH oldB = (HBRUSH)SelectObject(memDC, tabBrush);
            HPEN oldP = (HPEN)SelectObject(memDC, tabPen);

            // Clean 5px rounded card
            RoundRect(memDC, tab->bounds.left, tab->bounds.top, tab->bounds.right, tab->bounds.bottom, 6, 6);

            SelectObject(memDC, oldB);
            SelectObject(memDC, oldP);
            DeleteObject(tabBrush);
            DeleteObject(tabPen);
        }

        // Active tab top accent indicator line
        if (isActive) {
            HPEN accentPen = CreatePen(PS_SOLID, 2, colors.Accent);
            HPEN oldAP = (HPEN)SelectObject(memDC, accentPen);
            MoveToEx(memDC, tab->bounds.left + 6, tab->bounds.top + 1, nullptr);
            LineTo(memDC, tab->bounds.right - 6, tab->bounds.top + 1);
            SelectObject(memDC, oldAP);
            DeleteObject(accentPen);
        }

        // Favicon or fallback globe icon (vertically centered in 30px tab: y = 4 + 7 = 11)
        int iconX = tab->bounds.left + 8;
        int iconY = tab->bounds.top + 7;
        const int iconSize = 16;
        if (tab->faviconBitmap) {
            HDC iconDC = CreateCompatibleDC(memDC);
            HBITMAP oldIconBmp = (HBITMAP)SelectObject(iconDC, tab->faviconBitmap);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(memDC, iconX, iconY, iconSize, iconSize,
                       iconDC, 0, 0, tab->faviconWidth ? tab->faviconWidth : iconSize,
                       tab->faviconHeight ? tab->faviconHeight : iconSize, bf);
            SelectObject(iconDC, oldIconBmp);
            DeleteDC(iconDC);
        } else {
            // Draw Fluent Globe icon
            SelectObject(memDC, m_iconFont);
            SetTextColor(memDC, isActive ? colors.TextPrimary : colors.TextSecondary);
            RECT iconRect = { iconX, iconY, iconX + iconSize, iconY + iconSize };
            DrawTextW(memDC, fluent::icons::Globe, -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(memDC, m_font);
        }

        // Tab text - vertically centered
        SetTextColor(memDC, isActive ? colors.TextPrimary : colors.TextSecondary);
        RECT textRect = { iconX + iconSize + 6, tab->bounds.top, tab->closeButtonBounds.left - 4, tab->bounds.bottom };
        DrawTextW(memDC, tab->title.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Close button using Segoe Fluent Icons Close
        SelectObject(memDC, m_iconFont);
        SetTextColor(memDC, (isActive || isHovered) ? colors.TextPrimary : colors.TextTertiary);
        RECT closeRect = tab->closeButtonBounds;
        DrawTextW(memDC, fluent::icons::Close, -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, m_font);
    }

    // Render Add Tab button '+' using Segoe Fluent Icons AddTab
    SelectObject(memDC, m_iconFont);
    SetTextColor(memDC, colors.TextSecondary);
    RECT addRect = m_addTabButtonBounds;
    DrawTextW(memDC, fluent::icons::AddTab, -1, &addRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Render Windows 11 Fluent Caption Buttons (Minimize, Maximize/Restore, Close)
    HWND mainHwnd = m_browserWindow ? m_browserWindow->GetHwnd() : nullptr;
    bool isMaximized = mainHwnd ? (IsZoomed(mainHwnd) != FALSE) : false;

    auto drawCaptionBtn = [&](const RECT& bounds, const wchar_t* glyph, CaptionButtonType type) {
        bool hovered = (m_hoveredCaptionButton == type);
        if (hovered) {
            COLORREF bgCol = (type == CAPTION_CLOSE) ? RGB(196, 43, 28) : (m_isDark ? RGB(55, 55, 55) : RGB(229, 229, 229));
            HBRUSH hBr = CreateSolidBrush(bgCol);
            FillRect(memDC, &bounds, hBr);
            DeleteObject(hBr);
        }
        SelectObject(memDC, m_captionFont);
        SetTextColor(memDC, (hovered && type == CAPTION_CLOSE) ? RGB(255, 255, 255) : colors.TextPrimary);
        RECT r = bounds;
        DrawTextW(memDC, glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };

    drawCaptionBtn(m_minButtonBounds, fluent::icons::ChromeMinimize, CAPTION_MINIMIZE);
    drawCaptionBtn(m_maxButtonBounds, isMaximized ? fluent::icons::ChromeRestore : fluent::icons::ChromeMaximize, CAPTION_MAXIMIZE);
    drawCaptionBtn(m_closeButtonBounds, fluent::icons::ChromeClose, CAPTION_CLOSE);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldFont);
    SelectObject(memDC, oldBM);
    DeleteObject(memBM);
    DeleteDC(memDC);
}

LRESULT CALLBACK TabStrip::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TabStrip* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<TabStrip*>(cs->lpCreateParams);
        if (self) {
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } else {
        self = reinterpret_cast<TabStrip*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT TabStrip::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        Paint(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int newHover = -1;
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (PtInRect(&m_tabs[i]->bounds, pt)) {
                newHover = static_cast<int>(i);
                break;
            }
        }

        CaptionButtonType newCaptionHover = CAPTION_NONE;
        if (PtInRect(&m_closeButtonBounds, pt)) newCaptionHover = CAPTION_CLOSE;
        else if (PtInRect(&m_maxButtonBounds, pt)) newCaptionHover = CAPTION_MAXIMIZE;
        else if (PtInRect(&m_minButtonBounds, pt)) newCaptionHover = CAPTION_MINIMIZE;

        if (newHover != m_hoveredIndex || newCaptionHover != m_hoveredCaptionButton) {
            m_hoveredIndex = newHover;
            m_hoveredCaptionButton = newCaptionHover;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        bool changed = false;
        if (m_hoveredIndex != -1) {
            m_hoveredIndex = -1;
            changed = true;
        }
        if (m_hoveredCaptionButton != CAPTION_NONE) {
            m_hoveredCaptionButton = CAPTION_NONE;
            changed = true;
        }
        if (changed) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND mainHwnd = m_browserWindow ? m_browserWindow->GetHwnd() : nullptr;

        // Check Windows 11 Caption Buttons
        if (PtInRect(&m_minButtonBounds, pt)) {
            if (mainHwnd) ShowWindow(mainHwnd, SW_MINIMIZE);
            return 0;
        }
        if (PtInRect(&m_maxButtonBounds, pt)) {
            if (mainHwnd) ShowWindow(mainHwnd, IsZoomed(mainHwnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        if (PtInRect(&m_closeButtonBounds, pt)) {
            if (mainHwnd) PostMessageW(mainHwnd, WM_CLOSE, 0, 0);
            return 0;
        }

        // Check Add Tab button
        if (PtInRect(&m_addTabButtonBounds, pt)) {
            AddTab("https://servo.org");
            return 0;
        }

        // Check Tabs and Close buttons
        bool onTab = false;
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (PtInRect(&m_tabs[i]->closeButtonBounds, pt)) {
                CloseTab(static_cast<int>(i));
                return 0;
            }
            if (PtInRect(&m_tabs[i]->bounds, pt)) {
                SelectTab(static_cast<int>(i));
                onTab = true;
                break;
            }
        }

        // Click on empty space in titlebar / tabstrip initiates native window drag
        if (!onTab && mainHwnd) {
            ReleaseCapture();
            SendMessageW(mainHwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND mainHwnd = m_browserWindow ? m_browserWindow->GetHwnd() : nullptr;

        bool onTab = false;
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (PtInRect(&m_tabs[i]->bounds, pt)) {
                onTab = true;
                break;
            }
        }

        if (!onTab && !PtInRect(&m_addTabButtonBounds, pt) &&
            !PtInRect(&m_minButtonBounds, pt) && !PtInRect(&m_maxButtonBounds, pt) && !PtInRect(&m_closeButtonBounds, pt)) {
            if (mainHwnd) {
                ShowWindow(mainHwnd, IsZoomed(mainHwnd) ? SW_RESTORE : SW_MAXIMIZE);
            }
            return 0;
        }
        return 0;
    }

    case WM_MBUTTONUP: {
        // Middle click closes tab
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (PtInRect(&m_tabs[i]->bounds, pt)) {
                CloseTab(static_cast<int>(i));
                return 0;
            }
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (PtInRect(&m_tabs[i]->bounds, pt)) {
                int clickedIndex = static_cast<int>(i);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, L"Reload Tab\tCtrl+R");
                AppendMenuW(hMenu, MF_STRING, 2, L"Duplicate Tab");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, 3, L"Close Tab\tCtrl+W");
                AppendMenuW(hMenu, MF_STRING, 4, L"Close Other Tabs");
                AppendMenuW(hMenu, MF_STRING, 5, L"Close Tabs to the Right");

                POINT screenPt = pt;
                ClientToScreen(m_hwnd, &screenPt);
                int cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, m_hwnd, nullptr);
                DestroyMenu(hMenu);

                if (cmd == 1) {
                    if (m_tabs[clickedIndex]->viewHost) m_tabs[clickedIndex]->viewHost->Reload();
                } else if (cmd == 2) {
                    DuplicateTab(clickedIndex);
                } else if (cmd == 3) {
                    CloseTab(clickedIndex);
                } else if (cmd == 4) {
                    CloseOtherTabs(clickedIndex);
                } else if (cmd == 5) {
                    CloseTabsToRight(clickedIndex);
                }
                return 0;
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
