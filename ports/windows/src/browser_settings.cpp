/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "browser_settings.h"
#include "browser_data.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "advapi32.lib")

namespace {

const int kKnownClearMask = BrowserClearHistory | BrowserClearCookies |
                           BrowserClearCache | BrowserClearSiteData |
                           BrowserClearPasswords;

EngineDataClearFn g_engineDataClearHook = nullptr;

std::wstring GetBrowserDataDirectory() {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring dir = std::wstring(localAppData) + L"\\Servo Browser";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L".";
}

std::wstring GetSettingsIniPath() {
    return GetBrowserDataDirectory() + L"\\settings.ini";
}

std::wstring GetSitePermissionsPath() {
    return GetBrowserDataDirectory() + L"\\site_permissions.txt";
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

std::string TrimAscii(const std::string& value) {
    const char* whitespace = " \t\r\n";
    size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) return {};
    size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

std::string ToLowerAscii(std::string value) {
    for (char& c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'A' && uc <= 'Z') c = static_cast<char>(uc + ('a' - 'A'));
    }
    return value;
}

bool IsAllowedHostChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') ||
           (uc >= '0' && uc <= '9') || c == '.' || c == '-' || c == '_' ||
           c == '[' || c == ']' || c == ':';
}

// Returns a canonical http(s) origin, or an empty string for malformed input.
// Paths, query strings, fragments and user-info are discarded so a decision is
// never keyed by something a site can vary without changing its origin.
std::string SanitizeOrigin(const std::string& origin) {
    std::string value = TrimAscii(origin);
    if (value.empty()) return {};
    for (char c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc <= ' ' || uc == 127) return {};
    }

    std::string scheme = "https";
    size_t schemeEnd = value.find("://");
    if (schemeEnd != std::string::npos) {
        scheme = ToLowerAscii(value.substr(0, schemeEnd));
        if (scheme != "http" && scheme != "https") return {};
        value = value.substr(schemeEnd + 3);
    }

    size_t end = value.find_first_of("/?#\\");
    if (end != std::string::npos) value.resize(end);
    size_t at = value.find('@');
    if (at != std::string::npos) value = value.substr(at + 1);
    if (value.empty()) return {};

    std::string host;
    std::string portText;
    if (value.front() == '[') {
        size_t close = value.find(']');
        if (close == std::string::npos) return {};
        host = value.substr(0, close + 1);
        std::string rest = value.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return {};
            portText = rest.substr(1);
        }
    } else {
        size_t colon = value.find(':');
        if (colon != std::string::npos) {
            host = value.substr(0, colon);
            portText = value.substr(colon + 1);
            if (host.find(':') != std::string::npos) return {};
        } else {
            host = value;
        }
    }

    if (host.empty() || host.front() == '.' || host.back() == '.') return {};
    for (char c : host) {
        if (!IsAllowedHostChar(c)) return {};
    }

    std::string port;
    if (!portText.empty()) {
        for (char c : portText) {
            if (c < '0' || c > '9') return {};
        }
        size_t used = 0;
        long long portNumber = 0;
        try {
            portNumber = std::stoll(portText, &used);
        } catch (...) {
            return {};
        }
        if (used != portText.size() || portNumber < 1 || portNumber > 65535) return {};
        bool isDefault = (scheme == "http" && portNumber == 80) ||
                         (scheme == "https" && portNumber == 443);
        if (!isDefault) port = ":" + std::to_string(portNumber);
    }

    return scheme + "://" + ToLowerAscii(host) + port;
}

bool TryParseInt(const std::string& text, int& out) {
    try {
        size_t used = 0;
        long long value = std::stoll(text, &used);
        if (used != text.size()) return false;
        if (value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max()) {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    } catch (...) {
        return false;
    }
}

void AppendDetail(std::string& details, const std::string& text) {
    if (text.empty()) return;
    if (!details.empty()) details += "; ";
    details += text;
}

std::string UnavailableEngineCategories(int mask) {
    std::vector<std::string> names;
    if (mask & BrowserClearCookies) names.push_back("cookies");
    if (mask & BrowserClearCache) names.push_back("cache");
    if (mask & BrowserClearSiteData) names.push_back("site data");
    if (mask & BrowserClearPasswords) names.push_back("passwords");
    if (names.empty()) return {};

    std::ostringstream out;
    out << "engine-backed categories unavailable (no engine attached): ";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out << ", ";
        out << names[i];
    }
    return out.str();
}

} // namespace

BrowserSettings LoadBrowserSettings() {
    BrowserSettings s;
    std::wstring ini = GetSettingsIniPath();

    wchar_t buf[512];
    GetPrivateProfileStringW(L"General", L"SearchEngine", L"DuckDuckGo", buf, 512, ini.c_str());
    s.searchEngine = buf;

    GetPrivateProfileStringW(L"General", L"CustomSearchEngine", L"", buf, 512, ini.c_str());
    s.customSearchEngine = buf;

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
    s.verticalTabsWidth = GetPrivateProfileIntW(L"Appearance", L"VerticalTabsWidth", 180, ini.c_str());
    if (s.verticalTabsWidth < 100) s.verticalTabsWidth = 100;
    if (s.verticalTabsWidth > 420) s.verticalTabsWidth = 420;
    s.enableSuggestions = GetPrivateProfileIntW(L"Appearance", L"EnableSuggestions", 1, ini.c_str()) != 0;

    GetPrivateProfileStringW(L"Appearance", L"AccentColor", L"Default", buf, 512, ini.c_str());
    s.accentColor = buf;
    s.textScale = GetPrivateProfileIntW(L"Appearance", L"TextScale", 100, ini.c_str());
    s.textScale = std::clamp(s.textScale, 50, 200);
    s.reduceMotion = GetPrivateProfileIntW(L"Appearance", L"ReduceMotion", 0, ini.c_str()) != 0;

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

    s.privateMode = GetPrivateProfileIntW(L"Privacy", L"PrivateMode", 0, ini.c_str()) != 0;
    s.clearDataMask = GetPrivateProfileIntW(L"Privacy", L"ClearDataMask",
        BrowserClearHistory | BrowserClearCookies | BrowserClearCache, ini.c_str());
    s.clearDataMask &= kKnownClearMask;

    return s;
}

void SaveBrowserSettings(const BrowserSettings& s) {
    std::wstring ini = GetSettingsIniPath();

    WritePrivateProfileStringW(L"General", L"SearchEngine", s.searchEngine.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"General", L"CustomSearchEngine", s.customSearchEngine.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"General", L"HomePage", s.homePage.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"General", L"DownloadDir", s.downloadDir.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Privacy", L"UserAgentPersona", s.userAgentPersona.c_str(), ini.c_str());

    WritePrivateProfileStringW(L"Appearance", L"ThemeMode", std::to_wstring(s.themeMode).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"BackdropType", std::to_wstring(s.backdropType).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"EnableVerticalTabs", s.enableVerticalTabs ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"VerticalTabsWidth",
        std::to_wstring(std::clamp(s.verticalTabsWidth, 100, 420)).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"EnableSuggestions", s.enableSuggestions ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"AccentColor", s.accentColor.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"TextScale",
        std::to_wstring(std::clamp(s.textScale, 50, 200)).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Appearance", L"ReduceMotion", s.reduceMotion ? L"1" : L"0", ini.c_str());

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

    WritePrivateProfileStringW(L"Privacy", L"PrivateMode", s.privateMode ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Privacy", L"ClearDataMask",
        std::to_wstring(s.clearDataMask & kKnownClearMask).c_str(), ini.c_str());
}

std::vector<SitePermission> LoadSitePermissions() {
    std::vector<SitePermission> permissions;
    std::wstring path = GetSitePermissionsPath();
    std::ifstream file(path);
    if (!file.is_open()) return permissions;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        size_t first = line.find('\t');
        size_t second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;

        std::string storedOrigin = line.substr(0, first);
        std::string origin = SanitizeOrigin(storedOrigin);
        if (origin.empty() || origin != storedOrigin) continue;

        int permission = 0;
        int allowValue = 0;
        if (!TryParseInt(line.substr(first + 1, second - first - 1), permission) ||
            !TryParseInt(line.substr(second + 1), allowValue)) {
            continue;
        }

        SitePermission entry{ origin, permission, allowValue != 0 };
        auto existing = std::find_if(permissions.begin(), permissions.end(), [&](const SitePermission& item) {
            return item.origin == entry.origin && item.permission == entry.permission;
        });
        if (existing != permissions.end()) *existing = entry;
        else permissions.push_back(entry);
    }
    return permissions;
}

void SaveSitePermissions(const std::vector<SitePermission>& permissions) {
    std::wstring path = GetSitePermissionsPath();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;

    for (const SitePermission& input : permissions) {
        std::string origin = SanitizeOrigin(input.origin);
        if (origin.empty()) continue;
        file << origin << '\t' << input.permission << '\t' << (input.allow ? 1 : 0) << '\n';
    }
}

void SetSitePermission(const std::string& origin, int32_t permission, bool allow) {
    std::string sanitized = SanitizeOrigin(origin);
    if (sanitized.empty()) return;

    auto permissions = LoadSitePermissions();
    auto existing = std::find_if(permissions.begin(), permissions.end(), [&](const SitePermission& item) {
        return item.origin == sanitized && item.permission == permission;
    });
    if (existing != permissions.end()) {
        existing->allow = allow;
    } else {
        permissions.push_back({ sanitized, permission, allow });
    }
    SaveSitePermissions(permissions);
}

void SetEngineDataClearHook(EngineDataClearFn hook) {
    g_engineDataClearHook = hook;
}

ClearBrowsingDataResult ClearSharedBrowsingData(int mask) {
    ClearBrowsingDataResult result;
    result.requestedMask = mask;

    EngineDataClearFn hook = g_engineDataClearHook;
    if (!hook) {
        AppendDetail(result.details, UnavailableEngineCategories(mask));
        return result;
    }

    result = hook(mask);
    result.requestedMask = mask;
    result.clearedMask &= (mask & kKnownClearMask);
    return result;
}

ClearBrowsingDataResult BrowserSettings::ClearBrowsingData(int mask) {
    ClearBrowsingDataResult result;
    result.requestedMask = mask;

    if (mask & BrowserClearHistory) {
        BrowserData::ClearHistory();
        if (BrowserData::LoadHistory().empty()) {
            result.clearedMask |= BrowserClearHistory;
        } else {
            AppendDetail(result.details, "local history file could not be cleared");
        }
    }

    // Site permissions are chrome state, not engine site storage. Reset them
    // when site data is requested, but only report the site-data category as
    // cleared when the engine hook confirms that engine storage was cleared.
    if (mask & BrowserClearSiteData) {
        SaveSitePermissions({});
    }

    ClearBrowsingDataResult engine = ClearSharedBrowsingData(mask);
    result.clearedMask |= engine.clearedMask;
    AppendDetail(result.details, engine.details);

    if (mask & ~kKnownClearMask) {
        AppendDetail(result.details, "unknown clear-data categories were ignored");
    }

    return result;
}
