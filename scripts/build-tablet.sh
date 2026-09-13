#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
test "$(zig version)" = 0.16.0 || { echo 'Inkline currently requires Zig 0.16.0.' >&2; exit 1; }
export ZIG_GLOBAL_CACHE_DIR="$project_dir/.cache/zig-global"
python3 scripts/fetch-sdk.py
python3 scripts/extract-sdk-sysroot.py
ghostty_source=
if [ -f "$project_dir/.cache/ghostty/build.zig" ]; then
    ghostty_source="$project_dir/.cache/ghostty"
fi
cmake -S . -B build/tablet -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/cmake/RemarkableZig.cmake" \
    -DFETCHCONTENT_SOURCE_DIR_GHOSTTY="$ghostty_source" "$@"
cmake --build build/tablet -j 4
