# aria2

Fetchora is a front end; **aria2** does the actual downloading. The Windows
packages published on the Releases page include a copy of the engine so the app
works out of the box.

## What is bundled

| | |
| --- | --- |
| Program | `aria2c.exe` (unmodified) |
| Version | 1.37.0 |
| Licence | GNU General Public License, version 2 **or later** |
| Built by | the aria2 project (this is the official Windows build, not ours) |
| Downloaded from | <https://github.com/aria2/aria2/releases/tag/release-1.37.0> |
| Asset | `aria2-1.37.0-win-64bit-build1.zip` |
| Home page | <https://aria2.github.io/> |
| Source | <https://github.com/aria2/aria2> |

We do not patch it and we do not build it. The licence text is
[`LICENSE`](LICENSE) — byte-for-byte the `COPYING` file from that archive.

## Corresponding source

The binary is unmodified, so the corresponding source is the upstream release it
came from:

- tarball: <https://github.com/aria2/aria2/releases/download/release-1.37.0/aria2-1.37.0.tar.xz>
- repository: <https://github.com/aria2/aria2/tree/release-1.37.0>

The archive also ships `ChangeLog`, `NEWS` and `AUTHORS`, which are copied into
the package next to the engine.

If you would rather not rely on a link, Fetchora's maintainers will provide the
source on a physical medium on request, for no more than the cost of the
distribution — that is the offer GPL-2.0 section 3 asks for. Open an issue at
<https://github.com/aimineng/Fetchora/issues>.

## Libraries inside it

The official Windows build is statically linked and imports nothing but Windows
system DLLs, so the engine is a single self-contained file. It reports its own
configuration:

```
$ aria2c.exe --version
aria2 version 1.37.0
...
Libraries: zlib/1.3 expat/2.5.0 sqlite3/3.43.1 GMP/6.3.0 c-ares/1.19.1 libssh2/1.11.0
Compiler: mingw-w64 8.0.0 (alpha) / gcc 10-win32 20220113
```

Those versions belong to *that* build. A different aria2 build may link a
different set — for example one compiled against OpenSSL 3.x instead of GMP —
which is why each package contains `ARIA2-BUILD-INFO.txt`: the verbatim
`--version` output of the engine actually shipped. **Read that file, not this
one, if you need to know exactly what you have.**

The licences for every library the official builds have used are in
[`../`](../).

## Substituting a different engine

Nothing here is mandatory. The app looks for `aria2c`/`aria2c.exe` next to its
own executable, then on `PATH`, and Settings → RPC / 引擎 can point at any other
build. If you want to redistribute Fetchora with your own aria2, drop it into the
package, keep `ARIA2-BUILD-INFO.txt` in step with it, and carry the matching
licence texts.
