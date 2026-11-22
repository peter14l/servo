/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "browser_runtime.h"
#include "browser_settings.h"
#include "schannel_gateway.h"

#include <cstdio>
#include <cstdarg>
#include <memory>
#include <mutex>
#include <share.h>
#include <shlobj.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace {

CRITICAL_SECTION g_log_cs;
bool g_log_cs_ready = false;

volatile LONG g_wakePosted = 0;
volatile LONG64 g_wakeCallbacks = 0;
volatile LONG64 g_wakePosts = 0;
volatile LONG64 g_wakeCoalesced = 0;
volatile LONG64 g_spinCount = 0;
volatile LONG64 g_spinMsTotal = 0;
volatile LONG64 g_spinMsMax = 0;
volatile LONG64 g_paintCount = 0;
volatile LONG64 g_paintMsTotal = 0;
volatile LONG64 g_paintMsMax = 0;
volatile LONG64 g_controlCount = 0;
volatile LONG64 g_controlMsTotal = 0;
volatile LONG64 g_controlMsMax = 0;
volatile LONG64 g_droppedFrames = 0;

volatile LONG g_uiCurrentMsg = 0;
volatile LONG64 g_uiMsgStarted = 0;
volatile LONG g_uiWatchdogRunning = 0;

void RecordMax(volatile LONG64& counter, LONG64 value) {
    LONG64 current = InterlockedCompareExchange64(&counter, 0, 0);
    while (value > current) {
        LONG64 previous = InterlockedCompareExchange64(&counter, value, current);
        if (previous == current) break;
        current = previous;
    }
}

void LogPerfSummary(const char* when) {
    LogDebug(
        "[PERF] %s wake(cb=%lld post=%lld coalesced=%lld) spin(count=%lld ms=%lld max=%lld) "
        "paint(count=%lld ms=%lld max=%lld) control(count=%lld ms=%lld max=%lld) drop=%lld",
        when,
        (long long)InterlockedCompareExchange64(&g_wakeCallbacks, 0, 0),
        (long long)InterlockedCompareExchange64(&g_wakePosts, 0, 0),
        (long long)InterlockedCompareExchange64(&g_wakeCoalesced, 0, 0),
        (long long)InterlockedCompareExchange64(&g_spinCount, 0, 0),
        (long long)InterlockedCompareExchange64(&g_spinMsTotal, 0, 0),
        (long long)InterlockedCompareExchange64(&g_spinMsMax, 0, 0),
        (long long)InterlockedCompareExchange64(&g_paintCount, 0, 0),
        (long long)InterlockedCompareExchange64(&g_paintMsTotal, 0, 0),
        (long long)InterlockedCompareExchange64(&g_paintMsMax, 0, 0),
        (long long)InterlockedCompareExchange64(&g_controlCount, 0, 0),
        (long long)InterlockedCompareExchange64(&g_controlMsTotal, 0, 0),
        (long long)InterlockedCompareExchange64(&g_controlMsMax, 0, 0),
        (long long)InterlockedCompareExchange64(&g_droppedFrames, 0, 0));
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    wchar_t logPath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, logPath, MAX_PATH) > 0) {
        wchar_t* lastSlash = wcsrchr(logPath, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
        wcscat_s(logPath, L"servo_crash.log");
        FILE* f = _wfsopen(logPath, L"a", _SH_DENYNO);
        if (f) {
            DWORD code = ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
            PVOID addr = ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
            fprintf(f, "=== UNHANDLED CRASH ===\nCode: 0x%08lX\nAddress: %p\n", (unsigned long)code, addr);
            if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
                fprintf(f, "Access type: %s, Target address: %p\n",
                    ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" : "WRITE",
                    (void*)ep->ExceptionRecord->ExceptionInformation[1]);
            }
            MEMORY_BASIC_INFORMATION mbi = {};
            if (VirtualQuery(addr, &mbi, sizeof(mbi))) {
                wchar_t modPath[MAX_PATH] = {};
                GetModuleFileNameW((HMODULE)mbi.AllocationBase, modPath, MAX_PATH);
                wchar_t* modName = wcsrchr(modPath, L'\\');
                fprintf(f, "Crash module: %ls + 0x%llX (base: %p)\n",
                    modName ? modName + 1 : modPath,
                    (unsigned long long)((uintptr_t)addr - (uintptr_t)mbi.AllocationBase),
                    mbi.AllocationBase);
            }

            HANDLE hProc = GetCurrentProcess();
            wchar_t searchPath[MAX_PATH * 2] = {};
            GetModuleFileNameW(nullptr, searchPath, MAX_PATH);
            wchar_t* sp = wcsrchr(searchPath, L'\\');
            if (sp) *sp = L'\0';
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitializeW(hProc, searchPath, TRUE);

            auto symbolicate = [&](PVOID a) {
                char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)] = {};
                PSYMBOL_INFO sym = (PSYMBOL_INFO)buf;
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = MAX_SYM_NAME;
                DWORD64 disp = 0;
                if (SymFromAddr(hProc, (DWORD64)a, &disp, sym)) {
                    fprintf(f, "    -> %s + 0x%llX", sym->Name, (unsigned long long)disp);
                    IMAGEHLP_LINE64 line = {};
                    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                    DWORD ldisp = 0;
                    if (SymGetLineFromAddr64(hProc, (DWORD64)a, &ldisp, &line) && line.FileName) {
                        fprintf(f, "  (%s:%lu)", line.FileName, (unsigned long)line.LineNumber);
                    }
                    fprintf(f, "\n");
                } else {
                    fprintf(f, "    -> (no symbol, err=%lu)\n", GetLastError());
                }
            };

            fprintf(f, "Crash address symbol:\n");
            symbolicate(addr);

            PVOID frames[32] = {};
            USHORT frameCount = CaptureStackBackTrace(0, 32, frames, nullptr);
            fprintf(f, "Stack backtrace (%u frames):\n", (unsigned)frameCount);
            for (USHORT i = 0; i < frameCount; ++i) {
                MEMORY_BASIC_INFORMATION fmbi = {};
                if (VirtualQuery(frames[i], &fmbi, sizeof(fmbi))) {
                    wchar_t fmodPath[MAX_PATH] = {};
                    GetModuleFileNameW((HMODULE)fmbi.AllocationBase, fmodPath, MAX_PATH);
                    wchar_t* fmodName = wcsrchr(fmodPath, L'\\');
                    fprintf(f, "  [%u] %ls + 0x%llX (%p)\n",
                        (unsigned)i,
                        fmodName ? fmodName + 1 : fmodPath,
                        (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)fmbi.AllocationBase),
                        frames[i]);
                } else {
                    fprintf(f, "  [%u] %p\n", (unsigned)i, frames[i]);
                }
                symbolicate(frames[i]);
            }
            SymCleanup(hProc);
            fflush(f);
            fclose(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void InitLogging() {
    InitializeCriticalSection(&g_log_cs);
    g_log_cs_ready = true;
    SetUnhandledExceptionFilter(CrashHandler);

    if (GetEnvironmentVariableW(L"RUST_LOG", nullptr, 0) == 0) {
        _wputenv_s(L"RUST_LOG", L"warn");
    }

    wchar_t engLog[MAX_PATH];
    if (GetModuleFileNameW(nullptr, engLog, MAX_PATH) > 0) {
        wchar_t* lastSlash = wcsrchr(engLog, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
        wcscat_s(engLog, L"servo_engine.log");
        FILE* fpErr = nullptr;
        _wfreopen_s(&fpErr, engLog, L"a", stderr);
        FILE* fpOut = nullptr;
        _wfreopen_s(&fpOut, engLog, L"a", stdout);
        if (fpErr) setvbuf(fpErr, nullptr, _IONBF, 0);
        if (fpOut) setvbuf(fpOut, nullptr, _IONBF, 0);

        HANDLE hLogFile = CreateFileW(engLog, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hLogFile != INVALID_HANDLE_VALUE) {
            SetStdHandle(STD_ERROR_HANDLE, hLogFile);
            SetStdHandle(STD_OUTPUT_HANDLE, hLogFile);
        }
    }
}

DWORD WINAPI UiWatchdogProc(LPVOID) {
    int stalledSeconds = 0;
    int summaryTicks = 0;
    while (InterlockedCompareExchange(&g_uiWatchdogRunning, 1, 1) != 0) {
        Sleep(1000);
        if (InterlockedCompareExchange(&g_uiWatchdogRunning, 1, 1) == 0) break;
        if (++summaryTicks >= 10) {
            summaryTicks = 0;
            LogPerfSummary("periodic");
        }
        HWND hwnd = BrowserRuntime::Instance().GetMainHwnd();
        if (!hwnd) continue;
        DWORD_PTR result = 0;
        LRESULT ok = SendMessageTimeoutW(hwnd, WM_NULL, 0, 0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &result);
        if (ok == 0) {
            ++stalledSeconds;
            if (stalledSeconds == 3) {
                LONG current = InterlockedCompareExchange(&g_uiCurrentMsg, 0, 0);
                LONG64 started = InterlockedCompareExchange64(&g_uiMsgStarted, 0, 0);
                unsigned long long elapsed = started ? GetTickCount64() - started : 0;
                LogDebug("[HANG] UI thread unresponsive for >=3s (msg=0x%04X, handler running for %llu ms)",
                    (unsigned)current, elapsed);
            }
        } else if (stalledSeconds > 0) {
            LogDebug("[HANG] UI thread recovered after ~%d s", stalledSeconds);
            stalledSeconds = 0;
        }
    }
    return 0;
}

void OnDevToolsServerStarted(Servo* /*servo*/, uint16_t port, const char* token, void* /*userData*/) {
    // Deliberately no LogDebug here: the DevTools token is a credential.
    BrowserRuntime::Instance().NotifyDevTools(port, token ? token : "");
}

} // namespace

void LogDebug(const char* fmt, ...) {
    static FILE* f = nullptr;
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
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
    });
    if (f) {
        va_list args;
        va_start(args, fmt);
        if (g_log_cs_ready) EnterCriticalSection(&g_log_cs);
        vfprintf(f, fmt, args);
        fprintf(f, "\n");
        fflush(f);
        if (g_log_cs_ready) LeaveCriticalSection(&g_log_cs);
        va_end(args);
    }
}

void RecordPaintDuration(unsigned long long milliseconds) {
    InterlockedIncrement64(&g_paintCount);
    InterlockedExchangeAdd64(&g_paintMsTotal, (LONG64)milliseconds);
    RecordMax(g_paintMsMax, (LONG64)milliseconds);
}

void RecordDeferredControlDuration(unsigned long long milliseconds) {
    InterlockedIncrement64(&g_controlCount);
    InterlockedExchangeAdd64(&g_controlMsTotal, (LONG64)milliseconds);
    RecordMax(g_controlMsMax, (LONG64)milliseconds);
}

void RecordDroppedFrame() {
    InterlockedIncrement64(&g_droppedFrames);
}

void ServoSpinEventLoopOnce(struct Servo* servo) {
    static thread_local bool spinning = false;
    static thread_local bool pending = false;
    if (!servo) return;
    if (spinning) {
        pending = true;
        return;
    }
    spinning = true;
    struct SpinGuard {
        bool* flag;
        ~SpinGuard() { *flag = false; }
    } guard{&spinning};
    do {
        pending = false;
        servo_spin_event_loop(servo);
    } while (pending);
}

BrowserRuntime& BrowserRuntime::Instance() {
    static BrowserRuntime runtime;
    return runtime;
}

bool BrowserRuntime::Initialize() {
    m_mainThreadId = GetCurrentThreadId();
    InitLogging();
    LogDebug("--- Starting Servo Browser ---");

    ServoBuilder* builder = servo_builder_create();
    if (!builder) {
        LogDebug("Failed to initialize ServoBuilder.");
        return false;
    }

    ServoEventLoopWaker waker = {};
    waker.struct_size = sizeof(ServoEventLoopWaker);
    waker.wake_callback = [](void) { BrowserRuntime::Instance().OnServoWake(); };
    ServoError wakerErr = servo_builder_set_event_loop_waker(builder, waker);
    LogDebug("servo_builder_set_event_loop_waker returned: %d", (int)wakerErr);

    ServoOptions* options = servo_options_create();
    if (options) {
        servo_options_set_multiprocess(options, false);
        servo_options_set_hard_fail(options, false);

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

    BrowserSettings userSettings = LoadBrowserSettings();
    std::string uaString;
    if (userSettings.userAgentPersona.find(L"Chrome") != std::wstring::npos) {
        uaString = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
    } else if (userSettings.userAgentPersona.find(L"Safari") != std::wstring::npos) {
        uaString = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15";
    } else {
        uaString = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:132.0) Gecko/20100101 Firefox/132.0";
    }

    ServoPreferences* prefs = servo_preferences_create();
    if (prefs) {
        servo_preferences_set_bool(prefs, "devtools_server_enabled", false);
        servo_preferences_set_string(prefs, "user_agent", uaString.c_str());
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
        servo_preferences_set_i64(prefs, "js_mem_max", 1024);
        servo_preferences_set_bool(prefs, "js_mem_gc_incremental_enabled", false);
        servo_preferences_set_bool(prefs, "js_mem_gc_per_zone_enabled", false);
        servo_preferences_set_bool(prefs, "js_mem_gc_compacting_enabled", false);
        servo_preferences_set_i64(prefs, "js_mem_gc_high_frequency_high_limit_mb", 512);
        servo_preferences_set_i64(prefs, "js_mem_gc_high_frequency_low_limit_mb", 128);
        servo_preferences_set_i64(prefs, "js_mem_gc_malloc_threshold_base_mb", 64);
        servo_preferences_set_i64(prefs, "js_mem_gc_urgent_threshold_mb", 32);
        servo_preferences_set_i64(prefs, "session_history_max_length", 5);
        servo_builder_set_preferences(builder, prefs);
    }

    ServoRequestContext* reqCtx = servo_request_context_create();
    if (reqCtx) {
        wchar_t localAppData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
            std::wstring baseDir = std::wstring(localAppData) + L"\\Servo Browser";
            CreateDirectoryW(baseDir.c_str(), nullptr);
            std::wstring cookieDir = baseDir + L"\\Cookies";
            std::wstring cacheDir = baseDir + L"\\Cache";
            CreateDirectoryW(cookieDir.c_str(), nullptr);
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

    ServoDelegate delegate = {};
    delegate.struct_size = sizeof(ServoDelegate);
    delegate.notify_devtools_server_started = OnDevToolsServerStarted;
    servo_builder_set_delegate(builder, delegate);

    m_servo = servo_builder_build(builder);
    if (!m_servo) {
        LogDebug("Failed to build Servo instance.");
        return false;
    }
    LogDebug("Servo instance built.");
    servo_setup_logging(m_servo);
    SchannelGateway::Start();
    return true;
}

void BrowserRuntime::SetMainHwnd(HWND hwnd) {
    m_mainHwnd = hwnd;
}

void BrowserRuntime::SetDevToolsCallback(DevToolsCallback callback) {
    m_devToolsCallback = std::move(callback);
}

void BrowserRuntime::NotifyDevTools(uint16_t port, const std::string& token) {
    if (m_devToolsCallback) {
        m_devToolsCallback(port, token);
    }
}

void BrowserRuntime::OnServoWake() {
    InterlockedIncrement64(&g_wakeCallbacks);
    HWND hwnd = m_mainHwnd;
    if (hwnd && IsWindow(hwnd)) {
        if (InterlockedCompareExchange(&g_wakePosted, 1, 0) == 0) {
            if (PostMessageW(hwnd, WM_APP_SERVO_WAKE, 0, 0)) {
                InterlockedIncrement64(&g_wakePosts);
            } else {
                InterlockedExchange(&g_wakePosted, 0);
            }
        } else {
            InterlockedIncrement64(&g_wakeCoalesced);
        }
    } else if (m_mainThreadId != 0) {
        PostThreadMessageW(m_mainThreadId, WM_APP_SERVO_WAKE, 0, 0);
    }
}

bool PostUiTask(UiTask task) {
    if (!task) return false;
    HWND hwnd = BrowserRuntime::Instance().GetMainHwnd();
    if (!hwnd || !IsWindow(hwnd)) {
        task();
        return true;
    }
    auto* heapTask = new UiTask(std::move(task));
    if (PostMessageW(hwnd, WM_APP_RUN_UI_TASK, 0, reinterpret_cast<LPARAM>(heapTask))) {
        return true;
    }
    delete heapTask;
    LogDebug("[PERF] failed to post UI task (error %lu); native control not shown", GetLastError());
    return false;
}

void BrowserRuntime::PumpWake() {
    InterlockedExchange(&g_wakePosted, 0);
    ULONGLONG spinStart = GetTickCount64();
    InterlockedIncrement64(&g_spinCount);
    ServoSpinEventLoopOnce(m_servo);
    ULONGLONG spinMs = GetTickCount64() - spinStart;
    InterlockedExchangeAdd64(&g_spinMsTotal, (LONG64)spinMs);
    RecordMax(g_spinMsMax, (LONG64)spinMs);
}

void BrowserRuntime::PumpUiTask(LPARAM lParam) {
    std::unique_ptr<UiTask> task(reinterpret_cast<UiTask*>(lParam));
    if (!task || !*task) return;
    ULONGLONG controlStart = GetTickCount64();
    (*task)();
    ULONGLONG controlMs = GetTickCount64() - controlStart;
    RecordDeferredControlDuration(controlMs);
    if (controlMs >= 250) {
        LogDebug("[PERF] deferred native control took %llu ms", controlMs);
    }
}

void BrowserRuntime::PostInitialWake() {
    InterlockedExchange(&g_wakePosted, 1);
    if (PostMessageW(m_mainHwnd, WM_APP_SERVO_WAKE, 0, 0)) {
        InterlockedIncrement64(&g_wakePosts);
    } else {
        InterlockedExchange(&g_wakePosted, 0);
    }
}

void BrowserRuntime::StartWatchdog() {
    InterlockedExchange(&g_uiWatchdogRunning, 1);
    HANDLE watchdog = CreateThread(nullptr, 0, UiWatchdogProc, nullptr, 0, nullptr);
    if (watchdog) {
        CloseHandle(watchdog);
    } else {
        LogDebug("Failed to start UI watchdog thread (error %lu)", GetLastError());
    }
}

void BrowserRuntime::StopWatchdog() {
    InterlockedExchange(&g_uiWatchdogRunning, 0);
    LogPerfSummary("shutdown");
}

void BrowserRuntime::SetCurrentMessage(UINT message) {
    InterlockedExchange(&g_uiCurrentMsg, (LONG)message);
    InterlockedExchange64(&g_uiMsgStarted, (LONG64)GetTickCount64());
}
