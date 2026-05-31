cmake_minimum_required(VERSION 3.28)

find_program(CCACHE_PROGRAM ccache)

if (CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Ccache found: ${CCACHE_PROGRAM}")
endif ()

add_compile_definitions(
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Debug>:_DEBUG>   # 对于 MSVC 兼容性
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# Detect Android build: Gradle may set CMAKE_SYSTEM_NAME=Android or VCPKG_TARGET_ANDROID=ON
# before project() is called; the ANDROID variable is only set after project().
if(VCPKG_TARGET_ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(VCPKG_TARGET_ANDROID ON)
    # Gradle passes ANDROID_NDK as a CMake -D variable, propagate it to env
    if(DEFINED ANDROID_NDK AND NOT DEFINED ENV{ANDROID_NDK_HOME})
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
    elseif(DEFINED CMAKE_ANDROID_NDK AND NOT DEFINED ENV{ANDROID_NDK_HOME})
        set(ENV{ANDROID_NDK_HOME} "${CMAKE_ANDROID_NDK}")
    elseif(DEFINED ENV{ANDROID_NDK})
        set(ENV{ANDROID_NDK_HOME} "$ENV{ANDROID_NDK}")
    endif()
    # If Gradle already set CMAKE_TOOLCHAIN_FILE to vcpkg and VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    # to the Android NDK toolchain, we only need to set VCPKG_TARGET_TRIPLET here.
    # Otherwise, fall back to the vcpkg_android.cmake helper.
    if(DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
        # Gradle already configured the toolchain chain; just set the triplet
        if(DEFINED VCPKG_ROOT AND NOT DEFINED ENV{VCPKG_ROOT})
            set(ENV{VCPKG_ROOT} "${VCPKG_ROOT}")
        endif()
        if(ANDROID_ABI MATCHES "arm64-v8a")
            set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
        elseif(ANDROID_ABI MATCHES "armeabi-v7a")
            set(VCPKG_TARGET_TRIPLET "arm-android" CACHE STRING "" FORCE)
        elseif(ANDROID_ABI MATCHES "x86_64")
            set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
        elseif(ANDROID_ABI MATCHES "x86")
            set(VCPKG_TARGET_TRIPLET "x86-android" CACHE STRING "" FORCE)
        endif()
        message(STATUS "Android build with Gradle-managed toolchain: triplet=${VCPKG_TARGET_TRIPLET}")
    else()
        include(cmake/vcpkg_android.cmake)
    endif()
else()
    set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()

project(krkr2)

if(ANDROID)
    add_compile_options(-Wno-inconsistent-missing-override)
endif()

option(ENABLE_TESTS "enable tests execute build(exclude android)" ON)
option(BUILD_TOOLS "build tools execute build(exclude android ios)" ON)
option(BUILD_ENGINE_API "build engine_api bridge shared library" ON)
option(BUILD_GODOT_EXTENSION "build Godot GDExtension host library" ON)
option(BUILD_GPU_BRIDGE "build legacy external GPU bridge renderer" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KRKR2CORE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/core)
set(KRKR2PLUGIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/plugins)

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc>" "/MP" "/utf-8")
    add_link_options("/ignore:4099" "/INCREMENTAL" "/DEBUG:FASTLINK")
    # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Godot is the product app shell on the wired platforms. This native build
# produces engine libraries and the GDExtension host, not standalone launchers.

# external lib
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cpp/external)

# build library
add_subdirectory(${KRKR2CORE_PATH})
add_subdirectory(${KRKR2PLUGIN_PATH})
if(BUILD_ENGINE_API)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/engine_api)
endif()
if(BUILD_GODOT_EXTENSION)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/godot_extension)
endif()

if(ENABLE_TESTS AND NOT (IOS OR ANDROID))
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

if(BUILD_TOOLS AND NOT (IOS OR ANDROID))
    add_subdirectory(tools)
endif()
