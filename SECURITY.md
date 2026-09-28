# Security

Fetchora is a local download manager: it runs a supervised `aria2c` and, optionally, a small
HTTP/WebSocket bridge bound to `127.0.0.1` for the browser extension.

**Reporting a problem** — open a private
[security advisory](https://github.com/aimineng/Fetchora/security/advisories/new) rather than a
public issue. A short reproduction (version, platform, what you did, what happened) is enough;
please do not include download URLs that embed credentials.

**Trust boundaries worth knowing**

- The bridge listens on loopback only (`--rpc-listen-all=false`) and is meant for the bundled
  extension. It accepts the same commands as the interface — anything that can reach it can add
  or remove downloads.
- Torrent and magnet handling treats metadata as untrusted: tracker lists are parsed as text and
  refused when they are binary or oversized, and paths from a `.torrent` are confined to the
  download directory.
- The engine binary is the official aria2 release, pinned per platform and downloaded over HTTPS
  in CI and by `build.ps1`.
- Release builds are signed on Windows; Smart App Control may still block self-built binaries,
  which is a property of that policy, not of the build.

Only the latest release is supported.
