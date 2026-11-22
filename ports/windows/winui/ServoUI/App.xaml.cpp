#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "../../src/browser_runtime.h"
#include "../../src/servo_view_host.h"

#include <string_view>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace {

constexpr wchar_t kEngineMessageClass[] = L"ServoWinUIEngineMessageWindow";

LRESULT CALLBACK EngineMessageWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto& runtime = BrowserRuntime::Instance();
    switch (message) {
    case WM_APP_SERVO_WAKE:
        runtime.SetCurrentMessage(message);
        runtime.PumpWake();
        runtime.SetCurrentMessage(0);
        return 0;
    case WM_APP_RUN_UI_TASK:
        runtime.SetCurrentMessage(message);
        runtime.PumpUiTask(lParam);
        runtime.SetCurrentMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

HWND CreateEngineMessageWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = EngineMessageWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kEngineMessageClass;
    if (!GetClassInfoExW(wc.hInstance, kEngineMessageClass, &wc)) {
        RegisterClassExW(&wc);
    }
    return CreateWindowExW(0, kEngineMessageClass, L"ServoEngineMessage",
                           0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

std::string InitialUrlFromCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};
    std::string url;
    if (argc > 1 && argv[1] && argv[1][0] != L'\0') {
        int length = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, nullptr, 0, nullptr, nullptr);
        if (length > 1) {
            url.resize(static_cast<size_t>(length - 1));
            WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, &url[0], length, nullptr, nullptr);
        }
    }
    LocalFree(argv);
    return url;
}

} // namespace

namespace winrt::ServoUI::implementation
{
    App::App()
    {
        InitializeComponent();
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        // The engine waker has no user data, so it needs a stable HWND owned by
        // the UI thread. A message-only window keeps engine messages off the
        // XAML window's WndProc while still running tasks on the UI thread.
        HWND engineHwnd = CreateEngineMessageWindow();
        auto& runtime = BrowserRuntime::Instance();
        runtime.SetMainHwnd(engineHwnd);
        if (!runtime.Initialize()) {
            MessageBoxW(nullptr, L"Failed to initialize Servo.", L"Servo Browser Error", MB_ICONERROR);
            Application::Current().Exit();
            return;
        }
        runtime.StartWatchdog();

        // Servo's native child HWNDs all use one registered window class.
        ServoViewHost::RegisterWindowClass(GetModuleHandleW(nullptr));

        auto mainWindow = make<MainWindow>();
        mainWindow.ActivateAsInitial(to_hstring(std::string_view(InitialUrlFromCommandLine())));
        window = mainWindow;
        runtime.PostInitialWake();
    }
}
