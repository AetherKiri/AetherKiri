#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${VCPKG_ROOT:-}" ]]; then
    echo "VCPKG_ROOT is required" >&2
    exit 1
fi
if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then
    echo "vcpkg executable not found at ${VCPKG_ROOT}/vcpkg" >&2
    exit 1
fi

feed_url="${VCPKG_NUGET_FEED_URL:-https://nuget.pkg.github.com/${GITHUB_REPOSITORY_OWNER}/index.json}"
feed_name="${VCPKG_NUGET_FEED_NAME:-GitHubPackages}"
username="${VCPKG_NUGET_USERNAME:-${GITHUB_REPOSITORY_OWNER}}"
token="${VCPKG_NUGET_TOKEN:-}"
cache_mode="${VCPKG_BINARY_CACHE_MODE:-readwrite}"

if [[ -z "$token" ]]; then
    echo "VCPKG_NUGET_TOKEN is required" >&2
    exit 1
fi

nuget_exe="$("${VCPKG_ROOT}/vcpkg" fetch nuget | tail -n 1)"
if [[ ! -f "$nuget_exe" ]]; then
    echo "NuGet executable not found: $nuget_exe" >&2
    exit 1
fi

run_nuget() {
    if [[ "$RUNNER_OS" == "Windows" ]]; then
        "$nuget_exe" "$@"
    else
        mono "$nuget_exe" "$@"
    fi
}

run_nuget sources remove -Name "$feed_name" >/dev/null 2>&1 || true
run_nuget sources add \
    -Source "$feed_url" \
    -StorePasswordInClearText \
    -Name "$feed_name" \
    -UserName "$username" \
    -Password "$token"
run_nuget setapikey "$token" -Source "$feed_url"

echo "VCPKG_BINARY_SOURCES=clear;nuget,$feed_name,$cache_mode" >> "$GITHUB_ENV"
echo "Configured vcpkg binary cache source: $feed_name ($cache_mode)"
