#!/usr/bin/env bash
set -euo pipefail

# Strip distributable runtime binaries while retaining their required exports.
if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <macho|macho-executable|elf|wasm> <binary> [binary ...]" >&2
    exit 2
fi

FORMAT="$1"
shift

require_regular_file() {
    local path="$1"
    if [[ ! -f "$path" ]]; then
        echo "Error: runtime binary not found: $path" >&2
        exit 1
    fi
}

strip_macho() {
    local strip_mode="$1"
    shift
    local strip_tool="${AETHERKIRI_STRIP_TOOL:-}"
    if [[ -z "$strip_tool" ]]; then
        strip_tool="$(xcrun --find strip)"
    fi

    local binary
    for binary in "$@"; do
        require_regular_file "$binary"
        if [[ "$strip_mode" == "executable" ]]; then
            "$strip_tool" "$binary"
        else
            "$strip_tool" -S -x "$binary"
        fi
        if xcrun nm -m "$binary" 2>/dev/null | grep -q 'non-external'; then
            echo "Error: local Mach-O symbols remain after stripping: $binary" >&2
            exit 1
        fi
    done
}

find_elf_readelf() {
    local strip_tool="$1"
    local sibling
    sibling="$(dirname "$strip_tool")/llvm-readelf"
    if [[ -x "$sibling" ]]; then
        printf '%s\n' "$sibling"
    elif command -v llvm-readelf >/dev/null 2>&1; then
        command -v llvm-readelf
    elif command -v readelf >/dev/null 2>&1; then
        command -v readelf
    fi
}

strip_elf() {
    local strip_tool="${AETHERKIRI_STRIP_TOOL:-}"
    if [[ -z "$strip_tool" ]]; then
        strip_tool="$(command -v llvm-strip || command -v strip || true)"
    fi
    if [[ -z "$strip_tool" ]]; then
        echo "Error: no ELF strip tool was found." >&2
        exit 1
    fi

    local readelf_tool="${AETHERKIRI_READELF_TOOL:-}"
    if [[ -z "$readelf_tool" ]]; then
        readelf_tool="$(find_elf_readelf "$strip_tool")"
    fi

    local binary
    for binary in "$@"; do
        require_regular_file "$binary"
        "$strip_tool" --strip-all "$binary"
        if [[ -n "$readelf_tool" ]] &&
           "$readelf_tool" -S "$binary" 2>/dev/null |
               grep -Eq '\.(symtab|debug_[[:alnum:]_.-]+)'; then
            echo "Error: ELF symbol/debug sections remain after stripping: $binary" >&2
            exit 1
        fi
    done
}

find_wasm_readobj() {
    local emstrip_tool="$1"
    local resolved
    local candidate

    if command -v llvm-readobj >/dev/null 2>&1; then
        command -v llvm-readobj
        return
    fi
    resolved="$(perl -MCwd=abs_path -e 'print abs_path(shift)' "$emstrip_tool" 2>/dev/null || true)"
    for candidate in \
        "$(dirname "$resolved")/llvm/bin/llvm-readobj" \
        "$(dirname "$resolved")/../llvm/bin/llvm-readobj"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return
        fi
    done
}

strip_wasm() {
    local strip_tool="${AETHERKIRI_STRIP_TOOL:-$(command -v emstrip || true)}"
    if [[ -z "$strip_tool" ]]; then
        echo "Error: emstrip was not found." >&2
        exit 1
    fi
    local readobj_tool="${AETHERKIRI_WASM_READOBJ_TOOL:-}"
    if [[ -z "$readobj_tool" ]]; then
        readobj_tool="$(find_wasm_readobj "$strip_tool")"
    fi

    local binary
    local magic
    for binary in "$@"; do
        require_regular_file "$binary"
        "$strip_tool" --strip-debug "$binary"
        magic="$(od -An -tx1 -N4 "$binary" | tr -d ' \n')"
        if [[ "$magic" != "0061736d" ]]; then
            echo "Error: stripped file is not a WebAssembly module: $binary" >&2
            exit 1
        fi
        if [[ -n "$readobj_tool" ]] &&
           "$readobj_tool" --sections "$binary" 2>/dev/null |
               grep -Eq 'Name: (name|\.debug_[[:alnum:]_.-]+)$'; then
            echo "Error: WebAssembly name/debug sections remain after stripping: $binary" >&2
            exit 1
        fi
    done
}

case "$FORMAT" in
    macho) strip_macho library "$@" ;;
    macho-executable) strip_macho executable "$@" ;;
    elf) strip_elf "$@" ;;
    wasm) strip_wasm "$@" ;;
    *)
        echo "Error: unsupported runtime binary format '$FORMAT'." >&2
        exit 2
        ;;
esac
