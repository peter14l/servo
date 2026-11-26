// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.UI.Dispatching;
using Servo.Embedding;
using ServoBrowser.WinUI.Models;

namespace ServoBrowser.WinUI.Controls
{
    public sealed unsafe class ServoViewportHost : IDisposable
    {
        private delegate nint WndProcDelegate(nint hwnd, uint msg, nint wParam, nint lParam);

        private const string WindowClassName = "ServoWinUIChildViewport";
        private const int WS_CHILD = 0x40000000;
        private const int WS_VISIBLE = 0x10000000;
        private const int WS_CLIPCHILDREN = 0x02000000;
        private const int WS_CLIPSIBLINGS = 0x04000000;

        private const uint WM_DESTROY = 0x0002;
        private const uint WM_PAINT = 0x000F;
        private const uint WM_SETFOCUS = 0x0007;
        private const uint WM_KILLFOCUS = 0x0008;
        private const uint WM_ERASEBKGND = 0x0014;
        private const uint WM_KEYDOWN = 0x0100;
        private const uint WM_KEYUP = 0x0101;
        private const uint WM_CHAR = 0x0102;
        private const uint WM_MOUSEMOVE = 0x0200;
        private const uint WM_LBUTTONDOWN = 0x0201;
        private const uint WM_LBUTTONUP = 0x0202;
        private const uint WM_RBUTTONDOWN = 0x0204;
        private const uint WM_RBUTTONUP = 0x0205;
        private const uint WM_MBUTTONDOWN = 0x0207;
        private const uint WM_MBUTTONUP = 0x0208;
        private const uint WM_MOUSEWHEEL = 0x020A;
        private const uint WM_MOUSEHWHEEL = 0x020E;
        private const uint WM_MOUSELEAVE = 0x02A3;

        private const uint SWP_NOZORDER = 0x0004;
        private const uint SWP_NOACTIVATE = 0x0010;
        private const uint SWP_SHOWWINDOW = 0x0040;
        private const uint SWP_HIDEWINDOW = 0x0080;

        private static bool s_classRegistered;
        private static readonly WndProcDelegate s_wndProc = StaticWndProc;
        private static readonly Dictionary<nint, ServoViewportHost> s_hosts = new();

        private readonly DispatcherQueue _dispatcher;
        private readonly TabSession _session;
        private readonly GCHandle _selfHandle;
        private nint _hwnd;
        private nint _webview;
        private bool _visible;
        private bool _trackingMouseLeave;
        private uint _width = 1;
        private uint _height = 1;
        private float _scale = 1.0f;
        private bool _disposed;

        public event Action<TabSession, int>? LoadStatusChanged;
        public event Action<TabSession, string>? UrlChanged;
        public event Action<TabSession, string>? TitleChanged;
        public event Action<TabSession, bool>? AnimatingChanged;
        public event Action<TabSession, int, int>? FindResultReceived;
        public event Action<TabSession, string>? AlertRequested;

        public ServoViewportHost(nint parentHwnd, DispatcherQueue dispatcher, TabSession session, string initialUrl, float scale)
        {
            _dispatcher = dispatcher;
            _session = session;
            _scale = Math.Max(1.0f, scale);
            _selfHandle = GCHandle.Alloc(this);

            EnsureWindowClassRegistered();
            nint hInstance = GetModuleHandleW(null);
            _hwnd = CreateWindowExW(
                0,
                WindowClassName,
                string.Empty,
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                0, 0, 1, 1,
                parentHwnd,
                nint.Zero,
                hInstance,
                nint.Zero);

            if (_hwnd != nint.Zero)
            {
                s_hosts[_hwnd] = this;
                _session.ChildHwnd = _hwnd;
                CreateWebView(hInstance, initialUrl);
            }
        }

        public nint Hwnd => _hwnd;
        public nint WebView => _webview;
        public TabSession Session => _session;

        private void CreateWebView(nint hInstance, string initialUrl)
        {
            nint servo = App.ServoHandle;
            if (servo == nint.Zero || _hwnd == nint.Zero)
            {
                return;
            }

            nint ctx = ServoCapi.servo_rendering_context_create_windows_hwnd((void*)_hwnd, (void*)hInstance, _width, _height);
            if (ctx == nint.Zero)
            {
                return;
            }

            nint builder = ServoCapi.servo_webview_builder_create(servo, ctx);
            // Ownership of ctx transferred to servo_webview_builder_create (GEMINI.md invariant #3).
            ctx = nint.Zero;
            if (builder == nint.Zero)
            {
                return;
            }

            ServoCapi.servo_webview_builder_set_url(builder, initialUrl);
            ServoCapi.servo_webview_builder_set_hidpi_scale_factor(builder, _scale);

            var del = new ServoCapi.ServoWebViewDelegate
            {
                StructSize = (uint)Marshal.SizeOf<ServoCapi.ServoWebViewDelegate>(),
                Reserved = 0,
                UserData = GCHandle.ToIntPtr(_selfHandle),
                NotifyLoadStatusChanged = (nint)(delegate* unmanaged[Cdecl]<nint, int, nint, void>)&OnLoadStatusChanged,
                NotifyUrlChanged = (nint)(delegate* unmanaged[Cdecl]<nint, byte*, nint, void>)&OnUrlChanged,
                NotifyPageTitleChanged = (nint)(delegate* unmanaged[Cdecl]<nint, byte*, nint, void>)&OnTitleChanged,
                NotifyNewFrameReady = (nint)(delegate* unmanaged[Cdecl]<nint, nint, void>)&OnNewFrameReady,
                NotifyAnimatingChanged = (nint)(delegate* unmanaged[Cdecl]<nint, byte, nint, void>)&OnAnimatingChanged,
                NotifyCrashed = (nint)(delegate* unmanaged[Cdecl]<nint, byte*, byte*, nint, void>)&OnWebViewCrashed,
                OnFindResult = (nint)(delegate* unmanaged[Cdecl]<nint, int, int, nint, nint, void>)&OnFindResult,
                ShowAlertDialog = (nint)(delegate* unmanaged[Cdecl]<nint, byte*, nint, nint, void>)&OnShowAlertDialog,
            };
            ServoCapi.servo_webview_builder_set_delegate(builder, del);

            _webview = ServoCapi.servo_webview_builder_build(builder);
            _session.WebView = _webview;
        }

        public void SetBounds(int x, int y, int width, int height, float scale, bool visible)
        {
            if (_hwnd == nint.Zero || _disposed)
            {
                return;
            }

            _visible = visible;
            _width = (uint)Math.Max(1, width);
            _height = (uint)Math.Max(1, height);
            _scale = Math.Max(1.0f, scale);

            uint flags = SWP_NOZORDER | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
            SetWindowPos(_hwnd, nint.Zero, x, y, (int)_width, (int)_height, flags);

            if (_webview != nint.Zero)
            {
                ServoCapi.servo_webview_set_hidpi_scale_factor(_webview, _scale);
                ServoCapi.servo_webview_resize(_webview, _width, _height);
                if (visible)
                {
                    ServoCapi.servo_webview_show(_webview);
                    InvalidateRect(_hwnd, nint.Zero, false);
                }
                else
                {
                    ServoCapi.servo_webview_hide(_webview);
                }
            }
        }

        public void Load(string url)
        {
            if (_webview != nint.Zero)
            {
                ServoCapi.servo_webview_load(_webview, url);
            }
        }

        public void GoBack() { if (_webview != nint.Zero) ServoCapi.servo_webview_go_back(_webview, 1); }
        public void GoForward() { if (_webview != nint.Zero) ServoCapi.servo_webview_go_forward(_webview, 1); }
        public void Reload() { if (_webview != nint.Zero) ServoCapi.servo_webview_reload(_webview); }
        public void Stop() { if (_webview != nint.Zero) ServoCapi.servo_webview_stop(_webview); }
        public void SetZoom(float zoom) { if (_webview != nint.Zero) ServoCapi.servo_webview_set_page_zoom(_webview, zoom); }
        public float GetZoom() => _webview != nint.Zero ? ServoCapi.servo_webview_page_zoom(_webview) : 1.0f;
        public void NotifyTheme(bool isDark)
        {
            if (_webview != nint.Zero)
            {
                ServoCapi.servo_webview_notify_theme_change(_webview, isDark ? ServoCapi.SERVO_THEME_DARK : ServoCapi.SERVO_THEME_LIGHT);
            }
        }

        private nint HandleWndProc(nint hwnd, uint msg, nint wParam, nint lParam)
        {
            switch (msg)
            {
                case WM_ERASEBKGND:
                    return 1;

                case WM_PAINT:
                {
                    BeginPaint(hwnd, out var ps);
                    if (_webview != nint.Zero && _visible)
                    {
                        // GEMINI.md invariant #1: paint must be immediately followed by present.
                        ServoCapi.servo_webview_paint(_webview);
                        ServoCapi.servo_webview_present(_webview);
                    }
                    EndPaint(hwnd, ref ps);
                    return 0;
                }

                case WM_SETFOCUS:
                    if (_webview != nint.Zero) ServoCapi.servo_webview_set_focused(_webview, true);
                    return 0;

                case WM_KILLFOCUS:
                    if (_webview != nint.Zero) ServoCapi.servo_webview_set_focused(_webview, false);
                    return 0;

                case WM_MOUSEMOVE:
                    if (_webview != nint.Zero)
                    {
                        if (!_trackingMouseLeave)
                        {
                            var tme = new TRACKMOUSEEVENT { cbSize = (uint)Marshal.SizeOf<TRACKMOUSEEVENT>(), dwFlags = 0x00000002, hwndTrack = hwnd };
                            TrackMouseEvent(ref tme);
                            _trackingMouseLeave = true;
                        }
                        GetXY(lParam, out double x, out double y);
                        ServoCapi.servo_webview_notify_mouse_move_event(_webview, x, y, 0);
                    }
                    return 0;

                case WM_MOUSELEAVE:
                    _trackingMouseLeave = false;
                    if (_webview != nint.Zero)
                    {
                        ServoCapi.servo_webview_notify_mouse_leave_event(_webview, false);
                    }
                    return 0;

                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                    if (_webview != nint.Zero)
                    {
                        if (msg is WM_LBUTTONDOWN or WM_RBUTTONDOWN or WM_MBUTTONDOWN)
                        {
                            SetFocus(hwnd);
                        }
                        int evt = msg is WM_LBUTTONDOWN or WM_RBUTTONDOWN or WM_MBUTTONDOWN
                            ? ServoCapi.SERVO_MOUSE_BUTTON_EVENT_DOWN
                            : ServoCapi.SERVO_MOUSE_BUTTON_EVENT_UP;
                        int btn = msg is WM_LBUTTONDOWN or WM_LBUTTONUP ? ServoCapi.SERVO_MOUSE_BUTTON_LEFT
                            : msg is WM_RBUTTONDOWN or WM_RBUTTONUP ? ServoCapi.SERVO_MOUSE_BUTTON_RIGHT
                            : ServoCapi.SERVO_MOUSE_BUTTON_MIDDLE;
                        GetXY(lParam, out double x, out double y);
                        ServoCapi.servo_webview_notify_mouse_button_event(_webview, evt, btn, x, y, 0);
                    }
                    return 0;

                case WM_MOUSEWHEEL:
                case WM_MOUSEHWHEEL:
                    if (_webview != nint.Zero)
                    {
                        short delta = (short)((wParam.ToInt64() >> 16) & 0xFFFF);
                        double scaled = (delta / 120.0) * 40.0;
                        double dx = msg == WM_MOUSEHWHEEL ? -scaled : 0.0;
                        double dy = msg == WM_MOUSEWHEEL ? scaled : 0.0;
                        GetXY(lParam, out double x, out double y);
                        ServoCapi.servo_webview_notify_wheel_event(_webview, dx, dy, 0, ServoCapi.SERVO_WHEEL_DELTA_PIXEL, x, y);
                    }
                    return 0;

                case WM_KEYDOWN:
                case WM_KEYUP:
                    if (_webview != nint.Zero)
                    {
                        int vk = (int)(wParam.ToInt64() & 0xFF);
                        string keyName = VkToWebKey(vk);
                        if (!string.IsNullOrEmpty(keyName))
                        {
                            int evt = msg == WM_KEYDOWN ? ServoCapi.SERVO_KEY_EVENT_DOWN : ServoCapi.SERVO_KEY_EVENT_UP;
                            bool repeat = msg == WM_KEYDOWN && ((lParam.ToInt64() & 0x40000000) != 0);
                            ServoCapi.servo_webview_notify_key_event(_webview, evt, keyName, keyName, 0, 0, repeat, false);
                        }
                    }
                    return 0;

                case WM_CHAR:
                    if (_webview != nint.Zero)
                    {
                        char ch = (char)(wParam.ToInt64() & 0xFFFF);
                        if (!char.IsControl(ch))
                        {
                            string s = ch.ToString();
                            ServoCapi.servo_webview_notify_key_event(_webview, ServoCapi.SERVO_KEY_EVENT_DOWN, s, s, 0, 0, false, false);
                        }
                    }
                    return 0;

                case WM_DESTROY:
                    s_hosts.Remove(hwnd);
                    return 0;
            }

            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        private static void GetXY(nint lParam, out double x, out double y)
        {
            long lp = lParam.ToInt64();
            x = (short)(lp & 0xFFFF);
            y = (short)((lp >> 16) & 0xFFFF);
        }

        private static string VkToWebKey(int vk) => vk switch
        {
            0x08 => "Backspace",
            0x09 => "Tab",
            0x0D => "Enter",
            0x1B => "Escape",
            0x20 => " ",
            0x21 => "PageUp",
            0x22 => "PageDown",
            0x23 => "End",
            0x24 => "Home",
            0x25 => "ArrowLeft",
            0x26 => "ArrowUp",
            0x27 => "ArrowRight",
            0x28 => "ArrowDown",
            0x2E => "Delete",
            _ => string.Empty,
        };

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnLoadStatusChanged(nint webview, int status, nint userData)
        {
            if (TryGetHost(userData, out var host))
            {
                host._dispatcher.TryEnqueue(() =>
                {
                    host._session.IsLoading = status != ServoCapi.SERVO_LOAD_STATUS_COMPLETE;
                    host._session.CanGoBack = ServoCapi.servo_webview_can_go_back(webview);
                    host._session.CanGoForward = ServoCapi.servo_webview_can_go_forward(webview);
                    host.LoadStatusChanged?.Invoke(host._session, status);
                });
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnUrlChanged(nint webview, byte* urlPtr, nint userData)
        {
            string url = PtrToUtf8(urlPtr);
            if (TryGetHost(userData, out var host))
            {
                host._dispatcher.TryEnqueue(() =>
                {
                    host._session.Url = url;
                    host.UrlChanged?.Invoke(host._session, url);
                });
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnTitleChanged(nint webview, byte* titlePtr, nint userData)
        {
            string title = PtrToUtf8(titlePtr);
            if (TryGetHost(userData, out var host))
            {
                host._dispatcher.TryEnqueue(() =>
                {
                    host._session.Title = title;
                    host.TitleChanged?.Invoke(host._session, title);
                });
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnNewFrameReady(nint webview, nint userData)
        {
            if (TryGetHost(userData, out var host) && host._hwnd != nint.Zero && host._visible)
            {
                InvalidateRect(host._hwnd, nint.Zero, false);
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnAnimatingChanged(nint webview, byte animating, nint userData)
        {
            if (TryGetHost(userData, out var host))
            {
                bool isAnimating = animating != 0;
                host._dispatcher.TryEnqueue(() => host.AnimatingChanged?.Invoke(host._session, isAnimating));
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnWebViewCrashed(nint webview, byte* reasonPtr, byte* backtracePtr, nint userData)
        {
            string reason = PtrToUtf8(reasonPtr);
            string backtrace = PtrToUtf8(backtracePtr);
            Services.CrashTelemetryService.RecordCrash("WebViewPipelineCrash", $"{reason}\n{backtrace}");
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnFindResult(nint webview, int identifier, int count, nint result, nint userData)
        {
            if (TryGetHost(userData, out var host))
            {
                host._dispatcher.TryEnqueue(() => host.FindResultReceived?.Invoke(host._session, identifier, count));
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnShowAlertDialog(nint webview, byte* messagePtr, nint callback, nint userData)
        {
            string message = PtrToUtf8(messagePtr);
            if (TryGetHost(userData, out var host))
            {
                host._dispatcher.TryEnqueue(() => host.AlertRequested?.Invoke(host._session, message));
            }
        }

        private static bool TryGetHost(nint userData, out ServoViewportHost host)
        {
            if (userData != nint.Zero && GCHandle.FromIntPtr(userData).Target is ServoViewportHost h && !h._disposed)
            {
                host = h;
                return true;
            }
            host = null!;
            return false;
        }

        private static string PtrToUtf8(byte* ptr)
        {
            if (ptr == null) return string.Empty;
            int len = 0;
            while (ptr[len] != 0) len++;
            return Encoding.UTF8.GetString(ptr, len);
        }

        private static void EnsureWindowClassRegistered()
        {
            if (s_classRegistered) return;
            var wc = new WNDCLASSEXW
            {
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
                style = 0x0003,
                lpfnWndProc = Marshal.GetFunctionPointerForDelegate(s_wndProc),
                hInstance = GetModuleHandleW(null),
                lpszClassName = WindowClassName,
            };
            RegisterClassExW(ref wc);
            s_classRegistered = true;
        }

        private static nint StaticWndProc(nint hwnd, uint msg, nint wParam, nint lParam)
        {
            if (s_hosts.TryGetValue(hwnd, out var host))
            {
                return host.HandleWndProc(hwnd, msg, wParam, lParam);
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            if (_webview != nint.Zero)
            {
                ServoCapi.servo_webview_free(_webview);
                _webview = nint.Zero;
            }
            if (_hwnd != nint.Zero)
            {
                s_hosts.Remove(_hwnd);
                DestroyWindow(_hwnd);
                _hwnd = nint.Zero;
            }
            if (_selfHandle.IsAllocated)
            {
                _selfHandle.Free();
            }
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WNDCLASSEXW
        {
            public uint cbSize;
            public uint style;
            public nint lpfnWndProc;
            public int cbClsExtra;
            public int cbWndExtra;
            public nint hInstance;
            public nint hIcon;
            public nint hCursor;
            public nint hbrBackground;
            public string? lpszMenuName;
            public string lpszClassName;
            public nint hIconSm;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PAINTSTRUCT
        {
            public nint hdc;
            public int fErase;
            public int left, top, right, bottom;
            public int fRestore;
            public int fIncUpdate;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
            public byte[] rgbReserved;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct TRACKMOUSEEVENT
        {
            public uint cbSize;
            public uint dwFlags;
            public nint hwndTrack;
            public uint dwHoverTime;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern nint GetModuleHandleW(string? lpModuleName);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern ushort RegisterClassExW(ref WNDCLASSEXW lpwcx);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern nint CreateWindowExW(
            uint dwExStyle, string lpClassName, string lpWindowName, uint dwStyle,
            int x, int y, int nWidth, int nHeight, nint hWndParent, nint hMenu, nint hInstance, nint lpParam);

        [DllImport("user32.dll")]
        private static extern nint DefWindowProcW(nint hWnd, uint msg, nint wParam, nint lParam);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyWindow(nint hWnd);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetWindowPos(nint hWnd, nint hWndInsertAfter, int x, int y, int cx, int cy, uint uFlags);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool InvalidateRect(nint hWnd, nint lpRect, [MarshalAs(UnmanagedType.Bool)] bool bErase);

        [DllImport("user32.dll")]
        private static extern nint BeginPaint(nint hWnd, out PAINTSTRUCT lpPaint);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EndPaint(nint hWnd, ref PAINTSTRUCT lpPaint);

        [DllImport("user32.dll")]
        private static extern nint SetFocus(nint hWnd);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool TrackMouseEvent(ref TRACKMOUSEEVENT lpEventTrack);
    }
}
