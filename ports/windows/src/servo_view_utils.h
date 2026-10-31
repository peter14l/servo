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
    default: break;
    }

    // Try translating using current keyboard state to get exact Unicode character and casing
    BYTE kbdState[256] = {};
    GetKeyboardState(kbdState);
    UINT scanCode = (lParam >> 16) & 0xFF;
    wchar_t unicodeBuf[4] = {};
    int count = ToUnicode(static_cast<UINT>(wParam), scanCode, kbdState, unicodeBuf, 4, 0);
    if (count > 0 && unicodeBuf[0] >= 32) {
        char utf8Buf[8] = {};
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, unicodeBuf, count, utf8Buf, sizeof(utf8Buf) - 1, nullptr, nullptr);
        if (utf8Len > 0) {
            utf8Buf[utf8Len] = '\0';
            outKey = utf8Buf;
            outCode = "";
            return;
        }
    }

    // Fallback: single character ASCII if printable
    if (wParam >= 32 && wParam < 127) {
        char ch = static_cast<char>(wParam);
        if (!(kbdState[VK_SHIFT] & 0x80) && !(kbdState[VK_CAPITAL] & 0x01)) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = ch + ('a' - 'A');
            }
        }
        outKey = std::string(1, ch);
        outCode = "";
        return;
    }

    outKey = "Unidentified";
    outCode = "Unidentified";
}

} // namespace servo_view
