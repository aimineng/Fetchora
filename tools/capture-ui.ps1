#requires -Version 5.1
<#
    tools/capture-ui.ps1 - drive the real window and capture screenshots.

    Combines the two things that need a live UI: queueing downloads through the
    browser bridge so the task list has content, and injecting a click so the
    task-details pane can be photographed with a task selected.

    Usage (from the project root):
        powershell -File tools\capture-ui.ps1 -OutDir docs\screenshots -Language zh
#>
param(
    [string] $OutDir = 'docs\screenshots',
    [ValidateSet('zh', 'en', 'system')] [string] $Language = 'zh',
    [string] $TestUrl = 'http://127.0.0.1:8898/file{n}.bin',
    [int] $Downloads = 6,
    [int] $BridgePort = 8899,
    [switch] $KeepMica
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\Release\Fetchora.exe'
$out = Join-Path $root $OutDir
if (-not (Test-Path $exe)) { throw "build the app first: $exe not found" }
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ---------------------------------------------------------------- settings
$key = 'HKCU:\Software\Fetchora\Fetchora'
if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
New-ItemProperty -Path $key -Name 'language' -Value $Language -PropertyType String -Force | Out-Null
New-ItemProperty -Path $key -Name 'theme' -Value 'dark' -PropertyType String -Force | Out-Null
if (-not $KeepMica) {
    # QWidget::grab() composites a translucent window over the desktop, which
    # lightens the capture; disabling Mica gives the true surface colours.
    New-ItemProperty -Path $key -Name 'useMica' -Value 0 -PropertyType DWord -Force | Out-Null
}

Get-Process Fetchora, aria2c -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800
$env:PATH = "D:\Qt\6.10.3\mingw_64\bin;$env:PATH"

Add-Type -Namespace Native -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string name);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
'@

function Click-At([int] $x, [int] $y) {
    [Native.Win]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 150
    [Native.Win]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # left down
    [Native.Win]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # left up
}

Write-Host "capturing (language=$Language, mica=$KeepMica) ..." -ForegroundColor Cyan

# 1. The downloads page with live tasks, and the details pane after a click.
$shot = Join-Path $out 'download.png'
Remove-Item $shot -ErrorAction SilentlyContinue
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru -ArgumentList @(
    '--new-instance', '--page', 'download',
    '--screenshot', $shot, '--screenshot-delay', '26000'
)
Start-Sleep -Seconds 9
for ($i = 1; $i -le $Downloads; $i++) {
    $body = @{ url = $TestUrl.Replace('{n}', $i) } | ConvertTo-Json
    try {
        Invoke-RestMethod -Uri "http://127.0.0.1:$BridgePort/download" -Method Post `
            -ContentType 'application/json' -Body $body -TimeoutSec 8 | Out-Null
    } catch {
        Write-Warning "could not queue download $i - is tools/testsrv.js running?"
        break
    }
}
Start-Sleep -Seconds 6

# 2. Select the first task card so the inspector has something to show.
#    MainWindowHandle is more reliable than FindWindow(): the frameless window
#    has no standard caption class to look up.
$hwnd = [IntPtr] $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { $hwnd = [Native.Win]::FindWindow($null, 'Fetchora') }
if ($hwnd -ne [IntPtr]::Zero) {
    [Native.Win]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 500
    $r = New-Object Native.Win+RECT
    [Native.Win]::GetWindowRect($hwnd, [ref] $r) | Out-Null
    Write-Host ("window rect: {0},{1} - {2},{3}" -f $r.Left, $r.Top, $r.Right, $r.Bottom) -ForegroundColor DarkGray
    # The first card sits just under the header + statistics + filter strip.
    Click-At ($r.Left + 420) ($r.Top + 275)
    Start-Sleep -Milliseconds 600
    # The inspector's 文件 tab then shows the per-file view.
    Click-At ($r.Left + 745) ($r.Top + 200)
    Write-Host "clicked into the task list" -ForegroundColor DarkGray
} else {
    Write-Warning "could not find the window - the inspector screenshot will be empty"
}
Wait-Process -Id $proc.Id -Timeout 90 -ErrorAction SilentlyContinue

# 3. The remaining pages need no interaction.
foreach ($page in @('bittorrent', 'history', 'createtorrent', 'settings', 'about')) {
    & $exe --new-instance --page $page --screenshot (Join-Path $out "$page.png") --screenshot-delay 6000
}

Get-ChildItem $out -Filter *.png | Select-Object Name, Length | Format-Table -AutoSize
