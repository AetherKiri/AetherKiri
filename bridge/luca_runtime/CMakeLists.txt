cmake_minimum_required(VERSION 3.28)

project(aether_luca_runtime LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BuildLucaRs)

set(LUCA_RS_ROOT "${AETHERKIRI_LUCA_DIR}")
set(LUCA_RS_MANIFEST "${LUCA_RS_ROOT}/crates/luca_runtime/Cargo.toml")
if(NOT EXISTS "${LUCA_RS_MANIFEST}")
    message(FATAL_ERROR
        "AetherLuca checkout is unavailable. Run "
        "`git submodule update --init packages/AetherLuca`.")
endif()

aetherkiri_add_luca_rs(luca_runtime_rs)
if(NOT luca_runtime_rs_FOUND)
    # Toolchain or platform mapping unavailable; keep the rest of the build
    # green without the Luca runtime. godot_extension and the test wiring key
    # off target existence, so nothing else needs to change.
    message(STATUS "Luca runtime: Rust library unavailable, bridge skipped.")
    return()
endif()

add_library(aether_luca_runtime STATIC
    src/luca_runtime_provider.cpp
)

target_compile_features(aether_luca_runtime PUBLIC cxx_std_17)
set_target_properties(aether_luca_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_include_directories(aether_luca_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/abi/include"
)

# The provider archive references engine_register_runtime_provider, which the
# linking host provides (engine_api in the Godot extension, or the
# interceptor compiled into the provider unit test). The bridge itself
# deliberately stays engine_api-free, matching the rfvp precedent, so it also
# builds in test-only configurations without the engine_api target.
target_link_libraries(aether_luca_runtime PUBLIC
    luca_runtime_rs
)
