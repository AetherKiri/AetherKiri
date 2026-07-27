#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 4 ]]; then
    echo "Usage: $0 <opt> <plugin> <compiler> <compiler arguments...>" >&2
    exit 2
fi

opt="$1"
plugin="$2"
compiler="$3"
shift 3
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
if [[ ! -x "$opt" || ! -f "$plugin" ]]; then
    echo "Kagura FLA tools are unavailable: opt=$opt plugin=$plugin" >&2
    exit 1
fi

temporary_directory="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-fla.XXXXXX")"
trap 'rm -rf "$temporary_directory"' EXIT
input_bitcode="$temporary_directory/input.bc"
protected_ir="$temporary_directory/protected.ll"

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
    "$input_bitcode" -S -o "$protected_ir"
"$compiler" "${second_stage_args[@]}" -O0 -x ir -c "$protected_ir" -o "$object_file"

echo "[AetherKiri FLA] $normalized_source"
