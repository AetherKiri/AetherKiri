#!/usr/bin/env bash
set -euo pipefail

readonly KAGURA_REPOSITORY="https://github.com/ykus4/kagura.git"
readonly KAGURA_COMMIT="08a424d8c0a43b4a9ead4a137958daf7b7c1a5d2"

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <llvm-prefix> <work-directory>" >&2
    exit 2
fi

llvm_prefix="$1"
work_directory="$2"
llvm_config="${llvm_prefix}/bin/llvm-config"

if [[ ! -x "$llvm_config" ]]; then
    echo "LLVM configuration tool is missing: $llvm_config" >&2
    exit 1
fi

llvm_version="$($llvm_config --version)"
llvm_major="${llvm_version%%.*}"
if (( llvm_major < 17 || llvm_major > 22 )); then
    echo "Kagura requires LLVM 17 through 22; found $llvm_version" >&2
    exit 1
fi

source_directory="${work_directory}/source"
build_directory="${work_directory}/build"
mkdir -p "$work_directory"

if [[ ! -d "${source_directory}/.git" ]]; then
    git clone --filter=blob:none --no-checkout \
        "$KAGURA_REPOSITORY" "$source_directory"
fi

git -C "$source_directory" fetch --depth=1 origin "$KAGURA_COMMIT"
git -C "$source_directory" checkout --detach --force "$KAGURA_COMMIT"
actual_commit="$(git -C "$source_directory" rev-parse HEAD)"
if [[ "$actual_commit" != "$KAGURA_COMMIT" ]]; then
    echo "Kagura checkout mismatch: $actual_commit" >&2
    exit 1
fi

cmake -S "$source_directory" -B "$build_directory" -G Ninja \
    -D CMAKE_BUILD_TYPE=Release \
    -D LLVM_DIR="$($llvm_config --cmakedir)" \
    -D KAGURA_BUILD_TESTS=OFF \
    -D KAGURA_BITCODE_TOOLS=OFF \
    -D KAGURA_USE_CACHE=ON
cmake --build "$build_directory" --target KaguraObfuscator --parallel 4

plugin_path="$(find "$build_directory" -type f \
    \( -name 'libKaguraObfuscator.dylib' \
       -o -name 'KaguraObfuscator.dylib' \
       -o -name 'libKaguraObfuscator.so' \
       -o -name 'KaguraObfuscator.so' \
       -o -name 'KaguraObfuscator.dll' \) \
    -print -quit)"
if [[ -z "$plugin_path" || ! -f "$plugin_path" ]]; then
    echo "Kagura pass plugin was not produced" >&2
    exit 1
fi

if [[ -n "${GITHUB_ENV:-}" ]]; then
    {
        echo "AETHERKIRI_ENABLE_CODE_OBFUSCATION=1"
        echo "AETHERKIRI_OBFUSCATOR_PLUGIN=$plugin_path"
        echo "CC=${llvm_prefix}/bin/clang"
        echo "CXX=${llvm_prefix}/bin/clang++"
        echo "DYLD_LIBRARY_PATH=${llvm_prefix}/lib:${DYLD_LIBRARY_PATH:-}"
    } >> "$GITHUB_ENV"
fi
if [[ -n "${GITHUB_PATH:-}" ]]; then
    echo "${llvm_prefix}/bin" >> "$GITHUB_PATH"
fi

echo "Kagura $KAGURA_COMMIT built with LLVM $llvm_version: $plugin_path"
