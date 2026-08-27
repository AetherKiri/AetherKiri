#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SUBMODULE_PATH="${PROJECT_ROOT}/third_party/krkrz_dev"
MANIFEST_PATH="${PROJECT_ROOT}/runtime/kirikiri/manifests/plugins.toml"
REPOSITORY="${AETHERKIRI_KRKRZ_REPOSITORY:-wamsoft/krkrz_dev}"
REF="${AETHERKIRI_KRKRZ_REF:-master}"
URL="https://github.com/${REPOSITORY}.git"

if [[ ! -f "${PROJECT_ROOT}/.gitmodules" ]]; then
    echo "[ERROR] AetherKiri repository metadata is missing." >&2
    exit 1
fi
if [[ ! -f "$MANIFEST_PATH" ]]; then
    echo "[ERROR] krkrz_dev manifest is missing: ${MANIFEST_PATH}" >&2
    exit 1
fi

echo "==> Initializing krkrz_dev submodule (${REPOSITORY}@${REF})"
desired_revision="$(git -C "$PROJECT_ROOT" rev-parse HEAD:third_party/krkrz_dev)"
current_revision=""
if [[ -d "$SUBMODULE_PATH" ]]; then
    current_revision="$(git -C "$SUBMODULE_PATH" rev-parse HEAD 2>/dev/null || true)"
fi
if [[ "$current_revision" == "$desired_revision" ]]; then
    echo "krkrz_dev checkout already matches the parent gitlink"
else
    GIT_TERMINAL_PROMPT=0 git -C "$PROJECT_ROOT" \
        -c "submodule.third_party/krkrz_dev.url=${URL}" \
        submodule update --init --recursive --depth 1 third_party/krkrz_dev
fi

if [[ ! -d "$SUBMODULE_PATH" ]]; then
    echo "[ERROR] krkrz_dev checkout was not created: ${SUBMODULE_PATH}" >&2
    exit 1
fi

pinned_revision="$(git -C "$SUBMODULE_PATH" rev-parse HEAD)"
manifest_revision="$(sed -n 's/^upstream_revision[[:space:]]*=[[:space:]]*"\([0-9a-fA-F]*\)".*$/\1/p' "$MANIFEST_PATH" | head -1)"
if [[ -z "$manifest_revision" || "$manifest_revision" != "$pinned_revision" ]]; then
    echo "[ERROR] krkrz_dev manifest pin does not match the checked-out gitlink." >&2
    echo "        checkout: ${pinned_revision}" >&2
    echo "        manifest: ${manifest_revision:-missing}" >&2
    exit 1
fi

latest_revision="$(GIT_TERMINAL_PROMPT=0 git \
    -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=30 \
    ls-remote "$URL" "refs/heads/${REF}" | awk 'NR == 1 { print $1 }')"
if [[ -z "$latest_revision" ]]; then
    echo "[ERROR] Could not resolve ${URL} refs/heads/${REF}." >&2
    exit 1
fi

if [[ "$pinned_revision" != "$latest_revision" ]]; then
    cat >&2 <<EOF
[ERROR] The checked-in krkrz_dev revision is stale.
        parent gitlink: ${pinned_revision}
        ${REPOSITORY}/${REF}: ${latest_revision}

Update the parent gitlink and runtime/kirikiri/manifests/plugins.toml in a
reviewed change before accepting a CI build. CI never substitutes an
unreviewed upstream checkout into a reproducible product artifact.
EOF
    exit 1
fi

# The first update already initializes nested public submodules at the pinned
# commit. No second recursive fetch is needed; avoiding it keeps each CI job
# bounded even when the runner has a warm submodule cache.

echo "krkrz_dev revision: ${pinned_revision} (latest ${REPOSITORY}/${REF})"
if [[ -n "${GITHUB_ENV:-}" ]]; then
    echo "AETHERKIRI_KRKRZ_REVISION=${pinned_revision}" >> "$GITHUB_ENV"
fi
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    {
        echo "### krkrz_dev source"
        echo
        echo "- Repository: \`${REPOSITORY}\`"
        echo "- Ref: \`${REF}\`"
        echo "- Reviewed/latest revision: \`${pinned_revision}\`"
        echo "- Nested public submodules: initialized recursively"
    } >> "$GITHUB_STEP_SUMMARY"
fi
