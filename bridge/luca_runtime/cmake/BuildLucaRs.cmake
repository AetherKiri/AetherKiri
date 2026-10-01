# BuildLucaRs.cmake — builds the packages/AetherLuca workspace into a Rust
# static library consumable by the AetherKiri luca bridge.
#
# The public entry point is aetherkiri_add_luca_rs(<imported-target>), which:
#   * locates a Rust toolchain (cargo/rustc; cmake/RustToolchain.cmake has
#     already unified the configure-time cache),
#   * maps the active CMake platform to a Rust target triple,
#   * schedules an always-run `cargo rustc --crate-type staticlib` build of
#     the `luca_runtime` crate (cargo itself stays incremental),
#   * creates a GLOBAL IMPORTED STATIC target pointing at the archive so plain
#     target_link_libraries() propagation carries it through intermediate
#     static libraries up to the final GDExtension shared object, and exports
#     the crate's include/ directory (luca_ffi.h) as an interface include.
#
# This is the overlay-free sibling of BuildSiglusRs.cmake: AetherLuca is a
# first-party engine repository, so cargo builds directly from the checkout
# and only writes into the build tree's CARGO_TARGET_DIR.
#
# The function sets <target>_FOUND in the parent scope on success. Callers on
# platforms that are not wired yet should treat a FALSE result as "skip the
# Luca runtime", not as a configuration failure.

function(aetherkiri_deduce_luca_rust_target out_triple)
    if(WEB OR EMSCRIPTEN)
        # The wasm host path is designed in a later phase.
        set(${out_triple} "" PARENT_SCOPE)
        return()
    endif()
    if(ANDROID OR IOS)
        # Mobile targets are wired with the presentation phase (Phase 4).
        set(${out_triple} "" PARENT_SCOPE)
        return()
    endif()
    if(APPLE)
        if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
            set(${out_triple} "x86_64-apple-darwin" PARENT_SCOPE)
            return()
        elseif(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
            set(${out_triple} "aarch64-apple-darwin" PARENT_SCOPE)
            return()
        endif()
    endif()
    # Native desktop builds follow the toolchain's host triple.
    execute_process(
        COMMAND "${RUSTC_EXECUTABLE}" -vV
        OUTPUT_VARIABLE rustc_version_output
        ERROR_QUIET RESULT_VARIABLE rustc_result)
    if(NOT rustc_result EQUAL 0)
        set(${out_triple} "" PARENT_SCOPE)
        return()
    endif()
    string(REGEX MATCH "host: ([A-Za-z0-9_\\-]+)" _match "${rustc_version_output}")
    if(CMAKE_MATCH_1 STREQUAL "")
        set(${out_triple} "" PARENT_SCOPE)
    else()
        set(${out_triple} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
endfunction()

function(aetherkiri_add_luca_rs imported_target)
    find_program(CARGO_EXECUTABLE cargo)
    find_program(RUSTC_EXECUTABLE rustc)
    if(NOT CARGO_EXECUTABLE OR NOT RUSTC_EXECUTABLE)
        message(STATUS
            "Luca runtime disabled: Rust toolchain (cargo/rustc) not found. "
            "Install https://rustup.rs to enable it.")
        set(${imported_target}_FOUND FALSE PARENT_SCOPE)
        return()
    endif()

    # Prefer the toolchain directory rustup resolves so installed std
    # libraries stay visible even when a non-rustup rust shadows the proxies
    # on PATH (same reasoning as BuildSiglusRs.cmake).
    set(LUCA_TOOLCHAIN_BIN_DIR "")
    find_program(RUSTUP_EXECUTABLE rustup)
    if(RUSTUP_EXECUTABLE)
        execute_process(
            COMMAND "${RUSTUP_EXECUTABLE}" which rustc
            OUTPUT_VARIABLE resolved_rustc
            ERROR_QUIET RESULT_VARIABLE rustup_which_result)
        string(STRIP "${resolved_rustc}" resolved_rustc)
        if(rustup_which_result EQUAL 0 AND EXISTS "${resolved_rustc}")
            get_filename_component(LUCA_TOOLCHAIN_BIN_DIR "${resolved_rustc}" DIRECTORY)
            if(EXISTS "${LUCA_TOOLCHAIN_BIN_DIR}/cargo")
                set(CARGO_EXECUTABLE "${LUCA_TOOLCHAIN_BIN_DIR}/cargo")
            endif()
            if(EXISTS "${LUCA_TOOLCHAIN_BIN_DIR}/rustc")
                set(RUSTC_EXECUTABLE "${LUCA_TOOLCHAIN_BIN_DIR}/rustc")
            endif()
        endif()
    endif()

    aetherkiri_deduce_luca_rust_target(rust_triple)
    if(rust_triple STREQUAL "")
        message(STATUS
            "Luca runtime disabled: no Rust target mapping for this "
            "platform configuration yet.")
        set(${imported_target}_FOUND FALSE PARENT_SCOPE)
        return()
    endif()

    # Host build type and Rust profile stay in lockstep, with the same
    # opt-in profiling override as the siglus runtime.
    set(rust_profile_override "$ENV{AETHERKIRI_LUCA_RS_PROFILE}")
    if(DEFINED AETHERKIRI_LUCA_RS_PROFILE AND
            NOT "${AETHERKIRI_LUCA_RS_PROFILE}" STREQUAL "")
        set(rust_profile_override "${AETHERKIRI_LUCA_RS_PROFILE}")
    endif()
    string(TOLOWER "${rust_profile_override}" rust_profile_override)
    if(NOT "${rust_profile_override}" STREQUAL "" AND
            NOT "${rust_profile_override}" STREQUAL "debug" AND
            NOT "${rust_profile_override}" STREQUAL "release")
        message(FATAL_ERROR
            "AETHERKIRI_LUCA_RS_PROFILE must be debug or release, got "
            "${rust_profile_override}")
    endif()
    if("${rust_profile_override}" STREQUAL "release")
        set(rust_profile "release")
        set(rust_profile_flag "--release")
    elseif("${rust_profile_override}" STREQUAL "debug")
        set(rust_profile "debug")
        set(rust_profile_flag "")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(rust_profile "debug")
        set(rust_profile_flag "")
    else()
        set(rust_profile "release")
        set(rust_profile_flag "--release")
    endif()

    set(LUCA_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/luca-rs-target")
    set(LUCA_STATIC_LIB
        "${LUCA_CARGO_TARGET_DIR}/${rust_triple}/${rust_profile}/libluca_runtime.a")

    set(luca_path_leading "")
    if(LUCA_TOOLCHAIN_BIN_DIR)
        string(APPEND luca_path_leading "${LUCA_TOOLCHAIN_BIN_DIR}:")
    endif()
    set(LUCA_PATH_PREFIX "")
    if(CMAKE_HOST_UNIX)
        list(PREPEND LUCA_PATH_PREFIX "PATH=${luca_path_leading}$ENV{PATH}")
    else()
        list(PREPEND LUCA_PATH_PREFIX "PATH=${luca_path_leading}\;$ENV{PATH}")
    endif()

    # Rust code must use the same macOS minimum as the embedding application.
    if(APPLE AND NOT IOS AND CMAKE_OSX_DEPLOYMENT_TARGET
            AND rust_triple MATCHES "-apple-darwin$")
        list(APPEND LUCA_PATH_PREFIX
            "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()

    # Panic = abort keeps unwinding from ever crossing the C boundary.
    set(LUCA_RUSTFLAGS "-C panic=abort")

    add_custom_target(luca_rs_cargo_build ALL
        COMMAND ${CMAKE_COMMAND} -E env
            ${LUCA_PATH_PREFIX}
            "CARGO_TARGET_DIR=${LUCA_CARGO_TARGET_DIR}"
            "RUSTFLAGS=${LUCA_RUSTFLAGS}"
            "${CARGO_EXECUTABLE}" rustc
                --manifest-path "${LUCA_RS_MANIFEST}"
                --target "${rust_triple}"
                --lib
                ${rust_profile_flag}
                --crate-type staticlib
        BYPRODUCTS "${LUCA_STATIC_LIB}"
        USES_TERMINAL
        VERBATIM
    )

    add_library(${imported_target} STATIC IMPORTED GLOBAL)
    set_target_properties(${imported_target} PROPERTIES
        IMPORTED_LOCATION "${LUCA_STATIC_LIB}")
    add_dependencies(${imported_target} luca_rs_cargo_build)
    # luca_ffi.h lives with the crate so the engine repository stays the
    # single source of truth for its ABI.
    set_target_properties(${imported_target} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LUCA_RS_ROOT}/crates/luca_runtime/include")

    # luca_runtime is a pure-Rust staticlib whose std only needs libSystem /
    # default CRT libraries that every consumer already links. When future
    # phases add native dependencies (vorbis, wgpu, ...), their system link
    # surface belongs here so it propagates through the archive like the
    # siglus frameworks do.

    set(${imported_target}_FOUND TRUE PARENT_SCOPE)
endfunction()
