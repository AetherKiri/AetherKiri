#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <ios|macos> <ipa-or-pkg>" >&2
    exit 2
fi

platform="$1"
package_path="$2"
api_key_id="${APP_STORE_CONNECT_API_KEY_ID:?APP_STORE_CONNECT_API_KEY_ID is required}"
api_issuer_id="${APP_STORE_CONNECT_API_ISSUER_ID:?APP_STORE_CONNECT_API_ISSUER_ID is required}"
api_private_key_base64="${APP_STORE_CONNECT_API_PRIVATE_KEY_BASE64:?APP_STORE_CONNECT_API_PRIVATE_KEY_BASE64 is required}"

case "$platform" in
    ios)
        expected_extension="ipa"
        ;;
    macos)
        expected_extension="pkg"
        ;;
    *)
        echo "Unsupported App Store platform: $platform" >&2
        exit 2
        ;;
esac

if [[ ! -f "$package_path" ]]; then
    echo "App Store package not found: $package_path" >&2
    exit 1
fi
if [[ "${package_path##*.}" != "$expected_extension" ]]; then
    echo "$platform upload requires a .$expected_extension file: $package_path" >&2
    exit 1
fi
if [[ ! "$api_key_id" =~ ^[A-Z0-9]+$ ]]; then
    echo "APP_STORE_CONNECT_API_KEY_ID has an invalid format." >&2
    exit 1
fi
if [[ ! "$api_issuer_id" =~ ^[0-9A-Fa-f-]+$ ]]; then
    echo "APP_STORE_CONNECT_API_ISSUER_ID has an invalid format." >&2
    exit 1
fi

private_key_dir="$(mktemp -d "${RUNNER_TEMP:-/tmp}/aether-app-store-key.XXXXXX")"
private_key_path="$private_key_dir/AuthKey_${api_key_id}.p8"
cleanup() {
    rm -rf "$private_key_dir"
}
trap cleanup EXIT

printf '%s' "$api_private_key_base64" | /usr/bin/base64 -D > "$private_key_path"
chmod 600 "$private_key_path"
if ! grep -q 'BEGIN PRIVATE KEY' "$private_key_path"; then
    echo "Decoded App Store Connect API key is not a .p8 private key." >&2
    exit 1
fi
export API_PRIVATE_KEYS_DIR="$private_key_dir"

echo "Validating $platform package with App Store Connect..."
xcrun altool \
    --validate-app \
    --file "$package_path" \
    --type "$platform" \
    --apiKey "$api_key_id" \
    --apiIssuer "$api_issuer_id" \
    --output-format json

echo "Uploading $platform package to App Store Connect..."
xcrun altool \
    --upload-app \
    --file "$package_path" \
    --type "$platform" \
    --apiKey "$api_key_id" \
    --apiIssuer "$api_issuer_id" \
    --output-format json \
    --show-progress

echo "$platform package upload was accepted by App Store Connect."
