/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <functional>
#include <vector>
#include <servo/servo_capi.h>
#include "browser_shell.h"
#include "servo_view_utils.h"

class ServoViewHost {
public:
    static void RegisterWindowClass(HINSTANCE hInstance);
    static ServoViewHost* FromHwnd(HWND hwnd);
    static ServoViewHost* FromWebView(struct WebView* webview);
    static ServoViewHost* GetActiveHost();

    ServoViewHost(HWND parentHwnd, struct Servo* servo, IBrowserShell* browserShell, const std::string& initialUrl);
    ServoViewHost(HWND parentHwnd, struct Servo* servo, IBrowserShell* browserShell, struct ServoCreateNewWebViewRequest* request);
    ~ServoViewHost();

    HWND GetHwnd() const { return m_hwnd; }
    struct WebView* GetWebView() const { return m_webview; }
    // True once the popup create request has been handed to a WebView builder
    // (and must therefore not be freed by the Rust delegate callback).
    bool WasRequestConsumed() const { return m_requestConsumed; }
    bool IsValid() const { return m_hwnd != nullptr && m_webview != nullptr; }

    void SetBounds(int x, int y, int width, int height);
    void Show(bool show);
    void SetFocus();

    void LoadUrl(const std::string& url);
    void Reload();
    void Stop();
    void GoBack();
    void GoForward();
    bool CanGoBack() const;
    bool CanGoForward() const;

    std::string GetUrl() const;
    std::string GetTitle() const;
    void HandleFaviconChanged(const char* url);
    void ShowContextMenu(int screenX, int screenY);
    void ViewPageSource();
    void EvaluateJavaScript(const std::string& script);

    // Find-in-page. Servo's C API (servo_webview_find) accepts only `forward`
    // and `match_case`; `wholeWord` is kept for callers that share a signature
    // with other shells and is documented at the implementation as unsupported.
    void Find(const std::string& text, bool forward, bool matchCase);
    void Find(const std::string& text, bool forward, bool matchCase, bool wholeWord);

private:
    void RaiseForCompositionHosting();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    static void OnNewFrameReady(struct WebView* webview, void* userData);
    static void OnUrlChanged(struct WebView* webview, const char* url, void* userData);
    static void OnTitleChanged(struct WebView* webview, const char* title, void* userData);
    static void OnLoadStatusChanged(struct WebView* webview, int32_t status, void* userData);
    static void OnLoadError(struct WebView* webview, int32_t errorCode, const char* errorText, void* userData);
    static void OnLoadError(struct WebView* webview, const char* url, int32_t errorCode, const char* errorText, void* userData);
    static void OnCursorChanged(struct WebView* webview, int32_t cursorType, void* userData);
    static void OnFaviconChanged(struct WebView* webview, const char* url, void* userData);
    static void OnConsoleMessage(struct WebView* webview, int32_t level, const char* message, void* userData);
    static bool OnRequestCreateNew(struct WebView* webview, struct ServoCreateNewWebViewRequest* request, void* userData);
    static void OnAlertDialog(struct WebView* webview, const char* message, void (*callback)(bool, void*), void* userData);
    static void OnConfirmDialog(struct WebView* webview, const char* message, void (*callback)(bool, void*), void* userData);
    static void OnPromptDialog(struct WebView* webview, const char* message, const char* defaultVal, void (*callback)(const char*, void*), void* userData);
    static void OnFilePicker(struct WebView* webview, bool allowMultiple, void (*callback)(const char* const*, size_t, void*), void* userData);
    static void OnShowSelectElement(struct WebView* webview, const struct ServoSelectElementParams* params, void (*callback)(const size_t*, size_t, void*), void* userData);
    static void OnContextMenu(struct WebView* webview, const struct ServoContextMenuParams* params, void (*callback)(int32_t, void*), void* userData);
    static void OnBeforeDownload(struct WebView* webview, struct ServoDownloadItem* download, void (*callback)(bool, void*), void* userData);
    static void OnDownloadUpdated(struct WebView* webview, struct ServoDownloadItem* download, void* userData);
    static void OnRequestPermission(struct WebView* webview, const char* origin, int32_t permission, void (*callback)(bool, void*), void* callbackUserData, void* userData);
    static void OnNotifyCrashed(struct WebView* webview, const char* reason, const char* backtrace, void* userData);
    static void OnStatusTextChanged(struct WebView* webview, const char* status, void* userData);
    static void OnFindResult(struct WebView* webview, int identifier, int count, const struct ServoFindResult* result, void* userData);
    // `port` must be int32_t to match the generated C API delegate signature.
    static void OnRequestAuthentication(struct WebView* webview, const char* host, int32_t port, const char* realm, void (*callback)(const char*, const char*, void*), void* userData);

    static void OnClipboardClear(struct WebView* webview, void* userData);
    static void OnClipboardGetText(struct WebView* webview, struct ServoClipboardStringRequest* request, void* userData);
    static void OnClipboardSetText(struct WebView* webview, const char* text, void* userData);

    uint32_t GetServoModifiers() const;
    void InjectPageEnhancements();
    void RecordNavigationUrl(const std::string& url);
    static bool IsAlive(ServoViewHost* host);
    static void OnViewSourceResult(ServoValue* value, const char* errorMessage, void* userData);

    // Navigation errors and security state are reported to the shell, which
    // owns all user-visible chrome (lock icon, error interstitial, etc.).
    void HandleLoadError(const char* url, int32_t errorCode, const char* errorText);
    void ReportNavigationSecurity(const std::string& url);
    // Target-aware context menu helpers. The engine's ServoContextMenuParams
    // are cached for the current right-click; host-only commands are executed
    // here, while engine actions are emulated because the one-shot C callback
    // has already been answered with DISMISS.
    void ExecuteContextCommand(int command, const std::string& linkUrl, const std::string& imageUrl);
    void InjectEditShortcut(const char* key, const char* code, uint32_t modifiers);

    HWND m_hwnd;
    HWND m_parentHwnd;
    struct Servo* m_servo;
    struct WebView* m_webview;
    RenderingContext* m_context;
    IBrowserShell* m_browserShell;
    bool m_trackingMouse;
    bool m_requestConsumed = false;
    // Painting calls synchronously into WebRender, so only do it when a frame
    // is actually ready. Resize/expose set this again to avoid stale pixels.
    bool m_hasNewFrame = true;
    // On software-rendered pages a single frame can take >1 s. Dropping frames
    // keeps the UI thread responsive instead of painting back-to-back forever.
    ULONGLONG m_lastPaintEndMs = 0;
    DWORD m_paintThrottleMs = 0;
    bool m_paintTimerPending = false;
    HCURSOR m_currentCursor;
    std::vector<std::string> m_history;
    size_t m_historyIndex = 0;

    // Most recent engine-provided context-menu target (deep-copied in
    // OnContextMenu, which runs before the native menu is built in the common
    // case). A short freshness window prevents a stale target from a previous
    // right-click from changing a keyboard-invoked menu.
    std::string m_contextMenuLinkUrl;
    std::string m_contextMenuImageUrl;
    uint32_t m_contextMenuFlags = 0;
    ULONGLONG m_contextMenuParamsAtMs = 0;

    // Set to the error page URL so a failure while loading that page does not
    // trigger an endless error-page reload loop.
    std::string m_errorPageUrl;
};
