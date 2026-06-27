#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="debug"
SIMULATOR=false
SIMULATOR_ARCH="${IOS_SIMULATOR_ARCH:-x86_64}"
for arg in "$@"; do
    case "$arg" in
        debug|release|Debug|Release) BUILD_TYPE="$arg" ;;
        --simulator) SIMULATOR=true ;;
        --simulator-arch=*) SIMULATOR_ARCH="${arg#*=}" ;;
        *) echo "[WARN] Unknown iOS build argument ignored: $arg" ;;
    esac
done

BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"

if [[ "$SIMULATOR" == true ]]; then
    if [[ "$SIMULATOR_ARCH" == "x86_64" || "$SIMULATOR_ARCH" == "x64" ]]; then
        SIMULATOR_ARCH="x86_64"
        CMAKE_CONFIG_PRESET="iOS Simulator x64 Debug Config"
        CMAKE_BUILD_PRESET="iOS Simulator x64 Debug Build"
        CMAKE_BUILD_DIR="$PROJECT_ROOT/out/ios-simulator-x64/debug"
        GODOT_TRIPLET_DIR="ios-simulator-x64/debug"
        VCPKG_TRIPLET_DIR="x64-ios-simulator"
    elif [[ "$SIMULATOR_ARCH" == "arm64" ]]; then
        CMAKE_CONFIG_PRESET="iOS Simulator Debug Config"
        CMAKE_BUILD_PRESET="iOS Simulator Debug Build"
        CMAKE_BUILD_DIR="$PROJECT_ROOT/out/ios-simulator/debug"
        GODOT_TRIPLET_DIR="ios-simulator/debug"
        VCPKG_TRIPLET_DIR="arm64-ios-simulator"
    else
        echo "Error: Invalid simulator arch '$SIMULATOR_ARCH'. Use x86_64 or arm64." >&2
        exit 1
    fi
else
    CMAKE_CONFIG_PRESET="iOS ${BUILD_TYPE_CAP} Config"
    CMAKE_BUILD_PRESET="iOS ${BUILD_TYPE_CAP} Build"
    CMAKE_BUILD_DIR="$PROJECT_ROOT/out/ios/$BUILD_TYPE_LOWER"
    GODOT_TRIPLET_DIR="ios/$BUILD_TYPE_LOWER"
    VCPKG_TRIPLET_DIR="arm64-ios"
fi

GODOT_BIN="${GODOT_BIN:-/Applications/Godot.app/Contents/MacOS/Godot}"
GODOT_EXPORT_TEMPLATE="${GODOT_EXPORT_TEMPLATE:-$HOME/Library/Application Support/Godot/export_templates/4.7.stable/ios.zip}"
GODOT_APP_DIR="$PROJECT_ROOT/apps/godot_app"
GODOT_BIN_DIR="$GODOT_APP_DIR/bin/$GODOT_TRIPLET_DIR"
RUNTIME_CJK_FONT_SOURCE="$GODOT_APP_DIR/assets/fonts/aetherkiri-runtime-cjk.otf"
RUNTIME_SYMBOL_FONT_SOURCE="$GODOT_APP_DIR/assets/fonts/aetherkiri-runtime-symbols.ttf"
PARALLEL_JOBS="${JOBS:-8}"
FORCE_LOAD_PLUGIN_ARCHIVES=(
    "libkrkr2plugin.a"
    "libkagparserex.a"
    "liblayerExDraw.a"
    "libmotionplayer.a"
    "libpsbfile.a"
    "libpsdfile.a"
    "libpsdparse.a"
)
FORCE_LOAD_PLUGIN_SOURCES=(
    "cpp/plugins/libkrkr2plugin.a"
    "cpp/plugins/kagparserex/libkagparserex.a"
    "cpp/plugins/layerex_draw/liblayerExDraw.a"
    "cpp/plugins/motionplayer/libmotionplayer.a"
    "cpp/plugins/psbfile/libpsbfile.a"
    "cpp/plugins/psdfile/libpsdfile.a"
    "cpp/plugins/psdfile/psdparse/libpsdparse.a"
)
IOS_SDK_COMPAT_ARCHIVE="libios_sdk_compat_symbols.a"

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

preflight_simulator_template_arch() {
    local arch="$1"
    local template="$2"
    local tmpdir
    local libgodot
    local info

    if [[ ! -f "$template" ]]; then
        return
    fi

    tmpdir="$(mktemp -d /tmp/aetherkiri-ios-template.XXXXXX)"
    libgodot="$tmpdir/libgodot.ios.debug.xcframework/ios-arm64_x86_64-simulator/libgodot.a"
    unzip -q "$template" \
        'libgodot.ios.debug.xcframework/ios-arm64_x86_64-simulator/libgodot.a' \
        -d "$tmpdir"
    info="$(lipo -archs "$libgodot" 2>/dev/null || true)"
    rm -rf "$tmpdir"

    if [[ " $info " != *" $arch "* ]]; then
        echo "Error: Godot iOS simulator export template does not contain '$arch'." >&2
        echo "       $template" >&2
        echo "       architectures: ${info:-unknown}" >&2
        echo "       Install or build a Godot export template with an $arch simulator slice, or use --simulator-arch=x86_64." >&2
        exit 1
    fi
}

if [[ "$SIMULATOR" == true ]]; then
    preflight_simulator_template_arch "$SIMULATOR_ARCH" "$GODOT_EXPORT_TEMPLATE"
fi

resolve_ios_godot_cpp_lib() {
    local triplet_root="$1"
    local arch="$2"
    local build_type="$3"
    local config_name="release"
    local config_upper="RELEASE"
    if [[ "$build_type" == "debug" ]]; then
        config_name="debug"
        config_upper="DEBUG"
    fi

    local config_file="$triplet_root/share/unofficial-godot-cpp/unofficial-godot-cpp-config-$config_name.cmake"
    local location=""
    if [[ -f "$config_file" ]]; then
        location="$(sed -n "s|.*IMPORTED_LOCATION_${config_upper} \"\\(.*\\)\".*|\\1|p" "$config_file" | head -n 1)"
        location="${location//\$\{_IMPORT_PREFIX\}/$triplet_root}"
        if [[ -n "$location" && -f "$location" ]]; then
            printf '%s\n' "$location"
            return 0
        fi
    fi

    local search_dirs=()
    if [[ "$build_type" == "debug" ]]; then
        search_dirs+=("$triplet_root/debug/lib" "$triplet_root/lib")
    else
        search_dirs+=("$triplet_root/lib" "$triplet_root/debug/lib")
    fi

    local dir
    local found
    for dir in "${search_dirs[@]}"; do
        [[ -d "$dir" ]] || continue
        found="$(find "$dir" -maxdepth 1 -name "libgodot-cpp.ios.*.$arch.a" -print -quit 2>/dev/null || true)"
        if [[ -n "$found" ]]; then
            printf '%s\n' "$found"
            return 0
        fi
    done

    return 1
}

build_ios_sdk_compat_archive() {
    local output="$1"
    local triplet="$2"
    local arch="arm64"
    local sdk="iphoneos"
    local min_flag="-mios-version-min=${IOS_MIN_VERSION:-14.0}"
    local work_dir="$CMAKE_BUILD_DIR/ios_sdk_compat"
    local source="$work_dir/ios_sdk_compat_symbols.mm"
    local object="$work_dir/ios_sdk_compat_symbols.o"

    if [[ "$triplet" == "x64-ios-simulator" ]]; then
        arch="x86_64"
        sdk="iphonesimulator"
        min_flag="-mios-simulator-version-min=${IOS_MIN_VERSION:-14.0}"
    elif [[ "$triplet" == "arm64-ios-simulator" ]]; then
        sdk="iphonesimulator"
        min_flag="-mios-simulator-version-min=${IOS_MIN_VERSION:-14.0}"
    fi

    mkdir -p "$work_dir"
    cat > "$source" <<'EOF'
#import <Foundation/Foundation.h>

extern "C" {
extern "C" __attribute__((weak, visibility("default"))) NSString * const CADynamicRangeAutomatic = @"CADynamicRangeAutomatic";
extern "C" __attribute__((weak, visibility("default"))) NSString * const CADynamicRangeConstrainedHigh = @"CADynamicRangeConstrainedHigh";
extern "C" __attribute__((weak, visibility("default"))) NSString * const CADynamicRangeHigh = @"CADynamicRangeHigh";
extern "C" __attribute__((weak, visibility("default"))) NSString * const CADynamicRangeStandard = @"CADynamicRangeStandard";
extern "C" __attribute__((weak, visibility("default"))) NSString * const MTLLogStateErrorDomain = @"MTLLogStateErrorDomain";
extern "C" __attribute__((weak, visibility("default"))) NSString * const MTLTensorDomain = @"MTLTensorDomain";
extern "C" __attribute__((weak, visibility("default"))) NSString * const NSDeviceCertificationiPhonePerformanceGaming = @"NSDeviceCertificationiPhonePerformanceGaming";
extern "C" __attribute__((weak, visibility("default"))) NSString * const NSProcessInfoPerformanceProfileDidChangeNotification = @"NSProcessInfoPerformanceProfileDidChangeNotification";
extern "C" __attribute__((weak, visibility("default"))) NSString * const NSProcessPerformanceProfileDefault = @"NSProcessPerformanceProfileDefault";
extern "C" __attribute__((weak, visibility("default"))) NSString * const NSProcessPerformanceProfileSustained = @"NSProcessPerformanceProfileSustained";
}
EOF

    xcrun --sdk "$sdk" clang++ -arch "$arch" "$min_flag" -fobjc-arc -c "$source" -o "$object"
    libtool -static -o "$output" "$object"
}

combine_ios_static_extension() {
    local output="$1"
    local triplet="$2"
    local vcpkg_triplet_root="$CMAKE_BUILD_DIR/vcpkg_installed/$triplet"
    local vcpkg_lib_dir="$vcpkg_triplet_root/lib"
    local cubism_core_lib="$PROJECT_ROOT/cpp/plugins/cubism/Core/lib/ios/Release-iphoneos/libLive2DCubismCore.a"
    local godot_cpp_arch="arm64"
    local godot_cpp_lib=""
    local libs=(
        "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.a"
        "$CMAKE_BUILD_DIR/bridge/engine_api/libengine_api.a"
        "$CMAKE_BUILD_DIR/cpp/core/base/libcore_base_module.a"
        "$CMAKE_BUILD_DIR/cpp/core/environ/libcore_environ_module.a"
        "$CMAKE_BUILD_DIR/cpp/core/extension/libcore_extension_module.a"
        "$CMAKE_BUILD_DIR/cpp/core/movie/libcore_movie_module.a"
        "$CMAKE_BUILD_DIR/cpp/core/plugin/libcore_plugin_module.a"
        "$CMAKE_BUILD_DIR/cpp/core/sound/libcore_sound_module.a"
        "$CMAKE_BUILD_DIR/cpp/core/tjs2/libtjs2.a"
        "$CMAKE_BUILD_DIR/cpp/core/utils/libcore_utils_module.a"
        "$CMAKE_BUILD_DIR/cpp/core/visual/libcore_visual_module.a"
        "$CMAKE_BUILD_DIR/cpp/core/visual/simd/libtvpgl_simd.a"
        "$CMAKE_BUILD_DIR/cpp/plugins/libkrkr2plugin.a"
        "$CMAKE_BUILD_DIR/cpp/plugins/kagparserex/libkagparserex.a"
        "$CMAKE_BUILD_DIR/cpp/plugins/layerex_draw/liblayerExDraw.a"
        "$CMAKE_BUILD_DIR/cpp/plugins/motionplayer/libmotionplayer.a"
        "$CMAKE_BUILD_DIR/cpp/plugins/psbfile/libpsbfile.a"
        "$CMAKE_BUILD_DIR/cpp/plugins/psdfile/libpsdfile.a"
        "$CMAKE_BUILD_DIR/cpp/plugins/psdfile/psdparse/libpsdparse.a"
        "$CMAKE_BUILD_DIR/cpp/plugins/libCubismFramework.a"
        "$CMAKE_BUILD_DIR/cpp/external/libbpg/liblibbpg.a"
    )

    if [[ "$triplet" == "x64-ios-simulator" ]]; then
        godot_cpp_arch="x86_64"
        cubism_core_lib="$PROJECT_ROOT/cpp/plugins/cubism/Core/lib/ios/Release-iphonesimulator-x86_64/libLive2DCubismCore.a"
    elif [[ "$triplet" == "arm64-ios-simulator" ]]; then
        cubism_core_lib="$PROJECT_ROOT/cpp/plugins/cubism/Core/lib/ios/Release-iphonesimulator-arm64/libLive2DCubismCore.a"
    fi
    godot_cpp_lib="$(resolve_ios_godot_cpp_lib "$vcpkg_triplet_root" "$godot_cpp_arch" "$BUILD_TYPE_LOWER" || true)"
    if [[ ! -f "$godot_cpp_lib" ]]; then
        echo "Error: missing Godot C++ iOS archive for $BUILD_TYPE_LOWER build ($godot_cpp_arch)." >&2
        echo "       Expected an archive matching: $vcpkg_triplet_root/{lib,debug/lib}/libgodot-cpp.ios.*.$godot_cpp_arch.a" >&2
        exit 1
    fi
    libs=("$godot_cpp_lib" "${libs[@]}")
    libs+=("$cubism_core_lib")

    while IFS= read -r lib; do
        libs+=("$lib")
    done < <(find "$vcpkg_lib_dir" -maxdepth 1 -name 'lib*.a' \
        ! -name 'libgodot-cpp*.a' \
        ! -name 'libSDL2main.a' | sort)

    local existing_libs=()
    local lib
    for lib in "${libs[@]}"; do
        if [[ -f "$lib" ]]; then
            existing_libs+=("$lib")
        else
            echo "warning: skipping missing optional static library: $lib" >&2
        fi
    done

    local tmp
    tmp="$(mktemp /tmp/aetherkiri-ios-static.XXXXXX).a"
    libtool -static -o "$tmp" "${existing_libs[@]}"
    mv "$tmp" "$output"
}

stage_force_load_plugin_archives() {
    local destination="$1"
    local source
    mkdir -p "$destination"
    for source in "${FORCE_LOAD_PLUGIN_SOURCES[@]}"; do
        cp -f "$CMAKE_BUILD_DIR/$source" "$destination/" 2>/dev/null || true
    done
}

verify_exported_simulator_template_arch() {
    local export_root="$1"
    local arch="$2"
    local libgodot="$export_root/AetherKiri.xcframework/ios-arm64_x86_64-simulator/libgodot.a"
    local info

    if [[ ! -f "$libgodot" ]]; then
        echo "Error: exported Godot simulator template is missing: $libgodot" >&2
        exit 1
    fi

    info="$(lipo -archs "$libgodot" 2>/dev/null || true)"
    if [[ " $info " != *" $arch "* ]]; then
        echo "Error: Godot iOS simulator export template does not contain '$arch'." >&2
        echo "       $libgodot" >&2
        echo "       architectures: ${info:-unknown}" >&2
        echo "       Install or build a Godot export template with an $arch simulator slice, or use --simulator-arch=x86_64." >&2
        exit 1
    fi
}

stage_ios_runtime_fonts() {
    local export_root="$1"
    local app_source_dir="$export_root/AetherKiri"
    local font_dir="$app_source_dir/fonts"

    mkdir -p "$font_dir"
    if [[ -f "$RUNTIME_CJK_FONT_SOURCE" ]]; then
        cp -f "$RUNTIME_CJK_FONT_SOURCE" "$app_source_dir/default.otf"
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

patch_ios_runtime_font_resources() {
    local project_file="$1"
    if [[ ! -f "$project_file" ]]; then
        return
    fi
    if grep -Fq 'A3F001000000000000000002 /* default.otf */' "$project_file"; then
        return
    fi

    perl -0pi -e 's@(/\* Begin PBXBuildFile section \*/\n)@$1\t\tA3F001000000000000000001 /* default.otf in Resources */ = {isa = PBXBuildFile; fileRef = A3F001000000000000000002 /* default.otf */; };\n\t\tA3F001000000000000000003 /* fonts in Resources */ = {isa = PBXBuildFile; fileRef = A3F001000000000000000004 /* fonts */; };\n@' "$project_file"
    perl -0pi -e 's@(/\* Begin PBXFileReference section \*/\n)@$1\t\tA3F001000000000000000002 /* default.otf */ = {isa = PBXFileReference; lastKnownFileType = file; path = default.otf; sourceTree = "<group>"; };\n\t\tA3F001000000000000000004 /* fonts */ = {isa = PBXFileReference; lastKnownFileType = folder; path = fonts; sourceTree = "<group>"; };\n@' "$project_file"
    perl -0pi -e 's@(\t\tD0BCFE4118AEBDA2004A7AAE /\* AetherKiri \*/ = \{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = \(\n)@$1\t\t\t\tA3F001000000000000000002 /* default.otf */,\n\t\t\t\tA3F001000000000000000004 /* fonts */,\n@' "$project_file"
    perl -0pi -e 's@(\t\tD0BCFE3218AEBDA2004A7AAE /\* Resources \*/ = \{\n\t\t\tisa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = \(\n)@$1\t\t\t\tA3F001000000000000000001 /* default.otf in Resources */,\n\t\t\t\tA3F001000000000000000003 /* fonts in Resources */,\n@' "$project_file"
}

patch_ios_export_project() {
    local project_file="$1/project.pbxproj"
    local export_root
    export_root="$(dirname "$1")"
    local dummy_cpp="$export_root/AetherKiri/dummy.cpp"
    local info_plist="$export_root/AetherKiri/AetherKiri-Info.plist"
    local arch="$2"
    local export_build_type="$3"
    local flags
    flags='$(LD_CLASSIC_$(XCODE_VERSION_ACTUAL)) -Wl,-U,_aether_kiri_library_init'
    flags+=" -Wl,-force_load,AetherKiri/bin/ios/$export_build_type/$IOS_SDK_COMPAT_ARCHIVE"
    local archive
    for archive in "${FORCE_LOAD_PLUGIN_ARCHIVES[@]}"; do
        flags+=" -Wl,-force_load,AetherKiri/bin/ios/$export_build_type/$archive"
    done
    flags+=' -framework AudioToolbox -framework AVFoundation -framework CoreBluetooth -framework CoreHaptics -framework CoreMedia -framework CoreMotion -framework CoreVideo -framework GameController -framework VideoToolbox -framework CoreGraphics -framework QuartzCore -framework Metal -framework MetalKit -framework Security -framework SystemConfiguration -framework MobileCoreServices'

    if [[ -f "$project_file" ]]; then
        FLAGS="$flags" perl -0pi -e 's/OTHER_LDFLAGS = "[^"]*";/"OTHER_LDFLAGS = \"" . $ENV{FLAGS} . "\";"/eg' "$project_file"
        if [[ "$arch" == "x86_64" ]]; then
            perl -0pi -e 's/ARCHS = "arm64";/ARCHS = "x86_64";/g' "$project_file"
            perl -0pi -e 's/VALID_ARCHS = "arm64 x86_64";/VALID_ARCHS = "x86_64";/g' "$project_file"
        else
            perl -0pi -e 's/ARCHS = "x86_64";/ARCHS = "arm64";/g' "$project_file"
            perl -0pi -e 's/VALID_ARCHS = "x86_64";/VALID_ARCHS = "arm64";/g' "$project_file"
        fi
        stage_ios_runtime_fonts "$export_root"
        patch_ios_runtime_font_resources "$project_file"
    fi
    if [[ -f "$dummy_cpp" ]] && ! grep -Fq '__swift_FORCE_LOAD_$_swift_Builtin_float' "$dummy_cpp"; then
        cat >> "$dummy_cpp" <<'EOF'

extern "C" void aether_kiri_swift_builtin_float_force_load(void) __asm("__swift_FORCE_LOAD_$_swift_Builtin_float");
extern "C" void aether_kiri_swift_builtin_float_force_load(void) {}
EOF
    fi
    if [[ -f "$info_plist" ]]; then
        /usr/libexec/PlistBuddy -c 'Set :UIFileSharingEnabled true' "$info_plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy -c 'Add :UIFileSharingEnabled bool true' "$info_plist"
        /usr/libexec/PlistBuddy -c 'Set :LSSupportsOpeningDocumentsInPlace true' "$info_plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy -c 'Add :LSSupportsOpeningDocumentsInPlace bool true' "$info_plist"
    fi
}

with_ios_only_gdextension() {
    local gdextension_file="$GODOT_APP_DIR/aether_kiri.gdextension"
    local backup_file
    backup_file="$(mktemp /tmp/aetherkiri-gdextension.XXXXXX)"

    cp "$gdextension_file" "$backup_file"
    restore_gdextension() {
        trap - RETURN
        cp "$backup_file" "$gdextension_file"
        rm -f "$backup_file"
    }
    trap restore_gdextension RETURN

    awk '
        BEGIN { skip = 0 }
        /^\[dependencies\]/ { skip = 1 }
        /^\[/ && $0 != "[dependencies]" { skip = 0 }
        skip && /^macos\./ { while (getline line && line !~ /^}/) {} ; next }
        !skip || !/^macos\./ { print }
    ' "$backup_file" | grep -v '^macos\.' > "$gdextension_file"

    "$GODOT_BIN" --headless --path "$GODOT_APP_DIR" \
        "$EXPORT_MODE" "$EXPORT_PRESET" "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER/AetherKiri.xcodeproj"
}

echo "==> Building native engine and Godot extension"
cmake_config_args=(-D "CMAKE_MAKE_PROGRAM=$CMAKE_MAKE_PROGRAM")
if [[ "${SKIP_VCPKG_INSTALL:-}" == "1" ]]; then
    if [[ ! -d "$VCPKG_ROOT/installed/$VCPKG_TRIPLET_DIR" ]]; then
        echo "Error: SKIP_VCPKG_INSTALL=1 but prebuilt vcpkg triplet is missing: $VCPKG_ROOT/installed/$VCPKG_TRIPLET_DIR" >&2
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
cp -f "$CMAKE_BUILD_DIR/bridge/engine_api/libengine_api.a" "$GODOT_BIN_DIR/" 2>/dev/null || true
cp -f "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.a" "$GODOT_BIN_DIR/" 2>/dev/null || true
build_ios_sdk_compat_archive "$GODOT_BIN_DIR/$IOS_SDK_COMPAT_ARCHIVE" "$VCPKG_TRIPLET_DIR"
stage_force_load_plugin_archives "$GODOT_BIN_DIR"
if [[ -f "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.a" ]]; then
    combine_ios_static_extension "$GODOT_BIN_DIR/libaether_kiri_godot.a" "$VCPKG_TRIPLET_DIR"
fi
if [[ "$SIMULATOR" == true ]]; then
    GODOT_EXPORT_BIN_DIR="$GODOT_APP_DIR/bin/ios/$BUILD_TYPE_LOWER"
    mkdir -p "$GODOT_EXPORT_BIN_DIR"
    cp -f "$CMAKE_BUILD_DIR/bridge/engine_api/libengine_api.a" "$GODOT_EXPORT_BIN_DIR/" 2>/dev/null || true
    cp -f "$GODOT_BIN_DIR/libaether_kiri_godot.a" "$GODOT_EXPORT_BIN_DIR/" 2>/dev/null || true
    cp -f "$GODOT_BIN_DIR/$IOS_SDK_COMPAT_ARCHIVE" "$GODOT_EXPORT_BIN_DIR/" 2>/dev/null || true
    stage_force_load_plugin_archives "$GODOT_EXPORT_BIN_DIR"
fi

if [[ ! -x "$GODOT_BIN" ]]; then
    echo "Warning: Godot not found at $GODOT_BIN; native libraries were staged only." >&2
elif [[ ! -f "$GODOT_EXPORT_TEMPLATE" ]]; then
    echo "Warning: Godot iOS export template missing at $GODOT_EXPORT_TEMPLATE; native libraries were staged only." >&2
else
    echo "==> Exporting Godot iOS project"
    mkdir -p "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER"
    EXPORT_PRESET="iOS Debug"
    EXPORT_MODE="--export-debug"
    if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
        EXPORT_PRESET="iOS Release"
        EXPORT_MODE="--export-release"
    fi
    with_ios_only_gdextension
    if [[ "$SIMULATOR" == true ]]; then
        verify_exported_simulator_template_arch "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER" "$SIMULATOR_ARCH"
    fi
    stage_force_load_plugin_archives "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER/AetherKiri/bin/ios/$BUILD_TYPE_LOWER"
    cp -f "$GODOT_BIN_DIR/$IOS_SDK_COMPAT_ARCHIVE" "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER/AetherKiri/bin/ios/$BUILD_TYPE_LOWER/" 2>/dev/null || true
    PATCH_ARCH="arm64"
    if [[ "$SIMULATOR" == true ]]; then
        PATCH_ARCH="$SIMULATOR_ARCH"
    fi
    patch_ios_export_project "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER/AetherKiri.xcodeproj" "$PATCH_ARCH" "$BUILD_TYPE_LOWER"
fi

echo "iOS build output: $PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER"
