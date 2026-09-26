# Tagged Releases

Maintainer guide for the `Release` workflow. For everyday builds and platform
launch flows see [development.md](development.md).

## Workflow

Pushing a SemVer tag such as `0.3.0` or `0.3.0-beta.1` starts the `Release`
workflow. The tag, without an optional leading `v`, becomes the version shown
inside Aether and the Android `versionName`. The numeric SemVer core becomes
the iOS and macOS marketing version, while the GitHub run number and attempt
produce a monotonically increasing Apple build number and Android
`versionCode`.

The Apple jobs use the GitHub-hosted `macos-latest` image. Tagged iOS builds
fail early unless the selected Xcode contains the iOS 26 SDK or newer. Regular
CI continues to produce an unsigned IPA for validation. Tagged releases require
App Store signing for both Apple platforms. Set `AETHERID` to the registered
Bundle ID `com.liuyu.aether.aether` and configure these repository Actions
secrets:

- `IOS_DISTRIBUTION_CERTIFICATE_BASE64`: base64-encoded Apple Distribution
  `.p12` certificate and private key.
- `IOS_DISTRIBUTION_CERTIFICATE_PASSWORD`: password for that `.p12`.
- `IOS_PROVISIONING_PROFILE_BASE64`: base64-encoded App Store provisioning
  profile for `com.liuyu.aether.aether` and team `3JL7FE9XQT`.
- `MACOS_INSTALLER_CERTIFICATE_BASE64`: base64-encoded Mac Installer
  Distribution `.p12` certificate and private key.
- `MACOS_INSTALLER_CERTIFICATE_PASSWORD`: password for the Mac installer
  `.p12`.
- `MACOS_PROVISIONING_PROFILE_BASE64`: base64-encoded Mac App Store
  provisioning profile for `com.liuyu.aether.aether` and team `3JL7FE9XQT`.
- `APP_STORE_CONNECT_API_KEY_ID`: App Store Connect API key ID.
- `APP_STORE_CONNECT_API_ISSUER_ID`: App Store Connect issuer ID.
- `APP_STORE_CONNECT_API_PRIVATE_KEY_BASE64`: base64-encoded App Store
  Connect `AuthKey_*.p8` file.

## Store Packaging

The macOS App Store package is Apple Silicon-only, enables App Sandbox, and
allows read-write access to files selected by the user. The Release workflow
validates and uploads the signed iOS IPA and macOS installer package to App
Store Connect before publishing the GitHub Release. Apple processes accepted
uploads asynchronously; selecting the processed builds and submitting them to
App Review remains a separate App Store Connect operation. Missing or partial
signing and API credentials fail the tagged release instead of silently
publishing an unsigned store artifact.
