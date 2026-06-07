#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="${1:-debug}"
BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"

if [[ "$BUILD_TYPE_LOWER" != "debug" && "$BUILD_TYPE_LOWER" != "release" ]]; then
    echo "Error: Invalid build type '$BUILD_TYPE'. Use 'debug' or 'release'." >&2
    exit 1
fi

GODOT_BIN="${GODOT_BIN:-/Applications/Godot.app/Contents/MacOS/Godot}"
GODOT_TEMPLATE_DIR="${GODOT_TEMPLATE_DIR:-$HOME/Library/Application Support/Godot/export_templates/4.6.3.stable}"
GODOT_APP_DIR="$PROJECT_ROOT/apps/godot_app"
CMAKE_CONFIG_PRESET="Web ${BUILD_TYPE_CAP} Config"
CMAKE_BUILD_PRESET="Web ${BUILD_TYPE_CAP} Build"
CMAKE_BUILD_DIR="$PROJECT_ROOT/out/web/$BUILD_TYPE_LOWER"
GODOT_BIN_DIR="$GODOT_APP_DIR/bin/web/$BUILD_TYPE_LOWER"
GODOT_EXPORT_PRESET="Web ${BUILD_TYPE_CAP}"
GODOT_EXPORT_MODE="--export-debug"
PARALLEL_JOBS="${JOBS:-8}"

if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
    GODOT_EXPORT_MODE="--export-release"
fi

if [[ -d "$PROJECT_ROOT/.devtools/vcpkg/.git" ]]; then
    export VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
elif [[ -z "${VCPKG_ROOT:-}" ]]; then
    echo "Error: VCPKG_ROOT is not set and .devtools/vcpkg is missing." >&2
    exit 1
fi

if [[ -z "${EMSCRIPTEN_ROOT:-}" && -n "${EMSDK:-}" ]]; then
    export EMSCRIPTEN_ROOT="$EMSDK/upstream/emscripten"
fi
if [[ -z "${EMSCRIPTEN_ROOT:-}" ]]; then
    emcc_path="$(command -v emcc 2>/dev/null || true)"
    if [[ -n "$emcc_path" ]]; then
        emcc_realpath="$emcc_path"
        if command -v python3 >/dev/null 2>&1; then
            emcc_realpath="$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$emcc_path" 2>/dev/null || echo "$emcc_path")"
        fi
        emcc_dir="$(cd "$(dirname "$emcc_realpath")" && pwd)"
        for emscripten_candidate in "$emcc_dir" "$emcc_dir/../libexec" "$emcc_dir/.."; do
            if [[ -f "$emscripten_candidate/cmake/Modules/Platform/Emscripten.cmake" ]]; then
                export EMSCRIPTEN_ROOT="$(cd "$emscripten_candidate" && pwd)"
                break
            fi
        done
    fi
fi
if [[ -z "${EMSDK_PYTHON:-}" ]] && command -v python3 >/dev/null 2>&1; then
    export EMSDK_PYTHON="$(command -v python3)"
fi

command -v cmake >/dev/null
NINJA_BIN="${CMAKE_MAKE_PROGRAM:-$(command -v ninja || command -v ninja-build || true)}"
if [[ -z "$NINJA_BIN" ]]; then
    echo "Error: Ninja build tool not found. Install ninja and ensure it is available in PATH." >&2
    exit 1
fi
export CMAKE_MAKE_PROGRAM="$NINJA_BIN"
command -v emcc >/dev/null || {
    echo "Error: emcc not found. Activate Emscripten first, for example: source /path/to/emsdk/emsdk_env.sh" >&2
    exit 1
}
command -v em++ >/dev/null || {
    echo "Error: em++ not found. Activate Emscripten first." >&2
    exit 1
}
command -v emar >/dev/null || {
    echo "Error: emar not found. Activate Emscripten first." >&2
    exit 1
}
command -v embuilder >/dev/null || {
    echo "Error: embuilder not found. Activate Emscripten first." >&2
    exit 1
}
EMSCRIPTEN_TOOLCHAIN="$EMSCRIPTEN_ROOT/cmake/Modules/Platform/Emscripten.cmake"
if [[ ! -f "$EMSCRIPTEN_TOOLCHAIN" ]]; then
    echo "Error: Emscripten toolchain not found at $EMSCRIPTEN_TOOLCHAIN." >&2
    exit 1
fi

echo "==> Building Web native engine and Godot extension side module"
embuilder --pic build libunwind-mt-legacyexcept
embuilder --pic build libc++abi-mt-legacyexcept
embuilder --pic build libc++-mt-legacyexcept
WEB_C_FLAGS="-fPIC -pthread -msimd128 -sSUPPORT_LONGJMP=wasm -sWASM_LEGACY_EXCEPTIONS=1"
WEB_CXX_FLAGS="$WEB_C_FLAGS -fwasm-exceptions"
WEB_LINK_FLAGS="-pthread -msimd128 -sSUPPORT_LONGJMP=wasm -sWASM_LEGACY_EXCEPTIONS=1 -fwasm-exceptions"
cmake_config_args=(
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSCRIPTEN_TOOLCHAIN"
    -D "CMAKE_MAKE_PROGRAM=$CMAKE_MAKE_PROGRAM"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_C_FLAGS="$WEB_C_FLAGS"
    -DCMAKE_CXX_FLAGS="$WEB_CXX_FLAGS"
    -DCMAKE_EXE_LINKER_FLAGS="$WEB_LINK_FLAGS"
    -DCMAKE_SHARED_LINKER_FLAGS="$WEB_LINK_FLAGS"
)
if [[ "${SKIP_VCPKG_INSTALL:-}" == "1" ]]; then
    if [[ ! -d "$VCPKG_ROOT/installed/wasm32-emscripten" ]]; then
        echo "Error: SKIP_VCPKG_INSTALL=1 but prebuilt vcpkg triplet is missing: $VCPKG_ROOT/installed/wasm32-emscripten" >&2
        exit 1
    fi
    mkdir -p "$CMAKE_BUILD_DIR"
    rm -rf "$CMAKE_BUILD_DIR/vcpkg_installed"
    ln -s "$VCPKG_ROOT/installed" "$CMAKE_BUILD_DIR/vcpkg_installed"
    cmake_config_args+=(
        -D "VCPKG_MANIFEST_INSTALL=OFF"
        -D "VCPKG_INSTALLED_DIR=$CMAKE_BUILD_DIR/vcpkg_installed"
    )
fi

cmake --preset "$CMAKE_CONFIG_PRESET" --fresh "${cmake_config_args[@]}"
cmake --build --preset "$CMAKE_BUILD_PRESET" -- -j"$PARALLEL_JOBS"

mkdir -p "$GODOT_BIN_DIR"
wasm_source=""
if [[ -f "$CMAKE_BUILD_DIR/bridge/godot_extension/aether_kiri_godot.wasm" ]]; then
    wasm_source="$CMAKE_BUILD_DIR/bridge/godot_extension/aether_kiri_godot.wasm"
elif [[ -f "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.wasm" ]]; then
    wasm_source="$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.wasm"
else
    echo "Error: Web GDExtension side module was not produced in $CMAKE_BUILD_DIR/bridge/godot_extension." >&2
    exit 1
fi

wasm_magic="$(od -An -tx1 -N4 "$wasm_source" | tr -d ' \n')"
if [[ "$wasm_magic" != "0061736d" ]]; then
    echo "Error: $wasm_source is not a WebAssembly module." >&2
    exit 1
fi
cp -f "$wasm_source" "$GODOT_BIN_DIR/aether_kiri_godot.wasm"

debug_template_file="$GODOT_TEMPLATE_DIR/web_dlink_debug.zip"
release_template_file="$GODOT_TEMPLATE_DIR/web_dlink_release.zip"
if [[ ! -x "$GODOT_BIN" ]]; then
    echo "Warning: Godot not found at $GODOT_BIN; Web side module was staged only." >&2
elif [[ ! -f "$debug_template_file" || ! -f "$release_template_file" ]]; then
    echo "Warning: Godot Web dlink export templates are missing in $GODOT_TEMPLATE_DIR; Web side module was staged only." >&2
    echo "         Expected web_dlink_debug.zip and web_dlink_release.zip." >&2
else
    echo "==> Exporting Godot Web app"
    export_root="$PROJECT_ROOT/out/godot/web/$BUILD_TYPE_LOWER"
    export_path="$export_root/index.html"
    mkdir -p "$export_root"
    "$GODOT_BIN" --headless --path "$GODOT_APP_DIR" \
        "$GODOT_EXPORT_MODE" "$GODOT_EXPORT_PRESET" "$export_path"
    if command -v node >/dev/null; then
        node "$PROJECT_ROOT/build/patch_web_export.mjs" "$export_root"
    else
        echo "Warning: node not found; Web local game mount patch was not applied." >&2
    fi
fi

echo "Web build output: $PROJECT_ROOT/out/godot/web/$BUILD_TYPE_LOWER"
