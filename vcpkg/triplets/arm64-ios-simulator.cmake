set(VCPKG_BUILD_TYPE release)

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_SYSROOT iphonesimulator)
set(VCPKG_OSX_DEPLOYMENT_TARGET 15.0)
set(VCPKG_OSX_ARCHITECTURES arm64)

# Fix autotools cross-compilation detection for iOS Simulator.
# Apple Silicon hosts and arm64 simulator targets otherwise both appear as
# aarch64-apple-darwin, so configure tries to run target binaries. Keep the
# host triplet Darwin-shaped because older config.sub files reject ios-simulator.
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-apple-darwin;--build=x86_64-apple-darwin")
