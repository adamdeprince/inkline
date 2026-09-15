# Size optimization comparison

The 0.3.6 development ARM build was compared on 2026-09-15 using Zig 0.16.0 and the same
5.7.119 SDK, source, and target CPU. The default C/C++ mode is `-O2`;
libghostty uses Zig `ReleaseSafe` independently of that setting.
This snapshot precedes the final Qt-based PDF import helper, so these are
comparison-build sizes rather than exact sizes of the published executable.

| Build | Allocated ELF sections | Change |
| --- | ---: | ---: |
| C/C++ `-O2`, Ghostty `ReleaseSafe` | 2,636,689 bytes | baseline |
| C/C++ `-Os`, Ghostty `ReleaseSafe` | 2,599,821 bytes | 36,868 bytes smaller (1.4%) |
| C/C++ `-Os`, Ghostty `ReleaseSmall` | 1,400,750 bytes | 1,235,939 bytes smaller (46.9%) |

These are binary section sizes, including zero-initialized thread-local storage,
not measured process RSS. They do not include Qt shared libraries, fonts, heap
allocations, terminal cells, images, display surfaces, or child programs.
Firmware libraries and utility packages were not rebuilt. The whole system's
memory savings cannot be inferred from this comparison.

The default executable contains debug information and is about 12.4 MiB on
disk; the `-Os` build is about 2.2 MiB without it. Debug sections are not mapped
for normal execution, so that disk reduction is not a RAM saving.

Zig `ReleaseSmall` disables runtime safety checks. It is a separate tradeoff
from compiling the C/C++ code with `-Os`; the release keeps `ReleaseSafe`.
The experiment does not change the default optimization or runtime budgets.
The experimental `ReleaseSmall` executable exits with a segmentation fault during the tablet startup check. It is not a working release option.

Three runs of the working builds were measured on reMarkable 2 with an identical
1000 × 750 offscreen demo, 26-pixel text, and a sample two seconds after startup.
All staging, logs, and caches were in `/tmp`.

| Build | RSS range | Median RSS | Anonymous RSS |
| --- | ---: | ---: | ---: |
| `-O2` + `ReleaseSafe` | 32,860–33,112 KiB | 33,020 KiB | 10,144 KiB |
| `-Os` + `ReleaseSafe` | 32,828–33,124 KiB | 32,944 KiB | 10,140 KiB |

The ranges overlap: the 76 KiB median difference is smaller than the variation
between runs. This experiment does not demonstrate a useful process RAM saving
from `-Os`. It also does not measure battery life, physical e-paper timing,
multiple populated shells, or the rest of the installed utilities.

Reproduce with `cmake/RemarkableZig.cmake`, `CMAKE_BUILD_TYPE=MinSizeRel`, and
the pinned Ghostty source. Set `RMT_GHOSTTY_OPTIMIZE=ReleaseSmall` only for the
experimental third build. Compare allocated ELF sections, not total file size.

See [GCC's size optimization documentation](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
and [Zig's build-mode documentation](https://ziglang.org/documentation/0.16.0/#Build-Mode).
