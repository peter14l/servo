/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::path::PathBuf;

fn minidump_path() -> PathBuf {
    let mut path = std::env::temp_dir();
    path.push(format!(
        "servo-minidump-{}-{}.dmp",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0)
    ));
    path
}

#[cfg(not(any(
    target_os = "macos",
    target_os = "linux",
    target_os = "android",
    target_os = "windows"
)))]
pub fn install() {}

#[cfg(target_os = "windows")]
pub fn install() {
    use std::io::Write;
    use std::sync::atomic;
    use std::thread;

    unsafe extern "system" fn unhandled_exception_filter(info: *mut std::ffi::c_void) -> i32 {
        static BEEN_HERE: atomic::AtomicBool = atomic::AtomicBool::new(false);
        if !BEEN_HERE.swap(true, atomic::Ordering::SeqCst) {
            let path = minidump_path();
            let result = write_minidump_windows(info, &path);
            let stderr = std::io::stderr();
            let mut stderr = stderr.lock();
            let _ = write!(
                &mut stderr,
                "Windows Unhandled Exception in process {}",
                std::process::id()
            );
            if let Some(name) = thread::current().name() {
                let _ = write!(&mut stderr, " in thread \"{}\"", name);
            }
            let _ = writeln!(&mut stderr);
            match result {
                Ok(()) => {
                    let _ = writeln!(&mut stderr, "Minidump written to: {}", path.display());
                },
                Err(error) => {
                    let _ = writeln!(&mut stderr, "Failed to write minidump: {error}");
                },
            }
            let faulting_address = exception_address(info);
            if faulting_address != 0 {
                let _ = writeln!(
                    &mut stderr,
                    "Exception address: {}",
                    crate::backtrace::symbolicate_address(faulting_address)
                );
            }
            let _ = crate::backtrace::print(&mut stderr);
        }
        // EXCEPTION_CONTINUE_SEARCH (0)
        0
    }

    unsafe extern "system" {
        fn SetUnhandledExceptionFilter(
            filter: unsafe extern "system" fn(*mut std::ffi::c_void) -> i32,
        ) -> *mut std::ffi::c_void;
    }

    unsafe {
        SetUnhandledExceptionFilter(unhandled_exception_filter);
    }
}

/// Extracts the faulting instruction address from a pointer to an
/// `EXCEPTION_POINTERS` structure, as passed to an unhandled exception filter.
/// Returns 0 when no address is available.
#[cfg(target_os = "windows")]
fn exception_address(info: *mut std::ffi::c_void) -> usize {
    #[repr(C)]
    #[allow(non_snake_case)]
    #[allow(dead_code)] // mirror of the Win32 layout; not all fields are read
    struct EXCEPTION_RECORD {
        ExceptionCode: u32,
        ExceptionFlags: u32,
        ExceptionRecord: *mut EXCEPTION_RECORD,
        ExceptionAddress: *mut std::ffi::c_void,
        NumberParameters: u32,
    }

    #[repr(C)]
    #[allow(non_snake_case)]
    #[allow(dead_code)] // mirror of the Win32 layout; not all fields are read
    struct EXCEPTION_POINTERS {
        ExceptionRecord: *mut EXCEPTION_RECORD,
        ContextRecord: *mut std::ffi::c_void,
    }

    if info.is_null() {
        return 0;
    }
    let pointers = unsafe { &*(info as *const EXCEPTION_POINTERS) };
    if pointers.ExceptionRecord.is_null() {
        return 0;
    }
    unsafe { (*pointers.ExceptionRecord).ExceptionAddress as usize }
}

#[cfg(target_os = "windows")]
unsafe fn write_minidump_windows(
    exception_pointers: *mut std::ffi::c_void,
    path: &std::path::Path,
) -> std::io::Result<()> {
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;

    type HANDLE = *mut std::ffi::c_void;
    type BOOL = i32;
    type DWORD = u32;

    #[repr(C)]
    #[allow(non_snake_case)]
    struct MINIDUMP_EXCEPTION_INFORMATION {
        ThreadId: DWORD,
        ExceptionPointers: *mut std::ffi::c_void,
        ClientPointers: BOOL,
    }

    #[link(name = "Dbghelp")]
    unsafe extern "system" {
        fn MiniDumpWriteDump(
            hProcess: HANDLE,
            ProcessId: DWORD,
            hFile: HANDLE,
            DumpType: DWORD,
            ExceptionParam: *const MINIDUMP_EXCEPTION_INFORMATION,
            UserStreamParam: *mut std::ffi::c_void,
            CallbackParam: *mut std::ffi::c_void,
        ) -> BOOL;
    }
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetCurrentProcess() -> HANDLE;
        fn GetCurrentProcessId() -> DWORD;
        fn GetCurrentThreadId() -> DWORD;
        fn CreateFileW(
            lpFileName: *const u16,
            dwDesiredAccess: DWORD,
            dwShareMode: DWORD,
            lpSecurityAttributes: *mut std::ffi::c_void,
            dwCreationDisposition: DWORD,
            dwFlagsAndAttributes: DWORD,
            hTemplateFile: HANDLE,
        ) -> HANDLE;
        fn CloseHandle(hObject: HANDLE) -> BOOL;
    }

    const GENERIC_WRITE: DWORD = 0x4000_0000;
    const FILE_SHARE_READ: DWORD = 0x0000_0001;
    const CREATE_ALWAYS: DWORD = 2;
    const FILE_ATTRIBUTE_NORMAL: DWORD = 0x0000_0080;
    const INVALID_HANDLE_VALUE: HANDLE = !0usize as HANDLE;
    // MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo
    const MINIDUMP_TYPE: DWORD = 0x0000_0001 | 0x0000_0004 | 0x0000_0800 | 0x0000_1000;

    let wide: Vec<u16> = OsStr::new(path)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let file = unsafe {
        CreateFileW(
            wide.as_ptr(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            std::ptr::null_mut(),
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            std::ptr::null_mut(),
        )
    };
    if file == INVALID_HANDLE_VALUE {
        return Err(std::io::Error::last_os_error());
    }

    let exception_info = MINIDUMP_EXCEPTION_INFORMATION {
        ThreadId: unsafe { GetCurrentThreadId() },
        ExceptionPointers: exception_pointers,
        ClientPointers: 0,
    };
    let exception_param = if exception_pointers.is_null() {
        std::ptr::null()
    } else {
        &exception_info as *const _
    };

    let ok = unsafe {
        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            file,
            MINIDUMP_TYPE,
            exception_param,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
        )
    };
    unsafe {
        CloseHandle(file);
    }
    if ok == 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok(())
    }
}

#[cfg(any(target_os = "macos", target_os = "linux"))]
pub fn install() {
    use std::io::Write;
    use std::sync::atomic;
    use std::thread;

    use libc::siginfo_t;

    use crate::backtrace;

    fn handler(siginfo: &siginfo_t) {
        // Only print crash message and backtrace the first time, to avoid
        // infinite recursion if the printing causes another signal.
        static BEEN_HERE_BEFORE: atomic::AtomicBool = atomic::AtomicBool::new(false);
        if !BEEN_HERE_BEFORE.swap(true, atomic::Ordering::SeqCst) {
            let path = minidump_path();
            if let Err(error) = write_minidump_unix(&path) {
                let stderr = std::io::stderr();
                let mut stderr = stderr.lock();
                let _ = writeln!(&mut stderr, "Failed to write minidump: {error}");
            }
            // stderr is unbuffered, so we won’t lose output if we crash later
            // in this handler, and the std::io::stderr() call never allocates.
            // std::io::stdout() allocates the first time it’s called, which in
            // practice will often segfault (see below).
            let stderr = std::io::stderr();
            let mut stderr = stderr.lock();
            let _ = write!(&mut stderr, "Caught signal {}", siginfo.si_signo);
            if let Some(name) = thread::current().name() {
                let _ = write!(&mut stderr, " in thread \"{}\"", name);
            }
            let _ = writeln!(&mut stderr);
            let _ = writeln!(&mut stderr, "Minidump written to: {}", path.display());

            // This call always allocates, which in practice will segfault if
            // we’re handling a non-main-thread (e.g. layout) segfault. Strictly
            // speaking in POSIX terms, this is also undefined behaviour.
            let _ = backtrace::print(&mut stderr);
        }

        // Outside the BEEN_HERE_BEFORE check, we must only call functions we
        // know to be “async-signal-safe”, which includes sigaction(), raise(),
        // and _exit(), but generally doesn’t include anything that allocates.
        // https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03_03
        raise_signal_or_exit_with_error(siginfo.si_signo);
    }

    unsafe {
        signal_hook_registry::register_unchecked(libc::SIGSEGV, handler)
            .expect("Could not register SIGSEGV handler");
        signal_hook_registry::register_unchecked(libc::SIGILL, handler)
            .expect("Could not register SIGILL handler");
        signal_hook_registry::register_unchecked(libc::SIGIOT, handler)
            .expect("Could not register SIGIOT handler");
        signal_hook_registry::register_unchecked(libc::SIGBUS, handler)
            .expect("Could not register SIGBUS handler");
    }
}

#[cfg(any(target_os = "macos", target_os = "linux"))]
fn write_minidump_unix(path: &std::path::Path) -> std::io::Result<()> {
    #[cfg(target_os = "linux")]
    {
        use minidump_writer::minidump_writer::MinidumpWriterConfig;
        let mut file = std::fs::File::create(path)?;
        // The blamed thread is used both as the crashing-thread id in the
        // exception stream and for the `/proc/<tid>/…` process info streams,
        // so it must be the TID of the thread that received the signal.
        let tid = unsafe { libc::syscall(libc::SYS_gettid) } as i32;
        MinidumpWriterConfig::new(std::process::id() as i32, tid)
            .write(&mut file)
            .map_err(std::io::Error::other)?;
        return Ok(());
    }
    #[cfg(target_os = "macos")]
    {
        use minidump_writer::minidump_writer::MinidumpWriter;
        let mut file = std::fs::File::create(path)?;
        let mut writer = MinidumpWriter::new(None, None);
        writer.dump(&mut file).map_err(std::io::Error::other)?;
        return Ok(());
    }
    #[allow(unreachable_code)]
    Ok(())
}

#[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "android")))]
pub(crate) fn raise_signal_or_exit_with_error(_signal: i32) {
    std::process::exit(1);
}

#[cfg(any(target_os = "macos", target_os = "linux"))]
pub(crate) fn raise_signal_or_exit_with_error(signal: i32) {
    unsafe {
        // Reset the signal to the default action, and reraise the signal.
        // Unlike libc::_exit(sig), which terminates the process normally,
        // this terminates abnormally just like an uncaught signal, allowing
        // mach (or your shell) to distinguish it from an ordinary exit, and
        // allows your kernel to make a core dump if configured to do so.
        let mut action: libc::sigaction = std::mem::zeroed();
        action.sa_sigaction = libc::SIG_DFL;
        libc::sigaction(signal, &action, std::ptr::null_mut());
        libc::raise(signal);
    }
}
