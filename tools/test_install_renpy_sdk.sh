#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
installer="$repo_root/tools/install_renpy_sdk.sh"
[[ -x "$installer" ]] || { echo "installer is not executable: $installer" >&2; exit 1; }
bash -n "$installer"

linux_config="$($installer --platform linux --arch x86_64 --print-config)"
grep -Fx 'RENPY_VERSION=8.5.3' <<<"$linux_config"
grep -Fx 'RENPY_ARCHIVE=renpy-8.5.3-sdk.tar.bz2' <<<"$linux_config"
grep -Fx 'RENPY_SHA256=eb0a9be7f0fb13632fe25ceade9a8bed5a1b4d6b6e83bd19eeeb29e1a1bb4a45' <<<"$linux_config"
grep -Fx 'RENPY_URL=https://www.renpy.org/dl/8.5.3/renpy-8.5.3-sdk.tar.bz2' <<<"$linux_config"

mac_config="$($installer --platform darwin --arch arm64 --print-config)"
grep -Fx 'RENPY_ARCHIVE=renpy-8.5.3-sdkarm.tar.bz2' <<<"$mac_config"
grep -Fx 'RENPY_SHA256=0579782517f203ba3535dcc2dab54e34bfc318f2f2a7510a5130b6f809f901f6' <<<"$mac_config"
mac_x86_config="$($installer --platform darwin --arch x86_64 --print-config)"
grep -Fx 'RENPY_ARCHIVE=renpy-8.5.3-sdk.tar.bz2' <<<"$mac_x86_config"
grep -Fx 'RENPY_SHA256=eb0a9be7f0fb13632fe25ceade9a8bed5a1b4d6b6e83bd19eeeb29e1a1bb4a45' <<<"$mac_x86_config"

if "$installer" --platform windows --arch x86_64 --print-config >/dev/null 2>&1; then
    echo "installer accepted unsupported Windows platform" >&2
    exit 1
fi
if "$installer" --platform linux --arch arm64 --print-config >/dev/null 2>&1; then
    echo "installer accepted unsupported Linux architecture" >&2
    exit 1
fi
printf 'Ren\x27Py SDK installer validation ok\n'
