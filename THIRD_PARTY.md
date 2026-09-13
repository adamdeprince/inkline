# Inkline third-party code

- **libghostty-vt**, MIT, copyright the Ghostty contributors. Source is pinned to
  `448062571c5edf010b7490d06869b88b5ebf8f80` in `CMakeLists.txt` and fetched from
  <https://github.com/ghostty-org/ghostty>. Its license remains with the source
  checkout. Builds currently use Zig 0.16.0. The local patch in
  `patches/ghostty-arm32-seek.patch` fixes Linux ARM32 seeking without libc,
  following the `llseek` path used by Zig's own I/O implementation.
  `patches/ghostty-linux-libc.patch` selects libc for Linux C embedding so the
  Wuffs no-libc stubs do not override the application's allocator. The source
  bundle includes the patched Ghostty tree, its dependency lockfile, and the
  cached dependency source archives with their original notices and licenses.
- **Sixel decoder**, GPL-3.0-or-later, adapted from the local Goblin Mosh
  development tree, `/Users/adam/dev/mosh/src/terminal/sixel.cc`. The decoder was
  extracted into `src/sixel.cpp`; its mosh-specific state/encoder dependencies
  were removed, image limits were reduced for the tablet, and input is viewed
  without copying the complete payload. The source snapshot
  hash is recorded in `toolchains/sixel-source.json`. The sibling checkout was
  read without modification. A copy of GPLv3 is included in `LICENSE`.
- **Qt 6 Core/Gui/Quick and related modules**, dynamically linked to the stock
  firmware libraries (6.8.2 on the tested tablet). See the Qt license notices
  supplied with the firmware and <https://www.qt.io/licensing/>. Inkline does
  not bundle or replace Qt libraries.
- **libpng**, linked through CMake's PNG package. See the libpng license shipped
  by the build environment; its zlib dependency retains its own license.

The sixel-derived component is GPL-3.0-or-later. A combined terminal containing it
must be distributed compatibly with that license, including corresponding source.
