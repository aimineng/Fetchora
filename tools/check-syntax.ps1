#requires -Version 5.1
<#
    check-syntax.ps1 - fast, build-free syntax check for a single translation unit.

    AutoUIC normally generates ui_<Page>.h from <Page>.ui; when editing one file
    a full CMake build is slow, so this script runs uic by hand into
    build/syntaxinc (mirroring the include path the sources use) and then calls
    g++ -fsyntax-only.

    Usage (from the project root):
        powershell -File tools\check-syntax.ps1 -Sources ui\pages\AboutPage.cpp
#>
param(
    [Parameter(Mandatory = $true)]
    [string[]] $Sources
)

$ErrorActionPreference = 'Continue'

# `powershell -File script.ps1 -Sources a,b,c` binds the whole "a,b,c" as one
# string, so split any comma separated entry into individual paths.
$Sources = $Sources | ForEach-Object { $_ -split ',' } | Where-Object { $_ } | ForEach-Object { $_.Trim() }

$qtBin = 'D:\Qt\6.10.3\mingw_64\bin'
$mingwBin = 'D:\Qt\Tools\mingw1310_64\bin'
$env:PATH = "$mingwBin;$qtBin;$env:PATH"

$root = Split-Path -Parent $PSScriptRoot
$qtInc = 'D:\Qt\6.10.3\mingw_64\include'
$incDir = Join-Path $root 'build\syntaxinc'

$includes = @(
    "-I$root"
    "-I$qtInc"
    "-I$qtInc\QtCore"
    "-I$qtInc\QtGui"
    "-I$qtInc\QtWidgets"
    "-I$qtInc\QtNetwork"
    "-I$qtInc\QtSql"
    "-I$qtInc\QtSvg"
    "-I$qtInc\QtConcurrent"
    "-I$incDir"
)

$failed = 0
foreach ($src in $Sources) {
    $full = if ([System.IO.Path]::IsPathRooted($src)) { $src } else { Join-Path $root $src }
    if (-not (Test-Path $full)) {
        Write-Host "MISSING: $src" -ForegroundColor Red
        $failed++
        continue
    }

    # Generate the matching ui_<Name>.h when the translation unit has a .ui file.
    $dir = Split-Path -Parent $full
    $base = [System.IO.Path]::GetFileNameWithoutExtension($full)
    $uiFile = Join-Path $dir "$base.ui"
    if (Test-Path $uiFile) {
        $rel = $full.Substring($root.Length).TrimStart('\')
        $relDir = Split-Path -Parent $rel
        $outDir = Join-Path $incDir $relDir
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        $outHeader = Join-Path $outDir "ui_$base.h"
        & (Join-Path $qtBin 'uic.exe') $uiFile -o $outHeader
        if ($LASTEXITCODE -ne 0) {
            Write-Host "UIC FAILED: $uiFile" -ForegroundColor Red
            $failed++
            continue
        }
        Write-Host "uic  ok   $relDir\ui_$base.h"
    }

    Write-Host "g++  .... $src"
    # UNICODE/_UNICODE match what Qt's CMake target adds on Windows; without them
    # LoadIcon/MessageBox resolve to the ANSI variants and the check reports
    # errors the real build does not have.
    & g++ -fsyntax-only -std=c++17 -Wall -Wno-unused-parameter -DUNICODE -D_UNICODE @includes $full
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $src" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "ok       $src" -ForegroundColor Green
    }
}

if ($failed -gt 0) {
    Write-Host "$failed file(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host "all clean" -ForegroundColor Green
exit 0
