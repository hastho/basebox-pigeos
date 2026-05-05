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

## Dependency Management Strategy

BaseBox uses Meson wraps for maximum platform compatibility (Raspberry Pi, Debian, Fedora, macOS without Homebrew).

### Wrap Status
- **zlib**: System-only (zlib-ng requires CMake, disabled)
- **libpng**: Wrap available (`fallback: ['libpng', 'png_dep']`)
- **opusfile**: System-only (uses autotools, no Meson build)
- **SDL2**: Wrap available
- **SDL2_net**: System-only (wrap doesn't properly provide dependency)
- **speexdsp**: Wrap available (`fallback: ['speexdsp', 'speexdsp_dep']`)
- **slirp**: Wrap available (`fallback: ['slirp', 'libslirp_dep']`)
- **fluidsynth**: Wrap available (`fallback: ['fluidsynth', 'fluidsynth_dep']`)
- **mt32emu**: Wrap available (`fallback: ['mt32emu', 'mt32emu_dep']`)
- **tracy**: Wrap available (`fallback: ['tracy', 'tracy_dep']`)

### Build Flags for PC-GEOS

```shell
# macOS: Force wraps to avoid Homebrew dependency hell
meson setup build --wrap-mode=forcefallback \
  -Duse_fluidsynth=false \
  -Duse_mt32emu=false

# Linux: Default wrap mode (system packages preferred)
meson setup build -Duse_fluidsynth=false -Duse_mt32emu=false
```

### Implementation Status
- [x] Simplify zlib to system-only
- [x] Add fallback support to all dependencies in meson.build
- [x] Fix src/libs/zmbv/meson.build (remove system_zlib_ng_dep reference)
- [x] Update macos.yml with -Duse_fluidsynth=false -Duse_mt32emu=false
- [x] Update linux.yml with flags for build and build_release jobs
- [x] Test meson setup with --wrap-mode=forcefallback
- [x] Compile and verify build works
- [x] Commit changes to Git
- [ ] Simplify macos.yml (remove Homebrew install steps after wraps work)
- [ ] Remove dylibbundler dependency in macOS workflow (static linking)
- [ ] Test full workflow on macOS to verify static linking

## PC-GEOS Feature Configuration

Features disabled for PC-GEOS (retro computing focus):
- **FluidSynth**: Disabled (`-Duse_fluidsynth=false`) - modern MIDI synthesis not needed
- **MT32Emu**: Disabled (`-Duse_mt32emu=false`) - MT-32 emulation not needed

Features kept enabled:
- **SDL_net**: Enabled for networking capabilities
- **slirp**: Enabled for virtual networking
- **SpeexDSP**: Enabled for audio processing