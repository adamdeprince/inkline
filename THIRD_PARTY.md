# Inkline third-party code

Inkline is distributed under **GPL-3.0-or-later**; see `LICENSE` and `NOTICE`.
Third-party files retain the licenses below. Settings → About & licenses embeds
the complete texts listed in `licenses/catalog.json`, so the notices remain
available offline. The release also includes their original plain-text files.

- **libghostty-vt**, MIT, copyright the Ghostty contributors. Source is pinned to
  `448062571c5edf010b7490d06869b88b5ebf8f80` in `CMakeLists.txt` and fetched from
  <https://github.com/ghostty-org/ghostty>. Its license remains with the source
  checkout. Builds currently use Zig 0.16.0. The local patch in
  `patches/ghostty-arm32-seek.patch` fixes Linux ARM32 seeking without libc,
  following the `llseek` path used by Zig's own I/O implementation.
  `patches/ghostty-inkline-retention.patch` adds Inkline's exact history limit
  and off-screen graphics reclamation entry point without injecting VT input.
  `patches/ghostty-inkline-erase.patch` clears visible Kitty images when ED0
  erases the entire screen, as used by the tablet's BusyBox `clear` command.
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
  not bundle or replace Qt libraries. `licenses/Qt.txt` describes the modules,
  source locations and replacement rights; `licenses/LGPL-3.0.txt` and `LICENSE`
  provide the LGPLv3 and incorporated GPLv3 terms.
- **libpng**, linked through CMake's PNG package. See the libpng license shipped
  by the build environment; its zlib dependency retains its own license.
  The target SDK's libpng 1.6.42 and zlib 1.3.1 notices are preserved in
  `licenses/libpng.txt` and `licenses/zlib.txt`.

- **uucode**, MIT, copyright 2026 Jacob Sandlund, at revision
  `2826a37a4562284fdacd8fa029d49509cc9bffcd`. Ghostty uses its Unicode tables and
  character handling. `licenses/uucode-MIT.txt` also refers to Bjoern Hoehrmann's
  UTF-8 decoder and Unicode data licenses. Those two notices were recovered
  unchanged from that exact upstream revision at
  <https://github.com/jacobsandlund/uucode/tree/2826a37a4562284fdacd8fa029d49509cc9bffcd/licenses>
  because the cached source archive omits its `licenses/` directory; they are
  included as `uucode-UTF8-MIT.txt` and `uucode-Unicode.txt`.
- **Wuffs**, copyright 2023 The Wuffs Authors, supplies MIT and Apache 2.0 terms
  in `licenses/Wuffs.txt`. Ghostty uses revision
  `7411f488fe2e2c205c3d3b3d28638b7356522930` for image decoding.
- **Zig runtime**, MIT, copyright Zig contributors; see `licenses/Zig-MIT.txt`.
  Its musl-derived math routines retain the notice in `licenses/Zig-musl.txt`,
  copied from the Zig 0.16.0 distribution. This does not bundle a second libc.
  The source bundle also carries build dependencies translate-c (MIT, Zig
  contributors) and aro (MIT, copyright 2021 Veikka Tuominen, with Unicode data).
  Their original license files are copied into `licenses/` for easy access.
  Ghostty's cached zlib source retains its own notice in `Ghostty-zlib.txt`.

The sixel-derived component is GPL-3.0-or-later. A combined terminal containing it
must be distributed compatibly with that license, including corresponding source.

- **GoblinView input methods**, Apache-2.0, copyright 2026 Adam DePrince.
  `src/input_method.cpp` adapts the September 14, 2026 local `desqview/src/im.cc`
  snapshot with owned, bounded dictionaries and Qt key routing. The original
  phonetic/dead-key tables are retained; prefix handling and UTF-8 candidate
  truncation are corrected. The source archive is pinned in
  `scripts/utilities/inputs.lock.json`. Its license and notice are included in
  `assets/input-methods/licenses/`.
- **Pinyin/Wubi dictionaries**: mozillazg/pinyin-data (MIT), Unicode Unihan
  (Unicode Data Files license), and KyleBing/rime-wubi86-jidian (Apache-2.0).
  Original license texts and provenance accompany the unchanged generated
  dictionaries in `assets/input-methods/`.
- **Noto Sans Mono CJK SC**, SIL Open Font License 1.1, from notofonts/noto-cjk.
  The unmodified font, license, exact source revision and SHA-256 are in
  `assets/fonts/`. It is loaded privately by Inkline. The font's embedded
  copyright, © 2014–2021 Adobe, is also reproduced in `licenses/NotoCJK-NOTICE.txt`.
- **Noto Sans Symbols 2 and GNU Unifont**, SIL Open Font License 1.1. Inkline
  bundles the unmodified Noto Symbols 2.008 face and GNU Unifont/Unifont Upper
  18.0.01 as ordered fallbacks. This preserves the existing text and CJK faces
  while covering the BMP, many supplementary scripts, and common monochrome
  symbols. Exact sources, hashes and copyright holders are in
  `licenses/UnicodeFonts-NOTICE.txt`; the OFL text is in `assets/fonts/`.
- **USB FunctionFS keyboard and mode switching**, adapted from Adam de Prince's
  local Inkline Outpost `usb-modes` code, snapshot 2026-09-16. The original
  descriptors and control handler are retained in `scripts/usb/keyboard.py`;
  Inkline adds host-specific Unicode encoding, streaming, UI integration and
  recovery. The original modem and satellite tools are not bundled or modified.
