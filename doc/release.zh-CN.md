# 标签发布（Tagged Releases）

`Release` workflow 的维护者指南。日常构建与各平台启动流程见
[development.zh-CN.md](development.zh-CN.md)。

## 流程

推送形如 `0.3.0` 或 `0.3.0-beta.1` 的 SemVer 标签即可触发 `Release` workflow。
去掉可选前导 `v` 的标签即 Aether 应用内显示的版本号，同时作为 Android
`versionName`。SemVer 数字核心作为 iOS/macOS 营销版本号；GitHub run 号与
attempt 组合出单调递增的 Apple build number 和 Android `versionCode`。

Apple 相关 job 运行在 GitHub 托管的 `macos-latest` 镜像上。打了标签的 iOS
构建要求所选 Xcode 携带 iOS 26 SDK 或更新版本，否则会提前失败。常规 CI
仍产出未签名 IPA 用于验证。标签发布要求两个 Apple 平台都完成 App Store
签名。将 `AETHERID` 设为已注册的 Bundle ID `com.liuyu.aether.aether`，并配置
以下仓库 Actions secrets：

- `IOS_DISTRIBUTION_CERTIFICATE_BASE64`：base64 编码的 Apple Distribution
  `.p12` 证书与私钥。
- `IOS_DISTRIBUTION_CERTIFICATE_PASSWORD`：该 `.p12` 的密码。
- `IOS_PROVISIONING_PROFILE_BASE64`：base64 编码的 App Store provisioning
  profile，对应 `com.liuyu.aether.aether` 与 team `3JL7FE9XQT`。
- `MACOS_INSTALLER_CERTIFICATE_BASE64`：base64 编码的 Mac Installer
  Distribution `.p12` 证书与私钥。
- `MACOS_INSTALLER_CERTIFICATE_PASSWORD`：Mac installer `.p12` 的密码。
- `MACOS_PROVISIONING_PROFILE_BASE64`：base64 编码的 Mac App Store
  provisioning profile，对应 `com.liuyu.aether.aether` 与 team `3JL7FE9XQT`。
- `APP_STORE_CONNECT_API_KEY_ID`：App Store Connect API key ID。
- `APP_STORE_CONNECT_API_ISSUER_ID`：App Store Connect issuer ID。
- `APP_STORE_CONNECT_API_PRIVATE_KEY_BASE64`：base64 编码的 App Store
  Connect `AuthKey_*.p8` 文件。

## 商店打包

macOS App Store 包仅支持 Apple Silicon，启用 App Sandbox，并允许读写用户
选中的文件。Release workflow 会校验签名后的 iOS IPA 与 macOS installer 包，
先上传到 App Store Connect，再发布 GitHub Release。Apple 对已接收的上传做
异步处理；挑选处理完成的构建并提交 App Review 是独立的 App Store Connect
操作。签名或 API 凭据缺失、不完整时，标签发布会直接失败，而不是静默发布
未签名的商店产物。
