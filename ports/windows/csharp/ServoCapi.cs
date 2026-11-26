// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.Runtime.InteropServices;

namespace Servo.Embedding
{
    public static unsafe class ServoCapi
    {
        private const string LibName = "servo_capi";

        public const int SERVO_LOAD_STATUS_STARTED = 0;
        public const int SERVO_LOAD_STATUS_HEAD_PARSED = 1;
        public const int SERVO_LOAD_STATUS_COMPLETE = 2;

        public const int SERVO_KEY_EVENT_DOWN = 0;
        public const int SERVO_KEY_EVENT_UP = 1;

        public const int SERVO_MOUSE_BUTTON_EVENT_DOWN = 0;
        public const int SERVO_MOUSE_BUTTON_EVENT_UP = 1;

        public const int SERVO_MOUSE_BUTTON_LEFT = 0;
        public const int SERVO_MOUSE_BUTTON_MIDDLE = 1;
        public const int SERVO_MOUSE_BUTTON_RIGHT = 2;

        public const int SERVO_WHEEL_DELTA_PIXEL = 0;
        public const int SERVO_WHEEL_DELTA_LINE = 1;
        public const int SERVO_WHEEL_DELTA_PAGE = 2;

        public const int SERVO_THEME_LIGHT = 0;
        public const int SERVO_THEME_DARK = 1;

        [StructLayout(LayoutKind.Sequential)]
        public struct ServoEventLoopWaker
        {
            public uint StructSize;
            public uint Reserved;
            public delegate* unmanaged[Cdecl]<void> WakeCallback;
        }

        // Exact layout matching ffi/capi/webview_delegate.rs :: ServoWebViewDelegate.
        // Every field is a pointer-sized slot (nint) so Marshal.SizeOf matches Rust's size_of.
        [StructLayout(LayoutKind.Sequential)]
        public struct ServoWebViewDelegate
        {
            public uint StructSize;
            public uint Reserved;
            public nint UserData;
            public nint NotifyLoadStatusChanged;
            public nint NotifyUrlChanged;
            public nint NotifyPageTitleChanged;
            public nint NotifyNewFrameReady;
            public nint NotifyFocusChanged;
            public nint NotifyGotFocus;
            public nint NotifyLoadStarted;
            public nint NotifyLoadEnded;
            public nint NotifyLoadError;
            public nint NotifyCursorChanged;
            public nint NotifyStatusTextChanged;
            public nint NotifyFaviconChanged;
            public nint ShowConsoleMessage;
            public nint ShowAlertDialog;
            public nint ShowConfirmDialog;
            public nint ShowPromptDialog;
            public nint ShowSelectElement;
            public nint ShowColorPicker;
            public nint RequestPermission;
            public nint RequestAuthentication;
            public nint OnCertificateError;
            public nint RequestUnload;
            public nint InterceptRequest;
            public nint NotifyClosed;
            public nint OnBeforeDownload;
            public nint OnDownloadUpdated;
            public nint OnFindResult;
            public nint OnWindowResized;
            public nint OnWindowMoved;
            public nint OnWindowMinimized;
            public nint OnWindowMaximized;
            public nint OnWindowRestored;
            public nint OnWindowFullscreenChanged;
            public nint OnWindowCloseRequested;
            public nint OnWindowMoveOrResizeStarted;
            public nint NotifyAnimatingChanged;
            public nint NotifyHistoryChanged;
            public nint NotifyCrashed;
            public nint ShowIme;
            public nint HideIme;
            public nint ShowFilePicker;
            public nint ShowContextMenu;
            public nint ScreenGeometry;
            public nint RequestMoveTo;
            public nint RequestResizeTo;
            public nint RequestCreateNew;
            public nint OnScriptMessage;
            public nint InterceptRequestStreaming;
            public nint NotifyAccessibilityTreeUpdate;
        }

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_builder_create();

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_builder_set_event_loop_waker(nint builder, ServoEventLoopWaker waker);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_builder_set_options(nint builder, nint options);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_builder_set_preferences(nint builder, nint preferences);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_builder_build(nint builder);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_free(nint servo);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_spin_event_loop(nint servo);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_options_create();

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_options_free(nint options);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_options_set_config_dir(nint options, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_preferences_create();

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_preferences_free(nint preferences);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_preferences_set_i64(nint preferences, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, long value);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_preferences_set_bool(nint preferences, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.U1)] bool value);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_rendering_context_create_window(void* displayHandle, void* windowHandle, uint width, uint height);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_rendering_context_create_windows_hwnd(void* hwnd, void* hinstance, uint width, uint height);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_rendering_context_create_software(uint width, uint height);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_rendering_context_free(nint context);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_rendering_context_present(nint context);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_webview_builder_create(nint servo, nint context);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_builder_set_url(nint builder, [MarshalAs(UnmanagedType.LPUTF8Str)] string url);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_builder_set_delegate(nint builder, ServoWebViewDelegate @delegate);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_builder_set_hidpi_scale_factor(nint builder, float scaleFactor);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern nint servo_webview_builder_build(nint builder);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_free(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_paint(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_present(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_load(nint webview, [MarshalAs(UnmanagedType.LPUTF8Str)] string url);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_reload(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_stop(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool servo_webview_can_go_back(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.U1)]
        public static extern bool servo_webview_can_go_forward(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_go_back(nint webview, nuint amount);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_go_forward(nint webview, nuint amount);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_resize(nint webview, uint width, uint height);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_set_hidpi_scale_factor(nint webview, float scaleFactor);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_show(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_hide(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_set_focused(nint webview, [MarshalAs(UnmanagedType.U1)] bool focused);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void servo_webview_notify_theme_change(nint webview, int theme);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_set_page_zoom(nint webview, float zoom);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern float servo_webview_page_zoom(nint webview);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_find(nint webview, [MarshalAs(UnmanagedType.LPUTF8Str)] string text, [MarshalAs(UnmanagedType.U1)] bool forward, [MarshalAs(UnmanagedType.U1)] bool matchCase);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_find_next(nint webview, int identifier, [MarshalAs(UnmanagedType.U1)] bool forward);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_find_clear(nint webview, int identifier);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int servo_webview_get_favicon_image(nint webview, uint* width, uint* height, uint* format, byte* buffer, nuint* bufferLen);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong servo_webview_notify_key_event(
            nint webview,
            int eventType,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string code,
            int location,
            uint modifiers,
            [MarshalAs(UnmanagedType.U1)] bool repeat,
            [MarshalAs(UnmanagedType.U1)] bool isComposing);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong servo_webview_notify_mouse_button_event(nint webview, int eventType, int button, double x, double y, uint modifiers);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong servo_webview_notify_mouse_move_event(nint webview, double x, double y, uint modifiers);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong servo_webview_notify_mouse_leave_event(nint webview, [MarshalAs(UnmanagedType.U1)] bool focusMovingToAnotherIframe);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong servo_webview_notify_wheel_event(nint webview, double deltaX, double deltaY, double deltaZ, int deltaMode, double x, double y);
    }
}
