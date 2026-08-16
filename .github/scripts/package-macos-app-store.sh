#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <Aether.app> <output-directory>" >&2
    exit 2
fi

source_app="$1"
output_dir="$2"
bundle_id="${AETHER_APP_BUNDLE_ID:-com.liuyu.aether.aether}"
team_id="${AETHER_APPLE_TEAM_ID:-3JL7FE9XQT}"
target_arch="${AETHERKIRI_MACOS_ARCH:-x86_64}"
marketing_version="${AETHER_APPLE_VERSION:?AETHER_APPLE_VERSION is required}"
build_number="${AETHER_BUILD_NUMBER:?AETHER_BUILD_NUMBER is required}"
application_certificate_base64="${IOS_DISTRIBUTION_CERTIFICATE_BASE64:?IOS_DISTRIBUTION_CERTIFICATE_BASE64 is required}"
application_certificate_password="${IOS_DISTRIBUTION_CERTIFICATE_PASSWORD:?IOS_DISTRIBUTION_CERTIFICATE_PASSWORD is required}"
installer_certificate_base64="${MACOS_INSTALLER_CERTIFICATE_BASE64:?MACOS_INSTALLER_CERTIFICATE_BASE64 is required}"
installer_certificate_password="${MACOS_INSTALLER_CERTIFICATE_PASSWORD:?MACOS_INSTALLER_CERTIFICATE_PASSWORD is required}"
profile_base64="${MACOS_PROVISIONING_PROFILE_BASE64:?MACOS_PROVISIONING_PROFILE_BASE64 is required}"

if [[ "$target_arch" != "arm64" && "$target_arch" != "x86_64" ]]; then
    echo "Unsupported macOS App Store architecture: $target_arch" >&2
    exit 1
fi

if [[ ! -d "$source_app" ]]; then
    echo "macOS app bundle not found: $source_app" >&2
    exit 1
fi

work_dir="$(mktemp -d "${RUNNER_TEMP:-/tmp}/aether-macos-signing.XXXXXX")"
keychain_path="$work_dir/aether-signing.keychain-db"
application_certificate_path="$work_dir/apple-distribution.p12"
installer_certificate_path="$work_dir/mac-installer-distribution.p12"
profile_path="$work_dir/app-store.provisionprofile"
profile_plist="$work_dir/profile.plist"
entitlements_path="$work_dir/Aether.entitlements"
signed_entitlements_path="$work_dir/signed-entitlements.plist"
app_store_app="$work_dir/Aether.app"
keychain_password="$(openssl rand -hex 24)"
original_keychains=()

while IFS= read -r keychain; do
    keychain="${keychain//\"/}"
    keychain="${keychain#"${keychain%%[![:space:]]*}"}"
    if [[ -n "$keychain" ]]; then
        original_keychains+=("$keychain")
    fi
done < <(security list-keychains -d user)

cleanup() {
    if ((${#original_keychains[@]})); then
        security list-keychains -d user -s "${original_keychains[@]}" >/dev/null 2>&1 || true
    fi
    security delete-keychain "$keychain_path" >/dev/null 2>&1 || true
    rm -rf "$work_dir"
}
trap cleanup EXIT

printf '%s' "$application_certificate_base64" | /usr/bin/base64 -D > "$application_certificate_path"
printf '%s' "$installer_certificate_base64" | /usr/bin/base64 -D > "$installer_certificate_path"
printf '%s' "$profile_base64" | /usr/bin/base64 -D > "$profile_path"

security create-keychain -p "$keychain_password" "$keychain_path"
security set-keychain-settings -lut 21600 "$keychain_path"
security unlock-keychain -p "$keychain_password" "$keychain_path"
security import "$application_certificate_path" \
    -k "$keychain_path" \
    -P "$application_certificate_password" \
    -A \
    -t cert \
    -f pkcs12
security import "$installer_certificate_path" \
    -k "$keychain_path" \
    -P "$installer_certificate_password" \
    -A \
    -t cert \
    -f pkcs12
security set-key-partition-list \
    -S apple-tool:,apple:,codesign: \
    -s \
    -k "$keychain_password" \
    "$keychain_path"
security list-keychains -d user -s "$keychain_path" "${original_keychains[@]}"

application_identity="$(
    security find-identity -v -p codesigning "$keychain_path" |
        sed -nE 's/.*"(Apple Distribution:[^"]+)".*/\1/p' |
        head -n 1
)"
if [[ -z "$application_identity" ]]; then
    application_identity="$(
        security find-identity -v -p codesigning "$keychain_path" |
            sed -nE 's/.*"((Mac App Distribution|3rd Party Mac Developer Application):[^"]+)".*/\1/p' |
            head -n 1
    )"
fi
installer_identity="$(
    security find-identity -v -p basic "$keychain_path" |
        sed -nE 's/.*"((Mac Installer Distribution|3rd Party Mac Developer Installer):[^"]+)".*/\1/p' |
        head -n 1
)"
if [[ -z "$application_identity" ]]; then
    echo "Apple Distribution or Mac App Distribution identity was not imported." >&2
    security find-identity -v "$keychain_path" >&2 || true
    exit 1
fi
if [[ -z "$installer_identity" ]]; then
    echo "Mac Installer Distribution identity was not imported." >&2
    security find-identity -v "$keychain_path" >&2 || true
    exit 1
fi

security cms -D -i "$profile_path" > "$profile_plist"
profile_team_id="$(/usr/libexec/PlistBuddy -c 'Print :TeamIdentifier:0' "$profile_plist")"
profile_app_identifier="$(
    /usr/libexec/PlistBuddy -c 'Print :Entitlements:com.apple.application-identifier' "$profile_plist" 2>/dev/null ||
        /usr/libexec/PlistBuddy -c 'Print :Entitlements:application-identifier' "$profile_plist"
)"
profile_get_task_allow="$(
    /usr/libexec/PlistBuddy -c 'Print :Entitlements:get-task-allow' "$profile_plist" 2>/dev/null ||
        printf 'false'
)"
if [[ "$profile_team_id" != "$team_id" ]]; then
    echo "macOS profile team $profile_team_id does not match expected team $team_id." >&2
    exit 1
fi
if [[ "$profile_app_identifier" != "$team_id.$bundle_id" ]]; then
    echo "macOS profile targets $profile_app_identifier, expected $team_id.$bundle_id." >&2
    exit 1
fi
if [[ "$profile_get_task_allow" == "true" || "$profile_get_task_allow" == "1" ]]; then
    echo "The macOS App Store profile unexpectedly enables get-task-allow." >&2
    exit 1
fi

actual_bundle_id="$(plutil -extract CFBundleIdentifier raw "$source_app/Contents/Info.plist")"
actual_marketing_version="$(plutil -extract CFBundleShortVersionString raw "$source_app/Contents/Info.plist")"
actual_build_number="$(plutil -extract CFBundleVersion raw "$source_app/Contents/Info.plist")"
if [[ "$actual_bundle_id" != "$bundle_id" ||
      "$actual_marketing_version" != "$marketing_version" ||
      "$actual_build_number" != "$build_number" ]]; then
    echo "macOS app metadata does not match the requested release." >&2
    exit 1
fi

ditto "$source_app" "$app_store_app"
cp "$profile_path" "$app_store_app/Contents/embedded.provisionprofile"

# The Apple release declares only encryption that is exempt from export
# documentation. Set that policy on the exact bundle that is signed and placed
# in the installer, even if an older Godot export omitted it.
/usr/libexec/PlistBuddy \
    -c 'Set :ITSAppUsesNonExemptEncryption false' \
    "$app_store_app/Contents/Info.plist" 2>/dev/null ||
    /usr/libexec/PlistBuddy \
        -c 'Add :ITSAppUsesNonExemptEncryption bool false' \
        "$app_store_app/Contents/Info.plist"
uses_non_exempt_encryption="$(
    plutil -extract ITSAppUsesNonExemptEncryption raw \
        "$app_store_app/Contents/Info.plist"
)"
if [[ "$uses_non_exempt_encryption" != "false" ]]; then
    echo "The macOS App Store bundle declares non-exempt encryption." >&2
    exit 1
fi

# Godot currently exports only LSMinimumSystemVersionByArchitecture. App Store
# Connect also requires the scalar LSMinimumSystemVersion key. Keep it aligned
# with the selected runtime architecture before sealing the bundle.
minimum_system_version="$(
    plutil -extract "LSMinimumSystemVersionByArchitecture.$target_arch" raw \
        "$app_store_app/Contents/Info.plist"
)"
if [[ -z "$minimum_system_version" ]]; then
    echo "The macOS application is missing its $target_arch deployment target." >&2
    exit 1
fi
/usr/libexec/PlistBuddy \
    -c "Set :LSMinimumSystemVersion $minimum_system_version" \
    "$app_store_app/Contents/Info.plist" 2>/dev/null ||
    /usr/libexec/PlistBuddy \
        -c "Add :LSMinimumSystemVersion string $minimum_system_version" \
        "$app_store_app/Contents/Info.plist"
scalar_system_version="$(
    plutil -extract LSMinimumSystemVersion raw \
        "$app_store_app/Contents/Info.plist"
)"
if [[ "$scalar_system_version" != "$minimum_system_version" ]]; then
    echo "The macOS scalar deployment target does not match the $target_arch target." >&2
    exit 1
fi

# App Store Connect rejects macOS bundles that contain quarantine metadata.
# Clear extended attributes before signing so the sealed bundle cannot retain
# download-origin metadata from export templates or cached dependencies.
xattr -cr "$app_store_app"
if xattr -r "$app_store_app" 2>/dev/null | grep -q 'com\.apple\.quarantine$'; then
    echo "The macOS application still contains quarantine metadata." >&2
    exit 1
fi

plutil -create xml1 "$entitlements_path"
/usr/libexec/PlistBuddy -c 'Add :com.apple.security.app-sandbox bool true' "$entitlements_path"
/usr/libexec/PlistBuddy -c 'Add :com.apple.security.files.user-selected.read-write bool true' "$entitlements_path"
/usr/libexec/PlistBuddy -c 'Add :com.apple.security.files.bookmarks.app-scope bool true' "$entitlements_path"
/usr/libexec/PlistBuddy -c "Add :com.apple.application-identifier string $team_id.$bundle_id" "$entitlements_path"
/usr/libexec/PlistBuddy -c "Add :com.apple.developer.team-identifier string $team_id" "$entitlements_path"

while IFS= read -r -d '' runtime_binary; do
    if file -b "$runtime_binary" | grep -q 'Mach-O'; then
        runtime_architectures="$(lipo -archs "$runtime_binary")"
        if [[ " $runtime_architectures " != *" $target_arch "* ]]; then
            echo "Mac App Store runtime is missing $target_arch: $runtime_binary ($runtime_architectures)" >&2
            exit 1
        fi
        codesign --force \
            --sign "$application_identity" \
            --keychain "$keychain_path" \
            --timestamp \
            "$runtime_binary"
    fi
done < <(find "$app_store_app/Contents/Frameworks" -type f -print0)

codesign --force \
    --sign "$application_identity" \
    --keychain "$keychain_path" \
    --timestamp \
    --entitlements "$entitlements_path" \
    "$app_store_app"
codesign --verify --deep --strict --verbose=2 "$app_store_app"
codesign -d --entitlements :- "$app_store_app" > "$signed_entitlements_path" 2>/dev/null
if [[ "$(/usr/libexec/PlistBuddy -c 'Print :com.apple.security.app-sandbox' "$signed_entitlements_path")" != "true" ]]; then
    echo "Signed macOS application is missing App Sandbox." >&2
    exit 1
fi

mkdir -p "$output_dir"
output_pkg="$output_dir/Aether-Mac-App-Store.pkg"
productbuild \
    --component "$app_store_app" /Applications \
    --sign "$installer_identity" \
    --keychain "$keychain_path" \
    --timestamp \
    "$output_pkg"
pkgutil --check-signature "$output_pkg"

echo "Mac App Store package created: $output_pkg"
