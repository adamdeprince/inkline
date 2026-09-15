#!/bin/sh
# Linux host; build directories may live on tmpfs. No system installation.
set -eu
ulimit -c 0
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test "$(uname -s)" = Linux || { echo 'Use a Linux build host for the QEMU build helpers.' >&2; exit 1; }
: "${QEMU_ARM:?Set QEMU_ARM to a qemu-arm executable}"
test "$(zig version)" = 0.16.0
export ZIG_GLOBAL_CACHE_DIR="$root/.cache/zig-global"
export CC="$root/scripts/utilities/emacs/cc.py"
export AR="$root/scripts/zig-ar.sh" RANLIB="$root/scripts/zig-ranlib.sh"
export CFLAGS='-O2 -g0'
sdk="$root/.cache/sdk/sysroot-5.7.119"
deps="$root/.cache/emacs-deps"
export PKG_CONFIG_LIBDIR="$sdk/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$sdk" PKG_CONFIG_PATH=
export LIBGNUTLS_CFLAGS="-I$deps/usr/include"
export LIBGNUTLS_LIBS="$deps/usr/lib/arm-linux-gnueabihf/libgnutls.so"
export TREE_SITTER_CFLAGS="-I$deps/usr/include"
export TREE_SITTER_LIBS="$deps/usr/lib/arm-linux-gnueabihf/libtree-sitter.a"
mkdir -p "$root/build/emacs/arm"
cd "$root/build/emacs/arm"
if [ ! -f Makefile ]; then
    "$root/.cache/utilities/src/emacs-31.1/configure" \
        --build="$(gcc -dumpmachine)" --host=arm-linux-gnueabihf \
        --prefix=/home/root/.local/share/inkline-utilities/emacs/current/runtime/emacs \
        --without-x --without-ns --without-pgtk --without-cairo \
        --without-native-compilation --without-sound --without-dbus \
        --without-gsettings --without-libsystemd --without-gpm --without-selinux \
        --with-gnutls --with-tree-sitter --with-modules --with-threads \
        --with-dumping=pdumper --disable-build-details
fi
# Emacs fingerprints its final ELF before dumping it. Wrap only after that
# step; changing the binary to a script before fingerprinting is incorrect.
python3 - "$root" <<'PY'
from pathlib import Path
import shlex
import sys
root = Path(sys.argv[1])
path = Path("src/Makefile")
text = path.read_text()
marker = "# inkline-qemu-build-helper"
if marker not in text:
    old = "\t$(AM_V_at)mv $@.tmp $@\n"
    assert old in text
    text = text.replace(old, old + "\t" + shlex.quote(str(root / "scripts/utilities/emacs/cc.py")) + " --wrap $@ " + marker + "\n")
    path.write_text(text)
PY
if [ -f src/temacs ]; then "$CC" --wrap src/temacs; fi
make -j"${BUILD_JOBS:-8}"
make install DESTDIR="$root/build/emacs/install"
prefix="$root/build/emacs/install/home/root/.local/share/inkline-utilities/emacs/current/runtime/emacs"
# QEMU launchers are build tools, never installed into the tablet package.
cp src/temacs.arm "$prefix/bin/emacs-31.1"
for program in emacsclient etags ctags ebrowse; do
    if [ -f "lib-src/$program.arm" ]; then cp "lib-src/$program.arm" "$prefix/bin/$program"; fi
done
for program in hexl movemail; do
    if [ -f "lib-src/$program.arm" ]; then
        find "$prefix/libexec" -type f -name "$program" -exec cp "lib-src/$program.arm" '{}' \;
    fi
done
echo "Emacs 31.1 no-X build staged at $prefix"
