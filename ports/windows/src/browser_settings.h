/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>

struct BrowserSettings {
    std::wstring searchEngine = L"DuckDuckGo";
    std::wstring homePage = L"https://servo.org";
    std::wstring downloadDir;
    std::wstring userAgentPersona = L"Firefox 132 (Desktop) [Recommended]";
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

// Persisted in %LOCALAPPDATA%\Servo Browser. Shared by every chrome so a
// setting changed in one shell is honoured by the other.
BrowserSettings LoadBrowserSettings();
void SaveBrowserSettings(const BrowserSettings& settings);
