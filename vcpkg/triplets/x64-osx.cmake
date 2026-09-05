set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.0)

# Meson-based ports (e.g. pango) require an Objective-C compiler on darwin.
# When cross-building x64-osx on arm64 runners, Meson gets its compilers from
# the generated cross file, which only includes languages detected by the
# vcpkg get_cmake_vars probe. Enable OBJC there so `objc` lands in the cross
# file instead of failing with "'objc' compiler binary not defined".
set(VCPKG_ENABLE_OBJC ON)
