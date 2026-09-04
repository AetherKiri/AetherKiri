#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
  echo "Usage: $0 /path/to/vcpkg" >&2
  exit 2
fi

vcpkg_exe="$1"
: "${GITHUB_REPOSITORY_OWNER:?GITHUB_REPOSITORY_OWNER is required}"
: "${GITHUB_ACTOR:?GITHUB_ACTOR is required}"
: "${GITHUB_ENV:?GITHUB_ENV is required}"
: "${VCPKG_PACKAGES_TOKEN:?VCPKG_PACKAGES_TOKEN is required}"

if [[ ! -x "$vcpkg_exe" ]]; then
  echo "vcpkg executable not found: $vcpkg_exe" >&2
  exit 1
fi
mono_bin="${MONO_BIN:-$(command -v mono || command -v mono.exe || true)}"
if command -v cygpath >/dev/null 2>&1 &&
   [[ "$mono_bin" == [A-Za-z]:\\* || "$mono_bin" == [A-Za-z]:/* ]]; then
  mono_bin="$(cygpath -u "$mono_bin")"
fi
if [[ -z "$mono_bin" || ( ! -f "$mono_bin" && ! -x "$mono_bin" ) ]]; then
  echo "mono is required by the vcpkg NuGet binary provider" >&2
  exit 1
fi

feed_url="https://nuget.pkg.github.com/${GITHUB_REPOSITORY_OWNER}/index.json"
source_name="AetherKiriGitHubPackages"
nuget_exe="$($vcpkg_exe fetch nuget | tail -n 1 | tr -d '\r')"

# Git Bash receives native Windows paths from vcpkg. Convert the NuGet path
# before testing or invoking it so the same provider setup works on both hosts.
if command -v cygpath >/dev/null 2>&1 &&
   [[ "$nuget_exe" == [A-Za-z]:\\* || "$nuget_exe" == [A-Za-z]:/* ]]; then
  nuget_exe="$(cygpath -u "$nuget_exe")"
fi

if [[ ! -f "$nuget_exe" ]]; then
  echo "vcpkg did not provide a NuGet executable: $nuget_exe" >&2
  exit 1
fi

# NuGet keeps credentials in the runner's temporary user profile. GitHub masks
# the token in logs, and the profile is discarded with the hosted runner.
"$mono_bin" "$nuget_exe" sources remove \
  -Name "$source_name" \
  -NonInteractive >/dev/null 2>&1 || true
"$mono_bin" "$nuget_exe" sources add \
  -Source "$feed_url" \
  -StorePasswordInClearText \
  -Name "$source_name" \
  -UserName "$GITHUB_ACTOR" \
  -Password "$VCPKG_PACKAGES_TOKEN" \
  -NonInteractive

cache_mode="read"
if [[ "${AETHERKIRI_VCPKG_CACHE_WRITE:-false}" == "true" ]]; then
  cache_mode="readwrite"
  "$mono_bin" "$nuget_exe" setapikey "$VCPKG_PACKAGES_TOKEN" \
    -Source "$feed_url" \
    -NonInteractive
fi

{
  echo "VCPKG_BINARY_SOURCES=clear;nuget,${feed_url},${cache_mode}"
  echo "VCPKG_NUGET_REPOSITORY=${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY}.git"
} >> "$GITHUB_ENV"

echo "Configured the vcpkg GitHub Packages cache in ${cache_mode} mode."
