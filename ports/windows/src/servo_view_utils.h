/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

namespace servo_view {

inline LPCWSTR MapServoCursorToWin32(int32_t cursorType) {
    switch (cursorType) {
    case 0: return IDC_ARROW;       // SERVO_CURSOR_POINTER
    case 1: return IDC_CROSS;       // SERVO_CURSOR_CROSSHAIR
    case 2: return IDC_HAND;        // SERVO_CURSOR_HAND
    case 3: return IDC_IBEAM;       // SERVO_CURSOR_IBEAM (Text/Input)
    case 4: return IDC_WAIT;        // SERVO_CURSOR_WAIT
    case 5: return IDC_HELP;        // SERVO_CURSOR_HELP
    case 6: return IDC_SIZEWE;      // SERVO_CURSOR_EAST_RESIZE
    case 7: return IDC_SIZENS;      // SERVO_CURSOR_NORTH_RESIZE
    case 8: return IDC_SIZENESW;    // SERVO_CURSOR_NORTH_EAST_RESIZE
    case 9: return IDC_SIZENWSE;    // SERVO_CURSOR_NORTH_WEST_RESIZE
    case 10: return IDC_SIZENS;     // SERVO_CURSOR_SOUTH_RESIZE
    case 11: return IDC_SIZENWSE;   // SERVO_CURSOR_SOUTH_EAST_RESIZE
    case 12: return IDC_SIZENESW;   // SERVO_CURSOR_SOUTH_WEST_RESIZE
    case 13: return IDC_SIZEWE;     // SERVO_CURSOR_WEST_RESIZE
    case 14: return IDC_SIZENS;     // SERVO_CURSOR_NORTH_SOUTH_RESIZE
    case 15: return IDC_SIZEWE;     // SERVO_CURSOR_EAST_WEST_RESIZE
    case 16: return IDC_SIZENESW;   // SERVO_CURSOR_NORTH_EAST_SOUTH_WEST_RESIZE
    case 17: return IDC_SIZENWSE;   // SERVO_CURSOR_NORTH_WEST_SOUTH_EAST_RESIZE
    case 18: return IDC_SIZEWE;     // SERVO_CURSOR_COLUMN_RESIZE
    case 19: return IDC_SIZENS;     // SERVO_CURSOR_ROW_RESIZE
    case 24: return IDC_NO;         // SERVO_CURSOR_NOT_ALLOWED
    default: return IDC_ARROW;
    }
}

inline void TranslateWin32Key(WPARAM wParam, LPARAM lParam, std::string& outKey, std::string& outCode) {
    switch (wParam) {
    case VK_RETURN: outKey = "Enter"; outCode = "Enter"; return;
    case VK_BACK: outKey = "Backspace"; outCode = "Backspace"; return;
    case VK_TAB: outKey = "Tab"; outCode = "Tab"; return;
    case VK_ESCAPE: outKey = "Escape"; outCode = "Escape"; return;
    case VK_LEFT: outKey = "ArrowLeft"; outCode = "ArrowLeft"; return;
    case VK_UP: outKey = "ArrowUp"; outCode = "ArrowUp"; return;
    case VK_RIGHT: outKey = "ArrowRight"; outCode = "ArrowRight"; return;
    case VK_DOWN: outKey = "ArrowDown"; outCode = "ArrowDown"; return;
    case VK_DELETE: outKey = "Delete"; outCode = "Delete"; return;
    case VK_INSERT: outKey = "Insert"; outCode = "Insert"; return;
    case VK_HOME: outKey = "Home"; outCode = "Home"; return;
    case VK_END: outKey = "End"; outCode = "End"; return;
    case VK_PRIOR: outKey = "PageUp"; outCode = "PageUp"; return;
    case VK_NEXT: outKey = "PageDown"; outCode = "PageDown"; return;
    case VK_SPACE: outKey = " "; outCode = "Space"; return;
    case VK_SHIFT: outKey = "Shift"; outCode = ((lParam & 0x01000000) != 0) ? "ShiftRight" : "ShiftLeft"; return;
    case VK_CONTROL: outKey = "Control"; outCode = ((lParam & 0x01000000) != 0) ? "ControlRight" : "ControlLeft"; return;
    case VK_MENU: outKey = "Alt"; outCode = ((lParam & 0x01000000) != 0) ? "AltRight" : "AltLeft"; return;
    case VK_CAPITAL: outKey = "CapsLock"; outCode = "CapsLock"; return;
    case VK_NUMLOCK: outKey = "NumLock"; outCode = "NumLock"; return;
    case VK_SCROLL: outKey = "ScrollLock"; outCode = "ScrollLock"; return;
    case VK_PAUSE: outKey = "Pause"; outCode = "Pause"; return;
    case VK_SNAPSHOT: outKey = "PrintScreen"; outCode = "PrintScreen"; return;
    case VK_APPS: outKey = "ContextMenu"; outCode = "ContextMenu"; return;
    case VK_F1: outKey = "F1"; outCode = "F1"; return;
    case VK_F2: outKey = "F2"; outCode = "F2"; return;
    case VK_F3: outKey = "F3"; outCode = "F3"; return;
    case VK_F4: outKey = "F4"; outCode = "F4"; return;
    case VK_F5: outKey = "F5"; outCode = "F5"; return;
    case VK_F6: outKey = "F6"; outCode = "F6"; return;
    case VK_F7: outKey = "F7"; outCode = "F7"; return;
    case VK_F8: outKey = "F8"; outCode = "F8"; return;
    case VK_F9: outKey = "F9"; outCode = "F9"; return;
    case VK_F10: outKey = "F10"; outCode = "F10"; return;
    case VK_F11: outKey = "F11"; outCode = "F11"; return;
    case VK_F12: outKey = "F12"; outCode = "F12"; return;
    default: break;
    }

    // Assign standard physical keyboard code
    if (wParam >= 'A' && wParam <= 'Z') {
        outCode = "Key" + std::string(1, static_cast<char>(wParam));
    } else if (wParam >= '0' && wParam <= '9') {
        outCode = "Digit" + std::string(1, static_cast<char>(wParam));
    } else if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) {
        outCode = "Numpad" + std::to_string(wParam - VK_NUMPAD0);
    } else if (wParam == VK_MULTIPLY) {
        outCode = "NumpadMultiply";
    } else if (wParam == VK_ADD) {
        outCode = "NumpadAdd";
    } else if (wParam == VK_SUBTRACT) {
        outCode = "NumpadSubtract";
    } else if (wParam == VK_DECIMAL) {
        outCode = "NumpadDecimal";
    } else if (wParam == VK_DIVIDE) {
        outCode = "NumpadDivide";
    } else if (wParam == VK_OEM_1) {
        outCode = "Semicolon";
    } else if (wParam == VK_OEM_PLUS) {
        outCode = "Equal";
    } else if (wParam == VK_OEM_COMMA) {
        outCode = "Comma";
    } else if (wParam == VK_OEM_MINUS) {
        outCode = "Minus";
    } else if (wParam == VK_OEM_PERIOD) {
        outCode = "Period";
    } else if (wParam == VK_OEM_2) {
        outCode = "Slash";
    } else if (wParam == VK_OEM_3) {
        outCode = "Backquote";
    } else if (wParam == VK_OEM_4) {
        outCode = "BracketLeft";
    } else if (wParam == VK_OEM_5) {
        outCode = "Backslash";
    } else if (wParam == VK_OEM_6) {
        outCode = "BracketRight";
    } else if (wParam == VK_OEM_7) {
        outCode = "Quote";
    }

    // Try translating using current keyboard state to get exact Unicode character and casing
    BYTE kbdState[256] = {};
    GetKeyboardState(kbdState);

    // Explicitly synchronize modifier keys with GetKeyState to prevent stuck modifiers
    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool isCaps = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    kbdState[VK_SHIFT] = isShift ? 0x80 : 0;
    kbdState[VK_CAPITAL] = isCaps ? 0x01 : 0;
    kbdState[VK_CONTROL] = (GetKeyState(VK_CONTROL) & 0x8000) ? 0x80 : 0;
    kbdState[VK_MENU] = (GetKeyState(VK_MENU) & 0x8000) ? 0x80 : 0;

    UINT scanCode = (lParam >> 16) & 0xFF;
    wchar_t unicodeBuf[4] = {};
    int count = ToUnicode(static_cast<UINT>(wParam), scanCode, kbdState, unicodeBuf, 4, 0x4);
    if (count <= 0) {
        count = ToUnicode(static_cast<UINT>(wParam), scanCode, kbdState, unicodeBuf, 4, 0);
    }
    if (count > 0 && unicodeBuf[0] >= 32) {
        char utf8Buf[8] = {};
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, unicodeBuf, count, utf8Buf, sizeof(utf8Buf) - 1, nullptr, nullptr);
        if (utf8Len > 0) {
            utf8Buf[utf8Len] = '\0';
            outKey = utf8Buf;
            return;
        }
    }

    // Fallback: single character ASCII if printable
    if (wParam >= 32 && wParam < 127) {
        char ch = static_cast<char>(wParam);
        bool shouldUpper = isShift ^ isCaps;
        if (ch >= 'A' && ch <= 'Z') {
            ch = shouldUpper ? ch : (ch + ('a' - 'A'));
        }
        outKey = std::string(1, ch);
        return;
    }

    outKey = "Unidentified";
    outCode = "Unidentified";
}

} // namespace servo_view
