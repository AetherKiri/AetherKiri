#!/usr/bin/env bash
set -euo pipefail

: "${AETHER_INTERNAL_DEPLOY_KEY:?Missing AetherInternal CI deploy key}"
: "${AETHER_SOFTPAL_DEPLOY_KEY:?Missing AetherSoftPal CI deploy key}"

key_dir="$(mktemp -d "${RUNNER_TEMP:-/tmp}/aether-submodules.XXXXXX")"
trap 'rm -rf "$key_dir"' EXIT
chmod 700 "$key_dir"
printf '%s\n' "$AETHER_INTERNAL_DEPLOY_KEY" > "$key_dir/internal"
printf '%s\n' "$AETHER_SOFTPAL_DEPLOY_KEY" > "$key_dir/softpal"
chmod 600 "$key_dir/internal" "$key_dir/softpal"

# GitHub associates each deploy key with one private repository. The existing
# key continues to fetch the other runtimes; SoftPal uses its own read-only key.
export GIT_SSH_COMMAND="ssh -i $key_dir/internal -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new"
git submodule update --init --recursive --depth 1 \
    packages/AetherInternal \
    packages/AetherKrkr \
    packages/AetherMinori \
    packages/AetherSiglus \
    packages/OnscripterYuri \
    packages/psdfile \
    packages/rfvp \
    packages/tjs2Decompiler

export GIT_SSH_COMMAND="ssh -i $key_dir/softpal -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new"
git submodule update --init --depth 1 packages/AetherSoftPal
