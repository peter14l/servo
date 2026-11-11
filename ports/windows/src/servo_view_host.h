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
#include <servo/servo_capi.h>
#include "servo_view_utils.h"

class BrowserWindow;

class ServoViewHost {
public:
    static void RegisterWindowClass(HINSTANCE hInstance);

    ServoViewHost(HWND parentHwnd, struct Servo* servo, BrowserWindow* browserWindow, const std::string& initialUrl);
    ServoViewHost(HWND parentHwnd, struct Servo* servo, BrowserWindow* browserWindow, struct ServoCreateNewWebViewRequest* request);
    ~ServoViewHost();

    HWND GetHwnd() const { return m_hwnd; }
    struct WebView* GetWebView() const { return m_webview; }

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
    void EvaluateJavaScript(const std::string& script);

private:
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
    static void OnContextMenu(struct WebView* webview, const struct ServoContextMenuParams* params, void (*callback)(int32_t, void*), void* userData);
    static void OnBeforeDownload(struct WebView* webview, struct ServoDownloadItem* download, void (*callback)(bool, void*), void* userData);
    static void OnDownloadUpdated(struct WebView* webview, struct ServoDownloadItem* download, void* userData);
    static void OnRequestPermission(struct WebView* webview, const char* origin, int32_t permission, void (*callback)(bool, void*), void* callbackUserData, void* userData);
    static void OnNotifyCrashed(struct WebView* webview, const char* reason, const char* backtrace, void* userData);
    static void OnStatusTextChanged(struct WebView* webview, const char* status, void* userData);
    static void OnRequestAuthentication(struct WebView* webview, const char* host, uint16_t port, const char* realm, void (*callback)(const char*, const char*, void*), void* userData);

    static void OnClipboardClear(struct WebView* webview, void* userData);
    static void OnClipboardGetText(struct WebView* webview, struct ServoClipboardStringRequest* request, void* userData);
    static void OnClipboardSetText(struct WebView* webview, const char* text, void* userData);

    uint32_t GetServoModifiers() const;
    void InjectPageEnhancements();

    HWND m_hwnd;
    HWND m_parentHwnd;
    struct Servo* m_servo;
    struct WebView* m_webview;
    RenderingContext* m_context;
    BrowserWindow* m_browserWindow;
    bool m_trackingMouse;
    HCURSOR m_currentCursor;
};
