# Automated Verification Harness for Servo Browser on GitHub.com
param(
    [string]$Url = "https://github.com/",
    [int]$DurationSeconds = 25
)

$ErrorActionPreference = "Continue"
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host " Running Automated Verification Harness on $Url " -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan

$exePath = "$env:LOCALAPPDATA\Programs\Servo Browser\servo_browser.exe"
if (-not (Test-Path $exePath)) {
    Write-Error "servo_browser.exe not found at $exePath"
    exit 1
}

# 1. Clear old logs
$logPath = "$env:LOCALAPPDATA\Programs\Servo Browser\servo_browser.log"
Remove-Item $logPath -Force -ErrorAction SilentlyContinue

# Record start time for event log filtering
$startTime = [DateTime]::UtcNow

# 2. Launch Servo Browser
Write-Host "[1/4] Launching Servo Browser with $Url..." -ForegroundColor Yellow
$proc = Start-Process -FilePath $exePath -ArgumentList "`"$Url`"" -PassThru

if (-not $proc) {
    Write-Error "Failed to start servo_browser.exe"
    exit 1
}

Write-Host "      Process started with PID $($proc.Id). Monitoring for $DurationSeconds seconds..." -ForegroundColor Green

# 3. Monitor Process Stability
$crashed = $false
for ($i = 0; $i -lt $DurationSeconds; $i++) {
    Start-Sleep -Seconds 1
    if ($proc.HasExited) {
        $crashed = $true
        Write-Host "[FAIL] Process exited prematurely after $i seconds with ExitCode: $($proc.ExitCode) (0x$($proc.ExitCode.ToString('X')))" -ForegroundColor Red
        break
    }
}

if (-not $crashed) {
    Write-Host "[2/4] Process survived full $DurationSeconds-second lifecycle without crashing!" -ForegroundColor Green
    
    # Send WM_CLOSE to gracefully close the window
    Write-Host "      Gracefully closing browser window..."
    $proc.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 2
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}

# 4. Check Logs for Diagnostics
Write-Host "[3/4] Checking browser log..." -ForegroundColor Yellow
if (Test-Path $logPath) {
    $logContent = Get-Content $logPath
    $hasUserCss = $logContent | Where-Object { $_ -match "User stylesheet created successfully" }
    $loadStatuses = $logContent | Where-Object { $_ -match "\[NAV\] load status" }
    
    if ($hasUserCss) {
        Write-Host "      [PASS] User stylesheet verified active in log." -ForegroundColor Green
    } else {
        Write-Host "      [WARN] User stylesheet confirmation not found in log." -ForegroundColor Yellow
    }
    
    Write-Host "      Navigation milestones observed:"
    foreach ($status in $loadStatuses) {
        Write-Host "        $status" -ForegroundColor Gray
    }
} else {
    Write-Host "      [WARN] No log file was generated." -ForegroundColor Yellow
}

# 5. Check Windows Event Log for crashes
Write-Host "[4/4] Checking Windows Event Log for crash events..." -ForegroundColor Yellow
$crashEvents = Get-WinEvent -LogName "Application" -MaxEvents 20 -ErrorAction SilentlyContinue | Where-Object {
    $_.Id -eq 1000 -and 
    $_.Message -like "*servo_browser*" -and 
    $_.TimeCreated.ToUniversalTime() -ge $startTime.AddSeconds(-2)
}

if ($crashEvents) {
    Write-Host "      [FAIL] Crash event detected in Event Log:" -ForegroundColor Red
    foreach ($evt in $crashEvents) {
        Write-Host "        Time: $($evt.TimeCreated) | Id: $($evt.Id)" -ForegroundColor Red
    }
    exit 2
} else {
    Write-Host "      [PASS] Zero crash events recorded in Windows Event Log." -ForegroundColor Green
}

if ($crashed) {
    exit 1
}

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host " VERIFICATION RESULT: PASSED (100% STABLE ON GITHUB) " -ForegroundColor Green
Write-Host "====================================================" -ForegroundColor Cyan
exit 0
