# make-icons.ps1 - regenerate every icon asset from code.
#
# The application has no binary image sources: the logo is drawn
# programmatically so it stays crisp at any size and the repository carries no
# opaque assets. Run this after changing the artwork below.
#
# Outputs:
#   app.ico                     multi-size Windows icon (compiled into the exe
#                               by version.rc, so it must sit next to it)
#   resources/app.ico           same file, kept with the other generated assets
#   resources/app-<size>.png    standalone PNGs (docs, store listings, ...)
#   Plugin/icons/icon<size>.png browser extension icons
#   Plugin/icon.png             128 px copy used by the extension manifest

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$Root = $PSScriptRoot
$ResDir = Join-Path $Root "resources"
$PluginIcons = Join-Path $Root "Plugin\icons"
New-Item -ItemType Directory -Force -Path $ResDir, $PluginIcons | Out-Null

# Fluent-style blue plate with a white download arrow. Keep these two colours
# in sync with Theme.qml's defaultAccent.
$GradientTop = [System.Drawing.Color]::FromArgb(255, 74, 170, 232)
$GradientBottom = [System.Drawing.Color]::FromArgb(255, 12, 92, 168)

function New-LogoBitmap {
    param([int]$Size)

    $bmp = New-Object System.Drawing.Bitmap($Size, $Size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    $s = [double]$Size

    # --- rounded "squircle" plate -----------------------------------------
    $r = [Math]::Max(2.0, $s * 0.225)
    $d = $r * 2.0
    $plate = New-Object System.Drawing.Drawing2D.GraphicsPath
    $plate.AddArc(0, 0, $d, $d, 180, 90)
    $plate.AddArc($s - $d, 0, $d, $d, 270, 90)
    $plate.AddArc($s - $d, $s - $d, $d, $d, 0, 90)
    $plate.AddArc(0, $s - $d, $d, $d, 90, 90)
    $plate.CloseFigure()

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point(0, 0)),
        (New-Object System.Drawing.Point(0, $Size)),
        $GradientTop, $GradientBottom)
    $g.FillPath($brush, $plate)

    # Soft highlight across the top half for depth.
    $gloss = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point(0, 0)),
        (New-Object System.Drawing.Point(0, [int]($s * 0.5))),
        [System.Drawing.Color]::FromArgb(70, 255, 255, 255),
        [System.Drawing.Color]::FromArgb(0, 255, 255, 255))
    $g.FillPath($gloss, $plate)
    $g.SetClip($plate)

    # --- download arrow ----------------------------------------------------
    $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    $cx = $s / 2.0
    $stemTop = $s * 0.20
    $stemBottom = $s * 0.50
    $half = $s * 0.085

    $arrow = New-Object System.Drawing.Drawing2D.GraphicsPath
    $arrow.AddPolygon(@(
        (New-Object System.Drawing.PointF([float]($cx - $half), [float]$stemTop)),
        (New-Object System.Drawing.PointF([float]($cx + $half), [float]$stemTop)),
        (New-Object System.Drawing.PointF([float]($cx + $half), [float]$stemBottom)),
        (New-Object System.Drawing.PointF([float]($cx + $s * 0.235), [float]$stemBottom)),
        (New-Object System.Drawing.PointF([float]$cx, [float]($s * 0.735))),
        (New-Object System.Drawing.PointF([float]($cx - $s * 0.235), [float]$stemBottom)),
        (New-Object System.Drawing.PointF([float]($cx - $half), [float]$stemBottom))
    ))
    $g.FillPath($white, $arrow)

    # --- baseline bar ------------------------------------------------------
    $barW = $s * 0.50
    $barH = [Math]::Max(1.4, $s * 0.075)
    $bx = $cx - $barW / 2.0
    $by = $s * 0.80
    $bar = New-Object System.Drawing.Drawing2D.GraphicsPath
    $bar.AddArc($bx, $by, $barH, $barH, 90, 180)
    $bar.AddArc($bx + $barW - $barH, $by, $barH, $barH, 270, 180)
    $bar.CloseFigure()
    $g.FillPath($white, $bar)

    $g.ResetClip()
    $g.Dispose()
    $arrow.Dispose(); $bar.Dispose(); $plate.Dispose()
    $brush.Dispose(); $gloss.Dispose(); $white.Dispose()
    return $bmp
}

function Get-PngBytes {
    param([System.Drawing.Bitmap]$Bitmap)
    $ms = New-Object System.IO.MemoryStream
    $Bitmap.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $ms.ToArray()
    $ms.Dispose()
    return $bytes
}

# ---------------------------------------------------------------- render all
# Windows picks the closest match, so cover the common DPI-driven sizes.
# Keys are strings: an [ordered] dictionary is an IDictionary, and indexing it
# with an int would be interpreted as a positional index, not a key.
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$pngs = [ordered]@{}
foreach ($size in $sizes) {
    $bmp = New-LogoBitmap -Size $size
    $pngs["$size"] = Get-PngBytes -Bitmap $bmp
    $bmp.Save((Join-Path $ResDir "app-$size.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# --------------------------------------------------- multi-size .ico (Vista+)
# PNG-compressed entries, which is what Windows expects at 256 px.
# The byte list is assembled explicitly rather than through BinaryWriter: the
# writer's overload resolution on a PowerShell array is not reliable for the
# payload and silently drops the image data.
$headerSize = 6 + 16 * $sizes.Count
$entries = @()
$offset = $headerSize
foreach ($size in $sizes) {
    $data = $pngs["$size"]
    # Width/height are single bytes; 0 encodes 256.
    $dim = $size
    if ($size -ge 256) { $dim = 0 }
    $entries += [pscustomobject]@{
        Dim  = [int]$dim
        Len  = [int]$data.Length
        Off  = [int]$offset
        Data = $data
    }
    $offset += $data.Length
}

$bytes = New-Object System.Collections.Generic.List[byte]
function Add-U16 { param([int]$v) $bytes.Add([byte]($v -band 0xFF)); $bytes.Add([byte](($v -shr 8) -band 0xFF)) }
function Add-U32 { param([long]$v)
    $bytes.Add([byte]($v -band 0xFF))
    $bytes.Add([byte](($v -shr 8) -band 0xFF))
    $bytes.Add([byte](($v -shr 16) -band 0xFF))
    $bytes.Add([byte](($v -shr 24) -band 0xFF))
}

Add-U16 0                      # reserved, must be 0
Add-U16 1                      # type: 1 = icon
Add-U16 $sizes.Count           # number of images

foreach ($e in $entries) {
    $bytes.Add([byte]$e.Dim)   # width
    $bytes.Add([byte]$e.Dim)   # height
    $bytes.Add([byte]0)        # palette entry count (0 for true colour)
    $bytes.Add([byte]0)        # reserved
    Add-U16 1                  # colour planes
    Add-U16 32                 # bits per pixel
    Add-U32 $e.Len             # payload size
    Add-U32 $e.Off             # payload offset
}
foreach ($e in $entries) {
    foreach ($b in $e.Data) { $bytes.Add([byte]$b) }
}

$ico = $bytes.ToArray()
$expected = $headerSize + ($entries | Measure-Object -Property Len -Sum).Sum
if ($ico.Length -ne $expected) {
    throw "icon assembly produced $($ico.Length) bytes, expected $expected"
}

# version.rc references app.ico relatively, so it must exist in the project
# root as well as in resources/.
[System.IO.File]::WriteAllBytes((Join-Path $Root "app.ico"), $ico)
[System.IO.File]::WriteAllBytes((Join-Path $ResDir "app.ico"), $ico)

# ------------------------------------------------------------ extension icons
foreach ($size in @(16, 32, 48, 128)) {
    [System.IO.File]::WriteAllBytes((Join-Path $PluginIcons "icon$size.png"), $pngs["$size"])
}
# 256 px is handy for store listings and the extension's larger surfaces.
[System.IO.File]::WriteAllBytes((Join-Path $PluginIcons "icon256.png"), $pngs["256"])
Copy-Item (Join-Path $PluginIcons "icon128.png") (Join-Path $Root "Plugin\icon.png") -Force

# ------------------------------------------------------------- macOS icon
# app.icns is a container of these same PNGs, so it is regenerated from them by a
# tiny Node script rather than with iconutil (which only exists on macOS). Node
# is optional: without it the macOS bundle just gets the generic app icon.
$icns = Join-Path $Root "tools\make-icns.js"
if ((Test-Path $icns) -and (Get-Command node -ErrorAction SilentlyContinue)) {
    & node $icns
    if ($LASTEXITCODE -ne 0) { throw "make-icns.js failed" }
} else {
    Write-Warning "node not found - skipped app.icns (macOS bundles will use the default icon)."
}

Write-Host "Generated:"
Write-Host ("  app.ico, resources\app.ico      {0,9:N0} bytes ({1} images)" -f $ico.Length, $sizes.Count)
if (Test-Path (Join-Path $Root "app.icns")) {
    Write-Host ("  app.icns                        {0,9:N0} bytes" -f (Get-Item (Join-Path $Root "app.icns")).Length)
}
foreach ($size in $sizes) {
    Write-Host ("  resources\app-{0,-3}.png        {1,9:N0} bytes" -f $size, $pngs["$size"].Length)
}
foreach ($size in @(16, 32, 48, 128, 256)) {
    Write-Host ("  Plugin\icons\icon{0,-3}.png     {1,9:N0} bytes" -f $size, $pngs["$size"].Length)
}
