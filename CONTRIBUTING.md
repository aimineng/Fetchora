# Fetchora — contributing

Small, focused changes are easiest to review. Before opening a pull request:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
ctest --test-dir build --output-on-failure   # unit tests: parsers, no engine needed
Fetchora --self-test          # the same checks plus settings and the aria2 command line
```

- **Style** — `.clang-format` describes it (`clang-format -i <files>`). Four spaces, 100 columns,
  braces on their own line for functions.
- **Comments** — explain *why*, not what. A comment that restates the code is noise.
- **Commits** — one change per commit, a sentence in the imperative mood. Say what broke and why
  the fix is the right one when that is not obvious.
- **User-visible strings** — add them to `translations/en.*.map` and run
  `tools/make-translations.ps1`; the catalogue must stay at zero unfinished entries.
- **Tests** — `--self-test` is the cheapest place to pin behaviour down. If a bug was found by
  hand, give it a case there so it cannot come back.
- **Engines** — `tools/check-engine-supervision.ps1` drives a real aria2 through start, crash,
  restart, pause, remove and duplicate-link cases. Run it when touching `Aria2*` or `HttpServer`.

## Layout

```
src/app/                      main.cpp: command line, window assembly, headless modes
src/engine/                   the supervised aria2c process, its RPC client, the task
                              model, the bridge HTTP/WebSocket server
src/core/                     settings, logging, history, updates, torrent and tracker
                              parsing - everything the UI does not own
src/ui/                       Fluent widgets and the pages; pages/*.ui describe the structure
tools/                        build, translation, screenshot and check scripts
packaging/                    installer, AppImage and macOS bundle inputs
Plugin/                       the companion browser extension
```

`ui/pages/*.cpp` build their interactive parts in code (the widgets the `.ui` files cannot
express) and keep the rest in the `.ui` files — the split is deliberate, not accidental.

The include path adds `src/core` and `src/engine`, so includes inside those directories stay
short (`#include "SettingsManager.h"`); only `ui/…` and `resources/…` are spelled out.
