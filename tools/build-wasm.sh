#!/usr/bin/env bash

set -euo pipefail

build_type="${1:-Release}"
case "$build_type" in
  Debug|Release|RelWithDebInfo) ;;
  *)
    echo "usage: $0 [Debug|Release|RelWithDebInfo]" >&2
    exit 2
    ;;
esac

if ! command -v emcmake >/dev/null 2>&1 || ! command -v em++ >/dev/null 2>&1; then
  repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
  if [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
    # shellcheck source=/dev/null
    source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
  elif [ -f "$repo_root/../emsdk/emsdk_env.sh" ]; then
    # shellcheck source=/dev/null
    source "$repo_root/../emsdk/emsdk_env.sh" >/dev/null 2>&1
  elif [ -f "$repo_root/emsdk/emsdk_env.sh" ]; then
    # shellcheck source=/dev/null
    source "$repo_root/emsdk/emsdk_env.sh" >/dev/null 2>&1
  fi
fi

if ! command -v emcmake >/dev/null 2>&1 || ! command -v em++ >/dev/null 2>&1; then
  cat << 'EOF' >&2
Error: Emscripten toolchain (emcmake, em++) is not active or found in PATH.

To set up the reproducible toolchain (pinned reference: Emscripten 6.0.5):
  git clone https://github.com/emscripten-core/emsdk.git
  cd emsdk
  ./emsdk install 6.0.5
  ./emsdk activate 6.0.5
  source ./emsdk_env.sh

For detailed instructions, see docs/wasm/BUILD.md.
EOF
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build-wasm/${build_type}"

emcmake cmake \
  -S "$repo_root/sources" \
  -B "$build_dir" \
  -DWASM=ON \
  -DCMAKE_BUILD_TYPE="$build_type"

# Keep local workbench builds predictable on developer machines. Callers that
# explicitly want more workers can opt in with CMAKE_BUILD_PARALLEL_LEVEL.
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-1}"

