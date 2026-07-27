#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 5 ]]; then
    echo "Usage: $0 <llc> <opt> <plugin> <compiler> <compiler arguments...>" >&2
    exit 2
fi

llc="$1"
opt="$2"
plugin="$3"
compiler="$4"
shift 4
original_args=("$@")

source_file=""
object_file=""
for ((index = 0; index < ${#original_args[@]}; ++index)); do
    argument="${original_args[index]}"
    if [[ "$argument" == "-o" && $((index + 1)) -lt ${#original_args[@]} ]]; then
        object_file="${original_args[index + 1]}"
        ((++index))
    elif [[ "$argument" == *.c || "$argument" == *.cc || \
            "$argument" == *.cpp || "$argument" == *.cxx ]]; then
        source_file="$argument"
    fi
done

normalized_source="${source_file//\\//}"
if [[ "$normalized_source" != */packages/AetherInternal/* ]]; then
    exec "$compiler" "${original_args[@]}"
fi

if [[ -z "$source_file" || -z "$object_file" ]]; then
    echo "Kagura FLA launcher could not identify the Internal source or output" >&2
    exit 1
fi
if [[ ! -x "$llc" || ! -x "$opt" || ! -f "$plugin" ]]; then
    echo "Kagura FLA tools are unavailable: llc=$llc opt=$opt plugin=$plugin" >&2
    exit 1
fi

if [[ -n "${AETHERKIRI_FLA_MANIFEST:-}" ]]; then
    printf '%s\n' "$normalized_source" >> "$AETHERKIRI_FLA_MANIFEST"
fi

temporary_directory="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-fla.XXXXXX")"
trap 'rm -rf "$temporary_directory"' EXIT
input_bitcode="$temporary_directory/input.bc"
protected_bitcode="$temporary_directory/protected.bc"

first_stage_args=()
skip_next=0
for ((index = 0; index < ${#original_args[@]}; ++index)); do
    argument="${original_args[index]}"
    if ((skip_next)); then
        skip_next=0
        continue
    fi

    case "$argument" in
        -o)
            skip_next=1
            ;;
        *)
            first_stage_args+=("$argument")
            ;;
    esac

done

second_stage_args=()
skip_next=0
for ((index = 0; index < ${#original_args[@]}; ++index)); do
    argument="${original_args[index]}"
    if ((skip_next)); then
        skip_next=0
        continue
    fi

    case "$argument" in
        -o|-MF|-MT|-MQ|-MJ|-x|-include|-imacros)
            skip_next=1
            ;;
        -MD|-MMD|-MP|-MG|-c|-emit-llvm|-flto|-flto=*)
            ;;
        -O|-O0|-O1|-O2|-O3|-O4|-Os|-Oz|-Ofast|-Og|-O*)
            ;;
        "$source_file")
            ;;
        *)
            second_stage_args+=("$argument")
            ;;
    esac
done

"$compiler" "${first_stage_args[@]}" -emit-llvm -o "$input_bitcode"
"$opt" --load-pass-plugin="$plugin" \
    -passes='function(kagura-fla)' \
    -kagura-metrics \
    "$input_bitcode" -o "$protected_bitcode"

target_triple="aarch64-none-linux-android24"
for argument in "${original_args[@]}"; do
    if [[ "$argument" == --target=* ]]; then
        target_triple="${argument#--target=}"
        break
    fi
done
"$llc" -mtriple="$target_triple" -filetype=obj \
    -relocation-model=pic -function-sections -data-sections \
    "$protected_bitcode" -o "$object_file"

echo "[AetherKiri FLA] $normalized_source"
