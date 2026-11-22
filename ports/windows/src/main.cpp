/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <servo/servo_capi.h>
#include "browser_runtime.h"
#include "browser_window.h"
#include "tab_strip.h"
#include "omnibox.h"
#include "find_bar.h"
#include "download_manager.h"
#include "new_tab_page.h"
#include "servo_view_host.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Declare Per-Monitor V2 DPI awareness so Windows does not virtual-scale
    // coordinates and click hit-testing aligns 1:1 with WebRender's pixels.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr, L"Failed to initialize COM.", L"Servo Browser Error", MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    auto& runtime = BrowserRuntime::Instance();
    if (!runtime.Initialize()) {
        MessageBoxW(nullptr, L"Failed to initialize Servo.", L"Servo Browser Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    BrowserWindow::RegisterWindowClass(hInstance);
    TabStrip::RegisterWindowClass(hInstance);
    OmniboxToolbar::RegisterWindowClass(hInstance);
    FindBar::RegisterWindowClass(hInstance);
    DownloadManager::RegisterWindowClass(hInstance);
    ServoViewHost::RegisterWindowClass(hInstance);
    LogDebug("Window classes registered.");

    std::string defaultUrl = NewTabPage::EnsureNewTabPageExists();
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
    BrowserWindow* browserWindow = BrowserWindow::Create(runtime.GetServo(), initial_url);
    if (!browserWindow) {
        LogDebug("BrowserWindow::Create failed!");
        CoUninitialize();
        return 1;
    }

    runtime.SetMainHwnd(browserWindow->GetHwnd());
    runtime.SetDevToolsCallback([browserWindow](uint16_t port, const std::string& token) {
        browserWindow->SetDevToolsInfo(port, token);
    });
    LogDebug("Browser window created successfully. HWND: 0x%p", runtime.GetMainHwnd());

    runtime.PostInitialWake();
    runtime.StartWatchdog();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        ULONGLONG start = GetTickCount64();
        runtime.SetCurrentMessage(msg.message);
        if (msg.message == WM_APP_SERVO_WAKE) {
            runtime.PumpWake();
        } else if (msg.message == WM_APP_RUN_UI_TASK) {
            runtime.PumpUiTask(msg.lParam);
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= 250) {
            LogDebug("[SLOW] msg=0x%04X took %llu ms", msg.message, elapsed);
        }
        runtime.SetCurrentMessage(0);
    }
    runtime.StopWatchdog();
    LogDebug("Message pump finished with code %d", (int)msg.wParam);

    // Avoid background thread teardown races during CRT shutdown.
    ExitProcess(static_cast<UINT>(msg.wParam));
}
