#!/usr/bin/env bash
#
# build_ios.sh — One-step build script for krkr2 iOS (Flutter)
#
# Usage:
#   ./build_ios.sh [debug|release]
#
# Output: Flutter iOS .app (unsigned, for development/archive)
#
# This script will:
#   1. Build the C++ engine static library (libengine_api.a) via CMake/Ninja
#   2. Copy the static library to the Flutter plugin's Libs directory
#   3. Build the Flutter iOS application
#

set -euo pipefail

# ============================================================
# Configuration
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="debug"
SIMULATOR=false

for arg in "$@"; do
    case "$arg" in
        debug|release|Debug|Release)
            BUILD_TYPE="$arg"
            ;;
        --simulator)
            SIMULATOR=true
            ;;
        *)
            echo "[WARN] Unknown iOS build argument ignored: $arg"
            ;;
    esac
done

BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"

if [[ "$BUILD_TYPE_LOWER" != "debug" && "$BUILD_TYPE_LOWER" != "release" ]]; then
    echo "Error: Invalid build type '$BUILD_TYPE'. Use 'debug' or 'release'."
    exit 1
fi

# Capitalize for CMake preset names
BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"

if [[ "$SIMULATOR" == true ]]; then
    if [[ "$BUILD_TYPE_LOWER" != "debug" ]]; then
        echo "Error: iOS simulator builds are only configured for debug."
        exit 1
    fi
    CMAKE_CONFIG_PRESET="iOS Simulator Debug Config"
    CMAKE_BUILD_PRESET="iOS Simulator Debug Build"
    CMAKE_BUILD_DIR="$PROJECT_ROOT/out/ios-simulator/debug"
    VCPKG_TRIPLET="arm64-ios-simulator"
else
    CMAKE_CONFIG_PRESET="iOS ${BUILD_TYPE_CAP} Config"
    CMAKE_BUILD_PRESET="iOS ${BUILD_TYPE_CAP} Build"
    CMAKE_BUILD_DIR="$PROJECT_ROOT/out/ios/$BUILD_TYPE_LOWER"
    VCPKG_TRIPLET="arm64-ios"
fi

if [[ -d "$PROJECT_ROOT/.devtools/flutter" ]]; then
    FLUTTER_SDK="$PROJECT_ROOT/.devtools/flutter"
    FLUTTER_BIN="$FLUTTER_SDK/bin/flutter"
elif command -v flutter >/dev/null 2>&1; then
    FLUTTER_BIN="$(command -v flutter)"
    if command -v realpath >/dev/null 2>&1; then
        RESOLVED_BIN="$(realpath "$FLUTTER_BIN")"
    elif command -v python3 >/dev/null 2>&1; then
        RESOLVED_BIN="$(python3 -c "import os, sys; print(os.path.realpath(sys.argv[1]))" "$FLUTTER_BIN")"
    else
        RESOLVED_BIN="$FLUTTER_BIN"
    fi
    FLUTTER_SDK="$(dirname "$(dirname "$RESOLVED_BIN")")"
else
    echo "Error: Flutter SDK not found in .devtools and not in PATH."
    exit 1
fi

FLUTTER_APP_DIR="$PROJECT_ROOT/apps/flutter_app"

if [[ -d "$PROJECT_ROOT/.devtools/vcpkg/.git" ]]; then
    VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
elif [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/.vcpkg-root" ]]; then
    # Keep the environment VCPKG_ROOT if set
    :
else
    echo "[INFO] vcpkg not found. Automatically setting up vcpkg in .devtools/vcpkg..."
    mkdir -p "$PROJECT_ROOT/.devtools"
    git clone https://github.com/microsoft/vcpkg.git "$PROJECT_ROOT/.devtools/vcpkg"
    (cd "$PROJECT_ROOT/.devtools/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
    VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
fi

PARALLEL_JOBS="${JOBS:-8}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# ============================================================
# Helper functions
# ============================================================
log_step() {
    echo ""
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}========================================${NC}"
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_command() {
    if ! command -v "$1" &>/dev/null; then
        log_error "'$1' is not installed or not in PATH."
        exit 1
    fi
}

# ============================================================
# Pre-flight checks
# ============================================================
log_step "Pre-flight checks"

check_command cmake
check_command ninja

if [[ ! -x "$FLUTTER_BIN" ]]; then
    log_error "Flutter SDK not found at: $FLUTTER_SDK"
    log_info "Expected path: $FLUTTER_BIN"
    exit 1
fi

if [[ ! -d "$VCPKG_ROOT" ]]; then
    log_error "vcpkg not found at: $VCPKG_ROOT"
    exit 1
fi

log_info "Build type:    $BUILD_TYPE_CAP"
log_info "Simulator:     $SIMULATOR"
log_info "Project root:  $PROJECT_ROOT"
log_info "CMake preset:  $CMAKE_BUILD_PRESET"
log_info "Flutter SDK:   $FLUTTER_SDK"
log_info "Parallel jobs: $PARALLEL_JOBS"

# ============================================================
# Step 1: Build C++ engine (static library for iOS)
# ============================================================
log_step "Step 1/3: Building C++ engine (static library)"

export VCPKG_ROOT

# Configure (only if needed)
if [[ ! -f "$CMAKE_BUILD_DIR/build.ninja" ]]; then
    log_info "Running CMake configure..."
    cmake --preset "$CMAKE_CONFIG_PRESET"
else
    log_info "Build directory already configured, skipping configure."
fi

# Build
log_info "Building C++ engine with $PARALLEL_JOBS parallel jobs..."
cmake --build --preset "$CMAKE_BUILD_PRESET" -- -j"$PARALLEL_JOBS"

# Verify the static library was built
ENGINE_LIB="$CMAKE_BUILD_DIR/bridge/engine_api/libengine_api.a"
if [[ ! -f "$ENGINE_LIB" ]]; then
    log_error "Engine static library not found at: $ENGINE_LIB"
    log_error "C++ engine build may have failed."
    exit 1
fi

log_info "Engine static library built: $ENGINE_LIB"

# ============================================================
# Step 2: Merge all static libraries and copy to Flutter plugin
# ============================================================
log_step "Step 2/3: Merging static libraries and copying to Flutter plugin"

PLUGIN_LIBS_DIR="$PROJECT_ROOT/bridge/flutter_engine_bridge/ios/Libs"
mkdir -p "$PLUGIN_LIBS_DIR"

# --- Collect project static libraries ---
# Include project and plugin static libraries. libengine_api.a references plugin
# anchor symbols, but the plugin archives are built as separate static libs.
PROJECT_LIBS=()
while IFS= read -r -d '' lib; do
    PROJECT_LIBS+=("$lib")
done < <(find "$CMAKE_BUILD_DIR" -name "*.a" -not -path "*/vcpkg_installed/*" -print0)

# Merge project libs into libengine_project.a
# For psdparse: only extract its unique .o files (not already in libengine_api.a)
MERGE_TMPDIR=$(mktemp -d)
trap "rm -rf '$MERGE_TMPDIR'" EXIT

# First, merge all project libs normally
libtool -static -o "$MERGE_TMPDIR/libengine_project_base.a" "${PROJECT_LIBS[@]}"

# Build a set of .o names already in the project library
ar t "$MERGE_TMPDIR/libengine_project_base.a" | sort -u > "$MERGE_TMPDIR/project_objs.txt"

# Extract only unique .o files from psdparse (and other deep plugin sub-libs)
PSDPARSE_UNIQUE_OBJS=()
while IFS= read -r -d '' sublib; do
    sublibname="$(basename "$sublib" .a)"
    extractdir="$MERGE_TMPDIR/extract_${sublibname}"
    mkdir -p "$extractdir"
    (cd "$extractdir" && ar x "$sublib")
    for obj in "$extractdir"/*.o; do
        [[ -f "$obj" ]] || continue
        objname="$(basename "$obj")"
        if ! grep -qx "$objname" "$MERGE_TMPDIR/project_objs.txt"; then
            PSDPARSE_UNIQUE_OBJS+=("$obj")
        fi
    done
done < <(find "$CMAKE_BUILD_DIR/cpp/plugins" -mindepth 3 -name "*.a" -print0 2>/dev/null)

if [[ ${#PSDPARSE_UNIQUE_OBJS[@]} -gt 0 ]]; then
    log_info "  Plugin sub-lib unique .o files: ${#PSDPARSE_UNIQUE_OBJS[@]}"
    # Merge project base + unique plugin objects
    libtool -static -o "$PLUGIN_LIBS_DIR/libengine_project.a" \
        "$MERGE_TMPDIR/libengine_project_base.a" "${PSDPARSE_UNIQUE_OBJS[@]}"
else
    cp "$MERGE_TMPDIR/libengine_project_base.a" "$PLUGIN_LIBS_DIR/libengine_project.a"
fi
log_info "Project library -> $PLUGIN_LIBS_DIR/libengine_project.a"

# --- Collect vcpkg third-party static libraries ---
# Exclude redundant subset libraries that cause duplicate symbols:
#   libpng.a             (duplicate of libpng16.a)
#   libjpeg.a            (subset of libturbojpeg.a which has extra tj* APIs)
#   libwebpdecoder.a     (subset of libwebp.a)
#   libharfbuzz-subset.a (overlaps heavily with libharfbuzz.a)
VCPKG_EXCLUDE_LIBS="libpng.a|libjpeg.a|libwebpdecoder.a|libharfbuzz-subset.a"

VCPKG_LIB_DIR="$CMAKE_BUILD_DIR/vcpkg_installed/$VCPKG_TRIPLET/lib"
VCPKG_LIBS=()
if [[ -d "$VCPKG_LIB_DIR" ]]; then
    while IFS= read -r -d '' lib; do
        libbase="$(basename "$lib")"
        if echo "$libbase" | grep -qE "^($VCPKG_EXCLUDE_LIBS)$"; then
            log_info "  Skipping redundant: $libbase"
            continue
        fi
        VCPKG_LIBS+=("$lib")
    done < <(find "$VCPKG_LIB_DIR" -maxdepth 1 -name "*.a" -print0)
fi

log_info "  Vcpkg libs (after exclusion): ${#VCPKG_LIBS[@]}"

# Merge vcpkg libs into libengine_vendors.a
libtool -static -o "$PLUGIN_LIBS_DIR/libengine_vendors.a" "${VCPKG_LIBS[@]}"

log_info "Vendors library -> $PLUGIN_LIBS_DIR/libengine_vendors.a"

# ============================================================
# Step 3: Build Flutter iOS app
# ============================================================
log_step "Step 3/3: Building Flutter iOS app"

export PATH="$FLUTTER_SDK/bin:$PATH"

log_info "Running flutter pub get..."
(cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" pub get)

FLUTTER_BUILD_MODE="$BUILD_TYPE_LOWER"
log_info "Building Flutter iOS app ($FLUTTER_BUILD_MODE)..."

if [[ "$SIMULATOR" == true ]]; then
    (cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" build ios --simulator --debug)
elif [[ "$FLUTTER_BUILD_MODE" == "release" ]]; then
    (cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" build ios --release --no-codesign)
else
    (cd "$FLUTTER_APP_DIR" && "$FLUTTER_BIN" build ios --debug --no-codesign)
fi

# ============================================================
# Done
# ============================================================
log_step "Build complete!"

log_info "Engine static library: $PLUGIN_LIBS_DIR/libengine_api.a"
log_info "Flutter iOS build output: $FLUTTER_APP_DIR/build/ios/"
echo ""
log_info "To deploy to a device, open in Xcode:"
echo "  open \"$FLUTTER_APP_DIR/ios/Runner.xcworkspace\""
echo ""
log_info "Or use flutter run for development:"
echo "  cd \"$FLUTTER_APP_DIR\" && flutter run -d <device_id>"
echo ""
