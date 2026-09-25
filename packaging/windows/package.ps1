#requires -Version 5.1
<#
    package.ps1 - build the distributable Windows artifacts.

    Produces, in -OutDir:
        Fetchora-<version>-windows-x64.zip            portable, engine included
        Fetchora-<version>[-suffix]-windows-x64-setup.exe   Inno Setup installer

    Both CI (every push to main) and the release workflow call this, so the
    installer a user downloads from a Release is built by exactly the same code
    that built the one attached to a commit. Duplicating the steps in two
    workflows is how they drift apart.

    Local use, from the repository root, after `build.ps1 -Release`:

        .\packaging\windows\package.ps1 -QtDir C:\Qt\6.10.3\mingw_64

    It picks up aria2c.exe from the repository root when -Aria2Dist is not given.
#>
param(
    # Directory to assemble the payload in. Wiped first. Empty = <root>\stage\Fetchora.
    #
    # NOTE: these defaults are resolved in the body, not here. Windows
    # PowerShell 5.1 has not populated $PSScriptRoot yet while param() defaults
    # are being evaluated, so Join-Path $PSScriptRoot ... in this block fails
    # with "Cannot bind argument to parameter 'Path' because it is an empty
    # string".
    [string] $StageDir = '',

    # Qt installation prefix (the directory containing bin\windeployqt.exe).
    [Parameter(Mandatory = $true)]
    [string] $QtDir,

    # Version for the installer's Add/Remove Programs entry and its file name.
    # Empty = read it from the executable's own version resource, so the package
    # cannot disagree with what the app reports about itself.
    [string] $AppVersion = '',

    # Where the zip and the installer are written. Empty = the repository root.
    [string] $OutDir = '',

    # Optional: an extracted official aria2 archive (the directory holding
    # aria2c.exe and COPYING). When omitted, aria2c.exe is taken from the
    # repository root, which is where build.ps1 and the workflows put it.
    [string] $Aria2Dist = '',

    # Optional: appended to the installer's file name only, never to its version
    # metadata. CI uses '-<short sha>' so a per-commit build is identifiable.
    [string] $NameSuffix = '',

    # Optional: ISCC.exe to use. Otherwise it is searched for.
    [string] $IsccPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Step([string] $message) { Write-Host "==> $message" -ForegroundColor Cyan }
function Fail([string] $message) { Write-Host "::error::$message" -ForegroundColor Red; exit 1 }

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $StageDir) { $StageDir = Join-Path $root 'stage\Fetchora' }
if (-not $OutDir) { $OutDir = $root }
$StageDir = [System.IO.Path]::GetFullPath($StageDir)
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# ---------------------------------------------------------------- payload
Step "assembling $StageDir"
if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

$exe = Join-Path $root 'build\Release\Fetchora.exe'
if (-not (Test-Path $exe)) { Fail "build\Release\Fetchora.exe is missing - run build.ps1 -Release first" }
Copy-Item $exe $StageDir

# One source of truth for the version: the executable's own version resource,
# which CMake fills from the project version. Passing -AppVersion overrides it,
# but nothing has to.
if (-not $AppVersion) {
    $info = (Get-Item $exe).VersionInfo
    $AppVersion = $info.ProductVersion
    if (-not $AppVersion) { $AppVersion = $info.FileVersion }
    if (-not $AppVersion) { Fail 'could not read a version from Fetchora.exe; pass -AppVersion' }
}
Write-Host "    version $AppVersion"

foreach ($f in @('README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'ca-bundle.crt')) {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src $StageDir }
    elseif ($f -eq 'ca-bundle.crt') { Write-Host "::warning::ca-bundle.crt is not in the checkout - shipping without a CA bundle" }
    else { Fail "$f is missing from the checkout; the package would be incomplete" }
}

# ------------------------------------------------------------ bundled engine
Step 'bundling the aria2 engine'
if ($Aria2Dist) {
    if (-not (Test-Path $Aria2Dist)) { Fail "-Aria2Dist $Aria2Dist does not exist" }
    $aria2 = Get-ChildItem $Aria2Dist -Recurse -Filter aria2c.exe | Select-Object -First 1
    if ($aria2) {
        Copy-Item $aria2.FullName (Join-Path $StageDir 'aria2c.exe')
        # GPL-2.0 section 3 wants the change log and the source offer to travel
        # with the binary; these are the files aria2's own archive ships.
        foreach ($f in @('COPYING', 'LICENSE.OpenSSL', 'AUTHORS', 'ChangeLog', 'NEWS')) {
            $src = Get-ChildItem $Aria2Dist -Recurse -Filter $f | Select-Object -First 1
            if ($src) { Copy-Item $src.FullName (Join-Path $StageDir "aria2-$f") }
            else { Write-Host "::warning::aria2's $f was not in the archive" }
        }
    } else {
        Write-Host "::warning::-Aria2Dist has no aria2c.exe; falling back to the repository root"
    }
}
if (-not (Test-Path (Join-Path $StageDir 'aria2c.exe'))) {
    $local = Join-Path $root 'aria2c.exe'
    if (Test-Path $local) {
        Copy-Item $local (Join-Path $StageDir 'aria2c.exe')
    } else {
        Fail 'no aria2c.exe: pass -Aria2Dist or put one in the repository root'
    }
}
# Record what is actually in the package instead of trusting the notice's prose:
# which libraries an aria2 build links depends on how it was configured.
& (Join-Path $StageDir 'aria2c.exe') --version |
    Out-File -FilePath (Join-Path $StageDir 'ARIA2-BUILD-INFO.txt') -Encoding utf8
Get-Content (Join-Path $StageDir 'ARIA2-BUILD-INFO.txt') |
    Select-String -Pattern 'aria2 version|Libraries:|Compiler:' | ForEach-Object { Write-Host "    $($_.Line)" }

# ------------------------------------------------------------ licence texts
Step 'copying third-party licence texts'
$licences = Join-Path $root 'third_party'
if (-not (Test-Path $licences)) { Fail 'third_party/ is missing; the package cannot be shipped without it' }
Copy-Item $licences (Join-Path $StageDir 'licenses') -Recurse
Write-Host ("    {0} licence files" -f (Get-ChildItem (Join-Path $StageDir 'licenses') -Recurse -File | Measure-Object).Count)

# ------------------------------------------------------------- Qt runtime
Step 'deploying the Qt runtime'
$windeploy = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path $windeploy)) { Fail "windeployqt.exe is not under $QtDir\bin" }
# --no-translations is safe: LanguageManager loads the app's own catalogue from
# :/i18n, never Qt's qt_*.qm. --no-system-d3d-compiler matches build.ps1 -Deploy.
& $windeploy --release --no-translations --no-system-d3d-compiler (Join-Path $StageDir 'Fetchora.exe') |
    Out-Null
if ($LASTEXITCODE -ne 0) { Fail 'windeployqt failed' }

# ------------------------------------------------------------ portable zip
Step 'compressing the portable zip'
$zip = Join-Path $OutDir 'Fetchora-windows-x64.zip'
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path $StageDir -DestinationPath $zip -Force
Write-Host ("    {0} ({1:N0} bytes)" -f (Split-Path $zip -Leaf), (Get-Item $zip).Length)

# --------------------------------------------------------------- installer
Step 'compiling the Inno Setup installer'
if (-not $IsccPath) {
    $IsccPath = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
        # Chocolatey, which is how the GitHub runner installs it, also drops a
        # shim here.
        "$env:ProgramData\chocolatey\bin\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $IsccPath) {
    Fail 'ISCC.exe not found. Install Inno Setup 6 (winget install JRSoftware.InnoSetup) or pass -IsccPath.'
}

$iss = Join-Path $PSScriptRoot 'fetchora.iss'
# The installer is written straight into OutDir, next to the zip. That is not
# cosmetic: actions/upload-artifact keeps the directory structure relative to the
# least common ancestor of the matched files, so artifacts from two different
# directories arrive nested instead of as a flat list.
& $IsccPath "/DAppVersion=$AppVersion" "/DStageDir=$StageDir" "/DBuildDir=$OutDir" $iss
if ($LASTEXITCODE -ne 0) { Fail 'ISCC failed' }

# ISCC exiting 0 does not prove the file landed where the caller will look.
$built = Get-ChildItem $OutDir -Filter 'Fetchora-*-windows-x64-setup.exe' -ErrorAction SilentlyContinue |
         Select-Object -First 1
if (-not $built) {
    $found = (Get-ChildItem $OutDir -File | Select-Object -ExpandProperty Name) -join ', '
    Fail "ISCC produced no installer; $OutDir contains: [$found]"
}
$setup = $built.FullName
if ($NameSuffix) {
    $renamed = Join-Path $OutDir ("Fetchora-$AppVersion$NameSuffix-windows-x64-setup.exe")
    Remove-Item $renamed -Force -ErrorAction SilentlyContinue
    Move-Item $setup $renamed -Force
    $setup = $renamed
}
Write-Host ("    {0} ({1:N0} bytes)" -f (Split-Path $setup -Leaf), (Get-Item $setup).Length)

Write-Host ''
Write-Host 'packaged:' -ForegroundColor Green
Write-Host "    $zip"
Write-Host "    $setup"
