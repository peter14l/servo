/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "settings_dialog.h"
#include "browser_window.h"
#include "fluent_theme.h"
#include <commctrl.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vector>

static const wchar_t* SETTINGS_CLASS_NAME = L"ServoBrowserSettingsDialog";

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

enum ControlIds {
    IDC_COMBO_SEARCH = 101,
    IDC_EDIT_HOMEPAGE,
    IDC_EDIT_DOWNLOADS,
    IDC_BTN_BROWSE,
    IDC_COMBO_THEME,
    IDC_COMBO_BACKDROP,
    IDC_COMBO_UA,
    IDC_CHK_SUGGESTIONS,
    IDC_CHK_WEBGPU,
    IDC_CHK_WEBGL2,
    IDC_CHK_INDEXEDDB,
    IDC_BTN_CLEAR_DATA,
    IDC_BTN_SAVE,
    IDC_BTN_CANCEL
};

static std::wstring GetSettingsIniPath() {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring dir = std::wstring(localAppData) + L"\\Servo Browser";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\settings.ini";
    }
    return L"settings.ini";
}

BrowserSettings SettingsDialog::LoadSettings() {
    BrowserSettings s;
    std::wstring ini = GetSettingsIniPath();

    wchar_t buf[512];
    GetPrivateProfileStringW(L"General", L"SearchEngine", L"DuckDuckGo", buf, 512, ini.c_str());
    s.searchEngine = buf;

    GetPrivateProfileStringW(L"General", L"HomePage", L"https://servo.org", buf, 512, ini.c_str());
    s.homePage = buf;

    wchar_t userProfile[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        s.downloadDir = std::wstring(userProfile) + L"\\Downloads";
    }
    GetPrivateProfileStringW(L"General", L"DownloadDir", s.downloadDir.c_str(), buf, 512, ini.c_str());
    s.downloadDir = buf;

    GetPrivateProfileStringW(L"Privacy", L"UserAgentPersona", L"Chrome 131 (Windows 11)", buf, 512, ini.c_str());
    s.userAgentPersona = buf;

    s.themeMode = GetPrivateProfileIntW(L"Appearance", L"ThemeMode", 2, ini.c_str());
    s.backdropType = GetPrivateProfileIntW(L"Appearance", L"BackdropType", 0, ini.c_str());
    s.enableSuggestions = GetPrivateProfileIntW(L"Appearance", L"EnableSuggestions", 1, ini.c_str()) != 0;

    s.enableWebGPU = GetPrivateProfileIntW(L"Experiments", L"EnableWebGPU", 0, ini.c_str()) != 0;
    s.enableWebGL2 = GetPrivateProfileIntW(L"Experiments", L"EnableWebGL2", 1, ini.c_str()) != 0;
    s.enableIndexedDB = GetPrivateProfileIntW(L"Experiments", L"EnableIndexedDB", 1, ini.c_str()) != 0;

    return s;
}

void SettingsDialog::SaveSettings(const BrowserSettings& s) {
    std::wstring ini = GetSettingsIniPath();

    WritePrivateProfileStringW(L"General", L"SearchEngine", s.searchEngine.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"General", L"HomePage", s.homePage.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"General", L"DownloadDir", s.downloadDir.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Privacy", L"UserAgentPersona", s.userAgentPersona.c_str(), ini.c_str());

    WritePrivateProfileStringW(L"Appearance", L"ThemeMode", std::to_wstring(s.themeMode).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"BackdropType", std::to_wstring(s.backdropType).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"EnableSuggestions", s.enableSuggestions ? L"1" : L"0", ini.c_str());

    WritePrivateProfileStringW(L"Experiments", L"EnableWebGPU", s.enableWebGPU ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableWebGL2", s.enableWebGL2 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableIndexedDB", s.enableIndexedDB ? L"1" : L"0", ini.c_str());
}

void SettingsDialog::RegisterWindowClass(HINSTANCE hInstance) {
    if (!hInstance) hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SettingsDialog::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = SETTINGS_CLASS_NAME;
    RegisterClassExW(&wc);
}

void SettingsDialog::ShowModal(HWND parentHwnd, BrowserWindow* browserWindow) {
    RegisterWindowClass(nullptr);
    SettingsDialog dlg(parentHwnd, browserWindow);

    EnableWindow(parentHwnd, FALSE);
    ShowWindow(dlg.m_hwnd, SW_SHOW);
    UpdateWindow(dlg.m_hwnd);

    MSG msg;
    while (IsWindow(dlg.m_hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg.m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parentHwnd, TRUE);
    SetForegroundWindow(parentHwnd);
}

SettingsDialog::SettingsDialog(HWND parentHwnd, BrowserWindow* browserWindow)
    : m_hwnd(nullptr)
    , m_parentHwnd(parentHwnd)
    , m_browserWindow(browserWindow)
{
    m_settings = LoadSettings();

    int width = 560;
    int height = 620;

    RECT rcParent;
    GetWindowRect(parentHwnd, &rcParent);
    int x = rcParent.left + (rcParent.right - rcParent.left - width) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - height) / 2;

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    m_hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        SETTINGS_CLASS_NAME,
        L"Servo Browser Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        x, y, width, height,
        parentHwnd,
        nullptr,
        hInst,
        this
    );

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfHeight = -13;
    m_bodyFont = CreateFontIndirectW(&ncm.lfMessageFont);

    LOGFONTW hLf = ncm.lfMessageFont;
    hLf.lfHeight = -14;
    hLf.lfWeight = FW_SEMIBOLD;
    m_headerFont = CreateFontIndirectW(&hLf);

    LOGFONTW tLf = ncm.lfMessageFont;
    tLf.lfHeight = -18;
    tLf.lfWeight = FW_BOLD;
    m_titleFont = CreateFontIndirectW(&tLf);

    CreateControls();
}

SettingsDialog::~SettingsDialog() {
    if (m_bodyFont) DeleteObject(m_bodyFont);
    if (m_headerFont) DeleteObject(m_headerFont);
    if (m_titleFont) DeleteObject(m_titleFont);
}

void SettingsDialog::CreateControls() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    int x = 24, y = 20, colW = 500;

    // Header Title
    HWND lblTitle = CreateWindowW(L"STATIC", L"Settings", WS_CHILD | WS_VISIBLE,
        x, y, colW, 26, m_hwnd, nullptr, hInst, nullptr);
    SendMessageW(lblTitle, WM_SETFONT, (WPARAM)m_titleFont, TRUE);
    y += 34;

    // --- Section 1: General ---
    HWND secGen = CreateWindowW(L"STATIC", L"General & Navigation", WS_CHILD | WS_VISIBLE,
        x, y, colW, 20, m_hwnd, nullptr, hInst, nullptr);
    SendMessageW(secGen, WM_SETFONT, (WPARAM)m_headerFont, TRUE);
    y += 24;

    CreateWindowW(L"STATIC", L"Default Search Engine:", WS_CHILD | WS_VISIBLE,
        x, y + 4, 160, 20, m_hwnd, nullptr, hInst, nullptr);
    m_comboSearchEngine = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x + 160, y, 200, 160, m_hwnd, (HMENU)IDC_COMBO_SEARCH, hInst, nullptr);
    SendMessageW(m_comboSearchEngine, CB_ADDSTRING, 0, (LPARAM)L"DuckDuckGo");
    SendMessageW(m_comboSearchEngine, CB_ADDSTRING, 0, (LPARAM)L"Google");
    SendMessageW(m_comboSearchEngine, CB_ADDSTRING, 0, (LPARAM)L"Bing");
    SendMessageW(m_comboSearchEngine, CB_ADDSTRING, 0, (LPARAM)L"Startpage");
    int selEngine = (m_settings.searchEngine == L"Google") ? 1 :
                    (m_settings.searchEngine == L"Bing") ? 2 :
                    (m_settings.searchEngine == L"Startpage") ? 3 : 0;
    SendMessageW(m_comboSearchEngine, CB_SETCURSEL, selEngine, 0);
    SendMessageW(m_comboSearchEngine, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 32;

    CreateWindowW(L"STATIC", L"Home / Startup Page:", WS_CHILD | WS_VISIBLE,
        x, y + 4, 160, 20, m_hwnd, nullptr, hInst, nullptr);
    m_editHomePage = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", m_settings.homePage.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        x + 160, y, 320, 24, m_hwnd, (HMENU)IDC_EDIT_HOMEPAGE, hInst, nullptr);
    SendMessageW(m_editHomePage, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 32;

    CreateWindowW(L"STATIC", L"Download Location:", WS_CHILD | WS_VISIBLE,
        x, y + 4, 160, 20, m_hwnd, nullptr, hInst, nullptr);
    m_editDownloadDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", m_settings.downloadDir.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        x + 160, y, 240, 24, m_hwnd, (HMENU)IDC_EDIT_DOWNLOADS, hInst, nullptr);
    SendMessageW(m_editDownloadDir, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);

    m_btnBrowseDir = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + 408, y, 72, 24, m_hwnd, (HMENU)IDC_BTN_BROWSE, hInst, nullptr);
    SendMessageW(m_btnBrowseDir, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 40;

    // --- Section 2: Appearance & System ---
    HWND secApp = CreateWindowW(L"STATIC", L"Appearance & Backing", WS_CHILD | WS_VISIBLE,
        x, y, colW, 20, m_hwnd, nullptr, hInst, nullptr);
    SendMessageW(secApp, WM_SETFONT, (WPARAM)m_headerFont, TRUE);
    y += 24;

    CreateWindowW(L"STATIC", L"Theme Preference:", WS_CHILD | WS_VISIBLE,
        x, y + 4, 160, 20, m_hwnd, nullptr, hInst, nullptr);
    m_comboTheme = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x + 160, y, 200, 120, m_hwnd, (HMENU)IDC_COMBO_THEME, hInst, nullptr);
    SendMessageW(m_comboTheme, CB_ADDSTRING, 0, (LPARAM)L"Light");
    SendMessageW(m_comboTheme, CB_ADDSTRING, 0, (LPARAM)L"Dark");
    SendMessageW(m_comboTheme, CB_ADDSTRING, 0, (LPARAM)L"System Default");
    SendMessageW(m_comboTheme, CB_SETCURSEL, m_settings.themeMode, 0);
    SendMessageW(m_comboTheme, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 32;

    CreateWindowW(L"STATIC", L"Backdrop Material:", WS_CHILD | WS_VISIBLE,
        x, y + 4, 160, 20, m_hwnd, nullptr, hInst, nullptr);
    m_comboBackdrop = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x + 160, y, 200, 120, m_hwnd, (HMENU)IDC_COMBO_BACKDROP, hInst, nullptr);
    SendMessageW(m_comboBackdrop, CB_ADDSTRING, 0, (LPARAM)L"Mica Alt (Tabbed Window)");
    SendMessageW(m_comboBackdrop, CB_ADDSTRING, 0, (LPARAM)L"Mica (Main Window)");
    SendMessageW(m_comboBackdrop, CB_ADDSTRING, 0, (LPARAM)L"Solid Color");
    SendMessageW(m_comboBackdrop, CB_SETCURSEL, m_settings.backdropType, 0);
    SendMessageW(m_comboBackdrop, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 32;

    m_chkSuggestions = CreateWindowW(L"BUTTON", L"Show interactive search suggestions when typing in the Address Bar",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, colW, 22, m_hwnd, (HMENU)IDC_CHK_SUGGESTIONS, hInst, nullptr);
    SendMessageW(m_chkSuggestions, BM_SETCHECK, m_settings.enableSuggestions ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkSuggestions, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 38;

    // --- Section 3: Privacy & Anti-Bot Persona ---
    HWND secPriv = CreateWindowW(L"STATIC", L"Privacy & Browser Persona", WS_CHILD | WS_VISIBLE,
        x, y, colW, 20, m_hwnd, nullptr, hInst, nullptr);
    SendMessageW(secPriv, WM_SETFONT, (WPARAM)m_headerFont, TRUE);
    y += 24;

    CreateWindowW(L"STATIC", L"User-Agent Persona:", WS_CHILD | WS_VISIBLE,
        x, y + 4, 160, 20, m_hwnd, nullptr, hInst, nullptr);
    m_comboUserAgent = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x + 160, y, 260, 140, m_hwnd, (HMENU)IDC_COMBO_UA, hInst, nullptr);
    SendMessageW(m_comboUserAgent, CB_ADDSTRING, 0, (LPARAM)L"Chrome 131 (Windows 11) [Recommended]");
    SendMessageW(m_comboUserAgent, CB_ADDSTRING, 0, (LPARAM)L"Firefox 132 (Desktop)");
    SendMessageW(m_comboUserAgent, CB_ADDSTRING, 0, (LPARAM)L"Safari 18 (macOS)");
    int selUa = (m_settings.userAgentPersona.find(L"Firefox") != std::wstring::npos) ? 1 :
                (m_settings.userAgentPersona.find(L"Safari") != std::wstring::npos) ? 2 : 0;
    SendMessageW(m_comboUserAgent, CB_SETCURSEL, selUa, 0);
    SendMessageW(m_comboUserAgent, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 32;

    m_btnClearData = CreateWindowW(L"BUTTON", L"Clear Browsing Data (Cookies & Cache)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, 260, 26, m_hwnd, (HMENU)IDC_BTN_CLEAR_DATA, hInst, nullptr);
    SendMessageW(m_btnClearData, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 38;

    // --- Section 4: Web Platform & Experiments ---
    HWND secExp = CreateWindowW(L"STATIC", L"Web Platform & Engine Experiments", WS_CHILD | WS_VISIBLE,
        x, y, colW, 20, m_hwnd, nullptr, hInst, nullptr);
    SendMessageW(secExp, WM_SETFONT, (WPARAM)m_headerFont, TRUE);
    y += 24;

    m_chkIndexedDB = CreateWindowW(L"BUTTON", L"Enable IndexedDB API (Session & Token Storage)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, 240, 20, m_hwnd, (HMENU)IDC_CHK_INDEXEDDB, hInst, nullptr);
    SendMessageW(m_chkIndexedDB, BM_SETCHECK, m_settings.enableIndexedDB ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkIndexedDB, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);

    m_chkWebGL2 = CreateWindowW(L"BUTTON", L"Enable WebGL2 3D Graphics",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x + 250, y, 220, 20, m_hwnd, (HMENU)IDC_CHK_WEBGL2, hInst, nullptr);
    SendMessageW(m_chkWebGL2, BM_SETCHECK, m_settings.enableWebGL2 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkWebGL2, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 26;

    m_chkWebGPU = CreateWindowW(L"BUTTON", L"Enable Experimental WebGPU Pipeline",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, 280, 20, m_hwnd, (HMENU)IDC_CHK_WEBGPU, hInst, nullptr);
    SendMessageW(m_chkWebGPU, BM_SETCHECK, m_settings.enableWebGPU ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkWebGPU, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 42;

    // --- Footer Action Buttons ---
    m_btnSave = CreateWindowW(L"BUTTON", L"Save & Apply",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        x + 280, y, 110, 28, m_hwnd, (HMENU)IDC_BTN_SAVE, hInst, nullptr);
    SendMessageW(m_btnSave, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);

    m_btnCancel = CreateWindowW(L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x + 400, y, 90, 28, m_hwnd, (HMENU)IDC_BTN_CANCEL, hInst, nullptr);
    SendMessageW(m_btnCancel, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
}

void SettingsDialog::ClearBrowsingData() {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring cacheDir = std::wstring(localAppData) + L"\\Servo Browser\\Cache";
        std::wstring cookieDir = std::wstring(localAppData) + L"\\Servo Browser\\Cookies";

        auto deleteDirFiles = [](const std::wstring& dir) {
            std::wstring search = dir + L"\\*.*";
            WIN32_FIND_DATAW fd;
            HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        std::wstring path = dir + L"\\" + fd.cFileName;
                        DeleteFileW(path.c_str());
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        };

        deleteDirFiles(cacheDir);
        deleteDirFiles(cookieDir);

        MessageBoxW(m_hwnd, L"Browsing data (Cookies and HTTP Cache) successfully cleared.",
            L"Clear Browsing Data", MB_OK | MB_ICONINFORMATION);
    }
}

void SettingsDialog::ApplyAndClose() {
    // Read search engine
    int selEngine = (int)SendMessageW(m_comboSearchEngine, CB_GETCURSEL, 0, 0);
    m_settings.searchEngine = (selEngine == 1) ? L"Google" :
                              (selEngine == 2) ? L"Bing" :
                              (selEngine == 3) ? L"Startpage" : L"DuckDuckGo";

    // Read home page
    wchar_t hbuf[512] = {};
    GetWindowTextW(m_editHomePage, hbuf, 512);
    m_settings.homePage = hbuf;

    // Read download dir
    wchar_t dbuf[512] = {};
    GetWindowTextW(m_editDownloadDir, dbuf, 512);
    m_settings.downloadDir = dbuf;

    // Read appearance
    m_settings.themeMode = (int)SendMessageW(m_comboTheme, CB_GETCURSEL, 0, 0);
    m_settings.backdropType = (int)SendMessageW(m_comboBackdrop, CB_GETCURSEL, 0, 0);
    m_settings.enableSuggestions = (SendMessageW(m_chkSuggestions, BM_GETCHECK, 0, 0) == BST_CHECKED);

    // Read User-Agent
    int selUa = (int)SendMessageW(m_comboUserAgent, CB_GETCURSEL, 0, 0);
    m_settings.userAgentPersona = (selUa == 1) ? L"Firefox 132 (Desktop)" :
                                  (selUa == 2) ? L"Safari 18 (macOS)" : L"Chrome 131 (Windows 11)";

    // Read experiments
    m_settings.enableWebGPU = (SendMessageW(m_chkWebGPU, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_settings.enableWebGL2 = (SendMessageW(m_chkWebGL2, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_settings.enableIndexedDB = (SendMessageW(m_chkIndexedDB, BM_GETCHECK, 0, 0) == BST_CHECKED);

    SaveSettings(m_settings);

    // Notify main window to apply any live changes
    if (m_browserWindow) {
        m_browserWindow->ApplySettings(m_settings);
    }

    DestroyWindow(m_hwnd);
}

LRESULT CALLBACK SettingsDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<SettingsDialog*>(cs->lpCreateParams);
        if (self) {
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } else {
        self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsDialog::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_SAVE) {
            ApplyAndClose();
            return 0;
        } else if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
            DestroyWindow(m_hwnd);
            return 0;
        } else if (id == IDC_BTN_CLEAR_DATA) {
            ClearBrowsingData();
            return 0;
        } else if (id == IDC_BTN_BROWSE) {
            BROWSEINFOW bi = {};
            bi.hwndOwner = m_hwnd;
            bi.lpszTitle = L"Select Download Folder";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetWindowTextW(m_editDownloadDir, path);
                }
                CoTaskMemFree(pidl);
            }
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(m_hwnd);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
