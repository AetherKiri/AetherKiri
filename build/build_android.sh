#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="debug"
ABIS="arm64-v8a"
for arg in "$@"; do
    case "$arg" in
        debug|release|Debug|Release) BUILD_TYPE="$arg" ;;
        --abi=*) ABIS="${arg#*=}" ;;
        *) echo "[WARN] Unknown Android build argument ignored: $arg" ;;
    esac
done

BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"

if [[ "$BUILD_TYPE_LOWER" != "debug" && "$BUILD_TYPE_LOWER" != "release" ]]; then
    echo "Error: Invalid build type '$BUILD_TYPE'. Use 'debug' or 'release'." >&2
    exit 1
fi

ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
GODOT_BIN="${GODOT_BIN:-/Applications/Godot.app/Contents/MacOS/Godot}"
GODOT_TEMPLATE_DIR="${GODOT_TEMPLATE_DIR:-$HOME/Library/Application Support/Godot/export_templates/4.6.3.stable}"
GODOT_APP_DIR="$PROJECT_ROOT/apps/godot_app"
PARALLEL_JOBS="${JOBS:-8}"

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

find_android_ndk() {
    local candidate
    if [[ -d "$ANDROID_HOME/ndk" ]]; then
        find "$ANDROID_HOME/ndk" -maxdepth 1 -mindepth 1 -type d \
            -exec test -f '{}/build/cmake/android.toolchain.cmake' ';' -print \
            | sort -V | tail -1
        return 0
    fi

    for candidate in "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK:-}" "${NDK_HOME:-}"; do
        [[ -z "$candidate" ]] && continue
        candidate="${candidate%.}"
        if [[ -f "$candidate/build/cmake/android.toolchain.cmake" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

ANDROID_NDK_HOME_RESOLVED="$(find_android_ndk || true)"
if [[ -z "$ANDROID_NDK_HOME_RESOLVED" ]]; then
    echo "Error: Android NDK not found. Install one with Android Studio or sdkmanager." >&2
    exit 1
fi
export ANDROID_HOME
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export ANDROID_NDK_HOME="$ANDROID_NDK_HOME_RESOLVED"
export ANDROID_NDK="$ANDROID_NDK_HOME_RESOLVED"

command -v cmake >/dev/null
command -v ninja >/dev/null

if [[ ! -d "$ANDROID_HOME" ]]; then
    echo "Error: Android SDK not found at $ANDROID_HOME." >&2
    exit 1
fi

is_elf_file() {
    local path="$1"
    [[ -f "$path" ]] || return 1
    [[ "$(dd if="$path" bs=4 count=1 2>/dev/null | LC_ALL=C od -An -tx1 | tr -d ' \n')" == "7f454c46" ]]
}

copy_android_so() {
    local src="$1"
    local dst="$2"

    rm -f "$dst"
    if command -v rsync >/dev/null; then
        rsync -a "$src" "$dst"
    else
        dd if="$src" of="$dst" bs=1048576 status=none
        chmod 755 "$dst"
    fi
    if is_elf_file "$dst"; then
        return 0
    fi

    sync
    sleep 1
    rm -f "$dst"
    if command -v rsync >/dev/null; then
        rsync -a "$src" "$dst"
    else
        dd if="$src" of="$dst" bs=1048576 status=none
        chmod 755 "$dst"
    fi
    if ! is_elf_file "$dst"; then
        echo "Error: staged Android library is not a valid ELF file: $dst" >&2
        exit 1
    fi
}

build_abi() {
    local abi="$1"
    local cmake_config_preset
    local cmake_build_preset
    local cmake_build_dir
    local godot_bin_dir
    local vcpkg_triplet_dir
    local libomp_path

    case "$abi" in
        arm64-v8a)
            cmake_config_preset="Android arm64 ${BUILD_TYPE_CAP} Config"
            cmake_build_preset="Android arm64 ${BUILD_TYPE_CAP} Build"
            ;;
        *)
            echo "Error: Android ABI '$abi' is not wired for the Godot migration yet. Use arm64-v8a." >&2
            exit 1
            ;;
    esac

    cmake_build_dir="$PROJECT_ROOT/out/android/$abi/$BUILD_TYPE_LOWER"
    godot_bin_dir="$GODOT_APP_DIR/bin/android/$abi/$BUILD_TYPE_LOWER"
    vcpkg_triplet_dir="$cmake_build_dir/vcpkg_installed/arm64-android"

    echo "==> Building Android native libraries ($abi, $BUILD_TYPE_LOWER)"
    if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
        # Some vcpkg Android packages embed absolute pkg-config paths from the
        # first local install. Keep that compatibility path available so
        # release configure can consume restored binary packages.
        mkdir -p "$PROJECT_ROOT/out/android/$abi/debug"
        if [[ ! -e "$PROJECT_ROOT/out/android/$abi/debug/vcpkg_installed" ]]; then
            ln -s ../release/vcpkg_installed "$PROJECT_ROOT/out/android/$abi/debug/vcpkg_installed"
        fi
    elif [[ -L "$PROJECT_ROOT/out/android/$abi/debug/vcpkg_installed" ]]; then
        rm -f "$PROJECT_ROOT/out/android/$abi/debug/vcpkg_installed"
    fi

    cmake --preset "$cmake_config_preset" --fresh
    cmake --build --preset "$cmake_build_preset" -- -j"$PARALLEL_JOBS"

    mkdir -p "$godot_bin_dir"
    copy_android_so "$cmake_build_dir/bridge/engine_api/libengine_api.so" "$godot_bin_dir/libengine_api.so"
    copy_android_so "$cmake_build_dir/bridge/godot_extension/libaether_kiri_godot.so" "$godot_bin_dir/libaether_kiri_godot.so"
    copy_android_so "$vcpkg_triplet_dir/lib/libSDL2.so" "$godot_bin_dir/libSDL2.so"
    libomp_path="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/19/lib/linux/aarch64/libomp.so"
    if [[ ! -f "$libomp_path" ]]; then
        libomp_path="$(find "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt" -path '*/lib/linux/aarch64/libomp.so' -print -quit)"
    fi
    if [[ -z "$libomp_path" || ! -f "$libomp_path" ]]; then
        echo "Error: Android OpenMP runtime libomp.so not found under $ANDROID_NDK_HOME." >&2
        exit 1
    fi
    copy_android_so "$libomp_path" "$godot_bin_dir/libomp.so"
}

IFS=',' read -r -a ABI_LIST <<< "$ABIS"
for abi in "${ABI_LIST[@]}"; do
    build_abi "$abi"
done

if [[ ! -x "$GODOT_BIN" ]]; then
    echo "Warning: Godot not found at $GODOT_BIN; native libraries were staged only." >&2
elif [[ ! -f "$GODOT_TEMPLATE_DIR/android_debug.apk" || ! -f "$GODOT_TEMPLATE_DIR/android_release.apk" ]]; then
    echo "Warning: Godot Android export templates are missing in $GODOT_TEMPLATE_DIR; native libraries were staged only." >&2
    echo "         Expected android_debug.apk and android_release.apk." >&2
else
    echo "==> Exporting Godot Android APK"
    mkdir -p "$PROJECT_ROOT/out/godot/android/$BUILD_TYPE_LOWER"
    export_path="$PROJECT_ROOT/out/godot/android/$BUILD_TYPE_LOWER/AetherKiri-$BUILD_TYPE_LOWER.apk"
    export_preset="Android ${BUILD_TYPE_CAP}"
    export_mode="--export-debug"
    if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
        export_mode="--export-release"
    fi
    "$GODOT_BIN" --headless --path "$GODOT_APP_DIR" \
        "$export_mode" "$export_preset" "$export_path"
fi

echo "Android build output: $PROJECT_ROOT/out/godot/android/$BUILD_TYPE_LOWER"
