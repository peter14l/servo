/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <memory>

class BrowserWindow;

struct BrowserTab {
    int id;
    std::wstring title;
    std::string url;
    RECT bounds;
    RECT closeButtonBounds;
    HBITMAP faviconBitmap = nullptr;
    uint32_t faviconWidth = 0;
    uint32_t faviconHeight = 0;

    ~BrowserTab() {
        if (faviconBitmap) {
            DeleteObject(faviconBitmap);
            faviconBitmap = nullptr;
        }
    }
};

class TabStrip {
public:
    static void RegisterWindowClass(HINSTANCE hInstance);

    TabStrip(HWND parentHwnd, BrowserWindow* browserWindow);
    ~TabStrip();

    HWND GetHwnd() const { return m_hwnd; }
    void SetBounds(int x, int y, int width, int height);

    int AddTab(const std::string& url);
    void CloseTab(int index);
    void SelectTab(int index);
    int GetActiveIndex() const { return m_activeIndex; }
    int GetActiveTabIndex() const { return m_activeIndex; }
    int GetTabCount() const { return static_cast<int>(m_tabs.size()); }
    BrowserTab* GetActiveTab();
    std::vector<std::string> GetAllTabUrls() const;

    void NextTab();
    void PreviousTab();
    void SelectTabByIndex(int index);
    void ReopenClosedTab();
    void CloseOtherTabs(int keepIndex);
    void CloseTabsToRight(int fromIndex);
    void DuplicateTab(int index);
    void SetDarkMode(bool isDark);

    void UpdateActiveTitle(const std::wstring& title);
    void UpdateActiveUrl(const std::string& url);
    void UpdateTabFavicon(HBITMAP hBitmap, uint32_t width, uint32_t height);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void LayoutTabs();
    void Paint(HDC hdc);

    enum CaptionButtonType {
        CAPTION_NONE = 0,
        CAPTION_MINIMIZE,
        CAPTION_MAXIMIZE,
        CAPTION_CLOSE
    };

    HWND m_hwnd;
    BrowserWindow* m_browserWindow;
    std::vector<std::unique_ptr<BrowserTab>> m_tabs;
    std::vector<std::pair<std::string, std::wstring>> m_closedHistory;
    int m_activeIndex;
    int m_nextTabId;
    int m_hoveredIndex;
    CaptionButtonType m_hoveredCaptionButton;
    RECT m_addTabButtonBounds;
    RECT m_minButtonBounds;
    RECT m_maxButtonBounds;
    RECT m_closeButtonBounds;
    HFONT m_font;
    HFONT m_iconFont;
    HFONT m_captionFont;
    bool m_isDark;
};
