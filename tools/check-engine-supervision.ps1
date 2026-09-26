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
    [int] $ShutdownSeconds = 10,
    # Optional: a URL large enough that a download is still running when the
    # engine is killed (tools/testsrv.js serves one). When given, the later checks
    # assert that the queue survives the restart, that pause/resume resumes, and
    # that a removed task stays removed.
    [string] $DownloadUrl = '',
    # Optional: a *small* URL that finishes quickly. Downloading it twice is what
    # proves the second copy really happens - the case that matters is a file that
    # is already complete on disk, which aria2 would otherwise reuse.
    [string] $SmallDownloadUrl = ''
)

$ErrorActionPreference = 'Continue'

if (-not (Test-Path $App)) { throw "no application at $App" }
# PowerShell variable names are case-insensitive, so the path and the process
# object must not both be called "$app": the second assignment silently replaces
# the first and Stop-Process then gets a string.
$appPath = (Resolve-Path $App).Path
$appDir = Split-Path -Parent $appPath

# Whatever ran before this script did (an earlier test, a leftover engine) must
# not be counted as "the engine the app started": the byte totals below are
# summed over every task the engine knows, so a stray download makes the
# comparison meaningless. Everything is measured from here on.
Get-Process -Name 'aria2c' -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "stopping a leftover aria2c (pid $($_.Id)) so the run starts clean"
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 2
$script:startedAt = Get-Date

# A fresh name for every run. The downloads below are asserted on byte counts and
# on "did a second file appear", and a file left behind by an earlier run makes the
# task resume almost finished - which looks exactly like the failure being tested
# for. The name goes into the path (not a query string) so aria2 writes a new file.
$script:runTag = Get-Date -Format 'HHmmss'
function New-RunUrl($url) {
    if (-not $url) { return '' }
    $dot = $url.LastIndexOf('.')
    $slash = $url.LastIndexOf('/')
    if ($dot -gt $slash) { return $url.Substring(0, $dot) + "-$script:runTag" + $url.Substring($dot) }
    return "$url-$script:runTag"
}

# aria2c next to the app is what the packaged layout looks like; without it the
# engine is found on PATH instead, and either is fine.
$engineName = 'aria2c'
$engineExe = Join-Path $appDir "aria2c$([System.IO.Path]::GetExtension($appPath))"
if (-not (Test-Path $engineExe)) { $engineExe = Join-Path $appDir 'aria2c' }
if (-not (Test-Path $engineExe)) { $engineExe = $null }

function Get-Engines {
    # Only engines that appeared during this run: a process started earlier is
    # somebody else's and would corrupt every count and byte total below.
    @(Get-Process -Name $engineName -ErrorAction SilentlyContinue | Where-Object {
        try { $_.StartTime -ge $script:startedAt } catch { $true }
    })
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
    # Stop everything first: the app writes to the redirected stderr file, and
    # reading a file that is still open for writing blocks until the writer says
    # something - which is how this function managed to hang a whole test run.
    Get-Process -Name Fetchora -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Engines | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    Write-Host '--- application log ---'
    Get-AppLogTail | ForEach-Object { Write-Host "  $_" }
    if ($script:stderrFile -and (Test-Path $script:stderrFile)) {
        Write-Host '--- application stderr ---'
        Get-Content $script:stderrFile -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $_" }
    }
    exit 1
}

# What the engine is working on, straight from its RPC port. Used to wait for a
# download to actually be running before the test pulls the engine out from under
# it - killing it before the task exists would prove nothing.
function Get-EngineTasks {
    $body = '{"jsonrpc":"2.0","id":"1","method":"aria2.tellActive","params":[]}'
    try {
        $reply = Invoke-RestMethod -Uri 'http://127.0.0.1:6800/jsonrpc' -Method Post -Body $body `
            -ContentType 'application/json' -TimeoutSec 8
        return @($reply.result)
    } catch {
        return @()
    }
}

# The paused/waiting side of the same thing, and one task's full state.
function Get-EngineWaiting {    $body = '{"jsonrpc":"2.0","id":"1","method":"aria2.tellWaiting","params":[0,100]}'
    try {
        $reply = Invoke-RestMethod -Uri 'http://127.0.0.1:6800/jsonrpc' -Method Post -Body $body `
            -ContentType 'application/json' -TimeoutSec 8
        return @($reply.result)
    } catch {
        return @()
    }
}

# Everything the engine has stopped working on (complete, failed or removed).
function Get-EngineStopped {
    $body = '{"jsonrpc":"2.0","id":"1","method":"aria2.tellStopped","params":[0,200]}'
    try {
        $reply = Invoke-RestMethod -Uri 'http://127.0.0.1:6800/jsonrpc' -Method Post -Body $body `
            -ContentType 'application/json' -TimeoutSec 8
        return @($reply.result)
    } catch {
        return @()
    }
}

function Get-EngineState($gid) {    $body = (@{ jsonrpc = '2.0'; id = '1'; method = 'aria2.tellStatus'; params = @($gid) } |
        ConvertTo-Json -Compress -Depth 6)
    try {
        $reply = Invoke-RestMethod -Uri 'http://127.0.0.1:6800/jsonrpc' -Method Post -Body $body `
            -ContentType 'application/json' -TimeoutSec 8
        return $reply.result
    } catch {
        return $null
    }
}

# Every path the engine is writing to, in every bucket: a second copy of the same
# link shows up as a new path.
function Get-EnginePaths {
    $paths = @()
    foreach ($bucket in @('tellActive', 'tellWaiting', 'tellStopped')) {
        $params = if ($bucket -eq 'tellActive') { '[]' } else { '[0,200]' }
        $body = "{`"jsonrpc`":`"2.0`",`"id`":`"1`",`"method`":`"aria2.$bucket`",`"params`":$params}"
        try {
            $reply = Invoke-RestMethod -Uri 'http://127.0.0.1:6800/jsonrpc' -Method Post -Body $body `
                -ContentType 'application/json' -TimeoutSec 8
        } catch {
            continue
        }
        foreach ($task in @($reply.result)) {
            foreach ($file in @($task.files)) {
                if ($file.path) { $paths += $file.path }
            }
        }
    }
    return $paths
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
$bigUrl = New-RunUrl $DownloadUrl
$smallUrl = New-RunUrl $SmallDownloadUrl
$launchArgs = @('--new-instance')
if ($bigUrl) { $launchArgs += $bigUrl }
$appProcess = Resolve-AppProcess (Start-Process $appPath -ArgumentList $launchArgs -PassThru `
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
if ($DownloadUrl) {
    # Wait until the download is really running: the point of check 2b is that the
    # engine is killed *while* it transfers something.
    $deadline = (Get-Date).AddSeconds($StartupSeconds)
    $running = @()
    while ((Get-Date) -lt $deadline) {
        $running = Get-EngineTasks
        if ($running.Count -gt 0) { break }
        Start-Sleep -Seconds 2
    }
    if ($running.Count -eq 0) { Fail 'the download never started, so there is nothing to lose' }
    $bytesBefore = 0
    foreach ($t in $running) { $bytesBefore += [int64]$t.completedLength }
    Write-Host ("ok: {0} download(s) running, {1:N0} bytes in" -f $running.Count, $bytesBefore)
}
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

# ------------------------------------------------- 2b. the queue survives it
if ($DownloadUrl) {
    Write-Host "`n--- 2b. the download that was running came back"    # The restore happens within a second or two of the new engine answering; a
    # queue that was never restored stays empty forever, so a short wait is
    # enough and a long one would only hide a failure.
    Start-Sleep -Seconds 8
    $tasks = Get-EngineTasks
    if ($tasks.Count -eq 0) {
        Fail 'the replacement engine has no tasks: the queue was not handed back'
    }
    $bytes = 0
    foreach ($t in $tasks) { $bytes += [int64]$t.completedLength }
    # Not "at least as much as before": aria2's control file is written every
    # auto-save-interval, so a resumed task legitimately restarts from the last
    # saved bitfield and re-fetches a little. What must hold is that it came back
    # with its partial data and is *working*, not finished - the broken version
    # reported the restored download as complete on the spot, at full size.
    foreach ($t in $tasks) {
        if ($t.status -eq 'complete') {
            Fail 'the restored download was declared complete straight away'
        }
    }
    if ($bytes -le 0) {
        Fail 'the restored download has nothing on disk: it started over'
    }
    Write-Host ("ok: {0} task(s) restored, {1:N0} bytes already on disk" -f $tasks.Count, $bytes)

    # ------------------------------------------------- 2d. pause and resume it
    # Pausing used to end with the download declared complete at its full size:
    # --remove-control-file threw away the resume data, and --file-allocation
    # =prealloc had already made the file look finished. This runs before 2c,
    # which removes the only task it could pause.
    Write-Host "`n--- 2d. pausing and resuming keeps downloading"
    $again = Get-EngineTasks
    if ($again.Count -eq 0) { Fail 'no task left to pause' }
    $gid = $again[0].gid
    $pausedBytes = [int64]$again[0].completedLength

    Invoke-RestMethod -Uri 'http://127.0.0.1:8899/pause' -Method Post `
        -Body (@{ gid = $gid } | ConvertTo-Json -Compress) -ContentType 'application/json' `
        -TimeoutSec 8 | Out-Null
    Start-Sleep -Seconds 3
    $waiting = Get-EngineWaiting
    if (-not ($waiting | Where-Object { $_.gid -eq $gid })) {
        Fail 'the app was told to pause but the task is not paused'
    }
    Invoke-RestMethod -Uri 'http://127.0.0.1:8899/unpause' -Method Post `
        -Body (@{ gid = $gid } | ConvertTo-Json -Compress) -ContentType 'application/json' `
        -TimeoutSec 8 | Out-Null
    Start-Sleep -Seconds 6
    $resumed = @(Get-EngineTasks | Where-Object { $_.gid -eq $gid })
    if ($resumed.Count -eq 0) {
        $state = Get-EngineState $gid
        if ($state -and $state.status -eq 'complete') {
            Fail ("the resumed download reported itself complete at {0:N0} bytes" -f [int64]$state.completedLength)
        }
        Fail 'the resumed download is gone'
    }
    if ([int64]$resumed[0].completedLength -le $pausedBytes) {
        Fail ("the resumed download did not move past {0:N0} bytes" -f $pausedBytes)
    }
    Write-Host ("ok: resumed from {0:N0} to {1:N0} bytes" -f $pausedBytes, [int64]$resumed[0].completedLength)

    # --------------------------------------------- 2c. removing it really sticks
    # aria2 answers with a stopped result until removeDownloadResult has been
    # processed, so a delete used to be undone by the very next poll.
    Write-Host "`n--- 2c. a removed task stays removed"
    try {
        Invoke-RestMethod -Uri 'http://127.0.0.1:8899/remove' -Method Post `
            -Body (@{ gid = $gid } | ConvertTo-Json -Compress) `
            -ContentType 'application/json' -TimeoutSec 8 | Out-Null
    } catch {
        Fail "the app's bridge did not accept the remove request: $($_.Exception.Message)"
    }
    # Two poll cycles: one to carry the removal out, one to prove it stayed gone.
    Start-Sleep -Seconds 5
    $still = @(Get-EngineTasks | Where-Object { $_.gid -eq $gid })
    if ($still.Count -gt 0) {
        Fail 'the task the app was told to remove is still there'
    }
    Write-Host 'ok: the removed task stayed removed'

    # ------------------------------------------- 2e. asking for it again works
    # aria2 hands back the finished file for a URL it already downloaded, so a
    # second request has to be given a different output name to download at all.
    # The URL used here must be one that can finish, or there is no finished file
    # to be confused by.
    $repeatUrl = if ($smallUrl) { $smallUrl } else { $bigUrl }
    Write-Host "`n--- 2e. the same link a second time downloads a second copy"
    Invoke-RestMethod -Uri 'http://127.0.0.1:8899/download' -Method Post `
        -Body (@{ url = $repeatUrl } | ConvertTo-Json -Compress) -ContentType 'application/json' `
        -TimeoutSec 8 | Out-Null
    # Let the first copy finish: that is the state the second request has to cope
    # with. Waiting for "no active tasks" is not enough - an RPC hiccup returns an
    # empty list too - so this waits for the task itself to report complete.
    #
    # [System.IO.Path]::GetFileName, not Split-Path: PowerShell reads "http:" as a
    # drive name and Split-Path fails on a URL, which left the pattern empty and
    # matched the first complete task it saw - from an earlier run.
    $smallName = [System.IO.Path]::GetFileName(($repeatUrl -split '\?' | Select-Object -First 1))
    if (-not $smallName) { Fail "cannot tell the file name of $repeatUrl" }
    $done = $false
    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        foreach ($t in @(Get-EngineStopped)) {
            if ($t.status -ne 'complete') { continue }
            foreach ($f in @($t.files)) {
                if ($f.path -and [System.IO.Path]::GetFileName($f.path) -like "$smallName*") { $done = $true }
            }
        }
        if ($done) { break }
    }
    if (-not $done) { Fail "the download of $smallName never finished" }
    # Give the app its poll: it records the completion in the history on the poll
    # that sees it, and that record is one of the two things that make the second
    # request download a copy instead of reusing the file.
    Start-Sleep -Seconds 3
    $before = @(Get-EnginePaths)
    if ($before.Count -eq 0) { Fail 'the small download never appeared on disk' }

    Invoke-RestMethod -Uri 'http://127.0.0.1:8899/download' -Method Post `
        -Body (@{ url = $repeatUrl } | ConvertTo-Json -Compress) -ContentType 'application/json' `
        -TimeoutSec 8 | Out-Null
    Start-Sleep -Seconds 8
    $added = @(Get-EnginePaths | Where-Object { $before -notcontains $_ })
    if ($added.Count -eq 0) {
        Fail 'the second request for the same link reused the existing file instead of downloading a copy'
    }
    Write-Host ("ok: it is downloading {0}" -f (Split-Path $added[0] -Leaf))
}

# ------------------------------------------------------- 3. it dies with the app
Write-Host "`n--- 3. killing the app hard takes the engine with it"
if (-not $appProcess) { Fail 'lost track of the app process, cannot test the orphan case' }
Stop-Process -Id $appProcess.Id -Force -ErrorAction SilentlyContinue
Write-Host "killed app pid $($appProcess.Id)"

# Take this run's downloads with it: they are hundreds of megabytes and nothing
# else knows about them (unique names, see New-RunUrl).
foreach ($path in @(Get-EnginePaths)) {
    if ($path -notlike "*-$script:runTag*") { continue }
    Remove-Item $path -Force -ErrorAction SilentlyContinue
    Remove-Item "$path.aria2" -Force -ErrorAction SilentlyContinue
}
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
