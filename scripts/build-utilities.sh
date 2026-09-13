#!/bin/bash
# Build the utility release on an Apple Silicon macOS host.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
export ZIG_GLOBAL_CACHE_DIR="$root/.cache/zig-global"
sdk="$root/.cache/sdk/sysroot-5.7.119"
test -f "$sdk/usr/lib/libstdc++.so" || {
    echo 'Prepare the SDK with scripts/fetch-sdk.py and scripts/extract-sdk-sysroot.py first; see toolchains/README.md.' >&2
    exit 1
}
python3 scripts/utilities/debian.py
python3 scripts/utilities/prepare-inputs.py
cmake -S .cache/utilities/src/libwebp-1.6.0 -B build/utilities/webp -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/cmake/RemarkableZig.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$root/.cache/utilities/prefix" \
    -DBUILD_SHARED_LIBS=OFF -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF \
    -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF \
    -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF \
    -DWEBP_BUILD_EXTRAS=OFF
cmake --build build/utilities/webp --parallel 4
cmake --install build/utilities/webp
(
    cd .cache/utilities/src/goblin-mosh
    autoreconf -fi
    export CC="$root/scripts/utilities/zig-cc.sh" CXX="$root/scripts/utilities/zig-cxx.sh"
    export AR="$root/scripts/zig-ar.sh" RANLIB="$root/scripts/zig-ranlib.sh"
    export CFLAGS=-O2 CXXFLAGS=-O2
    export PKG_CONFIG_LIBDIR="$sdk/usr/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$sdk" PKG_CONFIG_PATH=
    export PROTOC="$root/.cache/utilities/protoc-25.8/bin/protoc"
    export WEBP_CFLAGS="-I$root/.cache/utilities/prefix/include"
    export WEBP_LIBS="$root/.cache/utilities/prefix/lib/libwebp.a $root/.cache/utilities/prefix/lib/libsharpyuv.a -lm"
    ./configure --build=aarch64-apple-darwin --host=arm-linux-gnueabihf \
        --prefix=/home/root/.local/share/inkline-utilities/goblin-mosh/current \
        --without-librsync --without-libraptorq --without-fips-crypto --without-utempter \
        --with-zstd --disable-syslog --disable-examples --disable-completion --disable-ufw
    make -j4
)
make -C .cache/utilities/src/goblin-view -j4 goblin-view \
    test-desktop test-host test-menu test-term test-im test-codec \
    UNAME=Linux CXX="$root/scripts/utilities/zig-cxx.sh" \
    CXXFLAGS='-std=c++23 -Wall -Wextra -O2' \
    CPPFLAGS='-Isrc -DGOBLIN_VIEW_DATADIR='\'\"../share/goblin-view\"\'
export RUSTC="${RUSTC:-$(rustup which --toolchain 1.98.1 rustc)}"
cargo_bin="${CARGO:-$(dirname -- "$RUSTC")/cargo}"
export CARGO_TARGET_DIR="$root/build/utilities/purrfect" CARGO_INCREMENTAL=0
export CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER="$root/scripts/utilities/zig-cc.sh"
export RUSTFLAGS='-C target-cpu=cortex-a7'
mkdir -p build/utilities
(
    cd .cache/utilities/src/goblin-purrfect
    "$cargo_bin" build --release --locked --offline --target armv7-unknown-linux-gnueabihf
    "$cargo_bin" metadata --locked --offline --filter-platform armv7-unknown-linux-gnueabihf \
        --format-version 1 > "$root/build/utilities/purrfect-metadata.json"
)
python3 scripts/package-utilities.py
printf 'Run the device preflights, then scripts/package-utilities.py --archive.\n'
