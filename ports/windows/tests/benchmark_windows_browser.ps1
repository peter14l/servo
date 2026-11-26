# Windows Browser Startup & Memory Stability Benchmark (REL-5)
param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,
    [int]$Iterations = 3,
    [int]$DwellSeconds = 10,
    [string]$OutputJson = "windows_perf_baseline.json"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ExecutablePath)) {
    throw "Executable not found: $ExecutablePath"
}

$samples = @()
for ($i = 1; $i -le $Iterations; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $ExecutablePath -ArgumentList "about:blank" -PassThru
    $sw.Stop()

    Start-Sleep -Seconds $DwellSeconds
    $proc.Refresh()
    $wsMb = [math]::Round($proc.WorkingSet64 / 1MB, 2)
    $pmMb = [math]::Round($proc.PrivateMemorySize64 / 1MB, 2)

    if (-not $proc.HasExited) {
        $proc.CloseMainWindow() | Out-Null
        if (-not $proc.WaitForExit(3000)) {
            $proc.Kill()
        }
    }

    $samples += [PSCustomObject]@{
        iteration = $i
        launchMs = $sw.ElapsedMilliseconds
        workingSetMB = $wsMb
        privateMemoryMB = $pmMb
    }
}

$result = [PSCustomObject]@{
    executable = $ExecutablePath
    timestampUtc = (Get-Date).ToUniversalTime().ToString("o")
    iterations = $Iterations
    dwellSeconds = $DwellSeconds
    meanLaunchMs = [math]::Round(($samples | Measure-Object -Property launchMs -Average).Average, 2)
    maxWorkingSetMB = ($samples | Measure-Object -Property workingSetMB -Maximum).Maximum
    maxPrivateMemoryMB = ($samples | Measure-Object -Property privateMemoryMB -Maximum).Maximum
    samples = $samples
}

$result | ConvertTo-Json -Depth 4 | Set-Content -Path $OutputJson -Encoding utf8
Write-Host "Wrote Windows perf baseline to $OutputJson"
