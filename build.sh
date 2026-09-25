#!/bin/sh
# build.sh - configure + build helper for Fetchora (Qt 6) on macOS and Linux.
#
# Usage:
#   ./build.sh                  # incremental build (Release)
#   ./build.sh --clean          # wipe the build directory and reconfigure
#   ./build.sh --debug          # Debug build instead of Release
#   ./build.sh --run            # build, then launch the app
#   ./build.sh --test           # build, then run the headless self-tests
#   ./build.sh --install        # build, then `cmake --install` (needs a prefix
#                               # you may write to, e.g. ./build.sh --prefix ~/.local)
#   ./build.sh --prefix <dir>   # CMAKE_INSTALL_PREFIX for --install
#   ./build.sh -- <cmake args>  # anything after -- goes to the configure step
#
# Requires: cmake >= 3.21, a C++17 compiler, Qt 6.5+ development packages and -
# for the self-tests only - aria2. On macOS, Homebrew's qt is found
# automatically; override with QT_PREFIX=/path/to/qt if yours differs.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

BUILD_TYPE=Release
CLEAN=0
RUN=0
TEST=0
INSTALL=0
PREFIX=""
EXTRA_ARGS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)   CLEAN=1 ;;
        --debug)   BUILD_TYPE=Debug ;;
        --release) BUILD_TYPE=Release ;;
        --run)     RUN=1 ;;
        --test)    TEST=1 ;;
        --install) INSTALL=1 ;;
        --prefix)
            shift
            if [ $# -eq 0 ]; then
                echo "error: --prefix needs a directory" >&2
                exit 2
            fi
            PREFIX=$1
            ;;
        # Everything after -- is handed to the configure step untouched, so an
        # unusual toolchain (ccache, a custom Qt, a cross file) stays reachable
        # without editing this script.
        --)
            shift
            EXTRA_ARGS=$*
            break
            ;;
        -h|--help)
            # Print the usage block at the top of this file (everything up to the
            # first blank line after it).
            sed -n '2,/^$/p' "$0"
            exit 0
            ;;
        *)
            echo "error: unknown option '$1' (try --help)" >&2
            exit 2
            ;;
    esac
    shift
done

BUILD_DIR="$ROOT/build/$BUILD_TYPE"

# ------------------------------------------------------------------ toolchain
# Qt lives in a different place on every platform. On macOS Homebrew is the
# normal source, and its prefix is architecture dependent (/opt/homebrew on
# Apple silicon, /usr/local on Intel), so ask brew instead of guessing.
CMAKE_PREFIX_ARG=""
if [ -n "${QT_PREFIX:-}" ]; then
    CMAKE_PREFIX_ARG="-DCMAKE_PREFIX_PATH=$QT_PREFIX"
elif [ "$(uname -s)" = "Darwin" ]; then
    if command -v brew >/dev/null 2>&1 && BREW_QT=$(brew --prefix qt 2>/dev/null); then
        CMAKE_PREFIX_ARG="-DCMAKE_PREFIX_PATH=$BREW_QT"
    elif [ -d /opt/homebrew/opt/qt ]; then
        CMAKE_PREFIX_ARG="-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt"
    elif [ -d /usr/local/opt/qt ]; then
        CMAKE_PREFIX_ARG="-DCMAKE_PREFIX_PATH=/usr/local/opt/qt"
    fi
fi

if [ "$CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    echo "Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring ($BUILD_TYPE) ..."
    # CMAKE_PREFIX_ARG and EXTRA_ARGS are deliberately unquoted: both are lists
    # of arguments, and the empty case must expand to nothing at all.
    # shellcheck disable=SC2086
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        $CMAKE_PREFIX_ARG $EXTRA_ARGS
fi

echo "Building ($BUILD_TYPE) ..."
cmake --build "$BUILD_DIR" --parallel

# --------------------------------------------------------------------- layout
# The target is always named Fetchora; only the platform decides whether that is
# a bare executable or a bundle.
if [ -d "$BUILD_DIR/Fetchora.app" ]; then
    EXE="$BUILD_DIR/Fetchora.app/Contents/MacOS/Fetchora"
    RUNTIME_DIR="$BUILD_DIR/Fetchora.app/Contents/Resources"
else
    EXE="$BUILD_DIR/Fetchora"
    RUNTIME_DIR="$BUILD_DIR"
fi
echo "Built: $EXE"

# ------------------------------------------------- engine + trust material
# CMake copies these when they sit next to CMakeLists.txt; otherwise look for a
# system install. aria2 is NOT bundled with this source tree on Unix - it is a
# normal package (`brew install aria2`, `apt install aria2`).
for name in aria2c ca-bundle.crt; do
    target="$RUNTIME_DIR/$name"
    if [ -e "$target" ]; then
        continue
    fi

    candidate=""
    if [ -e "$ROOT/$name" ]; then
        candidate="$ROOT/$name"
    elif [ "$name" = "aria2c" ]; then
        candidate=$(command -v aria2c 2>/dev/null || true)
    elif [ -e /etc/ssl/certs/ca-certificates.crt ]; then
        candidate=/etc/ssl/certs/ca-certificates.crt
    elif [ -e /etc/pki/tls/certs/ca-bundle.crt ]; then
        candidate=/etc/pki/tls/certs/ca-bundle.crt
    elif [ -e /etc/ssl/cert.pem ]; then
        candidate=/etc/ssl/cert.pem
    fi

    if [ -n "$candidate" ] && [ -e "$candidate" ]; then
        cp -f "$candidate" "$target"
        echo "Copied $name from $candidate"
    elif [ "$name" = "aria2c" ]; then
        echo "warning: aria2c is missing - install aria2 (brew/apt/dnf/pacman) or" >&2
        echo "         place the binary next to the executable." >&2
    fi
done

# ------------------------------------------------------------------- tests
# Runs the app's own headless tools. These validate the two things that are easy
# to get silently wrong: the aria2c command line (an unknown switch makes aria2
# exit 28 and the engine never starts) and the bencode output of the torrent
# maker (a malformed announce-list is unreadable by any client).
if [ "$TEST" -eq 1 ]; then
    echo
    echo "=== self-test: aria2c command line ==="
    selftest=$("$EXE" --self-test 2>&1 || true)
    printf '%s\n' "$selftest"
    case "$selftest" in
        *"RESULT: all switches accepted"*) ;;
        *) echo "aria2c rejected the generated command line" >&2; exit 1 ;;
    esac

    echo
    echo "=== self-test: torrent round trip ==="
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/fetchora-selftest-XXXXXX")
    trap 'rm -rf "$tmp"' EXIT INT TERM
    mkdir -p "$tmp/sub"
    printf 'hello\n' > "$tmp/a.txt"
    printf 'world\n' > "$tmp/sub/b.txt"

    created=$("$EXE" --make-torrent "$tmp" --output "$tmp/selftest.torrent" \
        --tracker "udp://tracker.opentrackr.org:1337/announce,udp://open.tracker.cl:1337/announce" \
        2>&1 || true)
    if [ ! -f "$tmp/selftest.torrent" ]; then
        printf '%s\n' "$created"
        echo "torrent creation produced no file" >&2
        exit 1
    fi

    info=$("$EXE" --inspect-torrent "$tmp/selftest.torrent" 2>&1 || true)
    printf '%s\n' "$info"

    # The same .torrent must yield the same info hash both ways; if the
    # announce-list or the file list were malformed the second read would throw.
    hash_create=$(printf '%s\n' "$created" | sed -n 's/^infoHash: *\([0-9a-f]\{40\}\).*/\1/p' | head -n 1)
    hash_inspect=$(printf '%s\n' "$info" | sed -n 's/^infoHash: *\([0-9a-f]\{40\}\).*/\1/p' | head -n 1)
    if [ -z "$hash_create" ] || [ "$hash_create" != "$hash_inspect" ]; then
        echo "info hash mismatch between create ($hash_create) and inspect ($hash_inspect)" >&2
        exit 1
    fi
    # grep must be inside `if`: a bare failing command would trip `set -e` and
    # abort the script without printing which check failed.
    if ! printf '%s\n' "$info" | grep -q 'trackers (2)'; then
        echo "announce-list lost its trackers" >&2
        exit 1
    fi
    if ! printf '%s\n' "$info" | grep -q 'isMultiFile: yes'; then
        echo "multi-file torrent was not detected" >&2
        exit 1
    fi
    if ! printf '%s\n' "$info" | grep -q 'sub/b\.txt'; then
        echo "nested file path was not preserved" >&2
        exit 1
    fi

    # aria2 itself must be able to parse what we produced. --dry-run exits
    # non-zero by design ("nothing was downloaded"), so only the error codes
    # that mean "bad torrent" (25-27) are treated as failures.
    engine="$RUNTIME_DIR/aria2c"
    if [ -x "$engine" ]; then
        check=$("$engine" --dry-run --console-log-level=error \
            --torrent-file="$tmp/selftest.torrent" 2>&1 || true)
        case "$check" in
            *errorCode=2[5-7]*)
                printf '%s\n' "$check"
                echo "aria2c rejected the generated torrent" >&2
                exit 1 ;;
        esac
        echo "aria2c parsed the generated torrent"
    fi

    echo "ALL SELF-TESTS PASSED"
fi

# ------------------------------------------------------------------ install
if [ "$INSTALL" -eq 1 ]; then
    echo
    echo "Installing ..."
    if [ -n "$PREFIX" ]; then
        cmake --install "$BUILD_DIR" --prefix "$PREFIX"
    else
        cmake --install "$BUILD_DIR"
    fi
fi

if [ "$RUN" -eq 1 ]; then
    echo "Launching ..."
    "$EXE" &
fi
