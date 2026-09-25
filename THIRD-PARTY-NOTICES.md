# Third-party notices

Fetchora is released under the [MIT licence](LICENSE). It drives other people's
software to do its job, and the release packages redistribute some of it, so the
notices below apply to the **binaries**, not to this repository's source.

Everything needed to satisfy them travels inside the packages, in the
`licenses/` (Windows installer and zip) or `licenses/` directory next to the
binary. The full index, with a table of components, licences and where each text
lives, is [`third_party/README.md`](third_party/README.md).

## Summary

| Component | Licence | How it reaches you |
| --- | --- | --- |
| **aria2** 1.37.0 | GPL-2.0-or-later | bundled `aria2c.exe` in the Windows packages; installed separately on macOS and Linux |
| **Qt** 6.10 | LGPL-3.0 (dynamic linking) | Qt DLLs / frameworks / shared objects shipped with every package |
| **OpenSSL** | Apache-2.0 (3.x); OpenSSL + SSLeay (1.0/1.1) | linked inside the bundled `aria2c` |
| **zlib** | zlib | linked inside the bundled `aria2c` |
| **expat** | MIT | linked inside the bundled `aria2c` |
| **SQLite** | Public domain | linked inside the bundled `aria2c`; also the format of the history database |
| **c-ares** | MIT | linked inside the bundled `aria2c` |
| **libssh2** | BSD-3-Clause | linked inside the bundled `aria2c` |
| **GMP** | LGPL-3.0-or-later or GPL-2.0-or-later | linked inside the bundled `aria2c` |

## What that means in practice

**aria2 is a separate program.** Fetchora talks to it over aria2's JSON-RPC
interface; the two are not combined into one work, and Fetchora's own source
stays MIT. But shipping the engine inside our package does mean we redistribute
a GPL-2.0-or-later binary, so its licence and a pointer to its source come with
it — see [`third_party/aria2/NOTICE.md`](third_party/aria2/NOTICE.md), which also
carries the written offer GPL-2.0 section 3 asks for.

**Qt is used the way the LGPL expects.** Fetchora links Qt dynamically: the Qt
libraries remain separate, replaceable files, so a recipient can swap in their
own build of Qt and relink. The Qt source is published at
<https://code.qt.io/> and <https://www.qt.io/download>. Qt 6 is also available
under GPL-3.0 and under a commercial licence; this project relies on the LGPL-3.0
option.

**macOS and Linux packages do not bundle aria2.** A `brew`- or `apt`-built aria2
is dynamically linked against that distribution's OpenSSL, libssh2 and friends,
so copying just the executable would produce something that cannot start on
another machine. Those builds tell you to install aria2 from your package manager
instead, which is both honest and what you want for security updates.

**The exact engine build is recorded, not assumed.** Every Windows package
contains `ARIA2-BUILD-INFO.txt`, generated at packaging time from
`aria2c.exe --version`. If a future release bundles a differently configured
aria2, that file — not this document — is the record of what is in it.

## Reporting a licensing problem

If something here is wrong or missing, please open an issue:
<https://github.com/aimineng/Fetchora/issues>. Getting this right matters more
than shipping quickly.
