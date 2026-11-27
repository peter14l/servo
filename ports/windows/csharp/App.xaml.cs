// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Servo.Embedding;

namespace ServoBrowser.WinUI
{
    public partial class App : Application
    {
        private static DispatcherQueue? s_dispatcherQueue;
        private static DispatcherQueueTimer? s_pumpTimer;
        private static nint s_servo = nint.Zero;
        private static bool s_spinning;
        private MainWindow? _window;

        public App()
        {
            InitializeComponent();
        }

        public static nint ServoHandle => s_servo;

        public static void StopEventLoopAndFree()
        {
            s_pumpTimer?.Stop();
            if (s_servo != nint.Zero)
            {
                ServoCapi.servo_free(s_servo);
                s_servo = nint.Zero;
            }
        }

        public static void SpinEventLoopOnce()
        {
            if (s_servo == nint.Zero || s_spinning)
            {
                return;
            }

            s_spinning = true;
            try
            {
                ServoCapi.servo_spin_event_loop(s_servo);
            }
            finally
            {
                s_spinning = false;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnServoWake()
        {
            s_dispatcherQueue?.TryEnqueue(SpinEventLoopOnce);
        }

        protected override unsafe void OnLaunched(LaunchActivatedEventArgs args)
        {
            s_dispatcherQueue = DispatcherQueue.GetForCurrentThread();
            Services.CrashTelemetryService.Initialize();

            nint builder = ServoCapi.servo_builder_create();
            if (builder != nint.Zero)
            {
                nint options = ServoCapi.servo_options_create();
                if (options != nint.Zero)
                {
                    string configDir = Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                        "ServoBrowser");
                    Directory.CreateDirectory(configDir);
                    ServoCapi.servo_options_set_config_dir(options, configDir);
                    if (ServoCapi.servo_builder_set_options(builder, options) != 0)
                    {
                        ServoCapi.servo_options_free(options);
                    }
                    // Ownership transferred on success.
                    options = nint.Zero;
                }

                nint prefs = ServoCapi.servo_preferences_create();
                if (prefs != nint.Zero)
                {
                    // Guardrails from GEMINI.md: cap SpiderMonkey heap <= 4096 MB and
                    // keep session history compact for heavy SPAs.
                    ServoCapi.servo_preferences_set_i64(prefs, "js_mem_max", 2048);
                    ServoCapi.servo_preferences_set_i64(prefs, "session_history_max_length", 5);

                    // Modern web platform & animation parity (matching ports/windows/src/main.cpp).
                    ServoCapi.servo_preferences_set_bool(prefs, "network_use_webpki_roots", true);
                    ServoCapi.servo_preferences_set_i64(prefs, "network_connection_timeout", 30);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_indexeddb_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_storage_manager_api_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_cookiestore_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_crypto_subtle_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_fontface_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_async_clipboard_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_offscreen_canvas_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_permissions_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_webgl2_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_visual_viewport_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_adoptedstylesheet_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_composition_event_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_credential_management_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_web_animations_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_entries_api_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_wakelock_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_webrtc_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_webrtc_transceiver_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_notification_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "dom_gamepad_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "layout_container_queries_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "layout_columns_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "layout_variable_fonts_enabled", true);
                    ServoCapi.servo_preferences_set_bool(prefs, "layout_writing_mode_enabled", true);

                    if (ServoCapi.servo_builder_set_preferences(builder, prefs) != 0)
                    {
                        ServoCapi.servo_preferences_free(prefs);
                    }
                    // Ownership transferred on success.
                    prefs = nint.Zero;
                }

                var waker = new ServoCapi.ServoEventLoopWaker
                {
                    StructSize = (uint)Marshal.SizeOf<ServoCapi.ServoEventLoopWaker>(),
                    Reserved = 0,
                    WakeCallback = &OnServoWake,
                };
                ServoCapi.servo_builder_set_event_loop_waker(builder, waker);
                s_servo = ServoCapi.servo_builder_build(builder);
                Services.CrashTelemetryService.Log($"servo_builder_build => 0x{s_servo:X}");
            }

            s_pumpTimer = s_dispatcherQueue.CreateTimer();
            s_pumpTimer.Interval = TimeSpan.FromMilliseconds(33);
            s_pumpTimer.Tick += (_, _) => SpinEventLoopOnce();
            s_pumpTimer.Start();

            _window = new MainWindow();
            _window.Activate();
        }
    }
}
