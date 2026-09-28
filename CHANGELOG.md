# Changelog

Versions are tags (`v0.1.5`); a Release is built from a tag, not from `main`.

## v0.1.5

- **Tracker page** — subscribe to the published tracker lists (ngosang, XIU2), keep a
  blacklist, see per-tracker health and the last announce read from aria2's own log, sync on
  demand. The field holds its subscriptions as chips that wrap, and drops the catalogue out of
  itself when clicked.
- **Per-task trackers** — moved into the task details (one field, add and remove), and the tab
  only exists for torrent tasks.
- **Deleting a task** — the row buttons are always visible instead of appearing on hover, a row
  can no longer outlive its task, and removals are written to the log.
- **Details pane** — updates in place instead of rebuilding, so it no longer flickers while a
  download runs; the files tab is gone and the tracker tab took its place.
- **Magnets** — a built-in tracker list is handed to every magnet (DHT alone finds no peers on
  some networks), and tracker lists can be imported from a file or a URL, with binary files,
  `.torrent` files and oversized input refused rather than parsed.
- **Interface** — single-pill settings rail, no white focus rings on inputs, focus outlines only
  for keyboard focus, a minimum window size, and toasts that wrap instead of clipping.

## v0.1.4 and earlier

See the [releases page](https://github.com/aimineng/Fetchora/releases).
