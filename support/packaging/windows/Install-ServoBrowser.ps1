<#
.SYNOPSIS
    Installs Servo Browser for Windows with desktop shortcuts, Start Menu integration,
    and Windows Default Browser registry capabilities.
.DESCRIPTION
    Registers browser capability metadata so the user can pick Servo Browser in
    Windows Settings > Default apps. This script never writes UserChoice: Windows
    deliberately reserves that key for an explicit user action.

    Any imported certificate is the CI self-signed test certificate for local dev
    builds only. Production signing, MSIX/Store-ready packaging and the update
    channel are separate deployment decisions and are intentionally not done here.
.PARAMETER InstallDir
    Target directory for installation (default: $env:LOCALAPPDATA\Programs\ServoBrowser)
.PARAMETER Silent
    Perform installation silently without prompts
#>
[CmdletBinding()]
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\ServoBrowser",
    [switch]$Silent
)

$ErrorActionPreference = "Stop"

# DEV/PREVIEW ONLY: trust the self-signed certificate shipped with CI packages so a
# local browser launches without a warning. This is not production code signing.
$CertPath = Join-Path $PSScriptRoot "ServoBrowser.cer"
if (Test-Path $CertPath) {
    Write-Host "[+] Importing local self-signed test certificate (dev only)" -ForegroundColor Yellow
    try {
        Import-Certificate -FilePath $CertPath -CertStoreLocation "Cert:\CurrentUser\TrustedPublisher" | Out-Null
    } catch {
        Write-Warning "Could not import $CertPath; the binary may show an untrusted-publisher warning."
    }
}

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Installing Servo Browser (for Windows)" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

# Locate executable
$ScriptDir = $PSScriptRoot
$RepoRoot = Resolve-Path "$ScriptDir\..\..\.."
$SourceExe = $null

$PossibleExes = @(
    "$ScriptDir\servobrowser.exe",
    "$ScriptDir\servoshell.exe",
    "$RepoRoot\target\release\servoshell.exe",
    "$RepoRoot\target\release\servobrowser.exe",
    "$RepoRoot\target\debug\servoshell.exe",
    "$RepoRoot\target\debug\servobrowser.exe"
)

foreach ($exe in $PossibleExes) {
    if (Test-Path $exe) {
        $SourceExe = $exe
        break
    }
}

if (-not $SourceExe) {
    Write-Warning "No pre-built binary found in target directories."
    Write-Host "Expected one of: $($PossibleExes -join ', ')" -ForegroundColor Yellow
    exit 1
}

Write-Host "[+] Source binary: $SourceExe" -ForegroundColor Green
Write-Host "[+] Destination:   $InstallDir" -ForegroundColor Green

# Create installation directory
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# Copy main executable as servobrowser.exe
$DestExe = Join-Path $InstallDir "servobrowser.exe"
Copy-Item -Path $SourceExe -Destination $DestExe -Force
Write-Host "[+] Installed executable to $DestExe" -ForegroundColor Green

# Ensure 16 MiB stack reserve on servobrowser.exe
$EditBinPaths = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\editbin.exe",
    "editbin.exe"
)
foreach ($eb in $EditBinPaths) {
    if (Test-Path $eb -ErrorAction SilentlyContinue) {
        & $eb /STACK:16777216 $DestExe | Out-Null
        Write-Host "[+] Configured 16 MiB stack reserve via editbin" -ForegroundColor Green
        break
    }
}

# Copy ANGLE runtime DLLs (libEGL.dll and libGLESv2.dll) required by surfman/graphics
$AngleDlls = @("libEGL.dll", "libGLESv2.dll")
foreach ($dll in $AngleDlls) {
    $dllSource = $null
    $possibleDllPaths = @(
        "$ScriptDir\$dll",
        (Join-Path (Split-Path -Parent $SourceExe) $dll),
        "$RepoRoot\target\release\$dll",
        "$RepoRoot\target\debug\$dll"
    )
    foreach ($p in $possibleDllPaths) {
        if (Test-Path $p) {
            $dllSource = $p
            break
        }
    }
    if (-not $dllSource) {
        # Search build directory
        $found = Get-ChildItem -Path "$RepoRoot\target" -Filter $dll -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            $dllSource = $found.FullName
        }
    }
    if ($dllSource) {
        Copy-Item -Path $dllSource -Destination (Join-Path $InstallDir $dll) -Force
        Write-Host "[+] Installed $dll from $dllSource" -ForegroundColor Green
    } else {
        Write-Warning "Could not find required graphics DLL: $dll"
    }
}

# Copy resources if available
$ResourceDirs = @(
    "$RepoRoot\resources",
    "$ScriptDir\resources"
)
foreach ($resDir in $ResourceDirs) {
    if (Test-Path $resDir) {
        $DestRes = Join-Path $InstallDir "resources"
        Copy-Item -Path $resDir -Destination $DestRes -Recurse -Force
        Write-Host "[+] Copied resources to $DestRes" -ForegroundColor Green
        break
    }
}

# Copy uninstaller script to destination
$UninstallerScript = Join-Path $ScriptDir "Uninstall-ServoBrowser.ps1"
if (Test-Path $UninstallerScript) {
    Copy-Item -Path $UninstallerScript -Destination (Join-Path $InstallDir "Uninstall-ServoBrowser.ps1") -Force
}

# Create Shortcuts (Desktop & Start Menu)
$WshShell = New-Object -ComObject WScript.Shell

# Desktop Shortcut
$DesktopPath = [Environment]::GetFolderPath([Environment+SpecialFolder]::Desktop)
$DesktopShortcutPath = Join-Path $DesktopPath "Servo Browser.lnk"
$Shortcut = $WshShell.CreateShortcut($DesktopShortcutPath)
$Shortcut.TargetPath = $DestExe
$Shortcut.WorkingDirectory = $InstallDir
$Shortcut.Description = "Servo Browser - Fast, minimal, memory-safe web browser"
$Shortcut.IconLocation = "$DestExe,0"
$Shortcut.Save()
Write-Host "[+] Created Desktop shortcut: $DesktopShortcutPath" -ForegroundColor Green

# Start Menu Shortcut
$StartMenuPath = [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs)
$StartMenuFolder = Join-Path $StartMenuPath "Servo Browser"
if (-not (Test-Path $StartMenuFolder)) {
    New-Item -ItemType Directory -Path $StartMenuFolder -Force | Out-Null
}
$StartMenuShortcutPath = Join-Path $StartMenuFolder "Servo Browser.lnk"
$Shortcut = $WshShell.CreateShortcut($StartMenuShortcutPath)
$Shortcut.TargetPath = $DestExe
$Shortcut.WorkingDirectory = $InstallDir
$Shortcut.Description = "Servo Browser - Fast, minimal, memory-safe web browser"
$Shortcut.IconLocation = "$DestExe,0"
$Shortcut.Save()
Write-Host "[+] Created Start Menu shortcut: $StartMenuShortcutPath" -ForegroundColor Green

# Utility shortcuts in the same Start Menu folder. Only routes the shell actually
# handles are registered, so no shortcut claims a feature that does not exist.
# DevTools is intentionally omitted: there is no --devtools command-line flag and
# Servo exposes Firefox RDP only, so a shortcut would imply an in-window frontend.
$UtilityShortcuts = @(
    @{ Name = "Settings"; Argument = "servo://settings"; Description = "Open Servo Browser settings" },
    @{ Name = "Bookmarks"; Argument = "servo://bookmarks"; Description = "Open Servo Browser bookmarks" },
    @{ Name = "History"; Argument = "servo://history"; Description = "Open Servo Browser history" },
    @{ Name = "Downloads"; Argument = "servo://downloads"; Description = "Open Servo Browser downloads" }
)
foreach ($entry in $UtilityShortcuts) {
    $utilityPath = Join-Path $StartMenuFolder "$($entry.Name).lnk"
    $utility = $WshShell.CreateShortcut($utilityPath)
    $utility.TargetPath = $DestExe
    $utility.Arguments = $entry.Argument
    $utility.WorkingDirectory = $InstallDir
    $utility.Description = $entry.Description
    $utility.IconLocation = "$DestExe,0"
    $utility.Save()
    Write-Host "[+] Created Start Menu shortcut: $utilityPath" -ForegroundColor Green
}

# Register in Windows Registry
# 1. App Paths
$AppPathReg = "HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\servobrowser.exe"
New-Item -Path $AppPathReg -Force | Out-Null
Set-ItemProperty -Path $AppPathReg -Name "(Default)" -Value $DestExe
Set-ItemProperty -Path $AppPathReg -Name "Path" -Value $InstallDir

# 2. Add/Remove Programs (Uninstall entry)
$UninstallReg = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ServoBrowser"
New-Item -Path $UninstallReg -Force | Out-Null
Set-ItemProperty -Path $UninstallReg -Name "DisplayName" -Value "Servo Browser"
Set-ItemProperty -Path $UninstallReg -Name "DisplayVersion" -Value "0.7.0"
Set-ItemProperty -Path $UninstallReg -Name "Publisher" -Value "The Servo Project Developers"
Set-ItemProperty -Path $UninstallReg -Name "DisplayIcon" -Value $DestExe
Set-ItemProperty -Path $UninstallReg -Name "InstallLocation" -Value $InstallDir
Set-ItemProperty -Path $UninstallReg -Name "UninstallString" -Value "powershell.exe -ExecutionPolicy Bypass -File `"$InstallDir\Uninstall-ServoBrowser.ps1`""
Set-ItemProperty -Path $UninstallReg -Name "NoModify" -Value 1
Set-ItemProperty -Path $UninstallReg -Name "NoRepair" -Value 1

# 3. Default Browser Registration (StartMenuInternet)
# We register the declarations Windows requires for a browser to appear in
# Settings > Apps > Default apps. We never touch UserChoice: that key is owned by
# the user and Windows may ignore or actively reject programmatic writes to it.
$BrowserReg = "HKCU:\Software\Clients\StartMenuInternet\ServoBrowser"
New-Item -Path $BrowserReg -Force | Out-Null
Set-ItemProperty -Path $BrowserReg -Name "(Default)" -Value "Servo Browser"

$BrowserIconReg = "$BrowserReg\DefaultIcon"
New-Item -Path $BrowserIconReg -Force | Out-Null
Set-ItemProperty -Path $BrowserIconReg -Name "(Default)" -Value "$DestExe,0"

$BrowserOpenReg = "$BrowserReg\shell\open\command"
New-Item -Path $BrowserOpenReg -Force | Out-Null
Set-ItemProperty -Path $BrowserOpenReg -Name "(Default)" -Value "`"$DestExe`""

# The ProgID referenced by URLAssociations/FileAssociations must exist or the
# capability declaration points at nothing.
$ProgIdReg = "HKCU:\Software\Classes\ServoBrowserHTML"
New-Item -Path $ProgIdReg -Force | Out-Null
Set-ItemProperty -Path $ProgIdReg -Name "(Default)" -Value "Servo Browser HTML Document"
$ProgIdIconReg = "$ProgIdReg\DefaultIcon"
New-Item -Path $ProgIdIconReg -Force | Out-Null
Set-ItemProperty -Path $ProgIdIconReg -Name "(Default)" -Value "$DestExe,0"
$ProgIdOpenReg = "$ProgIdReg\shell\open\command"
New-Item -Path $ProgIdOpenReg -Force | Out-Null
Set-ItemProperty -Path $ProgIdOpenReg -Name "(Default)" -Value "`"$DestExe`" `"%1`""

$CapabilitiesReg = "$BrowserReg\Capabilities"
New-Item -Path $CapabilitiesReg -Force | Out-Null
Set-ItemProperty -Path $CapabilitiesReg -Name "ApplicationDescription" -Value "Servo Browser is a fast, memory-safe web browser powered by Rust."
Set-ItemProperty -Path $CapabilitiesReg -Name "ApplicationIcon" -Value "$DestExe,0"
Set-ItemProperty -Path $CapabilitiesReg -Name "ApplicationName" -Value "Servo Browser"

$UrlAssocReg = "$CapabilitiesReg\URLAssociations"
New-Item -Path $UrlAssocReg -Force | Out-Null
Set-ItemProperty -Path $UrlAssocReg -Name "http" -Value "ServoBrowserHTML"
Set-ItemProperty -Path $UrlAssocReg -Name "https" -Value "ServoBrowserHTML"

$FileAssocReg = "$CapabilitiesReg\FileAssociations"
New-Item -Path $FileAssocReg -Force | Out-Null
Set-ItemProperty -Path $FileAssocReg -Name ".html" -Value "ServoBrowserHTML"
Set-ItemProperty -Path $FileAssocReg -Name ".htm" -Value "ServoBrowserHTML"
Set-ItemProperty -Path $FileAssocReg -Name ".url" -Value "ServoBrowserHTML"

# Register capabilities in RegisteredApplications
$RegisteredApps = "HKCU:\Software\RegisteredApplications"
if (-not (Test-Path $RegisteredApps)) {
    New-Item -Path $RegisteredApps -Force | Out-Null
}
Set-ItemProperty -Path $RegisteredApps -Name "ServoBrowser" -Value "Software\Clients\StartMenuInternet\ServoBrowser\Capabilities"

# 4. Diagnostics folder for update/feedback scaffolding.
# This is documentation/template scaffolding only: no updater or crash reporter is
# installed. See UPDATE_AND_FEEDBACK.md for the current (manual) path.
$DiagnosticsDir = Join-Path $InstallDir "Diagnostics"
if (-not (Test-Path $DiagnosticsDir)) {
    New-Item -ItemType Directory -Path $DiagnosticsDir -Force | Out-Null
}
$FeedbackTemplate = @'
Servo Browser - feedback / diagnostics template
==============================================

Nothing is uploaded automatically. Fill this in by hand and redact it before
sharing in an issue or with a maintainer.

- Version / commit:
- OS build:
- What happened:
- Steps to reproduce:
- Expected result:
- Relevant log path (see UPDATE_AND_FEEDBACK.md):

REDACT BEFORE SHARING:
- URLs with query strings, tokens, session ids or auth parameters
- Local filesystem paths that include your username or company name
- Cookies, credentials, DevTools tokens and certificate material
- Screenshots that show personal data

Do not paste raw crash dumps or logs until you have checked them for the above.
'@
$FeedbackTemplatePath = Join-Path $DiagnosticsDir "feedback-template.txt"
Set-Content -Path $FeedbackTemplatePath -Value $FeedbackTemplate -Encoding utf8
Write-Host "[+] Created diagnostics template: $FeedbackTemplatePath" -ForegroundColor Green

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Servo Browser successfully installed!" -ForegroundColor Green
Write-Host "  Run with: servobrowser or from Desktop shortcut" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Cyan
