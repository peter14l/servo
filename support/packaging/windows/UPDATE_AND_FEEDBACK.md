# Update, diagnostics and feedback (Windows browser)

Status: **scaffolding only** · Windows CI package · Companion: [`WINUI_UIUX_COMPLETION_CHECKLIST.md`](../../../docs/embedding/WINUI_UIUX_COMPLETION_CHECKLIST.md)

> [!IMPORTANT]
> There is **no auto-updater** in this package. Nothing checks for releases, downloads
> binaries, or restarts the app. The update path below is manual, and the diagnostics
> folder created by `Install-ServoBrowser.ps1` contains **templates only**. Do not
> describe any of this as working update or crash-reporting UX.

## 1. Manual update path

1. Find the CI run that produced your build of the **Native Windows Browser Build**
   workflow (`windows-browser.yml`). Release/GitHub Releases are not wired to this
   workflow yet.
2. Download the `servo-windows-browser-setup` artifact. It contains
   `servo-windows-browser-setup.zip` and `servo-windows-browser-manifest.json`.
3. **Verify the artifact before running it.** The manifest is machine-readable:

   ```json
   {
     "schema": "servo-windows-browser-artifact/v1",
     "artifact": "servo-windows-browser-setup.zip",
     "version": "0.7.0",
     "commit": "<git sha>",
     "sha256": "<lowercase hex digest>",
     "builtAtUtc": "<ISO 8601 timestamp>"
   }
   ```

   On Windows PowerShell:

   ```powershell
   $m = Get-Content .\servo-windows-browser-manifest.json -Raw | ConvertFrom-Json
   (Get-FileHash .\servo-windows-browser-setup.zip -Algorithm SHA256).Hash.ToLowerInvariant() -eq $m.sha256
   ```

   A `False` result means the download is incomplete or modified; do not install it.
4. Extract the ZIP and run the installer script for your package
   (`Install-ServoBrowser.ps1` in `support/packaging/windows`, or the installer shipped
   in `ports/windows/packaging`). Reinstalling over an existing copy is supported;
   user profile data under `%LOCALAPPDATA%\Servo Browser` is left in place.
5. If you want a clean removal first, run the matching uninstall script. Uninstalling
   with either script removes shortcuts and registered capability keys; it does **not**
   remove profile/history data or the dev-only trusted certificate.

The CI package is **self-signed**. Production signing, MSIX/Store-ready packaging, a
release channel and rollback are deployment decisions that have not been made here.

## 2. Where logs and diagnostics live

Log files are written next to the executable, so the path follows whichever installer
you used:

| File | Written by | Purpose |
| --- | --- | --- |
| `servo_engine.log` | `browser_runtime.cpp::InitLogging` | Engine stdout/stderr; verbosity follows `RUST_LOG` (defaults to `warn`). |
| `servo_browser.log` | `browser_runtime.cpp::LogDebug` | Shell debug trace. |
| `servo_crash.log` | `browser_runtime.cpp::CrashHandler` | Unhandled-exception code, address and module info. |
| `%LOCALAPPDATA%\Servo Browser\crash.html` | `browser_data.cpp` | Generated crash-recovery page for the affected tab. |
| `%LOCALAPPDATA%\Servo Browser\` | `browser_data.cpp::GetConfigDirectory` | Profile data: history, bookmarks, settings, web storage/IndexedDB. |

The install-time helper creates `<InstallDir>\Diagnostics\feedback-template.txt`. It is
a blank form to fill in by hand; no data is collected or uploaded.

## 3. What to redact before sharing

- URLs with query strings, tokens, session ids, auth parameters or fragment identifiers.
- Cookies, credentials, saved passwords, DevTools RDP tokens and certificate material.
- Local filesystem paths containing your username, company name or network shares.
- Screenshots or page dumps that show personal data, open tabs or account state.
- Full crash dumps or raw logs until you have scanned them for the above.

The DevTools token is a credential: it is intentionally not displayed or logged by the
shell. If you find one in a log, treat the log as a secret and redact the whole line.

## 4. Feedback today

There is no in-product feedback command. Open an issue in the Servo repository with the
filled-in template, the manifest values from section 1, and the relevant redacted log
excerpt. In-product crash reporting, symbol upload and update notification remain
future work tracked by the checklist (items `R2` and `R3`).
