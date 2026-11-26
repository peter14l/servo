# Native Windows Servo Browser - Automated Installer
param(
    [string]$TargetDir = "$env:LOCALAPPDATA\Programs\Servo Browser"
)

$ErrorActionPreference = "Stop"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " Installing Servo Browser (Native Windows) " -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# 1. Trust Self-Signed Certificate
$CertPath = Join-Path $ScriptDir "ServoBrowser.cer"
if (Test-Path $CertPath) {
    Write-Host "[1/4] Installing Self-Signed Certificate into Trusted Publishers..." -ForegroundColor Yellow
    try {
        Import-Certificate -FilePath $CertPath -CertStoreLocation "Cert:\CurrentUser\TrustedPublisher" | Out-Null
        Write-Host "      Certificate installed successfully." -ForegroundColor Green
    } catch {
        Write-Warning "Failed to install certificate automatically. You can install it manually from $CertPath"
    }
}

# 2. Copy Application Files
Write-Host "[2/4] Deploying application binaries to $TargetDir..." -ForegroundColor Yellow
if (-not (Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

$Exclude = @("install.ps1", "Servo-Browser-Setup.zip")
Get-ChildItem -Path $ScriptDir | Where-Object { $_.Name -notin $Exclude } | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination $TargetDir -Recurse -Force
}
Write-Host "      Binaries deployed." -ForegroundColor Green

# 3. Create Shortcuts
Write-Host "[3/4] Creating Start Menu and Desktop shortcuts..." -ForegroundColor Yellow
$WinUiExe = Join-Path $TargetDir "ServoBrowser.exe"
$ExePath = if (Test-Path $WinUiExe) { $WinUiExe } else { Join-Path $TargetDir "servo_browser.exe" }
$WshShell = New-Object -ComObject WScript.Shell

# CreateShortcut() reopens an existing .lnk and Save() preserves its old
# explicit IconLocation. Always delete first, then set IconLocation ourselves,
# otherwise an upgrade keeps showing the icon of a previous build.
function New-ServoShortcut {
    param([string]$Path)
    if (Test-Path $Path) {
        Remove-Item -Path $Path -Force -ErrorAction SilentlyContinue
    }
    $shortcut = $WshShell.CreateShortcut($Path)
    $shortcut.TargetPath = $ExePath
    $shortcut.WorkingDirectory = $TargetDir
    $shortcut.Description = "Servo Modern Web Browser"
    $shortcut.IconLocation = "$ExePath,0"
    $shortcut.Save()
}

$DesktopPath = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::Desktop)
New-ServoShortcut -Path (Join-Path $DesktopPath "Servo Browser.lnk")

$StartMenuPath = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::Programs)
New-ServoShortcut -Path (Join-Path $StartMenuPath "Servo Browser.lnk")

# Taskbar pins are separate .lnk files under User Pinned. Repair any of those
# that target this browser so an existing pin also drops the stale icon.
$PinnedPath = Join-Path $env:APPDATA "Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar"
if (Test-Path $PinnedPath) {
    Get-ChildItem -Path $PinnedPath -Filter '*.lnk' -ErrorAction SilentlyContinue | ForEach-Object {
        $existing = $WshShell.CreateShortcut($_.FullName)
        if ($existing.TargetPath -and ($existing.TargetPath -ieq $ExePath)) {
            $existing.IconLocation = "$ExePath,0"
            $existing.Save()
        }
    }
}

# Tell Explorer to refresh icon/association caches immediately.
Add-Type -Namespace ServoWin32 -Name Shell -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, System.IntPtr item1, System.IntPtr item2);
'@
[ServoWin32.Shell]::SHChangeNotify(0x08000000, 0x0000, [IntPtr]::Zero, [IntPtr]::Zero)
try { & "$env:WINDIR\System32\ie4uinit.exe" -show | Out-Null } catch { }
Write-Host "      Shortcuts created and icon cache refreshed." -ForegroundColor Green

# 4. Register in Windows Add/Remove Programs
Write-Host "[4/4] Registering in Windows Add/Remove Programs..." -ForegroundColor Yellow
$UninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ServoBrowser"
if (-not (Test-Path $UninstallKey)) {
    New-Item -Path $UninstallKey -Force | Out-Null
}

Set-ItemProperty -Path $UninstallKey -Name "DisplayName" -Value "Servo Browser"
Set-ItemProperty -Path $UninstallKey -Name "DisplayVersion" -Value "1.0.0"
Set-ItemProperty -Path $UninstallKey -Name "Publisher" -Value "Servo Project"
Set-ItemProperty -Path $UninstallKey -Name "DisplayIcon" -Value "$ExePath,0"
Set-ItemProperty -Path $UninstallKey -Name "InstallLocation" -Value $TargetDir
Set-ItemProperty -Path $UninstallKey -Name "UninstallString" -Value "powershell.exe -ExecutionPolicy Bypass -File `"$TargetDir\uninstall.ps1`""
Write-Host "      Registered successfully." -ForegroundColor Green

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " Servo Browser installation complete!     " -ForegroundColor Green
Write-Host " Launching Servo Browser...               " -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

Start-Process -FilePath $ExePath -WorkingDirectory $TargetDir
