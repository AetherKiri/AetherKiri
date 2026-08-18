cmake_minimum_required(VERSION 3.19)

find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(AETHERKIRI_RUST_TARGET "" CACHE STRING
    "Rust target triple for the Minori runtime (auto-detected when empty)")
set(MINORI_RUST_TARGET "${AETHERKIRI_RUST_TARGET}")
if(NOT MINORI_RUST_TARGET)
    if(ANDROID)
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            set(MINORI_RUST_TARGET "aarch64-linux-android")
        elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
            set(MINORI_RUST_TARGET "armv7-linux-androideabi")
        elseif(ANDROID_ABI STREQUAL "x86_64")
            set(MINORI_RUST_TARGET "x86_64-linux-android")
        else()
            message(FATAL_ERROR
                "Minori has no Rust target mapping for Android ABI ${ANDROID_ABI}")
        endif()
    elseif(EMSCRIPTEN)
        set(MINORI_RUST_TARGET "wasm32-unknown-emscripten")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
            if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
                set(MINORI_RUST_TARGET "aarch64-apple-ios-sim")
            else()
                set(MINORI_RUST_TARGET "x86_64-apple-ios")
            endif()
        else()
            set(MINORI_RUST_TARGET "aarch64-apple-ios")
        endif()
    elseif(APPLE)
        if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
            set(MINORI_RUST_TARGET "aarch64-apple-darwin")
        else()
            set(MINORI_RUST_TARGET "x86_64-apple-darwin")
        endif()
    endif()
endif()

set(MINORI_CARGO_TARGET_ARGS "")
set(MINORI_CARGO_LIBRARY_SUBDIR "")
if(MINORI_RUST_TARGET)
    list(APPEND MINORI_CARGO_TARGET_ARGS --target "${MINORI_RUST_TARGET}")
    set(MINORI_CARGO_LIBRARY_SUBDIR "${MINORI_RUST_TARGET}/")
    message(STATUS "Minori Rust target: ${MINORI_RUST_TARGET}")
else()
    message(STATUS "Minori Rust target: Cargo host default")
endif()

set(MINORI_CARGO_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml")
set(MINORI_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo-target")
set(MINORI_CARGO_ENV
    "CARGO_TARGET_DIR=${MINORI_CARGO_TARGET_DIR}")
set(MINORI_CARGO_UNSTABLE_ARGS "")
if(EMSCRIPTEN)
    # The Godot Web side module uses pthreads/shared memory, so every linked
    # wasm object, including Rust's standard library, must opt into the
    # matching memory and atomic instructions.
    list(APPEND MINORI_CARGO_ENV
        "RUSTFLAGS=-C panic=abort -C target-feature=+atomics,+bulk-memory,+mutable-globals")
    list(APPEND MINORI_CARGO_UNSTABLE_ARGS
        -Z build-std=std,panic_abort)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(MINORI_CARGO_PROFILE "release")
    set(MINORI_CARGO_PROFILE_ARG "--release")
else()
    set(MINORI_CARGO_PROFILE "debug")
    set(MINORI_CARGO_PROFILE_ARG "")
endif()
set(MINORI_STATIC_LIBRARY
    "${MINORI_CARGO_TARGET_DIR}/${MINORI_CARGO_LIBRARY_SUBDIR}${MINORI_CARGO_PROFILE}/libaether_minori_runtime.a")

add_custom_command(
    OUTPUT "${MINORI_STATIC_LIBRARY}"
    COMMAND ${CMAKE_COMMAND} -E env
        ${MINORI_CARGO_ENV}
        "${CARGO_EXECUTABLE}" build
        --manifest-path "${MINORI_CARGO_MANIFEST}"
        --lib ${MINORI_CARGO_PROFILE_ARG} ${MINORI_CARGO_TARGET_ARGS}
        ${MINORI_CARGO_UNSTABLE_ARGS}
    DEPENDS
        "${MINORI_CARGO_MANIFEST}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/command.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/profile.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/provider.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scene.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/script.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vfs.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm.rs"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)

add_custom_target(aether_minori_runtime_build DEPENDS "${MINORI_STATIC_LIBRARY}")
add_library(aether_minori_runtime STATIC IMPORTED GLOBAL)
set_target_properties(aether_minori_runtime PROPERTIES
    IMPORTED_LOCATION "${MINORI_STATIC_LIBRARY}")
add_dependencies(aether_minori_runtime aether_minori_runtime_build)
