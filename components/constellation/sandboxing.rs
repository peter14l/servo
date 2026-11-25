/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::ffi::OsStr;
use std::{env, process};

#[cfg(any(
    target_os = "macos",
    all(
        not(target_os = "windows"),
        not(target_os = "ios"),
        not(target_os = "android"),
        not(target_env = "ohos"),
        not(target_arch = "arm"),
        not(target_arch = "aarch64"),
        not(target_arch = "riscv32"),
        not(target_arch = "riscv64")
    )
))]
use gaol::profile::{Operation, PathPattern, Profile};
use serde::{Deserialize, Serialize};
use servo_config::opts::Opts;
use servo_config::prefs::Preferences;

use crate::event_loop::NewScriptEventLoopProcessInfo;
use crate::serviceworker::ServiceWorkerUnprivilegedContent;

#[derive(Deserialize, Serialize)]
#[expect(clippy::large_enum_variant)]
pub enum UnprivilegedContent {
    ScriptEventLoop(NewScriptEventLoopProcessInfo),
    ServiceWorker(ServiceWorkerUnprivilegedContent),
}

impl UnprivilegedContent {
    pub fn opts(&self) -> Opts {
        match self {
            UnprivilegedContent::ScriptEventLoop(content) => content.opts.clone(),
            UnprivilegedContent::ServiceWorker(content) => content.opts.clone(),
        }
    }

    pub fn prefs(&self) -> &Preferences {
        match self {
            UnprivilegedContent::ScriptEventLoop(content) => &content.prefs,
            UnprivilegedContent::ServiceWorker(content) => &content.prefs,
        }
    }
}

/// Our content process sandbox profile on Mac. As restrictive as possible.
#[cfg(target_os = "macos")]
pub fn content_process_sandbox_profile() -> Profile {
    use std::path::PathBuf;

    use embedder_traits::resources;
    use gaol::platform;

    let mut operations = vec![
        Operation::FileReadAll(PathPattern::Literal(PathBuf::from("/dev/urandom"))),
        Operation::FileReadAll(PathPattern::Subpath(PathBuf::from("/Library/Fonts"))),
        Operation::FileReadAll(PathPattern::Subpath(PathBuf::from("/System/Library/Fonts"))),
        Operation::FileReadAll(PathPattern::Subpath(PathBuf::from(
            "/System/Library/Frameworks/ApplicationServices.framework",
        ))),
        Operation::FileReadAll(PathPattern::Subpath(PathBuf::from(
            "/System/Library/Frameworks/CoreGraphics.framework",
        ))),
        Operation::FileReadMetadata(PathPattern::Literal(PathBuf::from("/"))),
        Operation::FileReadMetadata(PathPattern::Literal(PathBuf::from("/Library"))),
        Operation::FileReadMetadata(PathPattern::Literal(PathBuf::from("/System"))),
        Operation::FileReadMetadata(PathPattern::Literal(PathBuf::from("/etc"))),
        Operation::SystemInfoRead,
        Operation::PlatformSpecific(platform::macos::Operation::MachLookup(
            b"com.apple.FontServer".to_vec(),
        )),
    ];

    operations.extend(
        resources::sandbox_access_files()
            .into_iter()
            .map(|p| Operation::FileReadAll(PathPattern::Literal(p))),
    );
    operations.extend(
        resources::sandbox_access_files_dirs()
            .into_iter()
            .map(|p| Operation::FileReadAll(PathPattern::Subpath(p))),
    );

    Profile::new(operations).expect("Failed to create sandbox profile!")
}

/// Our content process sandbox profile on Linux. As restrictive as possible.
#[cfg(all(
    not(target_os = "macos"),
    not(target_os = "windows"),
    not(target_os = "ios"),
    not(target_os = "android"),
    not(target_env = "ohos"),
    not(target_arch = "arm"),
    not(target_arch = "aarch64"),
    not(target_arch = "riscv32"),
    not(target_arch = "riscv64")
))]
pub fn content_process_sandbox_profile() -> Profile {
    use std::path::PathBuf;

    use embedder_traits::resources;

    let mut operations = vec![Operation::FileReadAll(PathPattern::Literal(PathBuf::from(
        "/dev/urandom",
    )))];

    operations.extend(
        resources::sandbox_access_files()
            .into_iter()
            .map(|p| Operation::FileReadAll(PathPattern::Literal(p))),
    );
    operations.extend(
        resources::sandbox_access_files_dirs()
            .into_iter()
            .map(|p| Operation::FileReadAll(PathPattern::Subpath(p))),
    );

    Profile::new(operations).expect("Failed to create sandbox profile!")
}

#[cfg(any(
    target_os = "windows",
    target_os = "ios",
    target_os = "android",
    target_env = "ohos",
    target_arch = "arm",
    target_arch = "riscv32",
    target_arch = "riscv64",

    // exclude apple arm devices
    all(target_arch = "aarch64", not(target_os = "macos"))
))]
pub fn content_process_sandbox_profile() {
    // Sandboxed multiprocess is not implemented on this platform. This used to
    // hard-exit the process; it is now a no-op so callers can fall back to an
    // unsandboxed spawn (e.g. the Windows Job Object containment in this module).
}

/// Windows Job Object containment for unsandboxed content processes.
///
/// The browser creates a job with `JOB_OBJECT_LIMIT_ACTIVE_PROCESS = 1` and
/// `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, then assigns the freshly spawned
/// content process to it. The job handle is intentionally kept open for the
/// lifetime of the browser so that the content process is terminated if the
/// browser process exits.
#[cfg(target_os = "windows")]
mod windows_job {
    use std::ffi::c_void;
    use std::os::windows::io::AsRawHandle;

    const JOB_OBJECT_LIMIT_ACTIVE_PROCESS: u32 = 0x00000008;
    const JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: u32 = 0x00002000;
    const JOB_OBJECT_EXTENDED_LIMIT_INFORMATION: i32 = 9;

    #[repr(C)]
    struct IoCounters {
        read_operation_count: u64,
        write_operation_count: u64,
        other_operation_count: u64,
        read_transfer_count: u64,
        write_transfer_count: u64,
        other_transfer_count: u64,
    }

    #[repr(C)]
    struct JobobjectBasicLimitInformation {
        per_process_user_time_limit: i64,
        per_job_user_time_limit: i64,
        limit_flags: u32,
        minimum_working_set_size: usize,
        maximum_working_set_size: usize,
        active_process_limit: u32,
        affinity: usize,
        priority_class: u32,
        scheduling_class: u32,
    }

    #[repr(C)]
    struct JobobjectExtendedLimitInformation {
        basic_limit_information: JobobjectBasicLimitInformation,
        io_info: IoCounters,
        process_memory_limit: usize,
        job_memory_limit: usize,
        peak_process_memory_used: usize,
        peak_job_memory_used: usize,
    }

    unsafe extern "system" {
        fn CreateJobObjectW(lp_job_attributes: *const c_void, lp_name: *const u16) -> *mut c_void;
        fn SetInformationJobObject(
            h_job: *mut c_void,
            job_object_info_class: i32,
            lp_job_object_info: *const c_void,
            cb_job_object_info_length: u32,
        ) -> i32;
        fn AssignProcessToJobObject(h_job: *mut c_void, h_process: *mut c_void) -> i32;
        fn CloseHandle(h_object: *mut c_void) -> i32;
    }

    /// Assign `child` to a dedicated Job Object that prevents it from spawning
    /// further processes and terminates it if the browser process exits.
    pub fn constrain(child: &std::process::Child) {
        unsafe {
            let job = CreateJobObjectW(std::ptr::null(), std::ptr::null());
            if job.is_null() {
                log::warn!("Windows sandbox: failed to create Job Object for content process");
                return;
            }

            let mut info: JobobjectExtendedLimitInformation = std::mem::zeroed();
            info.basic_limit_information.limit_flags =
                JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            info.basic_limit_information.active_process_limit = 1;

            if SetInformationJobObject(
                job,
                JOB_OBJECT_EXTENDED_LIMIT_INFORMATION,
                &info as *const _ as *const c_void,
                std::mem::size_of::<JobobjectExtendedLimitInformation>() as u32,
            ) == 0
            {
                log::warn!("Windows sandbox: failed to set Job Object limits");
                CloseHandle(job);
                return;
            }

            if AssignProcessToJobObject(job, child.as_raw_handle()) == 0 {
                log::warn!("Windows sandbox: failed to assign content process to Job Object");
                CloseHandle(job);
                return;
            }
            // The job handle is deliberately leaked for the lifetime of the
            // browser: closing it would release the child from the job and,
            // with KILL_ON_JOB_CLOSE set, immediately terminate it.
        }
    }
}

#[cfg(any(
    target_os = "windows",
    target_os = "android",
    target_env = "ohos",
    target_arch = "arm",
    target_arch = "aarch64",
    target_arch = "riscv32",
    target_arch = "riscv64"
))]
pub fn spawn_multiprocess(
    content: UnprivilegedContent,
) -> Result<crate::process_manager::Process, ipc_channel::IpcError> {
    use ipc_channel::ipc::{IpcOneShotServer, IpcSender};
    // Note that this function can panic, due to process creation,
    // avoiding this panic would require a mechanism for dealing
    // with low-resource scenarios.
    let (server, token) = IpcOneShotServer::<IpcSender<UnprivilegedContent>>::new()
        .expect("Failed to create IPC one-shot server.");

    let path_to_self = env::current_exe().expect("Failed to get current executor.");
    let mut child_process = process::Command::new(path_to_self);
    setup_common(&mut child_process, token);

    #[allow(clippy::zombie_processes)]
    let child = child_process
        .spawn()
        .expect("Failed to start unsandboxed child process!");

    // On Windows, contain the freshly spawned content process in a Job Object
    // that blocks further process creation and kills it if the browser exits.
    #[cfg(target_os = "windows")]
    windows_job::constrain(&child);

    let (_receiver, sender) = server.accept().expect("Server failed to accept.");
    sender.send(content)?;

    Ok(crate::process_manager::Process::Unsandboxed(child))
}

#[cfg(all(
    not(target_os = "windows"),
    not(target_os = "ios"),
    not(target_os = "android"),
    not(target_env = "ohos"),
    not(target_arch = "arm"),
    not(target_arch = "aarch64"),
    not(target_arch = "riscv32"),
    not(target_arch = "riscv64")
))]
pub fn spawn_multiprocess(
    content: UnprivilegedContent,
) -> Result<crate::process_manager::Process, ipc_channel::IpcError> {
    use gaol::sandbox::{self, Sandbox, SandboxMethods};
    use ipc_channel::ipc::{IpcOneShotServer, IpcSender};

    // TODO: Move this impl out of the function. It is only currently here to avoid
    // duplicating the feature flagging.
    #[allow(non_local_definitions)]
    impl CommandMethods for gaol::sandbox::Command {
        fn arg<T>(&mut self, arg: T)
        where
            T: AsRef<OsStr>,
        {
            self.arg(arg);
        }

        fn env<T, U>(&mut self, key: T, val: U)
        where
            T: AsRef<OsStr>,
            U: AsRef<OsStr>,
        {
            self.env(key, val);
        }
    }

    // Note that this function can panic, due to process creation,
    // avoiding this panic would require a mechanism for dealing
    // with low-resource scenarios.
    let (server, token) = IpcOneShotServer::<IpcSender<UnprivilegedContent>>::new()
        .expect("Failed to create IPC one-shot server.");

    // If there is a sandbox, use the `gaol` API to create the child process.
    let process = if content.opts().sandbox {
        let mut command = sandbox::Command::me().expect("Failed to get current sandbox.");
        setup_common(&mut command, token);

        let profile = content_process_sandbox_profile();
        crate::process_manager::Process::Sandboxed(
            Sandbox::new(profile)
                .start(&mut command)
                .expect("Failed to start sandboxed child process!")
                .pid as u32,
        )
    } else {
        let path_to_self = env::current_exe().expect("Failed to get current executor.");
        let mut child_process = process::Command::new(path_to_self);
        setup_common(&mut child_process, token);

        crate::process_manager::Process::Unsandboxed(
            child_process
                .spawn()
                .expect("Failed to start unsandboxed child process!"),
        )
    };

    let (_receiver, sender) = server.accept().expect("Server failed to accept.");
    sender.send(content)?;

    Ok(process)
}

#[cfg(target_os = "ios")]
pub fn spawn_multiprocess(_content: UnprivilegedContent) -> Result<Process, Error> {
    log::error!("Multiprocess is not supported on iOS.");
    process::exit(1);
}

fn setup_common<C: CommandMethods>(command: &mut C, token: String) {
    C::arg(command, "--content-process");
    C::arg(command, token);

    if let Ok(value) = env::var("RUST_BACKTRACE") {
        C::env(command, "RUST_BACKTRACE", value);
    }

    if let Ok(value) = env::var("RUST_LOG") {
        C::env(command, "RUST_LOG", value);
    }
}

/// A trait to unify commands launched as multiprocess with or without a sandbox.
trait CommandMethods {
    /// A command line argument.
    fn arg<T>(&mut self, arg: T)
    where
        T: AsRef<OsStr>;

    /// An environment variable.
    fn env<T, U>(&mut self, key: T, val: U)
    where
        T: AsRef<OsStr>,
        U: AsRef<OsStr>;
}

impl CommandMethods for process::Command {
    fn arg<T>(&mut self, arg: T)
    where
        T: AsRef<OsStr>,
    {
        self.arg(arg);
    }

    fn env<T, U>(&mut self, key: T, val: U)
    where
        T: AsRef<OsStr>,
        U: AsRef<OsStr>,
    {
        self.env(key, val);
    }
}
