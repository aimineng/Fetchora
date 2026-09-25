#requires -Version 5.1
<#
    check-engine-supervision.ps1 - the engine must not outlive the app, and must
    come back when it dies.

    Three things are checked, all of them by observing real processes:

      1. starting the app also starts the engine,
      2. killing the engine makes the app start a new one,
      3. killing the app (hard, no chance to clean up) takes the engine with it.

    The third one is what an orphaned aria2c would violate: without the job object
    in Aria2Process, a crash leaves a downloader running in the background with no
    window to control it.

    Usage (from the project root, after the build):
        powershell -File tools\check-engine-supervision.ps1 -App build\Release\Fetchora.exe
#>
param(
    [Parameter(Mandatory = $true)] [string] $App,
    # Seconds to wait for the app to start, for the engine to come back, and for
    # the engine to disappear. CI machines are slow, so they are generous.
    [int] $StartupSeconds = 25,
    [int] $RestartSeconds = 20,
    [int] $ShutdownSeconds = 10
)

$ErrorActionPreference = 'Continue'

if (-not (Test-Path $App)) { throw "no application at $App" }
# PowerShell variable names are case-insensitive, so the path and the process
# object must not both be called "$app": the second assignment silently replaces
# the first and Stop-Process then gets a string.
$appPath = (Resolve-Path $App).Path
$appDir = Split-Path -Parent $appPath

# aria2c next to the app is what the packaged layout looks like; without it the
# engine is found on PATH instead, and either is fine.
$engineName = 'aria2c'
$engineExe = Join-Path $appDir "aria2c$([System.IO.Path]::GetExtension($appPath))"
if (-not (Test-Path $engineExe)) { $engineExe = Join-Path $appDir 'aria2c' }
if (-not (Test-Path $engineExe)) { $engineExe = $null }

function Get-Engines {
    @(Get-Process -Name $engineName -ErrorAction SilentlyContinue)
}

# The process object of the app we started. -PassThru is not reliable next to
# -RedirectStandardError (Windows PowerShell returns nothing), so the process is
# looked up by name when it comes back empty.
function Resolve-AppProcess($candidate) {
    if ($candidate -and $candidate -isnot [string]) { return $candidate }
    return Get-Process -Name 'Fetchora' -ErrorAction SilentlyContinue |
        Sort-Object StartTime | Select-Object -Last 1
}

function Get-AppLogTail {
    foreach ($dir in @("$env:LOCALAPPDATA\Fetchora\Fetchora\logs",
                       "$env:LOCALAPPDATA\Fetchora\logs",
                       "$env:APPDATA\Fetchora\logs",
                       "$HOME/.local/share/Fetchora/logs",
                       "$HOME/Library/Application Support/Fetchora/logs")) {
        if (-not (Test-Path $dir)) { continue }
        $file = Get-ChildItem $dir -Filter 'fetchora-*.log' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($file) { return Get-Content $file.FullName -Encoding UTF8 -Tail 40 }
    }
    return @('(the app wrote no log file - it did not get as far as opening one)')
}

function Fail($message) {
    Write-Host "FAILED: $message"
    Write-Host '--- application log ---'
    Get-AppLogTail | ForEach-Object { Write-Host "  $_" }
    if ($script:stderrFile -and (Test-Path $script:stderrFile)) {
        Write-Host '--- application stderr ---'
        Get-Content $script:stderrFile | ForEach-Object { Write-Host "  $_" }
    }
    exit 1
}

Get-Engines | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process -Name Fetchora -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

Write-Host "app:    $appPath"
Write-Host "engine: $(if ($engineExe) { $engineExe } else { "$engineName (from PATH)" })"

# ---------------------------------------------------------------- 1. it starts
Write-Host "`n--- 1. starting the app starts the engine"
$script:stderrFile = Join-Path ([System.IO.Path]::GetTempPath()) 'fetchora-supervision-stderr.txt'
Remove-Item $script:stderrFile -ErrorAction SilentlyContinue
$appProcess = Resolve-AppProcess (Start-Process $appPath -ArgumentList '--new-instance' -PassThru `
    -RedirectStandardError $script:stderrFile)
$deadline = (Get-Date).AddSeconds($StartupSeconds)
$first = @()
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    $first = Get-Engines
    if ($first.Count -gt 0) { break }
}
$appProcess = Resolve-AppProcess $appProcess
if ($first.Count -eq 0) {
    if ($appProcess) { $appProcess.Refresh() }
    if ($appProcess -and $appProcess.HasExited) {
        Fail "the app exited immediately (exit code $($appProcess.ExitCode)) instead of starting the engine"
    }
    Fail "the app is running (pid $($appProcess.Id)) but no $engineName appeared within $StartupSeconds s"
}
$appProcess.Refresh()
if ($appProcess.HasExited) { Fail 'the app exited during startup' }
Write-Host "ok: engine pid $($first.Id -join ','), app pid $($appProcess.Id)"

# ------------------------------------------------------------- 2. it comes back
Write-Host "`n--- 2. killing the engine makes the app start a new one"
$first | Stop-Process -Force -ErrorAction SilentlyContinue
$deadline = (Get-Date).AddSeconds($RestartSeconds)
$second = @()
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    $second = @(Get-Engines | Where-Object { $first.Id -notcontains $_.Id })
    if ($second.Count -gt 0) { break }
}
if ($second.Count -eq 0) { Fail "the engine did not come back within $RestartSeconds s" }
Write-Host "ok: engine pid $($second.Id -join ',') (was $($first.Id -join ','))"

# ------------------------------------------------------- 3. it dies with the app
Write-Host "`n--- 3. killing the app hard takes the engine with it"
if (-not $appProcess) { Fail 'lost track of the app process, cannot test the orphan case' }
Stop-Process -Id $appProcess.Id -Force -ErrorAction SilentlyContinue
Write-Host "killed app pid $($appProcess.Id)"
$deadline = (Get-Date).AddSeconds($ShutdownSeconds)
$left = Get-Engines
while ((Get-Date) -lt $deadline -and $left.Count -gt 0) {
    Start-Sleep -Seconds 2
    $left = Get-Engines
}
if ($left.Count -gt 0) {
    $left | Stop-Process -Force -ErrorAction SilentlyContinue
    Fail "the engine ($($left.Id -join ',')) survived the app being killed"
}
Write-Host 'ok: no engine left behind'

Write-Host "`nRESULT: engine supervision works (start, restart, no orphans)"
exit 0
