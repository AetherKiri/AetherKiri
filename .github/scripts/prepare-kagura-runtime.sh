#!/usr/bin/env bash
set -euo pipefail

readonly KAGURA_REPOSITORY="https://github.com/ykus4/kagura.git"
readonly KAGURA_COMMIT="08a424d8c0a43b4a9ead4a137958daf7b7c1a5d2"

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <work-directory>" >&2
    exit 2
fi

work_directory="$1"
source_directory="${work_directory}/source"
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

if [[ -n "${GITHUB_ENV:-}" ]]; then
    {
        echo "AETHERKIRI_ENABLE_RUNTIME_PROTECTION=1"
        echo "AETHERKIRI_KAGURA_SOURCE_DIR=$source_directory"
    } >> "$GITHUB_ENV"
fi

echo "Kagura runtime sources prepared at $source_directory ($KAGURA_COMMIT)"
