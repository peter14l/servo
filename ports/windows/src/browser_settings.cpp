/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "browser_settings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "advapi32.lib")

namespace {

std::wstring GetSettingsIniPath() {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring dir = std::wstring(localAppData) + L"\\Servo Browser";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\settings.ini";
    }
    return L"settings.ini";
}

bool ReadRegistryDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD* outVal) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD data = 0;
        DWORD size = sizeof(DWORD);
        LONG res = RegQueryValueExW(hKey, valueName, nullptr, &type,
            reinterpret_cast<LPBYTE>(&data), &size);
        RegCloseKey(hKey);
        if (res == ERROR_SUCCESS && (type == REG_DWORD || type == REG_BINARY)) {
            *outVal = data;
            return true;
        }
    }
    return false;
}

bool WriteRegistryDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD val) {
    HKEY hKey = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_WRITE, nullptr,
                        &hKey, &disposition) == ERROR_SUCCESS) {
        LONG res = RegSetValueExW(hKey, valueName, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&val), sizeof(DWORD));
        RegCloseKey(hKey);
        return res == ERROR_SUCCESS;
    }
    return false;
}

} // namespace

BrowserSettings LoadBrowserSettings() {
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

    GetPrivateProfileStringW(L"Privacy", L"UserAgentPersona",
        L"Firefox 132 (Desktop) [Recommended]", buf, 512, ini.c_str());
    s.userAgentPersona = buf;

    s.themeMode = GetPrivateProfileIntW(L"Appearance", L"ThemeMode", 2, ini.c_str());
    s.backdropType = GetPrivateProfileIntW(L"Appearance", L"BackdropType", 0, ini.c_str());
    s.enableVerticalTabs = GetPrivateProfileIntW(L"Appearance", L"EnableVerticalTabs", 0, ini.c_str()) != 0;
    s.enableSuggestions = GetPrivateProfileIntW(L"Appearance", L"EnableSuggestions", 1, ini.c_str()) != 0;

    s.enableWebGPU = GetPrivateProfileIntW(L"Experiments", L"EnableWebGPU", 0, ini.c_str()) != 0;
    s.enableWebGL2 = GetPrivateProfileIntW(L"Experiments", L"EnableWebGL2", 1, ini.c_str()) != 0;
    s.enableIndexedDB = GetPrivateProfileIntW(L"Experiments", L"EnableIndexedDB", 1, ini.c_str()) != 0;

    DWORD value = 1;
    if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableWebRTC", &value)) {
        value = GetPrivateProfileIntW(L"Experiments", L"EnableWebRTC", 1, ini.c_str());
    }
    s.enableWebRTC = value != 0;

    if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableNotifications", &value)) {
        value = GetPrivateProfileIntW(L"Experiments", L"EnableNotifications", 1, ini.c_str());
    }
    s.enableNotifications = value != 0;

    if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableGeolocation", &value)) {
        value = GetPrivateProfileIntW(L"Experiments", L"EnableGeolocation", 0, ini.c_str());
    }
    s.enableGeolocation = value != 0;

    if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableVariableFonts", &value)) {
        value = GetPrivateProfileIntW(L"Experiments", L"EnableVariableFonts", 1, ini.c_str());
    }
    s.enableVariableFonts = value != 0;

    return s;
}

void SaveBrowserSettings(const BrowserSettings& s) {
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

    WriteRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableWebRTC", s.enableWebRTC ? 1 : 0);
    WriteRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableNotifications", s.enableNotifications ? 1 : 0);
    WriteRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableGeolocation", s.enableGeolocation ? 1 : 0);
    WriteRegistryDword(HKEY_CURRENT_USER, L"Software\\Servo Browser", L"EnableVariableFonts", s.enableVariableFonts ? 1 : 0);

    WritePrivateProfileStringW(L"Experiments", L"EnableWebRTC", s.enableWebRTC ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableNotifications", s.enableNotifications ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableGeolocation", s.enableGeolocation ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Experiments", L"EnableVariableFonts", s.enableVariableFonts ? L"1" : L"0", ini.c_str());
}
