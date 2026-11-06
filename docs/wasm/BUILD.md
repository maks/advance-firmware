# Building the PicoTracker WASM Target

This document describes how to set up the reproducible toolchain and build the PicoTracker WebAssembly target.

## Reproducible Toolchain Specification

The WebAssembly build requires an Emscripten toolchain capable of compiling C++23, POSIX threads, Web Workers, and Audio Worklets.

The pinned reference toolchain versions are:

- **Emscripten SDK**: `6.0.5`
- **Node.js**: `24.19.0` (or 24.x)
- **CMake**: `3.13` or newer
- **Build System**: GNU Make or Ninja

### Setting Up Emscripten SDK

To install and activate the pinned version of Emscripten:

```sh
# Clone emsdk if not already present
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install and activate the pinned version
./emsdk install 6.0.5
./emsdk activate 6.0.5

# Activate environment in your current shell
source ./emsdk_env.sh
```

Ensure `emcmake` and `em++` are available on your `PATH`:

```sh
em++ --version
```

## Documented Build Entry Point (`tools/build-wasm.sh`)

The official entry point to build the WebAssembly binaries is `tools/build-wasm.sh`.

```sh
tools/build-wasm.sh [Release|Debug|RelWithDebInfo]
```

### Usage Examples

- **Release build (default)**:
  ```sh
  tools/build-wasm.sh Release
  ```

- **Debug build (includes debug symbols and WebAudio debugging)**:
  ```sh
  tools/build-wasm.sh Debug
  ```

- **Parallel build (if sufficient host RAM is available)**:
  ```sh
  CMAKE_BUILD_PARALLEL_LEVEL=4 tools/build-wasm.sh Debug
  ```

*Note: Parallel builds default to 1 worker because concurrent Emscripten linkers and C++ template instantiations consume substantial RAM.*

### What the Build Script Does

1. Verifies that the Emscripten toolchain is active in the environment (or auto-discovers a neighboring `emsdk/emsdk_env.sh`).
2. Configures CMake in `build-wasm/<build_type>` with `-DWASM=ON` and the Emscripten toolchain file.
3. Compiles the native C++23 source tree (Advance core tracker logic, instrument engines, audio pipeline, and WASM platform adapters).
4. Links the `picotracker_wasm` target using Emscripten pthread and WebAudio worklet settings.
5. Emits output artifacts directly to `web/public/wasm/`:
   - `web/public/wasm/picotracker.js`
   - `web/public/wasm/picotracker.wasm`
6. Runs the post-build link closure check (`cmake/VerifyCoreSymbols.cmake`) to verify that the core objects (`Application.cpp.o`, `Player.cpp.o`, `AppWindow.cpp.o`) are included in the link map.

The build embeds the curated `data/web-factory-content` tree in the WASM
virtual filesystem as `/defaults/factory`. On startup, the web app copies
missing factory themes, samples and every
`data/web-factory-content/projects/*` project into persistent `/data` storage
without replacing existing browser files, then writes `/data/.current` from
`data/web-factory-content/default-current.txt` when no current-project marker
exists yet. To change the selection or the project that loads first, edit
`data/web-factory-content` and rebuild the WASM target.

## Validation & Verification

### Factory Content Seeding Check

The storage seeding logic (factory projects, themes, samples and the
`/data/.current` default project marker) is covered by plain Node tests with an
in-memory Emscripten-like filesystem:

```sh
node web/tests/storage-seeding.test.mjs
```

### Link Closure Check

The CMake post-build step automatically verifies that Advance core symbols are retained in the final binary:

```sh
cmake -DWASM_BINARY=web/public/wasm/picotracker.wasm \
      -DLINK_MAP=build-wasm/Debug/Adapters/wasm/picotracker-core-link.map \
      -P cmake/VerifyCoreSymbols.cmake
```

### Advance Hardware Firmware Regression Check

Changes to shared headers and sources must never break the STM32H7 Advance hardware build:

```sh
cmake --build build-opt --parallel 4
```

### Host Unit Test Suite

Run the native host unit tests:

```sh
./build-host/picoTracker_tests
```

### Headless Browser Testing

Headless browser automation and manual verification can be performed using Obscura:

```sh
~/bin/obscura --help
```
