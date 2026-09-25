# SQLite

aria2 links SQLite for its Firefox-cookie and Metalink features, and Fetchora's
own download history is a plain SQLite database file.

**SQLite is in the public domain.** Its authors (D. Richard Hipp and others)
explicitly place it in the public domain rather than licensing it:

> The author disclaims copyright to this source code. In place of a legal notice,
> here is a blessing:
>
> May you do good and not evil.
> May you find forgiveness for yourself and forgive others.
> May you share freely, never taking more than you give.

Source: <https://sqlite.org/copyright.html>

No licence text is required to redistribute it, so none is shipped — this notice
exists so the component is not silently unaccounted for.

Fetchora uses SQLite only through Qt's `QSQLITE` driver (`qsqlite.dll` /
`libqsqlite.so`, © The Qt Company, LGPL-3.0 — see
[`../qt/LICENSE.LGPL3`](../qt/LICENSE.LGPL3)).
