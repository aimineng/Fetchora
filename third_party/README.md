# Third-party components

Fetchora itself is MIT licensed (see [`../LICENSE`](../LICENSE)). It is, however, a
front end: the downloading is done by **aria2**, which is a separate program with
its own licence, and the release packages ship the Qt runtime and — on Windows —
the aria2 engine.

Nothing in this directory is Fetchora's own code. These files are the licence
texts and notices that have to travel with the binaries we redistribute.

| Component | Licence | Bundled as | Notice |
| --- | --- | --- | --- |
| [aria2](https://aria2.github.io/) | GPL-2.0-or-later | `aria2c` / `aria2c.exe` (Windows packages) | [`aria2/NOTICE.md`](aria2/NOTICE.md) |
| [Qt 6](https://www.qt.io/) | LGPL-3.0 (dynamic linking) | Qt DLLs / frameworks / shared objects | [`qt/LICENSE.LGPL3`](qt/LICENSE.LGPL3) |
| [OpenSSL](https://www.openssl.org/) | Apache-2.0 (3.x), OpenSSL/SSLeay (1.0/1.1) | linked into the bundled `aria2c` | [`openssl/`](openssl) |
| [zlib](https://zlib.net/) | zlib | linked into the bundled `aria2c` | [`zlib/LICENSE`](zlib/LICENSE) |
| [expat](https://libexpat.github.io/) | MIT | linked into the bundled `aria2c` | [`expat/LICENSE`](expat/LICENSE) |
| [SQLite](https://sqlite.org/) | Public domain | linked into the bundled `aria2c` | [`sqlite3/NOTICE.md`](sqlite3/NOTICE.md) |
| [c-ares](https://c-ares.org/) | MIT | linked into the bundled `aria2c` | [`c-ares/LICENSE`](c-ares/LICENSE) |
| [libssh2](https://www.libssh2.org/) | BSD-3-Clause | linked into the bundled `aria2c` | [`libssh2/LICENSE`](libssh2/LICENSE) |
| [GMP](https://gmplib.org/) | LGPL-3.0-or-later **or** GPL-2.0-or-later | linked into the bundled `aria2c` | [`gmp/NOTICE.md`](gmp/NOTICE.md) |

## Which libraries are actually in the bundled engine

It depends on how that particular aria2 build was configured, so it is not
something this table can promise. Every Windows package therefore contains an
`ARIA2-BUILD-INFO.txt` next to `aria2c.exe`, generated at packaging time from the
engine's own output:

```
aria2c.exe --version
```

That file is the authoritative record for the binary you have. The table above
lists the licences we ship texts for, which covers every library the official
Windows builds have used.

## Why the licences are bundled at all

- **aria2 is GPL-2.0-or-later.** Redistributing the binary requires passing on
  the licence and making the corresponding source available. See
  [`aria2/NOTICE.md`](aria2/NOTICE.md) for where that source is.
- **The libraries linked into aria2** are permissively licensed (MIT, BSD, zlib,
  public domain) except GMP, which is copyleft; their notices must be reproduced.
- **Qt is LGPL-3.0.** Fetchora links Qt *dynamically*, which is what the LGPL
  permits for a proprietary or differently-licensed application: the Qt libraries
  stay replaceable, separate files. Their licence text ships alongside them.

If you build Fetchora from source and do not redistribute it, none of this
applies to you — but it applies to anyone handing out a copy of the binaries.
