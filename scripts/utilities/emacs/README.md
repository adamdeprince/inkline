# GNU Emacs 31.1 for reMarkable 2

This is a source build of the stable GNU release, configured without X, NS,
PGTK, Cairo, or native compilation. Run `emacs`; its launcher always passes
`--no-window-system` (`-nw`). `emacsclient` defaults to `--tty`. No graphical
desktop, X server, or compiler is needed on the tablet.

Org, TRAMP, TeX mode, GnuTLS, tree-sitter, SQLite, JSON, threads, and dynamic
modules are enabled. GnuTLS, SQLite, and their missing dependencies are
private to the bundle. Tree-sitter 0.20.7 is linked statically; grammars must
use an ABI supported by that version. Firmware supplies libc, libxml2,
liblcms2, libacl, libattr, libtinfo, and zlib. Personal Emacs configuration is
left in place. The separate ghostty-tex utility supplies AUCTeX and previews.

Native compilation is disabled at build time, so Emacs cannot create an
automatic `.eln` cache. Normal document saves, backups, package installation,
and explicit Lisp byte compilation can still write files. The ghostty-tex
integration keeps its builds, logs, and previews in `/tmp`.

## Rebuild on Linux

Use a Linux x86_64 or ARM64 host with Python 3.12+, GNU make, GCC (for detecting
the host architecture), pkg-config, Texinfo, xz, Zig **0.16.0**, and `qemu-arm`.
Packaging also requires `llvm-objcopy` or Rust's `llvm-tools` component. The
build does not install anything in the host's system directories or register
a global binfmt handler. A checkout on tmpfs keeps build output in RAM.

From the Inkline checkout:

```sh
python3 scripts/fetch-sdk.py
python3 scripts/extract-sdk-sysroot.py
python3 scripts/utilities/debian.py
python3 scripts/utilities/emacs/prepare.py
export QEMU_ARM="$(command -v qemu-arm)"
scripts/build-emacs.sh
python3 scripts/package-emacs.py
```

`inputs.lock.json` pins GNU's source archive, build headers, SQLite, and source
hashes. The GNU source signature was verified with signing key
`9B917007AE030E36E4FC248B695B7AE4BF066240`; the checksum is checked on every
preparation. The shared Debian lock pins GnuTLS and its dependencies. The old
Debian Emacs files fetched by that shared resolver are not shipped in this
package.

`cc.py` compiles for Cortex-A7 using the matching SDK's GNU ABI. Emacs runs
several target executables during its build; temporary QEMU launchers let
them run on the build host. In particular, `temacs` is wrapped only **after**
its ELF fingerprint has been computed for the portable dump. The installed
payload contains the ARM executables and dump, never those QEMU launchers.

## Package from macOS

The Emacs build itself needs Linux. The rest of Inkline can still be built
on macOS. Use the same checkout revision on a Linux builder, run the build
above, then copy its `build/emacs/install/` tree into the same relative path
in the Mac checkout. Prepare the SDK and locked dependencies on the Mac,
then run `python3 scripts/package-emacs.py` there. `build-utilities.sh`
expects that installed tree when assembling the complete utility set.

## Check and install

The package is staged at `build/utilities/packages/emacs`. Copy it to a
private directory under the tablet's RAM-backed `/tmp`, then run:

```sh
./emacs/install-device.sh --check
./emacs/install-device.sh
/home/root/.local/bin/emacs --version
```

The preflight checks the actual ARM binary, portable dump, Unicode buffers,
Org, TRAMP, TeX mode, JSON, TLS availability, tree-sitter, and an in-memory
SQLite query. It rejects an incompatible runtime before switching releases.
Installation keeps the previous release for rollback and preserves
`~/.emacs` and `~/.emacs.d`. Exit any old Emacs session and start `emacs`
again to use the new version.

After successful tablet checks, create the versioned download:

```sh
python3 scripts/package-utilities.py --archive emacs --release 2026-09-15.1
```

Publish the GNU source, matching private dependency sources, build recipes,
license notices, and SHA-256 files alongside the binary. Existing versioned
downloads must not be replaced.

`python3 scripts/package-emacs-sources.py` prepares the matching source
downloads under `build/emacs/sources` and verifies their locked hashes.

## Device validation, September 15, 2026

Emacs 31.1 is installed on reMarkable 2 firmware 3.27.3.0. The preflight and
real PTY test pass, including Unicode editing, saving and keyboard exit.
Personal init and early-init files retained their original SHA-256 hashes.
The current release occupies 141,456 KiB (about 138.1 MiB), excluding retained
older releases and personal files.

The ghostty-tex integration needed a guard around native-cache setup for an
Emacs build without native compilation. Update to `0.1.4+rm2.2` if that
add-on is installed. Its three device integration tests pass with Emacs 31.1:
separate build directories, preservation of personal configuration, and
LaTeX/BibTeX/PDF/DVI builds and recovery files confined to RAM. The bundled
AUCTeX 12.2 loads with upstream deprecation warnings under Emacs 31.1.
