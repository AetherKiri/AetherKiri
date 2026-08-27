#!/usr/bin/env bash
# Shared CMake feature forwarding for the platform build scripts.
#
# The platform presets intentionally choose conservative defaults for local
# builds. CI can opt into the complete compatibility profile by exporting
# these variables; keeping the conversion here makes sure every platform
# passes the same values to its fresh CMake configure.

AETHERKIRI_CMAKE_FEATURE_ARGS=()

_aetherkiri_append_bool_feature() {
    local variable="$1"
    local value="${!variable-}"

    [[ -z "$value" ]] && return 0
    case "$(printf '%s' "$value" | tr '[:upper:]' '[:lower:]')" in
        1|on|true|yes)
            value="ON"
            ;;
        0|off|false|no)
            value="OFF"
            ;;
        *)
            echo "[ERROR] ${variable} must be ON/OFF or true/false, got: ${value}" >&2
            return 1
            ;;
    esac

    AETHERKIRI_CMAKE_FEATURE_ARGS+=(
        -D
        "${variable}=${value}"
    )
}

# Keep this list explicit. Options that select an incompatible renderer or a
# platform-specific toolchain must not be enabled accidentally by a generic
# CI environment variable.
for _aetherkiri_feature in \
    AETHERKIRI_ENABLE_ONSCRIPTER \
    AETHER_USE_KRKRZ_OPTIONAL_PLUGINS \
    AETHER_BUILD_KRKRZ_CORE_PARITY; do
    if _aetherkiri_append_bool_feature "$_aetherkiri_feature"; then
        :
    else
        _aetherkiri_status=$?
        unset -f _aetherkiri_append_bool_feature
        unset _aetherkiri_feature
        return "${_aetherkiri_status}" 2>/dev/null || exit "${_aetherkiri_status}"
    fi
done
unset _aetherkiri_feature

unset -f _aetherkiri_append_bool_feature
