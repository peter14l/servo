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
#include <sstream>
#include <iomanip>
#include <cctype>
#include "new_tab_page.h"
#include "browser_data.h"

class BrowserWindow;

inline std::string UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else if (c == ' ') {
            escaped << '+';
        } else {
            escaped << '%' << std::uppercase << std::setw(2) << int(c) << std::nouppercase;
        }
    }

    return escaped.str();
}

inline std::string NormalizeOmniboxInput(const std::string& raw) {
    // 1. Trim leading and trailing whitespace
    size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = raw.find_last_not_of(" \t\r\n");
    std::string input = raw.substr(first, (last - first + 1));

    if (input.empty()) return "";

    // 2. Strip '? ' or '?' search prefix if present
    if (input.rfind("? ", 0) == 0) {
        input = input.substr(2);
        first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        input = input.substr(first);
    } else if (input.rfind("?", 0) == 0 && input.size() > 1 && input[1] != '/') {
        input = input.substr(1);
        first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        input = input.substr(first);
    }

    if (input == "servo://newtab" || input == "about:newtab") {
        return NewTabPage::EnsureNewTabPageExists();
    }
    if (input == "servo://bookmarks" || input == "servo:bookmarks" || input == "about:bookmarks") {
        return BrowserData::EnsureBookmarksPageExists();
    }
    if (input == "servo://history" || input == "servo:history" || input == "about:history") {
        return BrowserData::EnsureHistoryPageExists();
    }
    if (input == "servo://about" || input == "servo:about" || input == "about:about" || input == "servo://version" || input == "about:version") {
        return BrowserData::EnsureAboutPageExists();
    }

    // Rewrite apex youtube.com to www.youtube.com to prevent TLS renegotiation failure
    if (input == "youtube.com") {
        return "https://www.youtube.com/";
    }
    if (input.rfind("youtube.com/", 0) == 0) {
        return "https://www." + input;
    }
    if (input.rfind("https://youtube.com", 0) == 0) {
        return "https://www." + input.substr(8);
    }
    if (input.rfind("http://youtube.com", 0) == 0) {
        return "https://www." + input.substr(7);
    }

    // 3. Explicit scheme present
    if (input.find("://") != std::string::npos || input.rfind("about:", 0) == 0) {
        return input;
    }

    // 4. Localhost or loopback IP (with or without port)
    if (input == "localhost" || input.rfind("localhost:", 0) == 0 ||
        input.rfind("127.0.0.1", 0) == 0) {
        return "http://" + input;
    }

    // 5. Domain name without spaces
    if (input.find('.') != std::string::npos && input.find(' ') == std::string::npos) {
        return "https://" + input;
    }

    // 6. Search query via DuckDuckGo HTML endpoint
    return "https://duckduckgo.com/html/?q=" + UrlEncode(input);
}

class OmniboxToolbar {
public:
    static void RegisterWindowClass(HINSTANCE hInstance);

    OmniboxToolbar(HWND parentHwnd, BrowserWindow* browserWindow);
    ~OmniboxToolbar();

    HWND GetHwnd() const { return m_hwnd; }
    void SetBounds(int x, int y, int width, int height);

    void SetUrl(const std::string& url);
    void SetBookmarked(bool bookmarked);
    bool IsBookmarked() const { return m_isBookmarked; }
    void SetLoading(bool loading);
    void SetLoadingProgress(float progress);
    void UpdateNavigationState(bool canBack, bool canForward);
    void FocusAndSelectAll();
    void FocusForSearch();
    void SetDarkMode(bool isDark);

    void UpdateSuggestions(const std::wstring& query);
    void HideSuggestions();
    bool IsSuggestionsVisible() const;
    void SelectNextSuggestion();
    void SelectPreviousSuggestion();
    std::wstring GetSelectedSuggestionText() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK SuggestionsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void LayoutControls();
    void Paint(HDC hdc);
    void PaintSuggestions(HDC hdc);

    HWND m_hwnd;
    BrowserWindow* m_browserWindow;
    HWND m_editHwnd;

    RECT m_backBtnBounds;
    RECT m_forwardBtnBounds;
    RECT m_reloadBtnBounds;
    RECT m_homeBtnBounds;
    RECT m_omniboxBounds;
    RECT m_bookmarkBtnBounds;
    RECT m_downloadsBtnBounds;
    RECT m_settingsBtnBounds;

    HWND m_suggestionsHwnd;
    std::vector<std::wstring> m_suggestions;
    int m_selectedSuggestion;

    enum class NavButton {
        None,
        Back,
        Forward,
        Reload,
        Home,
        Bookmark,
        Downloads,
        Settings
    };
    NavButton m_hoveredNavBtn = NavButton::None;
    NavButton m_pressedNavBtn = NavButton::None;

    float m_loadProgress;
    UINT_PTR m_progressTimerId;

    bool m_canBack;
    bool m_canForward;
    bool m_isLoading;
    bool m_isBookmarked = false;
    bool m_isDark;
    HFONT m_font;
    HFONT m_iconFont;
    HFONT m_resultFont;
    HFONT m_resultBoldFont;
    HFONT m_badgeFont;
    HBRUSH m_editBrush;
};
