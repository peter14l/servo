/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shlobj.h>

#include <servo/servo_capi.h>
#include "browser_window.h"
#include "tab_strip.h"
#include "omnibox.h"
#include "find_bar.h"
#include "download_manager.h"
#include "settings_dialog.h"
#include "servo_view_host.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

#define WM_APP_SERVO_WAKE (WM_APP + 101)

#include <cstdio>
#include <cstdarg>
#include <share.h>

void LogDebug(const char* fmt, ...) {
    static FILE* f = nullptr;
    if (!f) {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *(lastSlash + 1) = L'\0';
            wcscat_s(exePath, L"servo_browser.log");
            f = _wfsopen(exePath, L"a", _SH_DENYNO);
        } else {
            f = _wfsopen(L"servo_browser.log", L"a", _SH_DENYNO);
        }
    }
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        fprintf(f, "\n");
        fflush(f);
        va_end(args);
    }
}

static DWORD g_main_thread_id = 0;
static HWND g_main_hwnd = nullptr;
static BrowserWindow* g_browser_window = nullptr;

static void on_servo_wake_callback(void) {
    if (g_main_hwnd && IsWindow(g_main_hwnd)) {
        PostMessageW(g_main_hwnd, WM_APP_SERVO_WAKE, 0, 0);
    } else if (g_main_thread_id != 0) {
        PostThreadMessageW(g_main_thread_id, WM_APP_SERVO_WAKE, 0, 0);
    }
}

static void on_devtools_server_started(struct Servo* servo, uint16_t port, const char* token, void* user_data) {
    (void)servo;
    (void)user_data;
    if (g_browser_window) {
        g_browser_window->SetDevToolsInfo(port, token ? token : "");
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_main_thread_id = GetCurrentThreadId();

    // Redirect stdout/stderr to servo_engine.log in %LOCALAPPDATA%\Servo Browser for engine diagnostics
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring engLog = std::wstring(localAppData) + L"\\Servo Browser\\servo_engine.log";
        FILE* fpErr = nullptr;
        _wfreopen_s(&fpErr, engLog.c_str(), L"a", stderr);
        FILE* fpOut = nullptr;
        _wfreopen_s(&fpOut, engLog.c_str(), L"a", stdout);
    }

    LogDebug("--- Starting Servo Browser ---");

    // 1. Initialize COM and Common Controls v6
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LogDebug("CoInitializeEx result: 0x%08X", hr);

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // 2. Initialize Servo
    ServoBuilder* builder = servo_builder_create();
    if (!builder) {
        LogDebug("Failed to initialize ServoBuilder.");
        MessageBoxW(nullptr, L"Failed to initialize ServoBuilder.", L"Servo Browser Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    LogDebug("ServoBuilder created.");

    // Set event loop waker
    ServoEventLoopWaker waker = {};
    waker.struct_size = sizeof(ServoEventLoopWaker);
    waker.wake_callback = on_servo_wake_callback;
    ServoError wakerErr = servo_builder_set_event_loop_waker(builder, waker);
    LogDebug("servo_builder_set_event_loop_waker returned: %d", (int)wakerErr);

    // Set startup options
    ServoOptions* options = servo_options_create();
    if (options) {
        servo_options_set_multiprocess(options, false);
        servo_builder_set_options(builder, options);
    }

    // 3. Load user settings
    BrowserSettings userSettings = SettingsDialog::LoadSettings();

    // Map User-Agent persona: Default to standard desktop Firefox to match SpiderMonkey engine
    // and prevent anti-bot heuristics (like Instagram/Meta rejecting non-Chromium engines pretending to be Chrome)
    std::string uaString;
    if (userSettings.userAgentPersona.find(L"Chrome") != std::wstring::npos) {
        uaString = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
    } else if (userSettings.userAgentPersona.find(L"Safari") != std::wstring::npos) {
        uaString = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15";
    } else {
        uaString = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:132.0) Gecko/20100101 Firefox/132.0";
    }

    // Set preferences: Configure persona and enable modern web platform capabilities
    ServoPreferences* prefs = servo_preferences_create();
    if (prefs) {
        servo_preferences_set_bool(prefs, "devtools_server_enabled", false);
        servo_preferences_set_string(prefs, "user_agent", uaString.c_str());

        // Modern web platform features required by complex web applications (Instagram, YouTube, Google, GitHub, ChatGPT)
        servo_preferences_set_bool(prefs, "network_use_webpki_roots", true);
        servo_preferences_set_bool(prefs, "dom_indexeddb_enabled", userSettings.enableIndexedDB);
        servo_preferences_set_bool(prefs, "dom_storage_manager_api_enabled", true);
        servo_preferences_set_bool(prefs, "dom_cookiestore_enabled", true);
        servo_preferences_set_bool(prefs, "dom_crypto_subtle_enabled", true);
        servo_preferences_set_bool(prefs, "dom_fontface_enabled", true);
        servo_preferences_set_bool(prefs, "dom_async_clipboard_enabled", true);
        servo_preferences_set_bool(prefs, "dom_offscreen_canvas_enabled", true);
        servo_preferences_set_bool(prefs, "dom_permissions_enabled", true);
        servo_preferences_set_bool(prefs, "dom_webgl2_enabled", userSettings.enableWebGL2);
        servo_preferences_set_bool(prefs, "dom_webgpu_enabled", userSettings.enableWebGPU);
        servo_preferences_set_bool(prefs, "dom_visual_viewport_enabled", true);
        servo_preferences_set_bool(prefs, "dom_adoptedstylesheet_enabled", true);
        servo_preferences_set_bool(prefs, "dom_composition_event_enabled", true);
        servo_preferences_set_bool(prefs, "dom_credential_management_enabled", true);
        servo_preferences_set_bool(prefs, "dom_web_animations_enabled", true);
        servo_preferences_set_bool(prefs, "layout_container_queries_enabled", true);
        servo_preferences_set_bool(prefs, "layout_columns_enabled", true);

        servo_builder_set_preferences(builder, prefs);
    }

    // Configure Persistent Request Context (Cookies & HTTP Cache storage in %LOCALAPPDATA%\Servo Browser)
    ServoRequestContext* reqCtx = servo_request_context_create();
    if (reqCtx) {
        wchar_t localAppData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
            std::wstring baseDir = std::wstring(localAppData) + L"\\Servo Browser";
            CreateDirectoryW(baseDir.c_str(), nullptr);
            std::wstring cookieDir = baseDir + L"\\Cookies";
            CreateDirectoryW(cookieDir.c_str(), nullptr);
            std::wstring cacheDir = baseDir + L"\\Cache";
            CreateDirectoryW(cacheDir.c_str(), nullptr);

            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, cookieDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Len > 1) {
                std::string cookieDirA(utf8Len - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, cookieDir.c_str(), -1, &cookieDirA[0], utf8Len, nullptr, nullptr);
                servo_request_context_set_cookie_path(reqCtx, cookieDirA.c_str());
            }

            utf8Len = WideCharToMultiByte(CP_UTF8, 0, cacheDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Len > 1) {
                std::string cacheDirA(utf8Len - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, cacheDir.c_str(), -1, &cacheDirA[0], utf8Len, nullptr, nullptr);
                servo_request_context_set_cache_path(reqCtx, cacheDirA.c_str());
            }
        }
        servo_request_context_set_accept_language(reqCtx, "en-US,en;q=0.9");
        servo_builder_set_request_context(builder, reqCtx);
    }

    // Set delegate for DevTools notifications
    ServoDelegate delegate = {};
    delegate.struct_size = sizeof(ServoDelegate);
    delegate.notify_devtools_server_started = on_devtools_server_started;
    servo_builder_set_delegate(builder, delegate);

    struct Servo* servo = servo_builder_build(builder);
    if (!servo) {
        LogDebug("Failed to build Servo instance.");
        MessageBoxW(nullptr, L"Failed to build Servo instance.", L"Servo Browser Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    LogDebug("Servo instance built.");

    servo_setup_logging(servo);

    // 3. Register Window Classes
    BrowserWindow::RegisterWindowClass(hInstance);
    TabStrip::RegisterWindowClass(hInstance);
    OmniboxToolbar::RegisterWindowClass(hInstance);
    FindBar::RegisterWindowClass(hInstance);
    DownloadManager::RegisterWindowClass(hInstance);
    ServoViewHost::RegisterWindowClass(hInstance);
    LogDebug("Window classes registered.");

    // 4. Create and Show Browser Window
    const char* initial_url = "https://servo.org";
    if (__argc > 1 && __argv[1] && __argv[1][0] != '\0') {
        initial_url = __argv[1];
    }
    BrowserWindow* browserWindow = BrowserWindow::Create(servo, initial_url);
    if (!browserWindow) {
        LogDebug("BrowserWindow::Create failed!");
        servo_free(servo);
        CoUninitialize();
        return 1;
    }

    g_browser_window = browserWindow;
    g_main_hwnd = browserWindow->GetHwnd();
    LogDebug("Browser window created successfully. HWND: 0x%p", g_main_hwnd);

    // Kickstart the Servo event loop so initial navigation starts immediately
    PostMessageW(g_main_hwnd, WM_APP_SERVO_WAKE, 0, 0);

    // 5. External Message Pump
    LogDebug("Entering message pump...");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_APP_SERVO_WAKE) {
            servo_spin_event_loop(servo);
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    LogDebug("Message pump finished with code %d", (int)msg.wParam);

    // 6. Cleanup
    delete browserWindow;
    servo_free(servo);
    CoUninitialize();

    return static_cast<int>(msg.wParam);
}
