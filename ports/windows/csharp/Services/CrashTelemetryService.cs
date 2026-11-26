// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

namespace ServoBrowser.WinUI.Services
{
    /// <summary>
    /// Opt-in local crash minidump and diagnostics recorder (REL-7).
    /// Disabled by default; never transmits data over the network.
    /// </summary>
    public static class CrashTelemetryService
    {
        private const int MaxRetainedDumps = 5;
        private static readonly string DumpDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ServoBrowser",
            "CrashDumps");

        public static bool IsOptedIn { get; set; }

        public static void Initialize()
        {
            AppDomain.CurrentDomain.UnhandledException += (_, args) =>
            {
                if (!IsOptedIn) return;
                RecordCrash("UnhandledException", args.ExceptionObject?.ToString() ?? "Unknown");
            };
        }

        public static void RecordCrash(string kind, string details)
        {
            if (!IsOptedIn) return;
            try
            {
                Directory.CreateDirectory(DumpDir);
                PruneOldDumps();

                string stamp = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss");
                string logPath = Path.Combine(DumpDir, $"crash-{stamp}.log");
                File.WriteAllText(logPath, $"[{DateTime.UtcNow:O}] {kind}\n{details}\n");

                string dumpPath = Path.Combine(DumpDir, $"crash-{stamp}.dmp");
                using var fs = new FileStream(dumpPath, FileMode.Create, FileAccess.Write, FileShare.None);
                using var proc = Process.GetCurrentProcess();
                MiniDumpWriteDump(
                    proc.Handle,
                    (uint)proc.Id,
                    fs.SafeFileHandle.DangerousGetHandle(),
                    0x00000000, // MiniDumpNormal
                    nint.Zero,
                    nint.Zero,
                    nint.Zero);
            }
            catch
            {
                // Crash recording must never throw.
            }
        }

        private static void PruneOldDumps()
        {
            var files = new DirectoryInfo(DumpDir)
                .GetFiles("crash-*.dmp")
                .OrderByDescending(f => f.CreationTimeUtc)
                .Skip(MaxRetainedDumps - 1)
                .ToList();
            foreach (var f in files)
            {
                f.Delete();
            }
        }

        [DllImport("dbghelp.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool MiniDumpWriteDump(
            nint hProcess,
            uint processId,
            nint hFile,
            uint dumpType,
            nint exceptionParam,
            nint userStreamParam,
            nint callbackParam);
    }
}
