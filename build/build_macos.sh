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
GODOT_EXPORT_TEMPLATE="${GODOT_EXPORT_TEMPLATE:-$HOME/Library/Application Support/Godot/export_templates/4.6.3.stable/macos.zip}"
GODOT_APP_DIR="$PROJECT_ROOT/apps/godot_app"
CMAKE_CONFIG_PRESET="MacOS ${BUILD_TYPE_CAP} Config"
CMAKE_BUILD_PRESET="MacOS ${BUILD_TYPE_CAP} Build"
CMAKE_BUILD_DIR="$PROJECT_ROOT/out/macos/$BUILD_TYPE_LOWER"
GODOT_BIN_DIR="$GODOT_APP_DIR/bin/macos/$BUILD_TYPE_LOWER"
GODOT_EXPORT_PRESET="macOS ${BUILD_TYPE_CAP}"
GODOT_EXPORT_MODE="--export-debug"
PARALLEL_JOBS="${JOBS:-8}"

if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
    GODOT_EXPORT_MODE="--export-release"
fi

ensure_vcpkg() {
    if [[ -f "$PROJECT_ROOT/.devtools/vcpkg/.vcpkg-root" ]]; then
        export VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
    elif [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/.vcpkg-root" ]]; then
        export VCPKG_ROOT
    else
        echo "[INFO] vcpkg not found. Automatically setting up vcpkg in .devtools/vcpkg..."
        mkdir -p "$PROJECT_ROOT/.devtools"
        rm -rf "$PROJECT_ROOT/.devtools/vcpkg"
        git clone https://github.com/microsoft/vcpkg.git "$PROJECT_ROOT/.devtools/vcpkg"
        (cd "$PROJECT_ROOT/.devtools/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
        export VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
    fi

    if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
        if [[ -x "$VCPKG_ROOT/bootstrap-vcpkg.sh" ]]; then
            echo "[INFO] vcpkg binary missing. Bootstrapping existing vcpkg tree..."
            (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics)
        else
            echo "[INFO] vcpkg tree is incomplete. Recreating .devtools/vcpkg..."
            mkdir -p "$PROJECT_ROOT/.devtools"
            rm -rf "$PROJECT_ROOT/.devtools/vcpkg"
            git clone https://github.com/microsoft/vcpkg.git "$PROJECT_ROOT/.devtools/vcpkg"
            (cd "$PROJECT_ROOT/.devtools/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
            export VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
        fi
    fi
}

ensure_vcpkg

command -v cmake >/dev/null
NINJA_BIN="${CMAKE_MAKE_PROGRAM:-$(command -v ninja || command -v ninja-build || true)}"
if [[ -z "$NINJA_BIN" ]]; then
    echo "Error: Ninja build tool not found. Install ninja and ensure it is available in PATH." >&2
    exit 1
fi
export CMAKE_MAKE_PROGRAM="$NINJA_BIN"

echo "==> Building native engine and Godot extension"
cmake_config_args=(-D "CMAKE_MAKE_PROGRAM=$CMAKE_MAKE_PROGRAM")
if [[ "${SKIP_VCPKG_INSTALL:-}" == "1" ]]; then
    if [[ ! -d "$VCPKG_ROOT/installed/arm64-osx" ]]; then
        echo "Error: SKIP_VCPKG_INSTALL=1 but prebuilt vcpkg triplet is missing: $VCPKG_ROOT/installed/arm64-osx" >&2
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
cp -f "$CMAKE_BUILD_DIR/bridge/engine_api/libengine_api.dylib" "$GODOT_BIN_DIR/"
cp -f "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.dylib" "$GODOT_BIN_DIR/"
codesign --force --sign - "$GODOT_BIN_DIR/libengine_api.dylib" "$GODOT_BIN_DIR/libaether_kiri_godot.dylib" >/dev/null 2>&1 || true

if [[ ! -x "$GODOT_BIN" ]]; then
    echo "Warning: Godot not found at $GODOT_BIN; native libraries were staged only." >&2
elif [[ ! -f "$GODOT_EXPORT_TEMPLATE" ]]; then
    echo "Warning: Godot macOS export template missing at $GODOT_EXPORT_TEMPLATE; native libraries were staged only." >&2
else
    echo "==> Exporting Godot macOS app"
    GODOT_EXPORT_APP="$PROJECT_ROOT/out/godot/macos/$BUILD_TYPE_LOWER/AetherKiri.app"
    mkdir -p "$PROJECT_ROOT/out/godot/macos/$BUILD_TYPE_LOWER"
    "$GODOT_BIN" --headless --path "$GODOT_APP_DIR" \
        "$GODOT_EXPORT_MODE" "$GODOT_EXPORT_PRESET" "$GODOT_EXPORT_APP"
    if [[ -d "$GODOT_EXPORT_APP/Contents/Frameworks" ]]; then
        cp -f "$GODOT_BIN_DIR/libengine_api.dylib" "$GODOT_EXPORT_APP/Contents/Frameworks/"
        cp -f "$GODOT_BIN_DIR/libaether_kiri_godot.dylib" "$GODOT_EXPORT_APP/Contents/Frameworks/"
        codesign --force --sign - \
            "$GODOT_EXPORT_APP/Contents/Frameworks/libengine_api.dylib" \
            "$GODOT_EXPORT_APP/Contents/Frameworks/libaether_kiri_godot.dylib" \
            >/dev/null 2>&1 || true
        codesign --force --deep --sign - "$GODOT_EXPORT_APP" >/dev/null 2>&1 || true
        codesign --verify --deep --strict --verbose=2 "$GODOT_EXPORT_APP"
    fi
fi

echo "macOS build output: $PROJECT_ROOT/out/godot/macos/$BUILD_TYPE_LOWER"
