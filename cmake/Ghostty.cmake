find_program(ZIG_EXECUTABLE zig REQUIRED)
set(RMT_ZIG_TARGET "" CACHE STRING "Zig target matching the C/C++ toolchain (empty for host)")
set(RMT_ZIG_CPU "" CACHE STRING "Zig CPU for the target (cortex_a7 for reMarkable 2)")
if(CMAKE_CROSSCOMPILING AND NOT RMT_ZIG_TARGET)
    message(FATAL_ERROR "Set RMT_ZIG_TARGET to match the cross compiler, e.g. arm-linux-gnueabihf")
endif()

set(_ghostty_prefix "${CMAKE_BINARY_DIR}/ghostty")
set(_ghostty_archive "${_ghostty_prefix}/lib/libghostty-vt.a")
set(RMT_GHOSTTY_OPTIMIZE ReleaseSafe CACHE STRING "Ghostty build mode (ReleaseSmall is an experimental size comparison and disables runtime safety)")
set_property(CACHE RMT_GHOSTTY_OPTIMIZE PROPERTY STRINGS ReleaseSafe ReleaseSmall)
if(NOT RMT_GHOSTTY_OPTIMIZE MATCHES "^(ReleaseSafe|ReleaseSmall)$")
    message(FATAL_ERROR "Unsupported RMT_GHOSTTY_OPTIMIZE")
endif()
set(_ghostty_flags -Demit-lib-vt -Demit-xcframework=false -Dsimd=false "-Doptimize=${RMT_GHOSTTY_OPTIMIZE}")
set(_ghostty_target)
if(RMT_ZIG_TARGET)
    list(APPEND _ghostty_target "-Dtarget=${RMT_ZIG_TARGET}")
endif()
if(RMT_ZIG_CPU)
    list(APPEND _ghostty_target "-Dcpu=${RMT_ZIG_CPU}")
endif()

# Always ask Zig to check its own cache: CMake cannot track all Zig inputs or
# flag changes. The default stays ReleaseSafe independently of CMake's mode;
# the explicit ReleaseSmall override exists for measured size comparisons.
add_custom_target(rmt_ghostty_build
    COMMAND "${CMAKE_COMMAND}" -E env "ZIG_GLOBAL_CACHE_DIR=${PROJECT_SOURCE_DIR}/.cache/zig-global"
        "${ZIG_EXECUTABLE}" build ${_ghostty_flags} ${_ghostty_target} --prefix "${_ghostty_prefix}"
    WORKING_DIRECTORY "${ghostty_SOURCE_DIR}"
    BYPRODUCTS "${_ghostty_archive}"
    COMMENT "Building libghostty-vt (${RMT_GHOSTTY_OPTIMIZE})" VERBATIM USES_TERMINAL
)
add_library(ghostty-vt-static STATIC IMPORTED GLOBAL)
set_target_properties(ghostty-vt-static PROPERTIES
    IMPORTED_LOCATION "${_ghostty_archive}"
    INTERFACE_INCLUDE_DIRECTORIES "${ghostty_SOURCE_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS GHOSTTY_STATIC
)
add_dependencies(ghostty-vt-static rmt_ghostty_build)

# This checks the engine's ARM32 build directly from macOS independently of Qt.
add_custom_target(armv7-engine
    COMMAND "${CMAKE_COMMAND}" -E env "ZIG_GLOBAL_CACHE_DIR=${PROJECT_SOURCE_DIR}/.cache/zig-global"
        "${ZIG_EXECUTABLE}" build ${_ghostty_flags} -Dtarget=arm-linux.5.4-gnueabihf -Dcpu=cortex_a7
        --prefix "${CMAKE_BINARY_DIR}/armv7/ghostty"
    WORKING_DIRECTORY "${ghostty_SOURCE_DIR}"
    COMMENT "Cross-compiling libghostty-vt for reMarkable 2" VERBATIM USES_TERMINAL
)
