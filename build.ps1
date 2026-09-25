# build.ps1 - configure + build helper for the Aria2 Downloader (Qt6 / MinGW)
#
# Usage:
#   .\build.ps1                 # incremental build (Debug)
#   .\build.ps1 -Release        # Release build
#   .\build.ps1 -Clean          # wipe the build directory and reconfigure
#   .\build.ps1 -Run            # build, then launch the app
#   .\build.ps1 -Test           # build, then run the headless self-tests
#   .\build.ps1 -Deploy         # build, then windeployqt into a portable folder
#
# The MinGW and Qt bin directories MUST be on PATH: g++ launches cc1plus/as/ld
# as child processes and those need the GCC runtime DLLs next to the driver.

param(
    [switch]$Clean,
    [switch]$Run,
    [switch]$Test,
    [switch]$Deploy,
    [switch]$Release,
    [string]$QtDir = "D:\Qt\6.10.3\mingw_64",
    [string]$MingwDir = "D:\Qt\Tools\mingw1310_64",
    [string]$CMake = "D:\Qt\Tools\CMake_64\bin\cmake.exe"
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$BuildType = if ($Release) { "Release" } else { "Debug" }
$BuildDir = Join-Path $Root "build\$BuildType"

# ---------------------------------------------------------------- toolchain
if (-not (Test-Path $CMake)) { throw "cmake not found at $CMake" }
if (-not (Test-Path "$MingwDir\bin\g++.exe")) { throw "MinGW not found at $MingwDir" }
if (-not (Test-Path "$QtDir\lib\cmake\Qt6\Qt6Config.cmake")) { throw "Qt not found at $QtDir" }

$env:PATH = "$MingwDir\bin;$QtDir\bin;$env:PATH"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning $BuildDir ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Host "Configuring ($BuildType) ..." -ForegroundColor Cyan
    & $CMake -S $Root -B $BuildDir -G "MinGW Makefiles" `
        -DCMAKE_BUILD_TYPE=$BuildType `
        -DCMAKE_PREFIX_PATH="$QtDir" `
        -DCMAKE_C_COMPILER="$MingwDir\bin\gcc.exe" `
        -DCMAKE_CXX_COMPILER="$MingwDir\bin\g++.exe" `
        -DCMAKE_MAKE_PROGRAM="$MingwDir\bin\mingw32-make.exe"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

Write-Host "Building ($BuildType) ..." -ForegroundColor Cyan
& $CMake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$Exe = Join-Path $BuildDir "Fetchora.exe"
Write-Host "Built: $Exe" -ForegroundColor Green

# ------------------------------------------------- engine + trust material
# CMake copies these when they sit next to CMakeLists.txt; fall back to any
# copy found in the tree or a normal install location.
foreach ($name in @("aria2c.exe", "ca-bundle.crt")) {
    $target = Join-Path $BuildDir $name
    if (Test-Path $target) { continue }

    $candidates = @("$Root\$name")
    $found = Get-ChildItem -Path $Root -Recurse -Filter $name -File -ErrorAction SilentlyContinue |
             Select-Object -First 1
    if ($found) { $candidates += $found.FullName }
    if ($name -eq "aria2c.exe") { $candidates += "C:\Program Files\aria2\aria2c.exe" }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            Copy-Item $candidate $target -Force
            Write-Host "Copied $name from $candidate" -ForegroundColor DarkGray
            break
        }
    }
    if (-not (Test-Path $target)) {
        Write-Warning "$name is missing - place it in the project root or next to the executable."
    }
}

# ------------------------------------------------------------------- tests
# Runs the app's own headless tools. These validate the two things that are
# easy to get silently wrong: the aria2c command line (an unknown switch makes
# aria2 exit 28 and the engine never starts) and the bencode output of the
# torrent maker (a malformed announce-list is unreadable by any client).
if ($Test) {
    Write-Host "`n=== self-test: aria2c command line ===" -ForegroundColor Cyan
    $selfTest = & $Exe --self-test 2>&1 | Out-String
    $selfTest.Trim() | Write-Host
    if ($selfTest -notmatch "RESULT: all switches accepted") {
        throw "aria2c rejected the generated command line"
    }

    Write-Host "`n=== self-test: torrent round trip ===" -ForegroundColor Cyan
    $tmp = Join-Path $env:TEMP ("aria2dl-selftest-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path "$tmp\sub" | Out-Null
    Set-Content -Path "$tmp\a.txt" -Value "hello" -Encoding ASCII
    Set-Content -Path "$tmp\sub\b.txt" -Value "world" -Encoding ASCII

    $created = & $Exe --make-torrent $tmp --output "$tmp\selftest.torrent" `
                      --tracker "udp://tracker.opentrackr.org:1337/announce,udp://open.tracker.cl:1337/announce" 2>&1 | Out-String
    if (-not (Test-Path "$tmp\selftest.torrent")) {
        $created | Write-Host
        throw "torrent creation produced no file"
    }

    $info = & $Exe --inspect-torrent "$tmp\selftest.torrent" 2>&1 | Out-String
    $info.Trim() | Write-Host

    # The same .torrent must yield the same info hash both ways; if the
    # announce-list or the file list were malformed the second read would throw.
    $hashCreate = [regex]::Match($created, 'infoHash:\s*([0-9a-f]{40})').Groups[1].Value
    $hashInspect = [regex]::Match($info, 'infoHash:\s*([0-9a-f]{40})').Groups[1].Value
    if (-not $hashCreate -or $hashCreate -ne $hashInspect) {
        throw "info hash mismatch between create ($hashCreate) and inspect ($hashInspect)"
    }
    if ($info -notmatch 'trackers \(2\)') { throw "announce-list lost its trackers" }
    if ($info -notmatch 'isMultiFile: yes') { throw "multi-file torrent was not detected" }
    if ($info -notmatch 'sub/b\.txt') { throw "nested file path was not preserved" }

    # aria2 itself must be able to parse what we produced. Note that --dry-run
    # exits non-zero by design ("nothing was downloaded"), so only the error
    # codes that mean "bad torrent" (25-27) are treated as failures.
    $engine = Join-Path $BuildDir "aria2c.exe"
    if (Test-Path $engine) {
        $check = & $engine --dry-run --console-log-level=error --torrent-file="$tmp\selftest.torrent" 2>&1 | Out-String
        if ($check -match "errorCode=2[5-7]") {
            $check | Write-Host
            throw "aria2c rejected the generated torrent"
        }
        Write-Host "aria2c parsed the generated torrent" -ForegroundColor DarkGray
    }

    Remove-Item $tmp -Recurse -Force
    Write-Host "ALL SELF-TESTS PASSED" -ForegroundColor Green
}

# The engine and the test tools above leave their own exit codes behind; make
# the script's own status explicit.
$global:LASTEXITCODE = 0

# ------------------------------------------------------------------ deploy
if ($Deploy) {
    $wq = Join-Path $QtDir "bin\windeployqt.exe"
    if (-not (Test-Path $wq)) { throw "windeployqt not found at $wq" }
    Write-Host "Deploying Qt runtime ..." -ForegroundColor Cyan
    & $wq --qmldir $Root --no-translations --no-system-d3d-compiler $Exe
}

if ($Run) {
    Write-Host "Launching ..." -ForegroundColor Green
    Start-Process -FilePath $Exe -WorkingDirectory $BuildDir
}
