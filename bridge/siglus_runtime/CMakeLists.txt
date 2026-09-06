cmake_minimum_required(VERSION 3.28)

project(aether_siglus_runtime LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BuildSiglusRs)

set(SIGLUS_RS_ROOT
    "${CMAKE_SOURCE_DIR}/packages/siglus_rs")
if(NOT EXISTS "${SIGLUS_RS_ROOT}/crates/siglus_scene_vm/Cargo.toml")
    message(FATAL_ERROR
        "siglus_rs submodule is unavailable. Run "
        "`git submodule update --init packages/siglus_rs`.")
endif()

aetherkiri_add_siglus_rs(siglus_scene_vm_rs)
if(NOT siglus_scene_vm_rs_FOUND)
    # Toolchain or platform mapping unavailable; keep the rest of the build
    # green without the Siglus runtime. godot_extension keys off target
    # existence, so nothing else needs to change.
    return()
endif()

find_package(OpenAL CONFIG REQUIRED)

if(APPLE)
    # vcpkg's static OpenAL target exports clang-driver flags. They are either
    # redundant (-pthread) or rejected when CMake uses swiftc as the final link
    # driver for the mixed Swift/C++ Godot extension. BuildSiglusRs.cmake lists
    # the required Apple frameworks as normal CMake link items instead.
    get_target_property(AETHERKIRI_OPENAL_LINK_LIBRARIES
        OpenAL::OpenAL INTERFACE_LINK_LIBRARIES)
    if(AETHERKIRI_OPENAL_LINK_LIBRARIES)
        list(FILTER AETHERKIRI_OPENAL_LINK_LIBRARIES
            EXCLUDE REGEX "pthread|^-Wl,-framework")
        set_property(TARGET OpenAL::OpenAL PROPERTY
            INTERFACE_LINK_LIBRARIES "${AETHERKIRI_OPENAL_LINK_LIBRARIES}")
    endif()
endif()

add_library(aether_siglus_runtime STATIC
    src/siglus_runtime_provider.cpp
    src/siglus_audio_output.cpp
)

target_compile_features(aether_siglus_runtime PUBLIC cxx_std_17)
set_target_properties(aether_siglus_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_include_directories(aether_siglus_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Note: the stub provider intentionally holds no Rust symbols yet, so the
# archive is produced but nothing pulls it into the final shared object until
# Phase 3 wires real call sites. The cargo build still runs on every build,
# which keeps cross-compilation breakage visible early.
target_link_libraries(aether_siglus_runtime PUBLIC
    engine_api
    siglus_scene_vm_rs
)

# Audio PCM consumer: drains the Rust kira mixer FIFO into an OpenAL stream.
# This is the same cross-platform output path the embedded Artemis runtime
# uses (OpenAL Soft on iOS/Android/desktop), which avoids owning a second
# SDL audio consumer alongside the krkrsdl3/ONS hosts.
target_link_libraries(aether_siglus_runtime PRIVATE
    OpenAL::OpenAL
)
