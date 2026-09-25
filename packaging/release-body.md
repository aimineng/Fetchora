### Downloads

| Platform | Asset | Notes |
| --- | --- | --- |
| Windows 10/11 (x64) | `Fetchora-*-windows-x64-setup.exe` | Installer. No administrator rights needed, and **aria2 is included**. |
| Windows 10/11 (x64), portable | `Fetchora-windows-x64.zip` | Same thing without an installer: unpack anywhere and run `Fetchora.exe`. |
| macOS (Apple silicon or Intel) | `Fetchora-macos-*.dmg` | Open the image and drag Fetchora into Applications. |
| Linux (x86_64) | `Fetchora-linux-*.tar.gz` | Binary, `fetchora.desktop` and the icon. |

**The Windows packages include the engine** - `aria2c.exe` 1.37.0, the official
upstream build, unmodified - so they work as downloaded. See `licenses/` for its
GPL-2.0 text and `ARIA2-BUILD-INFO.txt` for exactly what that build links.
**macOS and Linux do not bundle the engine.** Fetchora drives `aria2c` 1.36+;
install it with `brew install aria2` or your package manager. A brew- or apt-built
aria2 is dynamically linked against that system's own libraries, so copying just
the executable would produce something that cannot start elsewhere.

**Linux:** the tarball is portable but not self-contained - the binary links against
the *distribution's* Qt 6 (6.5 or newer) at runtime. Install it, for example on
Debian/Ubuntu:

```sh
sudo apt install libqt6widgets6 libqt6network6 libqt6sql6 libqt6svg6 \
                 libqt6concurrent6 libxcb-cursor0
```

Unpack the tarball anywhere, then optionally install the launcher and icon:

```sh
install -Dm644 fetchora.desktop ~/.local/share/applications/fetchora.desktop
install -Dm644 fetchora.png ~/.local/share/icons/hicolor/256x256/apps/fetchora.png
```

**macOS:** the build is unsigned and not notarised, so Gatekeeper will warn on first
launch - right-click the app and choose *Open*, or run
`xattr -dr com.apple.quarantine /Applications/Fetchora.app`.
