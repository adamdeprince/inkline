# reMarkable 2 SDK

Downloaded on 2026-09-09 from the manufacturer's
[SDK catalog](https://developer.remarkable.com/links).

| Property | Value |
| --- | --- |
| Product | reMarkable 2 (`rm2`) |
| Published tablet software pairing | 3.27.0.97 |
| Actual device software | 3.27.3.0 |
| SDK version | 5.7.119 |
| Build host | ARM64 / aarch64 Linux |
| Target | ARMv7 hard-float, Cortex-A7 |
| Download size | 401,651,736 bytes |
| Local installer | `.cache/sdk/remarkable-production-image-5.7.119-rm2-public-aarch64-toolchain.sh` |

The downloaded byte count and MD5 match the official server's HTTPS response.
The locally calculated SHA-256 is pinned in [sdk.lock.json](sdk.lock.json) for
repeat downloads. This is an integrity check, not a publisher signature.

```sh
python3 scripts/fetch-sdk.py
```

## Cross-compile Inkline on macOS

The project can use Zig on this ARM64 Mac with the SDK's target headers and
libraries. The application and all five test executables build successfully:

```sh
scripts/build-tablet.sh
```

`extract-sdk-sysroot.py` verifies the SDK checksum and extracts selected target
headers, Qt mkspecs, link libraries and pkg-config files into `.cache/sdk/sysroot-5.7.119`.
It excludes target programs, debug symbols and native Linux tools, and does not
execute the SDK installer. This helper requires Python 3.12 or newer.

`cmake/RemarkableZig.cmake` selects ARMv7 hard-float/Cortex-A7, Linux 5.4, the SDK's
glibc 2.39 headers and GNU C++ runtime. This avoids mixing Qt's libstdc++ ABI with
Zig's bundled libc++. The `inkline` application and test executables are in `build/tablet/`. All five
test suites have passed on firmware 3.27.3.0, including Qt rendering and input
encoding. A timed e-paper demonstration also exited and restored xochitl.

The standalone libghostty-vt engine has separately been cross-compiled for
`arm-linux.5.4-gnueabihf` / `cortex_a7` with Zig on macOS. Reproduce that check with
`cmake --build build/host --target armv7-engine` after configuring the host build.

## Manufacturer's Linux workflow

The SDK installer and native compiler require Linux. No Linux VM/container
runtime was found on PATH during initial setup. The project builds the Qt application with Zig directly on macOS; the
manufacturer's SDK activation workflow is below.

In an ARM64 Linux environment, with the project as the working directory:

```sh
sh .cache/sdk/remarkable-production-image-5.7.119-rm2-public-aarch64-toolchain.sh \
  -y -d "$PWD/toolchains/installed/5.7.119"
. "$PWD/toolchains/installed/5.7.119/environment-setup-cortexa7hf-neon-remarkable-linux-gnueabi"
```

The installer requires host tools including Python, xz and tar. Consult the
[official SDK documentation](https://developer.remarkable.com/documentation/sdk)
for the supported Linux environment. The newer SDK catalog lists aarch64 hosts
even though the overview page still says only x86_64 is available.

This is the SDK published for the 3.27 release line. The catalog does not list
3.27.3.0 separately. The probe confirms device OS 5.7.126 and Qt 6.8.2; see
[device findings](../docs/device.md) for the observed versions. The runtime ABI and a timed e-paper launch have been tested successfully. SDK versions and tablet software versions use
different numbering schemes; use the official catalog and device metadata together.

The previously downloaded 5.8.203 SDK is retained in the cache, with its metadata
in `sdk-3.28.lock.json`. It is not the selected toolchain for this device.
