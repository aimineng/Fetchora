<div align="center">

<img src="resources/app-256.png" width="112" alt="Fetchora icon">

# Fetchora

**A fast, fluent, aria2-powered download manager for Windows, macOS and Linux.**

Multi-protocol downloads · full BitTorrent client · a real Fluent 2 / WinUI 3 interface
built with Qt 6 Widgets — and **zero QML**.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/aimineng/Fetchora/actions/workflows/ci.yml/badge.svg)](https://github.com/aimineng/Fetchora/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/aimineng/Fetchora?include_prereleases&sort=semver)](https://github.com/aimineng/Fetchora/releases)
[![Qt](https://img.shields.io/badge/Qt-6.5%2B-41CD52.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-0078D4.svg)](#requirements)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](#build-from-source)

[简体中文](README.zh-CN.md) · [Features](#features) · [Build](#build-from-source) · [Shortcuts](#keyboard-shortcuts) · [FAQ](#faq)

</div>

---

## Why another download manager?

Because the fast ones look like 2009, and the pretty ones are slow. Fetchora puts the
battle-tested [aria2](https://aria2.github.io/) engine behind an interface that follows the
Windows 11 design language properly: Mica material, layered surfaces, 1 px lift strokes,
the system accent colour, Segoe Fluent Icons glyphs and Segoe UI Variable typography.

Everything the engine can do is exposed. Nothing is a "coming soon" placeholder.

On macOS and Linux the same interface runs on an ordinary native window — the Mica
backdrop, the accent-colour lookup and the Fluent icon font are Windows-only, so the
window keeps its platform frame and title bar, and the UI falls back to the platform's own
fonts and dark-mode setting. Everything else (downloads, BitTorrent, history, torrent
creator, browser bridge, tray icon where the desktop provides one) is identical.

## Features

### Downloading
- **HTTP / HTTPS / FTP / SFTP** with multi-connection segmented transfer.
- **BitTorrent** — magnet links, `.torrent` files, DHT, DHT6, PEX, LPD, MSE encryption,
  metadata exchange, seeding ratio/time limits, per-task tracker editing.
- **Metalink** (`.metalink` / `.meta4`) with automatic mirror selection.
- **Resume** everything, including across restarts, with a persistent aria2 session.
- **Per-task and global speed limits**, per-server connection caps, disk cache tuning.
- **Queue management** — reorder waiting tasks (move up / down / to top), pause-all and
  resume-all.
- **File-level selection** for multi-file torrents.
- **Scheduler** — start and stop the engine, and swap speed limits, on a daily window.

### Interface
- **Fluent 2 / WinUI 3** design: Mica backdrop, rounded corners, real dark/light themes,
  system accent colour, Fluent switches, sliders, combo boxes and progress bars — all
  hand-painted in C++ so they match the Windows 11 controls exactly.
- **Frameless window** with a native-behaving caption bar (snap layouts, double-click to
  maximise, drag-to-restore all work).
- **Live statistics** — download/upload speed, active/queued/completed counters, lifetime
  sizes, true average speed per task.
- **Task inspector** — overview numbers, per-file progress, connected peers, servers/URIs
  and the raw aria2 option map of the selected task.
- **Download history** in SQLite, searchable and filterable, with one-click re-download and
  a batch-selection mode for deleting several entries at once.
- **Torrent creator** — build standards-compliant `.torrent` files (tracker tiers, web
  seeds, private flag, automatic piece length) and inspect any existing torrent.
- **Command bar + filter chips + search**, keyboard shortcuts, tray icon with a live speed
  tooltip, native toast notifications.
- **Chinese / English** UI with instant runtime switching.

### Integration
- **Browser extension** (Manifest V3, Chromium) that hands downloads, magnets and
  `.torrent` files to the app over a self-hosted WebSocket bridge — see [`Plugin/`](Plugin).
- **JSON-RPC server** so any third-party aria2 client can drive the same engine.
- **Single instance** — a second launch forwards its URLs to the running window.
- **Command line** — pass URLs, magnets or `.torrent` paths directly.

### Reliability
- **The engine cannot outlive the app.** aria2c is started with
  `--stop-with-process=<our pid>`, so it shuts itself down - session file and all - the
  moment Fetchora is gone, however it went; on Windows it also runs inside a job object with
  kill-on-close, which does not depend on the engine cooperating. A crash, a task-manager
  kill or a debugger stop therefore cannot leave an orphaned downloader behind.
- **The engine supervises itself** — if aria2c dies while Fetchora is running it is started
  again within a couple of seconds, and both the death and the recovery are reported.
- **Logs with automatic housekeeping** — one file per day, split at 4 MB, pruned after
  7 days (configurable), with a crash handler that writes a minidump and a backtrace.
  See [Logs and crash reports](#logs-and-crash-reports).

## Screenshots

Captured from the real window with
`Fetchora --page <page> --screenshot shot.png --screenshot-delay 6000`
(`Fetchora.exe` on Windows).

**Downloads, with the task inspector open**

![Downloads page with the task inspector](docs/screenshots/download.png)

| BitTorrent | Settings |
| --- | --- |
| ![BitTorrent page](docs/screenshots/bittorrent.png) | ![Settings page](docs/screenshots/settings.png) |

| Create torrent | History |
| --- | --- |
| ![Create torrent page](docs/screenshots/createtorrent.png) | ![History page](docs/screenshots/history.png) |

![About page](docs/screenshots/about.png)

> `QWidget::grab()` composites a translucent (Mica) window over the desktop, which
> lightens the capture. The screenshots above were taken with Mica disabled so the real
> surface colours are visible; the app looks identical either way apart from the
> background tint. `tools/capture-ui.ps1` reproduces all of them.

## Requirements

### Installing

| Platform | How |
| --- | --- |
| Windows 10/11 (x64) | Run `Fetchora-*-windows-x64-setup.exe`. No administrator rights needed, and **aria2 is included**. |
| Windows, portable | Unpack `Fetchora-windows-x64.zip` anywhere and run `Fetchora.exe`. Same files, engine included. |
| macOS | Open `Fetchora-macos-*.dmg` and drag Fetchora into Applications. |
| Linux | Unpack `Fetchora-linux-*.tar.gz`; see the notes in the release for the Qt packages it needs. |

Grab them from [Releases](https://github.com/aimineng/Fetchora/releases).

### Running
| | |
| --- | --- |
| OS | Windows 10 1809+, macOS 12+, or a modern Linux desktop (X11 or Wayland) |
| Engine | `aria2c` **1.36 or newer** — bundled in the Windows packages, installed separately on macOS and Linux |
| Runtime | The Qt 6 runtime libraries (`windeployqt` on Windows, a normal package install or `macdeployqt` elsewhere) |

Mica and the rounded window corners need Windows 11 22H2+; on Windows 10 the window
simply paints its own opaque surface. On macOS and Linux the window uses the native frame
and title bar.

**Where the engine comes from.** The Windows packages carry the official aria2 1.37.0
build, so they work as downloaded. The macOS and Linux packages do not: a `brew`- or
`apt`-built aria2 is dynamically linked against that system's own libraries, so copying
just the executable would produce something that cannot start elsewhere — install it with
`brew install aria2` or your distribution's package instead.

When there is no bundled engine, the app looks for `aria2c` in this order: the path
configured in Settings → RPC/引擎, the application directory (and, in a macOS bundle,
`Contents/Resources`), `/opt/homebrew/bin`, `/usr/local/bin`, `/usr/bin`, then `PATH`.

### Building
| | |
| --- | --- |
| Compiler | MinGW-w64 GCC 13+ **or** MSVC 2019+ |
| Qt | 6.5 or newer — modules `Widgets`, `Network`, `Sql`, `Svg`, `Concurrent`, `LinguistTools` |
| CMake | 3.21 or newer |

## Build from source

The project builds with plain CMake on all three platforms; the helper scripts only wrap
a configure + build + test cycle.

### Prerequisites

| Platform | Install |
| --- | --- |
| Windows | Qt 6.5+ (`Widgets`, `Network`, `Sql`, `Svg`, `Concurrent`, `LinguistTools`) and MinGW-w64 GCC 13+ **or** MSVC 2019+ |
| macOS | `brew install qt aria2` |
| Debian / Ubuntu | `sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev libqt6sql6-sqlite aria2 ca-certificates` |
| other Linux | the same four Qt bits under your distribution's names — `qt6-base-devel`, `qt6-svg-devel` and the Qt 6 SQLite driver |

`cmake` 3.21 or newer is required everywhere. `LinguistTools` ships with the Qt base
development package on every one of these; on Debian/Ubuntu the SQLite driver is a
separate package (`libqt6sql6-sqlite`) that `qt6-base-dev` only recommends, and
`qt6-svg-dev` is needed for the SVG icon plumbing.

### Windows (PowerShell)

```powershell
git clone https://github.com/aimineng/Fetchora.git
cd Fetchora

# Debug build
.\build.ps1

# Release build, then launch
.\build.ps1 -Release -Run

# Release build + the headless self-tests
.\build.ps1 -Release -Test

# Release build + a portable folder with the Qt runtime
.\build.ps1 -Release -Deploy
```

`build.ps1` assumes the default Qt install layout. Override it if yours differs:

```powershell
.\build.ps1 -Release `
    -QtDir    "C:\Qt\6.10.3\mingw_64" `
    -MingwDir "C:\Qt\Tools\mingw1310_64" `
    -CMake    "C:\Qt\Tools\CMake_64\bin\cmake.exe"
```

### macOS

```sh
brew install qt aria2
git clone https://github.com/aimineng/Fetchora.git
cd Fetchora

./build.sh              # Release build; finds Homebrew's Qt automatically
./build.sh --test       # + the headless self-tests
./build.sh --run        # build, then launch
```

The result is the bundle `build/Release/Fetchora.app`. To launch it from a terminal:

```sh
open build/Release/Fetchora.app
# or, for the process's own stdout/stderr:
build/Release/Fetchora.app/Contents/MacOS/Fetchora --self-test
```

`build.sh` asks `brew --prefix qt` for the Qt prefix. If you use the official Qt online
installer instead, point it at the prefix yourself:

```sh
QT_PREFIX="$HOME/Qt/6.10.3/macos" ./build.sh
```

To make the bundle self-contained (Qt frameworks inside `Contents/Frameworks`):

```sh
"$(brew --prefix qt)/bin/macdeployqt" build/Release/Fetchora.app
```

`aria2` is an external program and is **not** copied into the bundle by macdeployqt;
`build.sh` copies the Homebrew binary into `Contents/Resources/`, where the app looks for
it. For distribution you should instead declare it as a dependency (both Homebrew and
MacPorts ship `aria2`) or bundle a signed copy.

### Linux

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev libqt6sql6-sqlite aria2 ca-certificates
git clone https://github.com/aimineng/Fetchora.git
cd Fetchora

./build.sh              # Release build -> build/Release/Fetchora
./build.sh --test       # + the headless self-tests
./build.sh --run        # build, then launch
./build.sh --install --prefix "$HOME/.local"   # desktop entry + icon + binary
```

The binary is `build/Release/Fetchora`; `build.sh` copies `aria2c` and `ca-bundle.crt`
next to it when it can find them, so the app finds the engine without any configuration.

`cmake --install` additionally installs `fetchora.desktop` into
`<prefix>/share/applications` and `fetchora.png` into the hicolor icon theme, so the app
shows up in the desktop's application menu. Packagers who would rather not run CMake can
use `packaging/fetchora.desktop` as-is.

A tray icon needs a StatusNotifier host (or the legacy XEmbed tray). On a desktop without
one the app detects this, logs it, and makes **close** mean **quit** instead of hiding the
window in a tray that does not exist.

### Plain CMake

```sh
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release --parallel
```

On Windows with MinGW, add the generator and prefix (see the PowerShell form further up).
The executable lands in `build/Release/` as `Fetchora.exe`, `Fetchora` or `Fetchora.app`,
and `aria2c`/`aria2c.exe` plus `ca-bundle.crt` are copied next to it automatically when
they exist in the project root.

### Self-tests

```sh
./build.sh --test          # macOS / Linux
.\build.ps1 -Release -Test # Windows
```

This validates the things that fail silently in a download manager:

1. **The update logic.** How versions are ordered (tags with and without a leading
   `v`, pre-releases, `0.1.10` against `0.1.9`) and which release asset each
   platform is offered — including the order the GitHub API happens to list the
   assets in. No network here; `--check-updates` is the mode that talks to GitHub.
2. **The aria2c command line.** Every switch the settings layer generates is checked
   against `aria2c --help=#all`. An unknown switch makes aria2 exit with code 28 and the
   engine never comes up.
3. **The bencode output.** A torrent is created from a nested directory, re-read, and the
   info hash is compared both ways — then aria2 itself is asked to parse it.

A failing check is printed with the exit code that says which group it came from: 5 is the
update logic, 2–4 are the engine checks.

## Translations

The source language is **Simplified Chinese**; English ships as a compiled catalogue.
Switching is instant and needs no restart (Settings → 常规 → 语言, or the tray menu).

```powershell
# Re-extract the strings after changing the UI
cmake --build build\Release --target update_translations
# then edit translations/fetchora_en.ts and rebuild
```

To add a language: copy `translations/fetchora_en.ts` to `translations/fetchora_<code>.ts`,
translate it, add the code to `LanguageManager::availableLanguages()` and to the `foreach`
list in `CMakeLists.txt`. Pull requests are welcome.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+N` | New download |
| `Ctrl+O` | Open a `.torrent` / `.metalink` file |
| `F5` | Refresh now |
| `Ctrl+,` | Open settings |
| `Ctrl+Q` | Quit |
| Double-click a task | Open it (complete) / pause it (active) / resume it (paused) |

## Command line

```
Fetchora [options] [urls...]      # Fetchora.exe on Windows

  -m, --minimized            Start hidden in the system tray
      --maximized            Start maximized
      --page <key>           Open on a page: download, queue, bittorrent, history,
                             createtorrent, settings, about
      --new-instance         Do not forward to a running instance
      --screenshot <file>    Render the window to a PNG and exit
      --screenshot-delay <ms>  Wait before --screenshot
      --self-test            Validate the update logic and the aria2c command line
      --check-updates        Ask GitHub for the newest release and exit
      --prerelease           Include pre-releases in --check-updates
      --make-torrent <src>   Create a .torrent (with --output, --tracker)
      --inspect-torrent <f>  Print a .torrent's contents
  -h, --help                 Show help
  -v, --version              Show version
```

## Updates

Fetchora is distributed through GitHub Releases, so the release list *is* the
update feed — there is no update server of ours to go down, and it is the same
place you would go to download it by hand.

- A few seconds after launch the app asks GitHub whether a newer release exists.
  If there is one you get a toast and a notification, **once per version** — not
  on every launch until you give in. *Check for updates at startup* under
  Settings → About → Updates turns it off.
- **About → Check for updates** does it on demand and shows what it found: the
  new version, when it was published, and an excerpt of its release notes.
  *Include pre-releases* (same settings card) decides whether pre-releases count;
  the About page shows the channel it is using next to the version.
- On Windows, **下载并安装** fetches the installer into
  `%TEMP%\Fetchora\updates` and then hands it to the system. The installer is an
  Inno Setup package, so it closes Fetchora, replaces the files and offers to
  start the app again. On macOS the downloaded `.dmg` is opened for you. On Linux
  the release page is opened instead, because the tarball is unpacked by hand.

Check it from a terminal — this is the same code the button runs, not a second
implementation:

```console
$ Fetchora --check-updates
current: 0.1.4
channel: stable
latest:  0.1.5  (tag v0.1.5)
published: 2026-10-02T09:12:44Z
prerelease: no
asset for this platform: Fetchora-0.1.5-windows-x64-setup.exe
update available: yes
RESULT: update available
```

**What the update check does not guarantee.** The releases are not code-signed,
so nothing verifies that a downloaded binary was built by us. What HTTPS to
`api.github.com` buys is that the answer came from GitHub — not that the file is
authentic. Fetchora says so rather than implying a guarantee it cannot make: it
never installs anything without asking, and it tells you the exact path it
downloaded to. If that is not good enough for your threat model, download from
the [Releases page](https://github.com/aimineng/Fetchora/releases) yourself, or
build from source.

## Browser extension

`Plugin/` contains a Manifest V3 extension for Chromium browsers (Edge, Chrome, Brave,
Vivaldi…). It intercepts downloads, magnet links and `.torrent` responses and pushes them
to the app over a local WebSocket bridge.

1. Start Fetchora and enable **设置 → 通知与集成 → 浏览器集成**.
2. Open `edge://extensions` (or `chrome://extensions`) and turn on *Developer mode*.
3. Choose **Load unpacked** and select the `Plugin` folder.

The bridge listens on `127.0.0.1:8899` by default; the port is configurable on both sides.

## Logs and crash reports

Fetchora keeps a log file, because "it just closed itself" is impossible to act on and a
log line is not.

| | |
| --- | --- |
| Where | `%LOCALAPPDATA%\Fetchora\logs` on Windows, `~/Library/Application Support/Fetchora/logs` on macOS, `~/.local/share/Fetchora/logs` on Linux (Settings → Advanced → Logs opens it) |
| Files | `fetchora-YYYY-MM-DD.log`, one per day |
| Contents | startup banner (version, OS, Qt, paths), engine stdout/stderr, warnings, every exit path, crash reports |
| Rotation | a file that passes 4 MB is renamed to `fetchora-YYYY-MM-DD-HHMMSS.log` and a new one is started |
| Cleanup | files older than 7 days are deleted at startup and once a day afterwards; the folder is also capped at 20 MB. *Days of logs to keep* changes the window |
| Flushing | every line is flushed as it is written, so the last line before a crash is on disk |

Every exit is logged with its reason, which is what makes a "flash quit" answerable:
`forwarded to the running instance and exiting` (a second launch handing its links over),
`window closed: hidden to the tray, still running`, `event loop finished (exit code 0)`, or
- if it really did crash - a `[F] [crash]` block.

On Windows a crash also writes `crash-YYYYMMDD-HHMMSS.dmp` (a minidump) plus the exception
code, the module base and a backtrace of return addresses. MinGW builds emit
`build/Release/Fetchora.map`, and the addresses in the log are offsets from that module
base, so `addr2line -e Fetchora.exe -f -C 0x<offset>` (or a look in the map) names the
function. Please attach the log - and the `.map` of the build you are running - to a bug
report.

## Project layout

```
.
├── main.cpp                 Entry point: shell, tray, shortcuts, CLI tools
├── Aria2Client.*            Complete aria2 JSON-RPC client (notifications + callbacks)
├── Aria2Process.*           aria2c child-process lifetime
├── Aria2Manager.*           Core: task model, polling, every user operation
├── SettingsManager.*        ~120 settings, aria2 argument + runtime option builder
├── DownloadHistory.*        SQLite history store
├── TorrentUtils.*           Bencode codec, torrent create / inspect / magnet
├── HttpServer.*             Combined HTTP + hand-written RFC 6455 WebSocket bridge
├── ui/                      The Fluent widget set (theme, chrome, controls, task list)
│   ├── FluentTheme.*        Design tokens + generated application style sheet
│   ├── FluentMainWindow.*   Frameless window, Mica, WM_NCHITTEST / WM_NCCALCSIZE
│   ├── FluentButton.*       Self-painted Fluent button (6 roles)
│   ├── FluentWidgets.*      Icon, card, info bar, stat card, progress bar, toasts
│   ├── FluentInputs.*       Text field, spin box, switch, check/radio, slider, combo
│   ├── FluentTitleBar.*     Caption bar
│   ├── FluentNavigationView.* WinUI navigation pane
│   ├── FluentTaskList.*     Task cards + the in-place updating list
│   ├── LanguageManager.*    Runtime zh/en switching
│   └── pages/               One .ui + .h + .cpp per screen
├── packaging/               Linux desktop entry (fetchora.desktop + .in)
├── Plugin/                  Chromium MV3 browser extension
├── tools/                   Developer helpers (syntax check, screen capture, icons)
├── translations/            Qt .ts catalogues
├── build.ps1                Windows build helper
└── build.sh                 macOS / Linux build helper
```

### Icons

The logo has no binary source: `make-icons.ps1` draws it with `System.Drawing` and writes
`app.ico` (compiled into the Windows executable by `version.rc`), `resources/app-<size>.png`
and the browser-extension icons. The app also paints the same artwork at runtime with
`QPainter` (`makeAppIcon()` in `main.cpp`), so the in-app window/tray icon is correct on
every platform with no files involved at all.

| Platform | Format | Where it comes from |
| --- | --- | --- |
| Windows | `.ico` | `app.ico`, referenced by `version.rc`, embedded by the resource compiler |
| macOS | `.icns` | `app.icns`, generated from `resources/app-*.png` by `node tools/make-icns.js`, copied into `Fetchora.app/Contents/Resources` and named in `Info.plist` |
| Linux | `.png` | `resources/app-256.png`, installed as `fetchora.png` into the hicolor theme by `cmake --install` |

`tools/make-icns.js` is a plain container repackager — it writes the `icns` header and one
PNG element per size, using the PNGs that already exist, so no artwork is invented. If
`app.icns` is absent, CMake configures cleanly and simply leaves `CFBundleIconFile` empty,
in which case Finder shows the generic application icon; a packager who wants a different
one can drop any real `.icns` in as `app.icns` (or run `iconutil -c icns` on an iconset
built from higher-resolution artwork).

### How the UI is put together

- **Structure lives in `.ui` files.** Every page is a `.ui` + `.h` + `.cpp` triple;
  `AUTOUIC` generates the headers. C++ only fills in dynamic content, wires signals and
  applies theme-dependent styling.
- **Look lives in generated style sheets.** Colours, radii and states change at runtime
  when the theme or the system accent changes, which a static `.ui` cannot express — so
  `FluentTheme::applicationStyleSheet()` produces the whole application sheet and every
  widget connects to `FluentTheme::changed`.
- **Controls Qt cannot style are painted.** The Fluent switch knob, the slider thumb, the
  checkbox tick, the combo chevron and the task cards are all `paintEvent` code, which is
  what makes them match the Windows 11 controls instead of merely resembling them.
- **The list never rebuilds.** aria2 is polled once a second; task cards are updated in
  place so the scroll position and the selection survive every poll.

## FAQ

**The engine never starts.**
Run `Fetchora --self-test` (`Fetchora.exe` on Windows). Nine times out of ten it is a
missing `aria2c` (install it: it is a separate package on every platform), an unknown
switch in 设置 → RPC/引擎 → 附加命令行参数, or a port that is already in use.

**The window flashes and disappears.**
Almost always the Qt runtime is missing next to the executable, so Windows refuses to load
it and *nothing of ours runs at all* - no window, no error, no log line. That happens when
the exe is copied out of a deployed folder, or run from a build tree whose Qt DLLs were
never deployed. It cannot happen with the packaged builds, and `cmake --build` deploys the
DLLs automatically now; `.\build.ps1 -Release -Deploy` does the same by hand. If the app
does start and then closes, the log says why: a second launch
(`another instance is already running`), closing to the tray, or a crash (a `[crash]`
block). See [Logs and crash reports](#logs-and-crash-reports).

**Downloads disappear from the list while the window is closed.**
They have not: the engine keeps downloading, and closing the window only hides it. The tray
icon shows the live speed, and *Show* brings the window back.

**Mica looks washed out in screenshots.**
`QWidget::grab()` composites the translucent window over the desktop, so the capture is
lighter than the real window. Disable Mica in settings to see the true surface colours.
Mica itself only exists on Windows 11 22H2+; macOS and Linux always use the opaque
surface.

**Downloads leave `.aria2` control files behind.**
That is aria2 keeping the resume state while it seeds. Turn off seeding (or enable
*移除控制文件*) and they are cleaned up.

**Closing the window does not quit the app.**
That is "close to tray". On a Linux desktop without a StatusNotifier host there is no tray
to minimise into, so the app detects that and closes for real instead.

**Why is there no "first launch" animation any more?**
Removed on purpose. There is a 9-second startup grace period that shows *引擎启动中*
instead of a red *未连接*; a status light that pulses is a distraction, not information.

## Contributing

Issues and pull requests are welcome. Before opening a PR:

```powershell
.\build.ps1 -Release -Test
powershell -File tools\check-syntax.ps1 -Sources ui\pages\YourPage.cpp   # Windows only
```

```sh
./build.sh --test
```

Please keep the two rules that make this codebase readable: **structure in `.ui`, look in
`FluentTheme`**, and **every user-visible string inside `tr()`**.

Everything platform specific follows the same rule: a `#ifdef Q_OS_WIN` block always has a
working non-Windows path next to it, and every such choice carries a comment saying why.

## Acknowledgements

- [aria2](https://aria2.github.io/) — the engine that does the actual work, by Tatsuhiro Tsujikawa.
- [Qt 6](https://www.qt.io/) — the application framework.
- **Segoe Fluent Icons** and **Segoe UI Variable** — Microsoft's Windows 11 icon and text
  faces, used through the system font stack.

## Platform notes

Where the three platforms differ, and why:

| Area | Windows | macOS / Linux |
| --- | --- | --- |
| Window | Frameless; the app paints the caption bar and keeps the native frame for shadow, snapping and resize borders (`WM_NCCALCSIZE` / `WM_NCHITTEST` / `WM_GETMINMAXINFO`) | Ordinary native window with a normal title bar, so the window can always be moved, zoomed and closed. The in-window caption row is still drawn as a header |
| Backdrop | Mica / Mica Alt / Acrylic through `DwmSetWindowAttribute`, rounded corners, immersive dark title bar | None (there is no Mica API); the window paints its own opaque surface, because a translucent window over the desktop would wash the palette out |
| Accent colour | Read from `HKCU\…\DWM\AccentColor` | The built-in Fluent blue; the dark/light default comes from the palette, not the registry |
| Fonts | Segoe UI Variable / Microsoft YaHei UI, Cascadia Mono | macOS: PingFang SC / Helvetica Neue / SF Mono. Linux: Noto Sans CJK SC / DejaVu Sans, JetBrains Mono / DejaVu Sans Mono |
| Engine lookup | hint → app dir → `C:\Program Files\aria2` → `PATH` (`aria2c.exe`) | hint → app dir → `Contents/Resources` + `Contents/MacOS` in a bundle → `/opt/homebrew/bin` → `/usr/local/bin` → `/usr/bin` → `PATH` (`aria2c`) |
| Tray | Always available | Only when the desktop provides a StatusNotifier host; otherwise close means quit |
| Single instance | Per-user `QLocalServer` endpoint (named pipe) | Per-user endpoint in the runtime/temp directory; the user name is part of the key so two accounts never collide |
| Icons | `app.ico` embedded by `version.rc` | `app.icns` in the bundle (macOS), `app-256.png` in the hicolor theme via `cmake --install` (Linux) |

## License

Fetchora itself is [MIT](LICENSE) © 2025 Fetchora contributors.

It drives other people's software, and the release packages redistribute some of it, so
those licences apply to the binaries too:

| Component | Licence | Where it is |
| --- | --- | --- |
| **aria2** 1.37.0 | GPL-2.0-or-later | bundled `aria2c.exe` in the Windows packages; installed separately on macOS and Linux |
| **Qt** 6.10 | LGPL-3.0 (dynamic linking) | Qt libraries shipped with every package |
| **OpenSSL**, **zlib**, **expat**, **SQLite**, **c-ares**, **libssh2**, **GMP** | Apache-2.0, zlib, MIT, public domain, MIT, BSD-3-Clause, LGPL-3.0-or-later/GPL-2.0-or-later | statically linked inside the bundled `aria2c` |

The licence texts travel in `licenses/` inside each Windows package, and the full
reasoning — including the written offer for aria2's source — is in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and
[`third_party/`](third_party/README.md).

Every Windows package also carries `ARIA2-BUILD-INFO.txt`, the verbatim output of
`aria2c --version`, because which libraries an aria2 build links depends on how it was
configured — the notice table covers the possibilities, that file records the fact.
