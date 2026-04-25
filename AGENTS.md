# AGENTS.md - DOSBox Staging

## Build Commands

```shell
# Setup and build (Meson only - ignore CMakeLists.txt)
meson setup build
meson compile -C build

# Debug build
meson setup -Dbuildtype=debug build/debug

# Debugger build
meson setup -Denable_debugger=normal build/debugger

# Sanitizer build (address,undefined)
meson setup -Dbuildtype=debug --native-file=.github/meson/native-clang.ini \
  -Doptimization=0 -Db_sanitize=address,undefined build/sanitizer
```

## Testing

```shell
# Run all tests (debug build required)
meson test -C build/debug

# Run single test
./build/debug/tests/<TEST_NAME> --gtest_filter=<TEST_CASE_NAME>

# List tests
meson test -C build/debug --list | grep gtest
```

**Note**: Tests are disabled in release builds by default.

## Code Quality

```shell
# Format changed files (requires clang-format >= 15.0.0)
./scripts/format-commit.sh

# Verify formatting
./scripts/format-commit.sh --verify

# Linting
./scripts/verify-bash.sh       # Shell scripts
./scripts/verify-python.sh     # Python (pylint)
./scripts/verify-markdown.sh   # Markdown (mdl)
```

## Important Quirks

- **Ignore CMakeLists.txt** - CMake support is experimental/internal only
- **GCC `-Os` bug** - Release builds with `CXXFLAGS="-Os"` hang; use `minsize` buildtype instead
- **ccache recommended** - Set `CCACHE_SLOPPINESS="pch_defines,time_macros"` for precompiled headers
- **Tracy profiler** - Enable with `-Dtracy=true`
- **Include order** - Use `scripts/verify-ctype-algorithm-include-order.sh` to check

## Architecture

- Main entrypoints: `src/main.cpp`, `src/dosbox.cpp`
- Internal libs in `src/` subdirs (cpu, dos, fpu, gui, hardware, etc.)
- Third-party in `src/libs/` (loguru, decoders, nuked, etc.)
- Subprojects in `subprojects/` (SDL2, fluidsynth, etc.)