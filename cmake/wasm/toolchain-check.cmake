if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "The WASM target requires the Emscripten emcmake/em++ toolchain")
endif()

if(NOT DEFINED EMSCRIPTEN_VERSION)
  message(FATAL_ERROR "Unable to determine the active Emscripten version")
endif()

# Pinned reference toolchain version documented in docs/wasm/BUILD.md
set(PICOTRACKER_WASM_PINNED_EMSCRIPTEN "6.0.5")

if(EMSCRIPTEN_VERSION VERSION_LESS "3.1.54")
  message(FATAL_ERROR "PicoTracker WASM requires Emscripten 3.1.54 or newer (pinned reference: ${PICOTRACKER_WASM_PINNED_EMSCRIPTEN})")
endif()

if(NOT EMSCRIPTEN_VERSION VERSION_EQUAL "${PICOTRACKER_WASM_PINNED_EMSCRIPTEN}")
  message(STATUS "Emscripten version ${EMSCRIPTEN_VERSION} active (pinned reference: ${PICOTRACKER_WASM_PINNED_EMSCRIPTEN})")
endif()
