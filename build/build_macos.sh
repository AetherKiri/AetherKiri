#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="${1:-debug}"
BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"
MACOS_ARCH="${AETHERKIRI_MACOS_ARCH:-x86_64}"

if [[ "$BUILD_TYPE_LOWER" != "debug" && "$BUILD_TYPE_LOWER" != "release" ]]; then
    echo "Error: Invalid build type '$BUILD_TYPE'. Use 'debug' or 'release'." >&2
    exit 1
fi
if [[ "$MACOS_ARCH" != "arm64" && "$MACOS_ARCH" != "x86_64" ]]; then
    echo "Error: Invalid AETHERKIRI_MACOS_ARCH '$MACOS_ARCH'. Use 'arm64' or 'x86_64'." >&2
    exit 1
fi

GODOT_BIN="${GODOT_BIN:-/Applications/Godot.app/Contents/MacOS/Godot}"
GODOT_EXPORT_TEMPLATE="${GODOT_EXPORT_TEMPLATE:-$HOME/Library/Application Support/Godot/export_templates/4.7.stable/macos.zip}"
GODOT_APP_DIR="$PROJECT_ROOT/apps/godot_app"
RUNTIME_CJK_FONT_SOURCE="$GODOT_APP_DIR/assets/fonts/aetherkiri-runtime-cjk.otf"
RUNTIME_SYMBOL_FONT_SOURCE="$GODOT_APP_DIR/assets/fonts/aetherkiri-runtime-symbols.ttf"
CMAKE_CONFIG_PRESET="MacOS ${BUILD_TYPE_CAP} Config"
CMAKE_BUILD_PRESET="MacOS ${BUILD_TYPE_CAP} Build"
CMAKE_BUILD_DIR="$PROJECT_ROOT/out/macos/$BUILD_TYPE_LOWER"
GODOT_EXPORT_ROOT="$PROJECT_ROOT/out/godot/macos/$BUILD_TYPE_LOWER"
VCPKG_TRIPLET="arm64-osx"
if [[ "$MACOS_ARCH" == "x86_64" ]]; then
    CMAKE_CONFIG_PRESET="MacOS x64 ${BUILD_TYPE_CAP} Config"
    CMAKE_BUILD_PRESET="MacOS x64 ${BUILD_TYPE_CAP} Build"
    CMAKE_BUILD_DIR="$PROJECT_ROOT/out/macos-x64/$BUILD_TYPE_LOWER"
    GODOT_EXPORT_ROOT="$PROJECT_ROOT/out/godot/macos-x64/$BUILD_TYPE_LOWER"
    VCPKG_TRIPLET="x64-osx"
fi
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

stage_macos_runtime_fonts() {
    local app_bundle="$1"
    local resource_dir="$app_bundle/Contents/Resources"
    local font_dir="$resource_dir/fonts"

    mkdir -p "$font_dir"
    if [[ -f "$RUNTIME_CJK_FONT_SOURCE" ]]; then
        cp -f "$RUNTIME_CJK_FONT_SOURCE" "$resource_dir/default.otf"
        cp -f "$RUNTIME_CJK_FONT_SOURCE" "$font_dir/default.otf"
    else
        echo "Warning: runtime CJK font missing: $RUNTIME_CJK_FONT_SOURCE" >&2
    fi
    if [[ -f "$RUNTIME_SYMBOL_FONT_SOURCE" ]]; then
        cp -f "$RUNTIME_SYMBOL_FONT_SOURCE" "$font_dir/symbols.ttf"
    else
        echo "Warning: runtime symbol font missing: $RUNTIME_SYMBOL_FONT_SOURCE" >&2
    fi
}

strip_macos_runtime_symbols() {
    "$PROJECT_ROOT/tools/strip_runtime_symbols.sh" macho "$@"
}

thin_macos_executable_to_arch() {
    local executable="$1"
    local architectures
    local thinned_executable

    architectures="$(lipo -archs "$executable")"
    if [[ " $architectures " != *" $MACOS_ARCH "* ]]; then
        echo "Error: exported macOS executable does not contain $MACOS_ARCH: $architectures" >&2
        exit 1
    fi
    if [[ "$architectures" == "$MACOS_ARCH" ]]; then
        return
    fi

    thinned_executable="${executable}.${MACOS_ARCH}"
    lipo "$executable" -thin "$MACOS_ARCH" -output "$thinned_executable"
    chmod +x "$thinned_executable"
    mv -f "$thinned_executable" "$executable"
}

echo "==> Building native engine and Godot extension"
cmake_config_args=(
    -D "CMAKE_MAKE_PROGRAM=$CMAKE_MAKE_PROGRAM"
    -D "AETHERKIRI_ENABLE_INTERNAL=${AETHERKIRI_ENABLE_INTERNAL:-ON}"
)
if [[ "${SKIP_VCPKG_INSTALL:-}" == "1" ]]; then
    if [[ ! -d "$VCPKG_ROOT/installed/$VCPKG_TRIPLET" ]]; then
        echo "Error: SKIP_VCPKG_INSTALL=1 but prebuilt vcpkg triplet is missing: $VCPKG_ROOT/installed/$VCPKG_TRIPLET" >&2
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
if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
    echo "==> Removing non-runtime symbols from staged macOS Release libraries"
    strip_macos_runtime_symbols \
        "$GODOT_BIN_DIR/libengine_api.dylib" \
        "$GODOT_BIN_DIR/libaether_kiri_godot.dylib"
fi
codesign --force --sign - "$GODOT_BIN_DIR/libengine_api.dylib" "$GODOT_BIN_DIR/libaether_kiri_godot.dylib" >/dev/null 2>&1 || true

if [[ ! -x "$GODOT_BIN" ]]; then
    echo "Warning: Godot not found at $GODOT_BIN; native libraries were staged only." >&2
elif [[ ! -f "$GODOT_EXPORT_TEMPLATE" ]]; then
    echo "Warning: Godot macOS export template missing at $GODOT_EXPORT_TEMPLATE; native libraries were staged only." >&2
else
    echo "==> Exporting Godot macOS app"
    GODOT_EXPORT_APP="$GODOT_EXPORT_ROOT/Aether.app"
    godot_export_command=("$GODOT_BIN")
    if [[ "$MACOS_ARCH" == "x86_64" ]]; then
        # The editor must match the staged GDExtension architecture while it
        # scans and exports the project. Godot.app is universal on Apple
        # Silicon, so select its Rosetta slice for this native build.
        godot_export_command=(arch -x86_64 "$GODOT_BIN")
    fi
    mkdir -p "$GODOT_EXPORT_ROOT"
    rm -rf "$GODOT_EXPORT_APP"
    "${godot_export_command[@]}" --headless --path "$GODOT_APP_DIR" \
        "$GODOT_EXPORT_MODE" "$GODOT_EXPORT_PRESET" "$GODOT_EXPORT_APP"
    if [[ -d "$GODOT_EXPORT_APP/Contents/Frameworks" ]]; then
        stage_macos_runtime_fonts "$GODOT_EXPORT_APP"
        /usr/libexec/PlistBuddy \
            -c 'Set :SKIncludeConsumableInAppPurchaseHistory true' \
            "$GODOT_EXPORT_APP/Contents/Info.plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy \
                -c 'Add :SKIncludeConsumableInAppPurchaseHistory bool true' \
                "$GODOT_EXPORT_APP/Contents/Info.plist"
        # The Apple release declares only encryption that is exempt from export
        # documentation. Embed that policy in every macOS bundle so App Store
        # Connect does not ask the same question on each upload.
        /usr/libexec/PlistBuddy \
            -c 'Set :ITSAppUsesNonExemptEncryption false' \
            "$GODOT_EXPORT_APP/Contents/Info.plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy \
                -c 'Add :ITSAppUsesNonExemptEncryption bool false' \
                "$GODOT_EXPORT_APP/Contents/Info.plist"
        if [[ "$(plutil -extract ITSAppUsesNonExemptEncryption raw \
            "$GODOT_EXPORT_APP/Contents/Info.plist")" != "false" ]]; then
            echo "Error: exported macOS app declares non-exempt encryption." >&2
            exit 1
        fi
        cp -f "$GODOT_BIN_DIR/libengine_api.dylib" "$GODOT_EXPORT_APP/Contents/Frameworks/"
        cp -f "$GODOT_BIN_DIR/libaether_kiri_godot.dylib" "$GODOT_EXPORT_APP/Contents/Frameworks/"
        echo "==> Thinning exported macOS executable to $MACOS_ARCH"
        thin_macos_executable_to_arch "$GODOT_EXPORT_APP/Contents/MacOS/Aether"
        if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
            echo "==> Removing non-runtime symbols from exported macOS Release executable"
            "$PROJECT_ROOT/tools/strip_runtime_symbols.sh" macho-executable \
                "$GODOT_EXPORT_APP/Contents/MacOS/Aether"
            runtime_binaries=()
            while IFS= read -r -d '' runtime_binary; do
                if file -b "$runtime_binary" | grep -q 'Mach-O'; then
                    runtime_binaries+=("$runtime_binary")
                fi
            done < <(
                find "$GODOT_EXPORT_APP/Contents/Frameworks" -type f -print0
            )
            if ((${#runtime_binaries[@]})); then
                echo "==> Removing non-runtime symbols from exported macOS Release libraries"
                strip_macos_runtime_symbols "${runtime_binaries[@]}"
            fi
        fi
        codesign --force --sign - \
            "$GODOT_EXPORT_APP/Contents/Frameworks/libengine_api.dylib" \
            "$GODOT_EXPORT_APP/Contents/Frameworks/libaether_kiri_godot.dylib" \
            >/dev/null 2>&1 || true
        codesign --force --deep --sign - "$GODOT_EXPORT_APP" >/dev/null 2>&1 || true
        codesign --verify --deep --strict --verbose=2 "$GODOT_EXPORT_APP"
    fi
fi

echo "macOS build output: $GODOT_EXPORT_ROOT"
