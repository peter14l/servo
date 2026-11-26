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
        private static nint s_servo = nint.Zero;
        private static bool s_spinning;
        private MainWindow? _window;

        public App()
        {
            InitializeComponent();
        }

        public static nint ServoHandle => s_servo;

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

        [UnmanagedCallersOnly(CallConventions = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static void OnServoWake()
        {
            s_dispatcherQueue?.TryEnqueue(SpinEventLoopOnce);
        }

        protected override unsafe void OnLaunched(LaunchActivatedEventArgs args)
        {
            s_dispatcherQueue = DispatcherQueue.GetForCurrentThread();

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
                    ServoCapi.servo_builder_set_options(builder, options);
                    ServoCapi.servo_options_free(options);
                }

                nint prefs = ServoCapi.servo_preferences_create();
                if (prefs != nint.Zero)
                {
                    // Guardrails from GEMINI.md: cap SpiderMonkey heap <= 4096 MB and
                    // keep session history compact for heavy SPAs.
                    ServoCapi.servo_preferences_set_i64(prefs, "js_mem_max", 2048);
                    ServoCapi.servo_preferences_set_i64(prefs, "session_history_max_length", 5);
                    ServoCapi.servo_builder_set_preferences(builder, prefs);
                    ServoCapi.servo_preferences_free(prefs);
                }

                var waker = new ServoCapi.ServoEventLoopWaker
                {
                    StructSize = (uint)Marshal.SizeOf<ServoCapi.ServoEventLoopWaker>(),
                    Reserved = 0,
                    WakeCallback = &OnServoWake,
                };
                ServoCapi.servo_builder_set_event_loop_waker(builder, waker);
                s_servo = ServoCapi.servo_builder_build(builder);
            }

            _window = new MainWindow();
            _window.Activate();
        }
    }
}
