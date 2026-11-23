/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>
#include <servo/servo_capi.h>

// Messages used to move engine work onto the chrome UI thread.
#define WM_APP_SERVO_WAKE (WM_APP + 101)
#define WM_APP_RUN_UI_TASK (WM_APP + 102)

using UiTask = std::function<void()>;

struct ClearBrowsingDataResult;

// Runs a task on the thread that owns the registered main HWND. Falls back to
// running inline only before that HWND exists.
bool PostUiTask(UiTask task);

// Serialized diagnostic logging next to the executable. Never pass IPC
// payloads or credentials to these functions.
void LogDebug(const char* fmt, ...);

// Performance counters read by the UI watchdog.
void RecordPaintDuration(unsigned long long milliseconds);
void RecordDeferredControlDuration(unsigned long long milliseconds);
void RecordDroppedFrame();

// Runs one Servo event-loop spin, guarding against re-entry from modal UI.
void ServoSpinEventLoopOnce(struct Servo* servo);

// Engine lifetime and plumbing shared by every Windows chrome implementation.
class BrowserRuntime {
public:
    static BrowserRuntime& Instance();

    BrowserRuntime(const BrowserRuntime&) = delete;
    BrowserRuntime& operator=(const BrowserRuntime&) = delete;

    // Creates the Servo instance. COM must already be initialized by the host.
    bool Initialize();
    void SetMainHwnd(HWND hwnd);
    HWND GetMainHwnd() const { return m_mainHwnd; }
    struct Servo* GetServo() const { return m_servo; }

    using DevToolsCallback = std::function<void(uint16_t, const std::string&)>;
    void SetDevToolsCallback(DevToolsCallback callback);
    void NotifyDevTools(uint16_t port, const std::string& token);
    uint16_t DevToolsPort() const { return m_devToolsPort; }

    // Called from the C waker (any thread) and from the UI thread respectively.
    void OnServoWake();
    void PumpWake();
    void PumpUiTask(LPARAM lParam);
    void PostInitialWake();

    void StartWatchdog();
    void StopWatchdog();
    // Gracefully releases Servo so persisted site data is written to disk.
    void Shutdown();
    // Lets the watchdog attribute a stall to the message being dispatched.
    void SetCurrentMessage(UINT message);

    // Registered with the shared settings helper at startup. Reports only the
    // categories that the verified Servo C APIs actually cleared.
    static ClearBrowsingDataResult ClearEngineData(int mask);

private:
    BrowserRuntime() = default;
    ~BrowserRuntime() = default;

    HWND m_mainHwnd = nullptr;
    DWORD m_mainThreadId = 0;
    struct Servo* m_servo = nullptr;
    DevToolsCallback m_devToolsCallback;
    uint16_t m_devToolsPort = 0;
};
