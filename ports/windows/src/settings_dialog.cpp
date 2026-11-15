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
    IDC_CHK_WEBRTC,
    IDC_CHK_NOTIFICATIONS,
    IDC_CHK_GEOLOCATION,
    IDC_CHK_VARIABLE_FONTS,
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

#pragma comment(lib, "advapi32.lib")

static bool ReadRegistryDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD* outVal) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD data = 0;
        DWORD size = sizeof(DWORD);
        LONG res = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(&data), &size);
        RegCloseKey(hKey);
        if (res == ERROR_SUCCESS && (type == REG_DWORD || type == REG_BINARY)) {
            *outVal = data;
            return true;
        }
    }
    return false;
}

static bool WriteRegistryDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD val) {
    HKEY hKey = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, &disposition) == ERROR_SUCCESS) {
        LONG res = RegSetValueExW(hKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(DWORD));
        RegCloseKey(hKey);
        return res == ERROR_SUCCESS;
    }
    return false;
}

std::string SettingsDialog::EnsureSettingsPageExists() {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring dir = std::wstring(localAppData) + L"\\Servo Browser";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring htmlPath = dir + L"\\settings.html";

        FILE* fp = _wfsopen(htmlPath.c_str(), L"w", _SH_DENYNO);
        if (fp) {
            const char* html =
                "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                "<title>Settings \xE2\x80\x94 Servo Browser</title>\n"
                "<link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">\n"
                "<link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>\n"
                "<link href=\"https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=Open+Sans:wght@400;600&family=Plus+Jakarta+Sans:wght@400;600;700&family=Roboto:wght@400;500;700&display=swap\" rel=\"stylesheet\">\n"
                "<style>\n"
                ":root {\n"
                "  --bg: #f3f3f3;\n"
                "  --sidebar-bg: rgba(243, 243, 243, 0.85);\n"
                "  --card-bg: #ffffff;\n"
                "  --card-border: rgba(0, 0, 0, 0.06);\n"
                "  --card-hover-border: rgba(0, 0, 0, 0.12);\n"
                "  --text-primary: #1a1a1a;\n"
                "  --text-secondary: #5c5c5c;\n"
                "  --text-tertiary: #8a8a8a;\n"
                "  --accent: #005fb8;\n"
                "  --accent-hover: #0052a3;\n"
                "  --nav-hover: rgba(0, 0, 0, 0.04);\n"
                "  --nav-active: rgba(0, 0, 0, 0.06);\n"
                "  --control-bg: #ffffff;\n"
                "  --control-border: #cecece;\n"
                "  --control-border-bottom: #999999;\n"
                "  --toggle-off: #888888;\n"
                "  --toggle-off-bg: transparent;\n"
                "  --divider: rgba(0, 0, 0, 0.08);\n"
                "  --danger: #c42b1c;\n"
                "  --danger-hover: #b82315;\n"
                "}\n"
                "@media (prefers-color-scheme: dark) {\n"
                "  :root {\n"
                "    --bg: #202020;\n"
                "    --sidebar-bg: rgba(32, 32, 32, 0.85);\n"
                "    --card-bg: #2b2b2b;\n"
                "    --card-border: rgba(255, 255, 255, 0.06);\n"
                "    --card-hover-border: rgba(255, 255, 255, 0.14);\n"
                "    --text-primary: #ffffff;\n"
                "    --text-secondary: #a6a6a6;\n"
                "    --text-tertiary: #767676;\n"
                "    --accent: #60cdff;\n"
                "    --accent-hover: #75d4ff;\n"
                "    --nav-hover: rgba(255, 255, 255, 0.05);\n"
                "    --nav-active: rgba(255, 255, 255, 0.08);\n"
                "    --control-bg: #323232;\n"
                "    --control-border: #484848;\n"
                "    --control-border-bottom: #5e5e5e;\n"
                "    --toggle-off: #999999;\n"
                "    --toggle-off-bg: transparent;\n"
                "    --divider: rgba(255, 255, 255, 0.08);\n"
                "    --danger: #e81123;\n"
                "    --danger-hover: #d10f1f;\n"
                "  }\n"
                "}\n"
                "* { box-sizing: border-box; margin: 0; padding: 0; }\n"
                "body {\n"
                "  font-family: 'Inter', 'Segoe UI Variable Text', 'Segoe UI', system-ui, sans-serif;\n"
                "  background: var(--bg);\n"
                "  color: var(--text-primary);\n"
                "  display: flex;\n"
                "  height: 100vh;\n"
                "  overflow: hidden;\n"
                "  user-select: none;\n"
                "}\n"
                ".ficon {\n"
                "  font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets', sans-serif;\n"
                "  font-style: normal;\n"
                "  display: inline-block;\n"
                "  line-height: 1;\n"
                "}\n"
                ".sidebar {\n"
                "  width: 270px;\n"
                "  background: var(--sidebar-bg);\n"
                "  padding: 24px 12px;\n"
                "  border-right: 1px solid var(--divider);\n"
                "  display: flex;\n"
                "  flex-direction: column;\n"
                "  gap: 3px;\n"
                "  flex-shrink: 0;\n"
                "}\n"
                ".sidebar-header {\n"
                "  display: flex;\n"
                "  align-items: center;\n"
                "  gap: 10px;\n"
                "  padding: 0 12px 18px 12px;\n"
                "  font-size: 18px;\n"
                "  font-weight: 600;\n"
                "  letter-spacing: -0.01em;\n"
                "}\n"
                ".sidebar-search {\n"
                "  margin: 0 4px 16px 4px;\n"
                "  position: relative;\n"
                "}\n"
                ".sidebar-search input {\n"
                "  width: 100%;\n"
                "  padding: 7px 12px 7px 32px;\n"
                "  font-size: 13px;\n"
                "  border-radius: 4px;\n"
                "  border: 1px solid var(--control-border);\n"
                "  background: var(--control-bg);\n"
                "  color: var(--text-primary);\n"
                "  outline: none;\n"
                "  font-family: inherit;\n"
                "}\n"
                ".sidebar-search input:focus {\n"
                "  border-bottom: 2px solid var(--accent);\n"
                "}\n"
                ".sidebar-search .ficon {\n"
                "  position: absolute;\n"
                "  left: 10px;\n"
                "  top: 9px;\n"
                "  font-size: 13px;\n"
                "  color: var(--text-tertiary);\n"
                "}\n"
                ".nav-item {\n"
                "  position: relative;\n"
                "  display: flex;\n"
                "  align-items: center;\n"
                "  gap: 12px;\n"
                "  padding: 9px 14px;\n"
                "  margin: 1px 4px;\n"
                "  border-radius: 4px;\n"
                "  cursor: pointer;\n"
                "  font-size: 13.5px;\n"
                "  color: var(--text-primary);\n"
                "  transition: background 120ms ease;\n"
                "  text-decoration: none;\n"
                "}\n"
                ".nav-item:hover {\n"
                "  background: var(--nav-hover);\n"
                "}\n"
                ".nav-item.active {\n"
                "  background: var(--nav-active);\n"
                "  font-weight: 600;\n"
                "}\n"
                ".nav-item.active::before {\n"
                "  content: \"\";\n"
                "  position: absolute;\n"
                "  left: 0;\n"
                "  top: 10px;\n"
                "  bottom: 10px;\n"
                "  width: 3px;\n"
                "  border-radius: 2px;\n"
                "  background: var(--accent);\n"
                "}\n"
                ".nav-item .ficon {\n"
                "  font-size: 16px;\n"
                "  width: 18px;\n"
                "  text-align: center;\n"
                "  color: var(--text-secondary);\n"
                "}\n"
                ".nav-item.active .ficon {\n"
                "  color: var(--accent);\n"
                "}\n"
                ".content {\n"
                "  flex: 1;\n"
                "  padding: 36px 52px;\n"
                "  overflow-y: auto;\n"
                "}\n"
                ".section {\n"
                "  display: none;\n"
                "  max-width: 820px;\n"
                "  animation: fadeIn 150ms cubic-bezier(0.1, 0.9, 0.2, 1);\n"
                "}\n"
                ".section.active {\n"
                "  display: block;\n"
                "}\n"
                "@keyframes fadeIn {\n"
                "  from { opacity: 0; transform: translateY(4px); }\n"
                "  to { opacity: 1; transform: translateY(0); }\n"
                "}\n"
                ".section-title {\n"
                "  font-size: 26px;\n"
                "  font-weight: 600;\n"
                "  margin-bottom: 22px;\n"
                "  letter-spacing: -0.015em;\n"
                "}\n"
                ".settings-card {\n"
                "  background: var(--card-bg);\n"
                "  border: 1px solid var(--card-border);\n"
                "  border-radius: 8px;\n"
                "  padding: 16px 20px;\n"
                "  margin-bottom: 10px;\n"
                "  display: flex;\n"
                "  justify-content: space-between;\n"
                "  align-items: center;\n"
                "  gap: 16px;\n"
                "  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.03), 0 1px 2px rgba(0, 0, 0, 0.02);\n"
                "  transition: border-color 150ms ease, background 150ms ease;\n"
                "}\n"
                ".settings-card:hover {\n"
                "  border-color: var(--card-hover-border);\n"
                "}\n"
                ".card-left {\n"
                "  display: flex;\n"
                "  align-items: center;\n"
                "  gap: 14px;\n"
                "}\n"
                ".card-icon {\n"
                "  font-size: 18px;\n"
                "  color: var(--text-secondary);\n"
                "  width: 24px;\n"
                "  text-align: center;\n"
                "}\n"
                ".card-info h3 {\n"
                "  font-size: 14px;\n"
                "  font-weight: 500;\n"
                "  color: var(--text-primary);\n"
                "  margin-bottom: 3px;\n"
                "}\n"
                ".card-info p {\n"
                "  font-size: 12.5px;\n"
                "  color: var(--text-secondary);\n"
                "  line-height: 1.4;\n"
                "}\n"
                "/* WinUI 3 Segmented Control */\n"
                ".segmented-group {\n"
                "  display: inline-flex;\n"
                "  flex-wrap: wrap;\n"
                "  background: var(--control-bg);\n"
                "  border: 1px solid var(--control-border);\n"
                "  border-radius: 6px;\n"
                "  padding: 2px;\n"
                "  gap: 2px;\n"
                "}\n"
                ".seg-btn {\n"
                "  background: transparent;\n"
                "  border: none;\n"
                "  border-radius: 4px;\n"
                "  padding: 6px 12px;\n"
                "  font-size: 12.5px;\n"
                "  font-family: inherit;\n"
                "  color: var(--text-primary);\n"
                "  cursor: pointer;\n"
                "  transition: all 120ms ease;\n"
                "  user-select: none;\n"
                "}\n"
                ".seg-btn:hover {\n"
                "  background: var(--nav-hover);\n"
                "}\n"
                ".seg-btn.active {\n"
                "  background: var(--accent);\n"
                "  color: #ffffff;\n"
                "  font-weight: 500;\n"
                "  box-shadow: 0 1px 3px rgba(0,0,0,0.15);\n"
                "}\n"
                "/* WinUI 3 Toggle Switch */\n"
                ".win-toggle {\n"
                "  display: inline-flex;\n"
                "  align-items: center;\n"
                "  gap: 10px;\n"
                "  cursor: pointer;\n"
                "  user-select: none;\n"
                "}\n"
                ".win-toggle-track {\n"
                "  width: 40px;\n"
                "  height: 20px;\n"
                "  border-radius: 10px;\n"
                "  background: transparent;\n"
                "  border: 1px solid var(--toggle-off);\n"
                "  position: relative;\n"
                "  transition: all 160ms cubic-bezier(0.1, 0.9, 0.2, 1);\n"
                "  box-sizing: border-box;\n"
                "}\n"
                ".win-toggle-thumb {\n"
                "  width: 12px;\n"
                "  height: 12px;\n"
                "  border-radius: 50%;\n"
                "  background: var(--toggle-off);\n"
                "  position: absolute;\n"
                "  top: 3px;\n"
                "  left: 3px;\n"
                "  transition: all 160ms cubic-bezier(0.1, 0.9, 0.2, 1);\n"
                "}\n"
                ".win-toggle.active .win-toggle-track {\n"
                "  background: var(--accent);\n"
                "  border-color: var(--accent);\n"
                "}\n"
                ".win-toggle.active .win-toggle-track .win-toggle-thumb {\n"
                "  transform: translateX(20px);\n"
                "  background: #ffffff;\n"
                "}\n"
                ".win-toggle-status {\n"
                "  font-size: 13px;\n"
                "  color: var(--text-primary);\n"
                "  min-width: 26px;\n"
                "}\n"
                "input[type=\"text\"], input[type=\"url\"] {\n"
                "  background: var(--control-bg);\n"
                "  color: var(--text-primary);\n"
                "  border: 1px solid var(--control-border);\n"
                "  border-bottom: 1px solid var(--control-border-bottom);\n"
                "  border-radius: 4px;\n"
                "  padding: 6px 12px;\n"
                "  font-size: 13px;\n"
                "  min-width: 220px;\n"
                "  font-family: inherit;\n"
                "  outline: none;\n"
                "  transition: border-color 120ms ease;\n"
                "}\n"
                "input[type=\"text\"]:focus, input[type=\"url\"]:focus {\n"
                "  border-bottom: 2px solid var(--accent);\n"
                "}\n"
                ".btn {\n"
                "  background: var(--control-bg);\n"
                "  color: var(--text-primary);\n"
                "  border: 1px solid var(--control-border);\n"
                "  border-bottom: 1px solid var(--control-border-bottom);\n"
                "  border-radius: 4px;\n"
                "  padding: 6px 16px;\n"
                "  font-size: 13px;\n"
                "  font-weight: 500;\n"
                "  cursor: pointer;\n"
                "  font-family: inherit;\n"
                "  transition: background 120ms ease;\n"
                "}\n"
                ".btn:hover {\n"
                "  background: var(--nav-hover);\n"
                "}\n"
                ".btn-accent {\n"
                "  background: var(--accent);\n"
                "  color: #ffffff;\n"
                "  border: 1px solid var(--accent);\n"
                "}\n"
                ".btn-accent:hover {\n"
                "  background: var(--accent-hover);\n"
                "}\n"
                ".btn-danger {\n"
                "  background: var(--danger);\n"
                "  color: #ffffff;\n"
                "  border: 1px solid var(--danger);\n"
                "}\n"
                ".btn-danger:hover {\n"
                "  background: var(--danger-hover);\n"
                "}\n"
                ".toast {\n"
                "  position: fixed;\n"
                "  bottom: 24px;\n"
                "  right: 28px;\n"
                "  background: var(--card-bg);\n"
                "  border: 1px solid var(--card-border);\n"
                "  border-radius: 8px;\n"
                "  padding: 12px 20px;\n"
                "  display: flex;\n"
                "  align-items: center;\n"
                "  gap: 10px;\n"
                "  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.16);\n"
                "  transform: translateY(60px);\n"
                "  opacity: 0;\n"
                "  transition: all 220ms cubic-bezier(0.1, 0.9, 0.2, 1);\n"
                "  pointer-events: none;\n"
                "}\n"
                ".toast.show {\n"
                "  transform: translateY(0);\n"
                "  opacity: 1;\n"
                "}\n"
                ".toast .ficon {\n"
                "  color: #107c10;\n"
                "  font-size: 16px;\n"
                "}\n"
                "</style>\n</head>\n<body>\n"
                "<div class=\"sidebar\">\n"
                "  <div class=\"sidebar-header\"><span class=\"ficon\">&#xE713;</span> Settings</div>\n"
                "  <div class=\"sidebar-search\"><span class=\"ficon\">&#xE721;</span><input type=\"text\" placeholder=\"Find a setting\" id=\"filterInput\" oninput=\"filterSettings()\"></div>\n"
                "  <a class=\"nav-item active\" onclick=\"showTab('general', this)\"><span class=\"ficon\">&#xE713;</span> General</a>\n"
                "  <a class=\"nav-item\" onclick=\"showTab('appearance', this)\"><span class=\"ficon\">&#xE790;</span> Appearance</a>\n"
                "  <a class=\"nav-item\" onclick=\"showTab('compatibility', this)\"><span class=\"ficon\">&#xE777;</span> Compatibility</a>\n"
                "  <a class=\"nav-item\" onclick=\"showTab('experiments', this)\"><span class=\"ficon\">&#xE9E9;</span> Experiments</a>\n"
                "  <a class=\"nav-item\" onclick=\"showTab('privacy', this)\"><span class=\"ficon\">&#xE72E;</span> Privacy & Data</a>\n"
                "  <a class=\"nav-item\" onclick=\"showTab('about', this)\"><span class=\"ficon\">&#xE946;</span> About Servo</a>\n"
                "</div>\n"
                "<div class=\"content\">\n"
                "  <div id=\"general\" class=\"section active\">\n"
                "    <h1 class=\"section-title\">General</h1>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE721;</span><div class=\"card-info\"><h3>Default search engine</h3><p>Used for searches submitted from the address bar.</p></div></div><div class=\"segmented-group\" id=\"grp-search\"><button class=\"seg-btn active\" data-group=\"search\" data-val=\"DuckDuckGo\" onclick=\"selectSegment('search','DuckDuckGo',this)\">DuckDuckGo</button><button class=\"seg-btn\" data-group=\"search\" data-val=\"Google\" onclick=\"selectSegment('search','Google',this)\">Google</button><button class=\"seg-btn\" data-group=\"search\" data-val=\"Bing\" onclick=\"selectSegment('search','Bing',this)\">Bing</button><button class=\"seg-btn\" data-group=\"search\" data-val=\"Ecosia\" onclick=\"selectSegment('search','Ecosia',this)\">Ecosia</button></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE80F;</span><div class=\"card-info\"><h3>Home page</h3><p>Page loaded on new windows and when clicking the Home button.</p></div></div><input type=\"url\" id=\"home\" onchange=\"save()\" value=\"https://servo.org\"></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE896;</span><div class=\"card-info\"><h3>Downloads location</h3><p>Folder where downloaded files are saved automatically.</p></div></div><input type=\"text\" id=\"dl\" onchange=\"save()\"></div>\n"
                "  </div>\n"
                "  <div id=\"appearance\" class=\"section\">\n"
                "    <h1 class=\"section-title\">Appearance</h1>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE8D2;</span><div class=\"card-info\"><h3>Browser Font (Google Fonts)</h3><p>Typography applied to navigation chrome, tabs, omnibox, and suggestions.</p></div></div><div class=\"segmented-group\" id=\"grp-font\"><button class=\"seg-btn active\" data-group=\"font\" data-val=\"Inter\" onclick=\"selectSegment('font','Inter',this)\">Inter (Google Fonts)</button><button class=\"seg-btn\" data-group=\"font\" data-val=\"Roboto\" onclick=\"selectSegment('font','Roboto',this)\">Roboto</button><button class=\"seg-btn\" data-group=\"font\" data-val=\"Plus Jakarta Sans\" onclick=\"selectSegment('font','Plus Jakarta Sans',this)\">Plus Jakarta Sans</button><button class=\"seg-btn\" data-group=\"font\" data-val=\"Open Sans\" onclick=\"selectSegment('font','Open Sans',this)\">Open Sans</button><button class=\"seg-btn\" data-group=\"font\" data-val=\"Segoe UI Variable Text\" onclick=\"selectSegment('font','Segoe UI Variable Text',this)\">Segoe UI</button></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE790;</span><div class=\"card-info\"><h3>App theme</h3><p>Select your preferred visual theme.</p></div></div><div class=\"segmented-group\" id=\"grp-theme\"><button class=\"seg-btn active\" data-group=\"theme\" data-val=\"2\" onclick=\"selectSegment('theme','2',this)\">System Default</button><button class=\"seg-btn\" data-group=\"theme\" data-val=\"1\" onclick=\"selectSegment('theme','1',this)\">Dark</button><button class=\"seg-btn\" data-group=\"theme\" data-val=\"0\" onclick=\"selectSegment('theme','0',this)\">Light</button></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE7F4;</span><div class=\"card-info\"><h3>Mica Alt tabbed backdrop</h3><p>Windows 11 translucent system material.</p></div></div><div class=\"segmented-group\" id=\"grp-backdrop\"><button class=\"seg-btn active\" data-group=\"backdrop\" data-val=\"0\" onclick=\"selectSegment('backdrop','0',this)\">Mica Alt (Tabbed)</button><button class=\"seg-btn\" data-group=\"backdrop\" data-val=\"1\" onclick=\"selectSegment('backdrop','1',this)\">Mica</button><button class=\"seg-btn\" data-group=\"backdrop\" data-val=\"2\" onclick=\"selectSegment('backdrop','2',this)\">Solid</button></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE8C4;</span><div class=\"card-info\"><h3>Vertical tabs</h3><p>Dock the tab bar on the left side of the window (Ctrl+Shift+V).</p></div></div><div class=\"win-toggle\" id=\"toggle-verticaltabs\" onclick=\"toggleSwitch('verticaltabs')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-verticaltabs\">Off</span></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE721;</span><div class=\"card-info\"><h3>Omnibox search & domain suggestions</h3><p>Show drop-down interactive suggestions with Rank-0 website priority while typing.</p></div></div><div class=\"win-toggle active\" id=\"toggle-sugg\" onclick=\"toggleSwitch('sugg')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-sugg\">On</span></div></div>\n"
                "  </div>\n"
                "  <div id=\"compatibility\" class=\"section\">\n"
                "    <h1 class=\"section-title\">Web Standards & Anti-Bot Compatibility</h1>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE72D;</span><div class=\"card-info\"><h3>Native Desktop Fingerprint</h3><p>Servo identifies natively as an authentic desktop browser out-of-the-box. Personas are deprecated in favor of compliant desktop APIs.</p></div></div><span style=\"font-size:12px;color:#107c10;font-weight:600;padding:4px 8px;background:rgba(16,124,16,0.1);border-radius:4px;\">Active</span></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE774;</span><div class=\"card-info\"><h3>Anti-Automation Shield</h3><p>navigator.webdriver is permanently suppressed and standard desktop plugins (PDF viewers) are exposed to prevent bot detection.</p></div></div><span style=\"font-size:12px;color:#107c10;font-weight:600;padding:4px 8px;background:rgba(16,124,16,0.1);border-radius:4px;\">Shielded</span></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE9E9;</span><div class=\"card-info\"><h3>Chrome Runtime & Permissions</h3><p>Provides window.chrome and navigator.permissions shims required by modern SPAs and bot detection heuristics.</p></div></div><span style=\"font-size:12px;color:#107c10;font-weight:600;padding:4px 8px;background:rgba(16,124,16,0.1);border-radius:4px;\">Enabled</span></div>\n"
                "  </div>\n"
                "  <div id=\"experiments\" class=\"section\">\n"
                "    <h1 class=\"section-title\">Experiments</h1>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE9E9;</span><div class=\"card-info\"><h3>WebGPU Support</h3><p>Experimental GPU compute & rendering via wgpu.</p></div></div><div class=\"win-toggle\" id=\"toggle-webgpu\" onclick=\"toggleSwitch('webgpu')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-webgpu\">Off</span></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE790;</span><div class=\"card-info\"><h3>WebGL 2.0</h3><p>Hardware 3D graphics acceleration via ANGLE.</p></div></div><div class=\"win-toggle active\" id=\"toggle-webgl2\" onclick=\"toggleSwitch('webgl2')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-webgl2\">On</span></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE74C;</span><div class=\"card-info\"><h3>IndexedDB Storage</h3><p>Client-side structured offline database.</p></div></div><div class=\"win-toggle active\" id=\"toggle-idb\" onclick=\"toggleSwitch('idb')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-idb\">On</span></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE717;</span><div class=\"card-info\"><h3>WebRTC Real-Time Communication</h3><p>Real-time audio/video streaming and peer-to-peer data channels.</p></div></div><div class=\"win-toggle active\" id=\"toggle-webrtc\" onclick=\"toggleSwitch('webrtc')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-webrtc\">On</span></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xEA8F;</span><div class=\"card-info\"><h3>Web Notifications</h3><p>Allows web applications to deliver desktop system notifications.</p></div></div><div class=\"win-toggle active\" id=\"toggle-notifications\" onclick=\"toggleSwitch('notifications')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-notifications\">On</span></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE819;</span><div class=\"card-info\"><h3>Geolocation API</h3><p>Prompt-based website access to physical device location.</p></div></div><div class=\"win-toggle\" id=\"toggle-geolocation\" onclick=\"toggleSwitch('geolocation')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-geolocation\">Off</span></div></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE8D2;</span><div class=\"card-info\"><h3>Variable Fonts Typography</h3><p>Support for OpenType Font Variations dynamic styling axes.</p></div></div><div class=\"win-toggle active\" id=\"toggle-varfonts\" onclick=\"toggleSwitch('varfonts')\"><div class=\"win-toggle-track\"><div class=\"win-toggle-thumb\"></div></div><span class=\"win-toggle-status\" id=\"lbl-varfonts\">On</span></div></div>\n"
                "  </div>\n"
                "  <div id=\"privacy\" class=\"section\">\n"
                "    <h1 class=\"section-title\">Privacy & Data</h1>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE74D;</span><div class=\"card-info\"><h3>Clear browsing data</h3><p>Permanently removes cookies, cache, and offline storage.</p></div></div><button class=\"btn btn-danger\" onclick=\"clearData()\">Clear Data</button></div>\n"
                "  </div>\n"
                "  <div id=\"about\" class=\"section\">\n"
                "    <h1 class=\"section-title\">About Servo</h1>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE713;</span><div class=\"card-info\"><h3>Native Windows Settings Dialog</h3><p>Open the standalone Windows 11 Mica modal settings dialog.</p></div></div><button class=\"btn btn-accent\" onclick=\"window.location.href='servo://native-settings'\">Open Dialog</button></div>\n"
                "    <div class=\"settings-card\"><div class=\"card-left\"><span class=\"ficon card-icon\">&#xE946;</span><div class=\"card-info\"><h3>Servo Modern Web Browser</h3><p>Version 0.0.1 (Windows x86_64, WinUI 3 Fluent Edition)</p><p style=\"margin-top:6px;font-size:12px;color:var(--text-secondary)\">WebRender &bull; SpiderMonkey &bull; Surfman &bull; Fluent UI</p></div></div><button class=\"btn btn-accent\" onclick=\"window.open('https://github.com/servo/servo')\"><span class=\"ficon\" style=\"margin-right:6px\">&#xE8A7;</span>GitHub</button></div>\n"
                "  </div>\n"
                "</div>\n"
                "<div id=\"toast\" class=\"toast\"><span class=\"ficon\">&#xE73E;</span> Settings saved</div>\n"
                "<script>\n"
                "const _memPrefs = {};\n"
                "function _getPref(k) { try { return localStorage.getItem(k); } catch(e) { return _memPrefs[k] || null; } }\n"
                "function _setPref(k, v) { try { localStorage.setItem(k, v); } catch(e) { _memPrefs[k] = v; } }\n"
                "function _clearPrefs() { try { localStorage.clear(); } catch(e) { for (let k in _memPrefs) delete _memPrefs[k]; } }\n"
                "function showTab(id, el) { document.querySelectorAll('.section').forEach(s => s.classList.remove('active')); document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active')); document.getElementById(id).classList.add('active'); el.classList.add('active'); }\n"
                "function filterSettings() { const q = document.getElementById('filterInput').value.toLowerCase(); document.querySelectorAll('.settings-card').forEach(c => { const text = c.textContent.toLowerCase(); c.style.display = text.includes(q) ? 'flex' : 'none'; }); }\n"
                "function selectSegment(group, val, btn) {\n"
                "  document.querySelectorAll('[data-group=\"' + group + '\"]').forEach(b => b.classList.remove('active'));\n"
                "  if (btn) btn.classList.add('active');\n"
                "  save();\n"
                "}\n"
                "function getSegmentVal(group) {\n"
                "  const el = document.querySelector('[data-group=\"' + group + '\"].active');\n"
                "  return el ? el.getAttribute('data-val') : '';\n"
                "}\n"
                "function setSegmentVal(group, val) {\n"
                "  const el = document.querySelector('[data-group=\"' + group + '\"][data-val=\"' + val + '\"]');\n"
                "  if (el) {\n"
                "    document.querySelectorAll('[data-group=\"' + group + '\"]').forEach(b => b.classList.remove('active'));\n"
                "    el.classList.add('active');\n"
                "  }\n"
                "}\n"
                "function toggleSwitch(key) {\n"
                "  const el = document.getElementById('toggle-' + key);\n"
                "  if (!el) return;\n"
                "  const active = el.classList.toggle('active');\n"
                "  const lbl = document.getElementById('lbl-' + key);\n"
                "  if (lbl) lbl.textContent = active ? 'On' : 'Off';\n"
                "  save();\n"
                "}\n"
                "function isToggleActive(key) {\n"
                "  const el = document.getElementById('toggle-' + key);\n"
                "  return el ? el.classList.contains('active') : false;\n"
                "}\n"
                "function setToggleActive(key, active) {\n"
                "  const el = document.getElementById('toggle-' + key);\n"
                "  if (el) {\n"
                "    if (active) el.classList.add('active');\n"
                "    else el.classList.remove('active');\n"
                "    const lbl = document.getElementById('lbl-' + key);\n"
                "    if (lbl) lbl.textContent = active ? 'On' : 'Off';\n"
                "  }\n"
                "}\n"
                "function load() {\n"
                "  const raw = _getPref('servo_prefs');\n"
                "  const s = raw ? JSON.parse(raw) : {};\n"
                "  if (s.font) { setSegmentVal('font', s.font); document.body.style.fontFamily = `'${s.font}', -apple-system, sans-serif`; }\n"
                "  if (s.search) setSegmentVal('search', s.search);\n"
                "  if (s.home) document.getElementById('home').value = s.home;\n"
                "  if (s.dl) document.getElementById('dl').value = s.dl;\n"
                "  if (s.theme !== undefined) setSegmentVal('theme', s.theme);\n"
                "  if (s.backdrop !== undefined) setSegmentVal('backdrop', s.backdrop);\n"
                "  if (s.verticalTabs !== undefined) setToggleActive('verticaltabs', s.verticalTabs);\n"
                "  if (s.sugg !== undefined) setToggleActive('sugg', s.sugg);\n"
                "  if (s.webgpu !== undefined) setToggleActive('webgpu', s.webgpu);\n"
                "  if (s.webgl2 !== undefined) setToggleActive('webgl2', s.webgl2);\n"
                "  if (s.idb !== undefined) setToggleActive('idb', s.idb);\n"
                "  if (s.webrtc !== undefined) setToggleActive('webrtc', s.webrtc);\n"
                "  if (s.notifications !== undefined) setToggleActive('notifications', s.notifications);\n"
                "  if (s.geolocation !== undefined) setToggleActive('geolocation', s.geolocation);\n"
                "  if (s.varfonts !== undefined) setToggleActive('varfonts', s.varfonts);\n"
                "}\n"
                "function save() {\n"
                "  const f = getSegmentVal('font') || 'Inter';\n"
                "  document.body.style.fontFamily = `'${f}', -apple-system, sans-serif`;\n"
                "  const s = {\n"
                "    font: f,\n"
                "    search: getSegmentVal('search'),\n"
                "    home: document.getElementById('home').value,\n"
                "    dl: document.getElementById('dl').value,\n"
                "    theme: getSegmentVal('theme'),\n"
                "    backdrop: getSegmentVal('backdrop'),\n"
                "    verticalTabs: isToggleActive('verticaltabs'),\n"
                "    sugg: isToggleActive('sugg'),\n"
                "    webgpu: isToggleActive('webgpu'),\n"
                "    webgl2: isToggleActive('webgl2'),\n"
                "    idb: isToggleActive('idb'),\n"
                "    webrtc: isToggleActive('webrtc'),\n"
                "    notifications: isToggleActive('notifications'),\n"
                "    geolocation: isToggleActive('geolocation'),\n"
                "    varfonts: isToggleActive('varfonts')\n"
                "  };\n"
                "  _setPref('servo_prefs', JSON.stringify(s));\n"
                "  console.log('__servo_ipc__:apply_settings:' + JSON.stringify(s));\n"
                "  const t = document.getElementById('toast');\n"
                "  t.classList.add('show');\n"
                "  setTimeout(() => t.classList.remove('show'), 1600);\n"
                "}\n"
                "function clearData() { _clearPrefs(); alert('Browsing data and caches cleared.'); }\n"
                "window.onload = load;\n"
                "</script>\n</body>\n</html>\n";
            fputs(html, fp);
            fclose(fp);
        }

        // Convert path to file URL
        std::string fileUrl = "file:///";
        for (wchar_t c : htmlPath) {
            if (c == L'\\') fileUrl += '/';
            else fileUrl += static_cast<char>(c);
        }
        return fileUrl;
    }
    return "about:blank";
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

    GetPrivateProfileStringW(L"Privacy", L"UserAgentPersona", L"Firefox 132 (Desktop) [Recommended]", buf, 512, ini.c_str());
    s.userAgentPersona = buf;

    s.themeMode = GetPrivateProfileIntW(L"Appearance", L"ThemeMode", 2, ini.c_str());
    s.backdropType = GetPrivateProfileIntW(L"Appearance", L"BackdropType", 0, ini.c_str());
    s.enableVerticalTabs = GetPrivateProfileIntW(L"Appearance", L"EnableVerticalTabs", 0, ini.c_str()) != 0;
    s.enableSuggestions = GetPrivateProfileIntW(L"Appearance", L"EnableSuggestions", 1, ini.c_str()) != 0;

    s.enableWebGPU = GetPrivateProfileIntW(L"Experiments", L"EnableWebGPU", 0, ini.c_str()) != 0;
    s.enableWebGL2 = GetPrivateProfileIntW(L"Experiments", L"EnableWebGL2", 1, ini.c_str()) != 0;
    s.enableIndexedDB = GetPrivateProfileIntW(L"Experiments", L"EnableIndexedDB", 1, ini.c_str()) != 0;

    DWORD dwWebRTC = 1;
    if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableWebRTC", &dwWebRTC)) {
        dwWebRTC = GetPrivateProfileIntW(L"Experiments", L"EnableWebRTC", 1, ini.c_str());
    }
    s.enableWebRTC = (dwWebRTC != 0);

    DWORD dwNotifications = 1;
    if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableNotifications", &dwNotifications)) {
        dwNotifications = GetPrivateProfileIntW(L"Experiments", L"EnableNotifications", 1, ini.c_str());
    }
    s.enableNotifications = (dwNotifications != 0);

    DWORD dwGeolocation = 0;
    if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableGeolocation", &dwGeolocation)) {
        dwGeolocation = GetPrivateProfileIntW(L"Experiments", L"EnableGeolocation", 0, ini.c_str());
    }
    s.enableGeolocation = (dwGeolocation != 0);

    DWORD dwVariableFonts = 1;
    if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableVariableFonts", &dwVariableFonts)) {
        dwVariableFonts = GetPrivateProfileIntW(L"Experiments", L"EnableVariableFonts", 1, ini.c_str());
    }
    s.enableVariableFonts = (dwVariableFonts != 0);

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
    WritePrivateProfileStringW(L"Appearance", L"EnableVerticalTabs", s.enableVerticalTabs ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"EnableSuggestions", s.enableSuggestions ? L"1" : L"0", ini.c_str());

    WritePrivateProfileStringW(L"Experiments", L"EnableWebGPU", s.enableWebGPU ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableWebGL2", s.enableWebGL2 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableIndexedDB", s.enableIndexedDB ? L"1" : L"0", ini.c_str());

    // Registry DWORD persistence for expanded web platform features
    WriteRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableWebRTC", s.enableWebRTC ? 1 : 0);
    WriteRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableNotifications", s.enableNotifications ? 1 : 0);
    WriteRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableGeolocation", s.enableGeolocation ? 1 : 0);
    WriteRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableVariableFonts", s.enableVariableFonts ? 1 : 0);

    // INI persistence mirror
    WritePrivateProfileStringW(L"Experiments", L"EnableWebRTC", s.enableWebRTC ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableNotifications", s.enableNotifications ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableGeolocation", s.enableGeolocation ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableVariableFonts", s.enableVariableFonts ? L"1" : L"0", ini.c_str());
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
    , m_titleFont(nullptr)
    , m_headerFont(nullptr)
    , m_bodyFont(nullptr)
    , m_comboSearchEngine(nullptr)
    , m_editHomePage(nullptr)
    , m_editDownloadDir(nullptr)
    , m_btnBrowseDir(nullptr)
    , m_comboTheme(nullptr)
    , m_comboBackdrop(nullptr)
    , m_comboUserAgent(nullptr)
    , m_chkSuggestions(nullptr)
    , m_chkWebGPU(nullptr)
    , m_chkWebGL2(nullptr)
    , m_chkIndexedDB(nullptr)
    , m_checkWebRTC(nullptr)
    , m_checkNotifications(nullptr)
    , m_checkGeolocation(nullptr)
    , m_checkVariableFonts(nullptr)
    , m_btnClearData(nullptr)
    , m_btnSave(nullptr)
    , m_btnCancel(nullptr)
{
    m_settings = LoadSettings();

    int width = 560;
    int height = 670;

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
    SendMessageW(m_comboUserAgent, CB_ADDSTRING, 0, (LPARAM)L"Firefox 132 (Desktop) [Recommended]");
    SendMessageW(m_comboUserAgent, CB_ADDSTRING, 0, (LPARAM)L"Chrome 131 (Windows 11)");
    SendMessageW(m_comboUserAgent, CB_ADDSTRING, 0, (LPARAM)L"Safari 18 (macOS)");
    int selUa = (m_settings.userAgentPersona.find(L"Chrome") != std::wstring::npos) ? 1 :
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

    m_chkIndexedDB = CreateWindowW(L"BUTTON", L"Enable IndexedDB API (Storage)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, 240, 20, m_hwnd, (HMENU)IDC_CHK_INDEXEDDB, hInst, nullptr);
    SendMessageW(m_chkIndexedDB, BM_SETCHECK, m_settings.enableIndexedDB ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkIndexedDB, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);

    m_chkWebGL2 = CreateWindowW(L"BUTTON", L"Enable WebGL2 3D Graphics",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x + 250, y, 240, 20, m_hwnd, (HMENU)IDC_CHK_WEBGL2, hInst, nullptr);
    SendMessageW(m_chkWebGL2, BM_SETCHECK, m_settings.enableWebGL2 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkWebGL2, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 24;

    m_chkWebGPU = CreateWindowW(L"BUTTON", L"Enable Experimental WebGPU",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, 240, 20, m_hwnd, (HMENU)IDC_CHK_WEBGPU, hInst, nullptr);
    SendMessageW(m_chkWebGPU, BM_SETCHECK, m_settings.enableWebGPU ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkWebGPU, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);

    m_checkWebRTC = CreateWindowW(L"BUTTON", L"Enable WebRTC PeerConnection",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x + 250, y, 240, 20, m_hwnd, (HMENU)IDC_CHK_WEBRTC, hInst, nullptr);
    SendMessageW(m_checkWebRTC, BM_SETCHECK, m_settings.enableWebRTC ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_checkWebRTC, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 24;

    m_checkNotifications = CreateWindowW(L"BUTTON", L"Enable Web Notifications",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, 240, 20, m_hwnd, (HMENU)IDC_CHK_NOTIFICATIONS, hInst, nullptr);
    SendMessageW(m_checkNotifications, BM_SETCHECK, m_settings.enableNotifications ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_checkNotifications, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);

    m_checkGeolocation = CreateWindowW(L"BUTTON", L"Enable Geolocation API",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x + 250, y, 240, 20, m_hwnd, (HMENU)IDC_CHK_GEOLOCATION, hInst, nullptr);
    SendMessageW(m_checkGeolocation, BM_SETCHECK, m_settings.enableGeolocation ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_checkGeolocation, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 24;

    m_checkVariableFonts = CreateWindowW(L"BUTTON", L"Enable Variable Font Typography",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, 280, 20, m_hwnd, (HMENU)IDC_CHK_VARIABLE_FONTS, hInst, nullptr);
    SendMessageW(m_checkVariableFonts, BM_SETCHECK, m_settings.enableVariableFonts ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_checkVariableFonts, WM_SETFONT, (WPARAM)m_bodyFont, TRUE);
    y += 34;

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
    m_settings.userAgentPersona = (selUa == 1) ? L"Chrome 131 (Windows 11)" :
                                  (selUa == 2) ? L"Safari 18 (macOS)" : L"Firefox 132 (Desktop) [Recommended]";

    // Read experiments
    m_settings.enableWebGPU = (SendMessageW(m_chkWebGPU, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_settings.enableWebGL2 = (SendMessageW(m_chkWebGL2, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_settings.enableIndexedDB = (SendMessageW(m_chkIndexedDB, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_settings.enableWebRTC = (SendMessageW(m_checkWebRTC, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_settings.enableNotifications = (SendMessageW(m_checkNotifications, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_settings.enableGeolocation = (SendMessageW(m_checkGeolocation, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_settings.enableVariableFonts = (SendMessageW(m_checkVariableFonts, BM_GETCHECK, 0, 0) == BST_CHECKED);

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
