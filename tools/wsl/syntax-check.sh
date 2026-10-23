#!/usr/bin/env bash
# Fast compile (syntax-only) check of Ghostship source files from WSL/Linux, without a full build.
# Downloads the header-only pieces of the third-party dependencies once into ~/.cache.
#
# Usage:
#   tools/wsl/syntax-check.sh                 # checks .c/.cpp files changed vs. HEAD (plus new files)
#   tools/wsl/syntax-check.sh path/to/file.cpp [more files...]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/ghostship-syntax-check"
mkdir -p "$CACHE"

fetch() { # url dest-file
    [ -s "$2" ] || curl -fsSL -o "$2" "$1"
}
fetch_tar() { # url extracted-dir
    if [ ! -d "$2" ]; then
        curl -fsSL "$1" | tar xz -C "$CACHE"
    fi
}

mkdir -p "$CACHE/inc/nlohmann" "$CACHE/inc/SDL2" "$CACHE/inc/backends"
fetch https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp "$CACHE/inc/nlohmann/json.hpp"
fetch https://raw.githubusercontent.com/bshoshany/thread-pool/v4.1.0/include/BS_thread_pool.hpp "$CACHE/inc/BS_thread_pool.hpp"
[ -s "$CACHE/inc/zip.h" ] || echo 'typedef struct zip zip_t;' > "$CACHE/inc/zip.h"
fetch_tar https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz "$CACHE/SDL2-2.30.9"
fetch_tar https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz "$CACHE/spdlog-1.14.1"
fetch_tar https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz "$CACHE/imgui-1.91.9b-docking"
PRISM=aa8370981b2cf57c46172e6aa639d720137f9a92
fetch_tar "https://github.com/KiritoDv/prism-processor/archive/$PRISM.tar.gz" "$CACHE/prism-processor-$PRISM"
[ -f "$CACHE/inc/SDL2/SDL.h" ] || cp "$CACHE"/SDL2-2.30.9/include/*.h "$CACHE/inc/SDL2/"
[ -d "$CACHE/inc/spdlog" ] || cp -r "$CACHE/spdlog-1.14.1/include/spdlog" "$CACHE/inc/"
if [ ! -f "$CACHE/inc/imgui.h" ]; then
    cp "$CACHE"/imgui-1.91.9b-docking/*.h "$CACHE/inc/"
    cp "$CACHE"/imgui-1.91.9b-docking/backends/*.h "$CACHE/inc/backends/"
fi

cd "$REPO"
INC=(-I"$CACHE/inc" -I"$CACHE/inc/SDL2" -I"$CACHE/prism-processor-$PRISM/src" -Iinclude -Isrc -I. -Isrc/port/ui
     -Ilibultraship/include -Ilibultraship/include/libultraship)
DEFS=(-DCVAR_PREFIX_ENHANCEMENT='"gEnhancements"' -DCVAR_PREFIX_SETTING='"gSettings"' -DCVAR_PREFIX_WINDOW='"gWindows"'
      -DCVAR_PREFIX_COSMETIC='"gCosmetics"' -DCVAR_PREFIX_CHEAT='"gCheats"' -DCVAR_PREFIX_AUDIO='"gAudioEditor"'
      -DCVAR_PREFIX_DEVELOPER_TOOLS='"gDeveloperTools"' -DCVAR_PREFIX_RANDOMIZER_ENHANCEMENT='"gRandoEnhancements"'
      -DCVAR_PREFIX_RANDOMIZER_SETTING='"gRandoSettings"' -DCVAR_PREFIX_TRACKER='"gTrackers"')

if [ $# -gt 0 ]; then
    FILES=("$@")
else
    mapfile -t FILES < <(git diff --name-only HEAD -- '*.c' '*.cpp'; git ls-files --others --exclude-standard -- '*.c' '*.cpp')
fi

status=0
for f in "${FILES[@]}"; do
    [ -f "$f" ] || continue
    case "$f" in
        *.cpp) out=$(g++ -std=c++20 -fsyntax-only "${DEFS[@]}" "${INC[@]}" "$f" 2>&1 | grep -E "error" || true) ;;
        # MSVC accepts the decomp's pointer/int conversions that newer gcc rejects.
        *.c) out=$(gcc -fsyntax-only -Wno-int-conversion -Wno-incompatible-pointer-types "${DEFS[@]}" "${INC[@]}" "$f" 2>&1 | grep -E "error" || true) ;;
        *) continue ;;
    esac
    if [ -n "$out" ]; then
        echo "FAIL $f"
        echo "$out" | head -20
        status=1
    else
        echo "ok   $f"
    fi
done
exit $status
