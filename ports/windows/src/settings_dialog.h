/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

class BrowserWindow;

struct BrowserSettings {
    std::wstring searchEngine = L"DuckDuckGo";
    std::wstring homePage = L"https://servo.org";
    std::wstring downloadDir;
    std::wstring userAgentPersona = L"Chrome 131 (Windows 11)";
    int themeMode = 2; // 0: Light, 1: Dark, 2: System Default
    int backdropType = 0; // 0: Mica Alt, 1: Mica, 2: Solid
    bool enableVerticalTabs = false;
    bool enableSuggestions = true;
    bool enableWebGPU = false;
    bool enableWebGL2 = true;
    bool enableIndexedDB = true;
    bool enableWebRTC = true;
    bool enableNotifications = true;
    bool enableGeolocation = false;
    bool enableVariableFonts = true;
};

class SettingsDialog {
public:
    static void RegisterWindowClass(HINSTANCE hInstance);
    static void ShowModal(HWND parentHwnd, BrowserWindow* browserWindow);
    static std::string EnsureSettingsPageExists();

    static BrowserSettings LoadSettings();
    static void SaveSettings(const BrowserSettings& settings);

private:
    SettingsDialog(HWND parentHwnd, BrowserWindow* browserWindow);
    ~SettingsDialog();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void ApplyAndClose();
    void ClearBrowsingData();

    HWND m_hwnd;
    HWND m_parentHwnd;
    BrowserWindow* m_browserWindow;
    BrowserSettings m_settings;

    HFONT m_titleFont;
    HFONT m_headerFont;
    HFONT m_bodyFont;

    // Controls
    HWND m_comboSearchEngine;
    HWND m_editHomePage;
    HWND m_editDownloadDir;
    HWND m_btnBrowseDir;
    HWND m_comboTheme;
    HWND m_comboBackdrop;
    HWND m_comboUserAgent;
    HWND m_chkSuggestions;
    HWND m_chkWebGPU;
    HWND m_chkWebGL2;
    HWND m_chkIndexedDB;
    HWND m_checkWebRTC;
    HWND m_checkNotifications;
    HWND m_checkGeolocation;
    HWND m_checkVariableFonts;
    HWND m_btnClearData;
    HWND m_btnSave;
    HWND m_btnCancel;
};
