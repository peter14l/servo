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
#include <shellapi.h>

#include <servo/servo_capi.h>
#include "browser_window.h"
#include "tab_strip.h"
#include "omnibox.h"
#include "find_bar.h"
#include "download_manager.h"
#include "settings_dialog.h"
#include "new_tab_page.h"
#include "servo_view_host.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

#define WM_APP_SERVO_WAKE (WM_APP + 101)

#include <cstdio>
#include <cstdarg>
#include <share.h>

// Serializes log writes: the UI watchdog thread reports on the UI thread while
// the UI thread itself may be inside `LogDebug`.
static CRITICAL_SECTION g_log_cs;
static bool g_log_cs_ready = false;

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
        if (g_log_cs_ready) {
            EnterCriticalSection(&g_log_cs);
        }
        vfprintf(f, fmt, args);
        fprintf(f, "\n");
        fflush(f);
        if (g_log_cs_ready) {
            LeaveCriticalSection(&g_log_cs);
        }
        va_end(args);
    }
}

static DWORD g_main_thread_id = 0;
static HWND g_main_hwnd = nullptr;
static BrowserWindow* g_browser_window = nullptr;

// UI responsiveness diagnostics. `g_ui_current_msg` is non-zero while the UI
// thread is inside a message handler; the watchdog thread turns that into
// `[HANG]`/`[SLOW]` lines in servo_browser.log, so a freeze can be attributed
// to a specific window message without attaching a debugger.
static volatile LONG g_ui_current_msg = 0;
static volatile LONG64 g_ui_msg_started = 0;
static volatile LONG g_ui_watchdog_running = 0;

static DWORD WINAPI UiWatchdogProc(LPVOID) {
    int stalled_seconds = 0;
    while (InterlockedCompareExchange(&g_ui_watchdog_running, 1, 1) != 0) {
        Sleep(1000);
        if (InterlockedCompareExchange(&g_ui_watchdog_running, 1, 1) == 0) {
            break;
        }
        HWND hwnd = g_main_hwnd;
        if (!hwnd) {
            continue;
        }
        DWORD_PTR result = 0;
        LRESULT ok = SendMessageTimeoutW(
            hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &result);
        if (ok == 0) {
            ++stalled_seconds;
            if (stalled_seconds == 3) {
                LONG current = InterlockedCompareExchange(&g_ui_current_msg, 0, 0);
                LONG64 started = InterlockedCompareExchange64(&g_ui_msg_started, 0, 0);
                unsigned long long elapsed = started ? GetTickCount64() - started : 0;
                LogDebug(
                    "[HANG] UI thread unresponsive for >=3s (msg=0x%04X, handler running for %llu ms)",
                    (unsigned)current, elapsed);
            }
        } else if (stalled_seconds > 0) {
            LogDebug("[HANG] UI thread recovered after ~%d s", stalled_seconds);
            stalled_seconds = 0;
        }
    }
    return 0;
}

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

    InitializeCriticalSection(&g_log_cs);
    g_log_cs_ready = true;

    // Make sure Servo's Rust warnings reach servo_engine.log even when the user
    // did not set RUST_LOG. The constellation logs script-thread hang alerts at
    // `warn` level, which are the key signal for diagnosing page freezes.
    if (GetEnvironmentVariableW(L"RUST_LOG", nullptr, 0) == 0) {
        _wputenv_s(L"RUST_LOG", L"warn");
    }

    // Redirect stdout/stderr to servo_engine.log next to the executable for engine diagnostics
    wchar_t engLog[MAX_PATH];
    if (GetModuleFileNameW(nullptr, engLog, MAX_PATH) > 0) {
        wchar_t* lastSlash = wcsrchr(engLog, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
        wcscat_s(engLog, L"servo_engine.log");
        FILE* fpErr = nullptr;
        _wfreopen_s(&fpErr, engLog, L"a", stderr);
        FILE* fpOut = nullptr;
        _wfreopen_s(&fpOut, engLog, L"a", stdout);

        if (fpErr) {
            setvbuf(fpErr, nullptr, _IONBF, 0);
        }
        if (fpOut) {
            setvbuf(fpOut, nullptr, _IONBF, 0);
        }

        HANDLE hLogFile = CreateFileW(
            engLog,
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hLogFile != INVALID_HANDLE_VALUE) {
            SetStdHandle(STD_ERROR_HANDLE, hLogFile);
            SetStdHandle(STD_OUTPUT_HANDLE, hLogFile);
        }
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

    // Set startup options and persistent profile directory (%LOCALAPPDATA%\Servo Browser)
    ServoOptions* options = servo_options_create();
    if (options) {
        servo_options_set_multiprocess(options, false);

        wchar_t localAppData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
            std::wstring baseDir = std::wstring(localAppData) + L"\\Servo Browser";
            CreateDirectoryW(baseDir.c_str(), nullptr);

            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, baseDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Len > 1) {
                std::string baseDirA(utf8Len - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, baseDir.c_str(), -1, &baseDirA[0], utf8Len, nullptr, nullptr);
                servo_options_set_config_dir(options, baseDirA.c_str());
            }
        }

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
        servo_preferences_set_i64(prefs, "network_connection_timeout", 30);
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
        servo_preferences_set_bool(prefs, "dom_entries_api_enabled", true);
        servo_preferences_set_bool(prefs, "dom_wakelock_enabled", true);
        servo_preferences_set_bool(prefs, "dom_webrtc_enabled", userSettings.enableWebRTC);
        servo_preferences_set_bool(prefs, "dom_webrtc_transceiver_enabled", userSettings.enableWebRTC);
        servo_preferences_set_bool(prefs, "dom_notification_enabled", userSettings.enableNotifications);
        servo_preferences_set_bool(prefs, "dom_geolocation_enabled", userSettings.enableGeolocation);
        servo_preferences_set_bool(prefs, "dom_gamepad_enabled", true);
        servo_preferences_set_bool(prefs, "layout_container_queries_enabled", true);
        servo_preferences_set_bool(prefs, "layout_columns_enabled", true);
        servo_preferences_set_bool(prefs, "layout_variable_fonts_enabled", userSettings.enableVariableFonts);
        servo_preferences_set_bool(prefs, "layout_writing_mode_enabled", true);
        servo_preferences_set_bool(prefs, "layout_css_ellipse_corners_enabled", true);
        servo_preferences_set_bool(prefs, "media_glvideo_enabled", true);
        servo_preferences_set_bool(prefs, "perf_thread_boost_enabled", true);

        // SpiderMonkey GC configuration and aggressive memory limits to prevent runaway RAM growth
        servo_preferences_set_i64(prefs, "js_mem_max", 1024); // Bound JS heap to 1024MB max
        servo_preferences_set_bool(prefs, "js_mem_gc_incremental_enabled", false); // Full GC only: pre-barriers not implemented
        servo_preferences_set_bool(prefs, "js_mem_gc_per_zone_enabled", false);
        servo_preferences_set_bool(prefs, "js_mem_gc_compacting_enabled", true); // Compact and release heap chunks back to OS
        servo_preferences_set_i64(prefs, "js_mem_gc_empty_chunk_count_min", 1);
        servo_preferences_set_i64(prefs, "js_mem_gc_high_frequency_high_limit_mb", 128);
        servo_preferences_set_i64(prefs, "js_mem_gc_high_frequency_low_limit_mb", 32);
        servo_preferences_set_i64(prefs, "js_mem_gc_malloc_threshold_base_mb", 16);
        servo_preferences_set_i64(prefs, "js_mem_gc_urgent_threshold_mb", 8);
        servo_preferences_set_i64(prefs, "session_history_max_length", 5); // Avoid unbounded document cache retention in memory

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
    std::string defaultUrl = NewTabPage::EnsureNewTabPageExists();

    // Do not use the CRT's __argc/__argv for a wWinMain entry point. Under
    // clang-cl + lld (the CI toolchain) __argv can be NULL even though __argc is
    // set, which made the released browser crash on startup when a URL argument
    // was supplied. CommandLineToArgvW is the documented, toolchain-independent
    // way to parse the command line.
    std::string initialUrlArg;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1 && argv[1] && argv[1][0] != L'\0') {
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                initialUrlArg.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, &initialUrlArg[0], len, nullptr, nullptr);
            }
        }
        LocalFree(argv);
    }

    const char* initial_url = initialUrlArg.empty() ? defaultUrl.c_str() : initialUrlArg.c_str();
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
    InterlockedExchange(&g_ui_watchdog_running, 1);
    HANDLE watchdog = CreateThread(nullptr, 0, UiWatchdogProc, nullptr, 0, nullptr);
    if (watchdog) {
        CloseHandle(watchdog);
    } else {
        LogDebug("Failed to start UI watchdog thread (error %lu)", GetLastError());
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        ULONGLONG start = GetTickCount64();
        InterlockedExchange(&g_ui_current_msg, (LONG)msg.message);
        InterlockedExchange64(&g_ui_msg_started, (LONG64)start);
        if (msg.message == WM_APP_SERVO_WAKE) {
            servo_spin_event_loop(servo);
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        InterlockedExchange(&g_ui_current_msg, 0);
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= 250) {
            LogDebug("[SLOW] msg=0x%04X took %llu ms", msg.message, elapsed);
        }
    }
    InterlockedExchange(&g_ui_watchdog_running, 0);
    LogDebug("Message pump finished with code %d", (int)msg.wParam);

    // 6. Cleanup
    delete browserWindow;
    servo_free(servo);
    CoUninitialize();

    return static_cast<int>(msg.wParam);
}
