#requires -Version 5.1
<#
    make-translations.ps1 - regenerate and fill in the Qt translation catalogues.

    Step 1  lupdate  scans every .cpp/.h/.ui in the project and rewrites the
                     .ts catalogue with the strings currently in the source.
    Step 2  en.map   is a plain "中文 => English" table that this script applies
                     to translations/fetchora_en.ts.
    Step 3  lrelease compiles the filled catalogue into a .qm, which is what
                     CMake embeds into the executable (qt_add_translations).

    Usage (from the project root):
        powershell -File tools\make-translations.ps1
        powershell -File tools\make-translations.ps1 -Language en
#>
param(
    [string] $Language = 'en',
    [string] $QtBin = 'D:\Qt\6.10.3\mingw_64\bin',
    [switch] $NoLupdate,
    [switch] $NoRelease,
    # Report coverage and exit without writing anything (safe to run in parallel).
    [switch] $Check
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$utf8 = New-Object System.Text.UTF8Encoding($false)

$lupdate = Join-Path $QtBin 'lupdate.exe'
$lrelease = Join-Path $QtBin 'lrelease.exe'
$tsFile = Join-Path $root "translations\fetchora_$Language.ts"

New-Item -ItemType Directory -Force -Path (Join-Path $root 'translations') | Out-Null

# ------------------------------------------------------------------ collect
# NOTE: Get-ChildItem -Include only filters when -Path carries a wildcard (or
# -Recurse is used), so the extension check is done with Where-Object instead.
$sources = @()
foreach ($dir in @('', 'ui', 'ui\pages')) {
    $full = if ($dir) { Join-Path $root $dir } else { $root }
    if (-not (Test-Path $full)) { continue }
    $sources += Get-ChildItem -Path $full -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Extension -in '.cpp', '.h', '.ui' } |
                Where-Object { $_.Name -notlike 'ui_*' -and $_.Name -notlike 'moc_*' } |
                ForEach-Object { $_.FullName }
}
$sources = $sources | Sort-Object -Unique
Write-Host "lupdate: $($sources.Count) source files"

# ------------------------------------------------------------------ lupdate
if (-not $NoLupdate) {
    if (-not (Test-Path $lupdate)) { throw "lupdate not found at $lupdate" }
    & $lupdate -no-obsolete -locations none @sources -ts $tsFile
    if ($LASTEXITCODE -ne 0) { throw "lupdate failed" }
}

if (-not (Test-Path $tsFile)) { throw "no catalogue at $tsFile" }

# --------------------------------------------------------------------- fill
# Every translations/<lang>*.map file contributes entries, so several people (or
# agents) can translate different screens without touching one shared file.
#
# A key may optionally carry a context prefix:
#
#     [TaskDetailsPanel] 连接 => Peers      <- only inside that context
#     连接 => Connections                   <- fallback for every other context
#
# Context-qualified entries always win, which is what lets one Chinese word mean
# different things on different screens.
$map = @{}
$mapByContext = @{}
$mapFiles = @(Get-ChildItem -Path (Join-Path $root 'translations') -Filter "$Language*.map" -File -ErrorAction SilentlyContinue)
foreach ($file in $mapFiles) {
    $added = 0
    foreach ($line in [System.IO.File]::ReadAllLines($file.FullName, $utf8)) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith('#')) { continue }
        $i = $trimmed.IndexOf(' => ')
        if ($i -lt 0) { continue }
        # "\n" is the only escape: it keeps multi-line source strings on one line.
        $key = $trimmed.Substring(0, $i).Replace('\n', "`n")
        $val = $trimmed.Substring($i + 4).Replace('\n', "`n")

        if ($key.StartsWith('[')) {
            $close = $key.IndexOf(']')
            if ($close -gt 1) {
                $ctx = $key.Substring(1, $close - 1)
                $bare = $key.Substring($close + 1).TrimStart()
                if (-not $mapByContext.ContainsKey($ctx)) { $mapByContext[$ctx] = @{} }
                $mapByContext[$ctx][$bare] = $val
                $added++
                continue
            }
        }
        $map[$key] = $val
        $added++
    }
    Write-Host ("map: {0} entries in {1}" -f $added, $file.Name)
}
if ($map.Count -eq 0) {
    Write-Warning "no translation maps found in translations/ - the catalogue stays untranslated"
}

[xml]$doc = [System.IO.File]::ReadAllText($tsFile, $utf8)
$filled = 0
$missing = New-Object System.Collections.Generic.List[string]

function Resolve-Translation {
    param([string] $Context, [string] $Source)
    if ($mapByContext.ContainsKey($Context) -and $mapByContext[$Context].ContainsKey($Source)) {
        return $mapByContext[$Context][$Source]
    }
    if ($map.ContainsKey($Source)) { return $map[$Source] }
    return $null
}

foreach ($ctx in $doc.DocumentElement.SelectNodes('context')) {
    $ctxName = $ctx.name
    foreach ($message in $ctx.SelectNodes('message')) {
        $source = $message.source.'#text'
        if (-not $source) { $source = $message.source }
        $node = $message.SelectSingleNode('translation')
        if (-not $node) {
            $node = $doc.CreateElement('translation')
            [void] $message.AppendChild($node)
        }
        if ($node.Attributes['type']) { $node.Attributes.Remove($node.Attributes['type']) }

        $value = Resolve-Translation -Context $ctxName -Source $source
        if ($null -ne $value) {
            $node.InnerText = $value
            $filled++
        } elseif (-not $node.InnerText) {
            $missing.Add($source)
        }
    }
}

if ($Check) {
    # Read-only report: how many messages each context still needs, and which
    # source strings the maps do not cover yet. Safe to run concurrently.
    Write-Host "`n--- coverage by context (read-only) ---"
    foreach ($ctx in $doc.DocumentElement.SelectNodes('context')) {
        $pending = @()
        foreach ($m in $ctx.SelectNodes('message')) {
            $src = $m.source.'#text'
            if (-not $src) { $src = $m.source }
            $tr = $m.translation
            if ($tr -is [System.Xml.XmlElement]) { $tr = $tr.InnerText }
            if ((-not $tr) -and ($null -eq (Resolve-Translation -Context $ctx.name -Source $src))) {
                $pending += $src
            }
        }
        if ($pending.Count -gt 0) {
            Write-Host ("{0,-22} {1,4} missing" -f $ctx.name, $pending.Count) -ForegroundColor Yellow
        }
    }
    Write-Host "`n--- missing source strings ---"
    $missing | Sort-Object -Unique | ForEach-Object { Write-Host $_ }
    Write-Host ("`n{0} message(s) would still be untranslated." -f ($missing | Sort-Object -Unique).Count)
    exit 0
}

$doc.Save($tsFile)
Write-Host "filled $filled message(s) in $Language.ts" -ForegroundColor Green
if ($missing.Count -gt 0) {
    Write-Host "$($missing.Count) message(s) still untranslated:" -ForegroundColor Yellow
    $missing | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
}

# ------------------------------------------------------------------ lrelease
if (-not $NoRelease) {
    if (-not (Test-Path $lrelease)) { throw "lrelease not found at $lrelease" }
    & $lrelease $tsFile -qm (Join-Path $root "translations\fetchora_$Language.qm")
    if ($LASTEXITCODE -ne 0) { throw "lrelease failed" }
    Write-Host "compiled translations\fetchora_$Language.qm" -ForegroundColor Green
}
