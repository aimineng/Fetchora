#requires -Version 5.1
<#
    check-link.ps1 - find symbols that a platform's build will fail to resolve.

    A missing definition is invisible to a syntax check: the code compiles and
    only the linker complains. This compiles the whole project for a *different*
    platform (by flipping the Q_OS_* macros) into object files, runs moc for
    every Q_OBJECT header, and then compares the undefined symbols against the
    ones the objects define.

    Anything left over that belongs to this project - rather than to Qt or the C
    runtime - is a symbol that platform's link step cannot resolve.

    Usage (from the project root):
        powershell -File tools\check-link.ps1 -Platform macos
        powershell -File tools\check-link.ps1 -Platform linux
#>
param(
    [ValidateSet('macos', 'linux')] [string] $Platform = 'macos',
    [string] $QtInclude = 'D:\Qt\6.10.3\mingw_64\include'
)

$ErrorActionPreference = 'Continue'
$env:PATH = "D:\Qt\Tools\mingw1310_64\bin;D:\Qt\6.10.3\mingw_64\bin;$env:PATH"

$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "build\linkcheck-$Platform"
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null

# The platform macro set the real build would use.
$defines = if ($Platform -eq 'macos') { @('-UQ_OS_WIN', '-DQ_OS_MACOS', '-DQ_OS_UNIX') }
           else { @('-UQ_OS_WIN', '-DQ_OS_LINUX', '-DQ_OS_UNIX') }

$incs = @('-I.', "-I$QtInclude", "-I$QtInclude\QtCore", "-I$QtInclude\QtGui",
          "-I$QtInclude\QtWidgets", "-I$QtInclude\QtNetwork", "-I$QtInclude\QtSql",
          "-I$QtInclude\QtSvg", "-I$QtInclude\QtConcurrent", '-Ibuild\syntaxinc')

# ---------------------------------------------------------------- ui_*.h + moc
New-Item -ItemType Directory -Force -Path (Join-Path $out 'ui/pages') | Out-Null
foreach ($ui in Get-ChildItem (Join-Path $root 'ui\pages') -Filter *.ui) {
    & uic.exe $ui.FullName -o (Join-Path $out "ui/pages/ui_$($ui.BaseName).h") | Out-Null
}
$incs += "-I$out"

$sources = @()
$sources += Get-ChildItem $root -File -Filter *.cpp | ForEach-Object { $_.FullName }
$sources += Get-ChildItem (Join-Path $root 'ui') -File -Filter *.cpp | ForEach-Object { $_.FullName }
$sources += Get-ChildItem (Join-Path $root 'ui\pages') -File -Filter *.cpp | ForEach-Object { $_.FullName }

# moc every Q_OBJECT header; without it every metaobject symbol looks undefined.
$mocSources = @()
$headers = @()
$headers += Get-ChildItem $root -File -Filter *.h
$headers += Get-ChildItem (Join-Path $root 'ui') -File -Filter *.h
$headers += Get-ChildItem (Join-Path $root 'ui\pages') -File -Filter *.h
foreach ($h in $headers) {
    if (-not (Select-String -Path $h.FullName -Pattern 'Q_OBJECT' -Quiet)) { continue }
    $mocPath = Join-Path $out ("moc_" + $h.BaseName + ".cpp")
    & moc.exe $h.FullName -o $mocPath @incs 2>$null
    if (Test-Path $mocPath) { $mocSources += $mocPath }
}
Write-Host "compiling $($sources.Count) sources + $($mocSources.Count) moc files for $Platform ..."

$objects = @()
$failed = 0
foreach ($src in ($sources + $mocSources)) {
    $obj = Join-Path $out ((Split-Path $src -Leaf) + '.o')
    & g++ -c -std=c++17 -DUNICODE -D_UNICODE @defines @incs $src -o $obj 2>$null
    if ($LASTEXITCODE -ne 0) { $failed++; Write-Host "  compile FAILED: $(Split-Path $src -Leaf)" }
    elseif (Test-Path $obj) { $objects += $obj }
}
if ($failed -gt 0) { Write-Host "$failed file(s) did not compile - fix those first"; exit 1 }

# ------------------------------------------------------------------- symbols
$defined = New-Object System.Collections.Generic.HashSet[string]
$undefined = New-Object System.Collections.Generic.HashSet[string]
foreach ($obj in $objects) {
    foreach ($line in (& nm.exe --defined-only $obj 2>$null)) {
        if ($line -match '^[0-9a-fA-F]+\s+\S\s+(\S+)$') { [void]$defined.Add($Matches[1]) }
    }
    foreach ($line in (& nm.exe -u $obj 2>$null)) {
        if ($line -match '^\s+U\s+(\S+)$') { [void]$undefined.Add($Matches[1]) }
    }
}

# Only this project's own symbols matter: Qt and the C runtime are linked in by
# the real build and are legitimately undefined here.
$ours = 'Fluent|Aria2|Torrent|Download|Http|Settings|Clipboard|Notification|Language|Task|Bencode|main'
$missing = $undefined | Where-Object { $_ -match $ours } | Where-Object { -not $defined.Contains($_) }

if ($missing.Count -gt 0) {
    Write-Host "`n$($missing.Count) symbol(s) undefined for $Platform :" -ForegroundColor Red
    $missing | Sort-Object | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "`nno unresolved project symbols for $Platform ($($defined.Count) defined, $($undefined.Count) undefined overall)" -ForegroundColor Green
exit 0
