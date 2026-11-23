/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// browser_shell.h owns the shared BrowserClearDataMask bits so every shell
// agrees on what ClearBrowsingData(mask) means.
#include "browser_shell.h"

struct ClearBrowsingDataResult;

struct BrowserSettings {
    std::wstring searchEngine = L"DuckDuckGo";
    std::wstring homePage = L"https://servo.org";
    std::wstring downloadDir;
    std::wstring userAgentPersona = L"Firefox 132 (Desktop) [Recommended]";
    int themeMode = 2; // 0: Light, 1: Dark, 2: System Default
    int backdropType = 0; // 0: Mica Alt, 1: Mica, 2: Solid
    bool enableVerticalTabs = false;
    int verticalTabsWidth = 180;
    bool enableSuggestions = true;
    bool enableWebGPU = false;
    bool enableWebGL2 = true;
    bool enableIndexedDB = true;
    bool enableWebRTC = true;
    bool enableNotifications = true;
    bool enableGeolocation = false;
    bool enableVariableFonts = true;

    // P1/P2 settings shared by every chrome. customSearchEngine is a URL
    // template ("%s" is replaced with the encoded query); an empty value means
    // "use searchEngine". accentColor is a named chrome accent, not an engine
    // preference. textScale is a percentage in [50, 200].
    std::wstring customSearchEngine;
    std::wstring accentColor = L"Default";
    int textScale = 100;
    bool reduceMotion = false;
    bool privateMode = false;
    int clearDataMask = BrowserClearHistory | BrowserClearCookies | BrowserClearCache;

    // Clears history and stored site permissions locally, and delegates
    // engine-backed categories to the registered BrowserRuntime hook.
    static ClearBrowsingDataResult ClearBrowsingData(int mask);
};

// One persisted per-origin decision. Permission ids follow the engine ids used
// by the permission delegate: 0 geolocation, 1 notifications, 2 media
// (camera/microphone), 3 clipboard. Unknown ids are preserved but not labelled.
struct SitePermission {
    std::string origin;
    int32_t permission = 0;
    bool allow = false;
};

// Persisted in %LOCALAPPDATA%\Servo Browser. Shared by every chrome so a
// setting changed in one shell is honoured by the other.
BrowserSettings LoadBrowserSettings();
void SaveBrowserSettings(const BrowserSettings& settings);

// Site permissions live in their own file so a settings reset does not erase
// per-origin decisions, and per-origin changes do not rewrite the whole INI.
std::vector<SitePermission> LoadSitePermissions();
void SaveSitePermissions(const std::vector<SitePermission>& permissions);
// Sanitizes the origin first. Invalid/empty origins are ignored (fail closed).
void SetSitePermission(const std::string& origin, int32_t permission, bool allow);

// Honest result of a shared clear-data request. `clearedMask` only contains
// categories that were actually cleared; `details` names anything the current
// build could not clear instead of claiming success.
struct ClearBrowsingDataResult {
    int requestedMask = 0;
    int clearedMask = 0;
    std::string details;
};

using EngineDataClearFn = ClearBrowsingDataResult (*)(int);

// BrowserRuntime registers its verified-C-API implementation at startup.
// Without an attached engine the helper reports engine categories as
// unavailable instead of silently claiming they were cleared.
void SetEngineDataClearHook(EngineDataClearFn hook);
ClearBrowsingDataResult ClearSharedBrowsingData(int mask);
