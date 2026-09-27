<div align="center">

<img src="resources/app-256.png" width="112" alt="Fetchora 图标">

# Fetchora

**基于 aria2 的下载器，界面是 Fluent 2 / WinUI 3 风格，用 Qt 6 Widgets 写成。**

多协议下载 · BitTorrent · Tracker 管理 · Windows / macOS / Linux

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/aimineng/Fetchora/actions/workflows/ci.yml/badge.svg)](https://github.com/aimineng/Fetchora/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/aimineng/Fetchora?include_prereleases&sort=semver)](https://github.com/aimineng/Fetchora/releases)

[English](README.md)

</div>

![下载任务](docs/screenshots/download.png)

## 功能

- **下载** —— 通过 [aria2](https://aria2.github.io/) 支持 HTTP/HTTPS、FTP、SFTP、BitTorrent、Metalink
  （随程序附带 1.37.0）。多连接、断点续传、队列、限速、镜像、单任务代理；磁力输入框可一次粘贴多条。
- **BitTorrent** —— 磁力链接与 `.torrent`，DHT/DHT6、PEX、LPD、MSE、做种限制。种子任务就是下载列表里的
  普通一行（用 `BT` 筛选条只看种子）。
- **Tracker 管理** —— 订阅公开列表（ngosang、XIU2）、维护黑名单、按需同步，健康度直接读 aria2 自己的
  announce 日志（不主动探测第三方服务器）；单个任务的 Tracker 在任务详情里编辑。
- **任务详情** —— 概要 / 连接 / 服务器 / Tracker / 实时 aria2 选项，全部就地更新，下载中不闪烁。
- **下载历史** —— 完成、失败、已移除的记录存在 SQLite，可搜索筛选。
- **界面** —— 深色/浅色 Fluent 主题、Windows 11 Mica、中英文可切换。
- **其它** —— 种子制作/编辑、更新检查、托盘图标、HTTP 桥接，以及 `Plugin/` 里的配套浏览器扩展。

## 环境要求

Qt **6.5+**（开发用 6.10.3）、CMake 3.21+、支持 C++17 的编译器。Windows 发布包含引擎；
macOS 和 Linux 请用包管理器安装 `aria2`。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

产物在 `build/`（MinGW Makefiles 是 `build/Release/`）。把 `third_party/aria2` 放到旁边，
或者让 `aria2c` 在 `PATH` 里。

```bash
./build.ps1                           # Windows 便捷脚本（有签名证书时加 -Sign）
tools/check-engine-supervision.ps1    # 引擎端到端检查，需要程序正在运行
Fetchora --self-test                  # 解析器、设置与 aria2 命令行检查，无需窗口
```

## 注意

- 自行构建的未签名 `Fetchora.exe` 可能被 **Smart App Control** 拦截，可在
  Windows 安全中心 → 应用和浏览器控制 里关闭。
- 引擎受监控：`aria2c` 意外退出会被自动拉起，队列继续。
- 设置、历史与日志分别在 `%APPDATA%\Fetchora` 和 `%LOCALAPPDATA%\Fetchora`。

## 命令行

```
Fetchora [选项] [URL|磁力链接|.torrent ...]
  --page <key>            download | tracker | history | createtorrent | settings | about
  --detail <section>      overview | peers | servers | tracker | options
  --sync-trackers         拉取全部 Tracker 订阅源后退出
  --self-test             运行内置检查后退出
  --make-torrent <目录>    从文件或目录制作 .torrent
  --inspect-torrent <文件> 打印 .torrent 内容
  --screenshot <文件>      渲染某页后退出（配合 --frames 可连拍）
```

## 许可

MIT，见 [LICENSE](LICENSE)。aria2 为 GPLv2+，以独立可执行文件形式分发。
