# BuildSiglusRs.cmake — builds the packages/siglus_rs workspace into a Rust
# static library consumable by the AetherKiri bridges.
#
# The public entry point is aetherkiri_add_siglus_rs(<imported-target>), which:
#   * locates a Rust toolchain (cargo/rustc),
#   * maps the active CMake platform to a Rust target triple,
#   * schedules an always-run `cargo rustc --crate-type staticlib` build of the
#     `siglus_scene_vm` crate (cargo itself stays incremental),
#   * creates a GLOBAL IMPORTED STATIC target pointing at the archive so plain
#     target_link_libraries() propagation carries it through intermediate
#     static libraries up to the final GDExtension shared object.
#
# The function sets <target>_FOUND in the parent scope on success. Callers on
# platforms that are not wired yet should treat a FALSE result as "skip the
# Siglus runtime", not as a configuration failure.

function(aetherkiri_deduce_rust_target out_triple)
    if(WEB OR EMSCRIPTEN)
        set(${out_triple} "wasm32-unknown-unknown" PARENT_SCOPE)
        return()
    endif()
    if(ANDROID)
        if(ANDROID_ABI MATCHES "arm64-v8a")
            set(${out_triple} "aarch64-linux-android" PARENT_SCOPE)
        elseif(ANDROID_ABI MATCHES "armeabi-v7a")
            set(${out_triple} "armv7-linux-androideabi" PARENT_SCOPE)
        elseif(ANDROID_ABI MATCHES "x86_64")
            set(${out_triple} "x86_64-linux-android" PARENT_SCOPE)
        elseif(ANDROID_ABI MATCHES "x86")
            set(${out_triple} "i686-linux-android" PARENT_SCOPE)
        else()
            set(${out_triple} "" PARENT_SCOPE)
        endif()
        return()
    endif()
    if(IOS)
        # CMAKE_OSX_ARCHITECTURES drives the device/simulator distinction;
        # simulator builds may be arm64 or x86_64 depending on the host.
        if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
            if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
                set(${out_triple} "x86_64-apple-ios" PARENT_SCOPE)
            else()
                set(${out_triple} "aarch64-apple-ios-sim" PARENT_SCOPE)
            endif()
        else()
            set(${out_triple} "aarch64-apple-ios" PARENT_SCOPE)
        endif()
        return()
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

function(aetherkiri_add_siglus_rs imported_target)
    find_program(CARGO_EXECUTABLE cargo)
    find_program(RUSTC_EXECUTABLE rustc)
    if(NOT CARGO_EXECUTABLE OR NOT RUSTC_EXECUTABLE)
        message(STATUS
            "Siglus runtime disabled: Rust toolchain (cargo/rustc) not found. "
            "Install https://rustup.rs to enable it.")
        set(${imported_target}_FOUND FALSE PARENT_SCOPE)
        return()
    endif()

    # A non-rustup toolchain (e.g. Homebrew's rust) can shadow the rustup
    # proxies on PATH while missing cross targets. Prefer the binary directory
    # of whichever toolchain `rustup` resolves to so installed std libraries
    # for android/ios/wasm targets are actually visible to the build.
    set(SIGLUS_TOOLCHAIN_BIN_DIR "")
    find_program(RUSTUP_EXECUTABLE rustup)
    if(RUSTUP_EXECUTABLE)
        execute_process(
            COMMAND "${RUSTUP_EXECUTABLE}" which rustc
            OUTPUT_VARIABLE resolved_rustc
            ERROR_QUIET RESULT_VARIABLE rustup_which_result)
        string(STRIP "${resolved_rustc}" resolved_rustc)
        if(rustup_which_result EQUAL 0 AND EXISTS "${resolved_rustc}")
            get_filename_component(SIGLUS_TOOLCHAIN_BIN_DIR "${resolved_rustc}" DIRECTORY)
            # Prefer the rustup-resolved binaries over whatever shadows them
            # on PATH (e.g. a non-rustup Homebrew rust without cross std).
            if(EXISTS "${SIGLUS_TOOLCHAIN_BIN_DIR}/cargo")
                set(CARGO_EXECUTABLE "${SIGLUS_TOOLCHAIN_BIN_DIR}/cargo")
            endif()
            if(EXISTS "${SIGLUS_TOOLCHAIN_BIN_DIR}/rustc")
                set(RUSTC_EXECUTABLE "${SIGLUS_TOOLCHAIN_BIN_DIR}/rustc")
            endif()
        endif()
    endif()

    aetherkiri_deduce_rust_target(rust_triple)
    if(rust_triple STREQUAL "")
        message(STATUS
            "Siglus runtime disabled: no Rust target mapping for this "
            "platform configuration yet.")
        set(${imported_target}_FOUND FALSE PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(rust_profile "debug")
        set(rust_profile_flag "")
    else()
        set(rust_profile "release")
        set(rust_profile_flag "--release")
    endif()

    set(SIGLUS_RS_ROOT "${CMAKE_SOURCE_DIR}/packages/siglus_rs")
    set(SIGLUS_RS_MANIFEST "${SIGLUS_RS_ROOT}/crates/siglus_scene_vm/Cargo.toml")
    if(NOT EXISTS "${SIGLUS_RS_MANIFEST}")
        message(FATAL_ERROR
            "siglus_rs submodule is unavailable. Run "
            "`git submodule update --init packages/siglus_rs`.")
    endif()

    set(SIGLUS_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/siglus-rs-target")
    set(SIGLUS_STATIC_LIB
        "${SIGLUS_CARGO_TARGET_DIR}/${rust_triple}/${rust_profile}/libsiglus_scene_vm.a")

    # Directories prepended to PATH for the cargo invocation. Kept as a single
    # combined PATH assignment because repeated PATH entries passed to
    # `cmake -E env` override each other instead of composing.
    set(siglus_path_leading "")

    if(ANDROID)
        # cc-rs-based native deps need the NDK clang wrappers. Static libs do
        # not link, but several dependency crates compile C code, which needs
        # an explicit cross compiler because the wrappers are not on PATH.
        set(siglus_ndk_home "$ENV{ANDROID_NDK_HOME}")
        if("${siglus_ndk_home}" STREQUAL "" AND DEFINED ANDROID_NDK)
            set(siglus_ndk_home "${ANDROID_NDK}")
        endif()
        if(NOT "${siglus_ndk_home}" STREQUAL "")
            if(CMAKE_HOST_APPLE)
                set(siglus_ndk_host_tag "darwin-x86_64")
            elseif(CMAKE_HOST_WIN32)
                set(siglus_ndk_host_tag "windows-x86_64")
            else()
                set(siglus_ndk_host_tag "linux-x86_64")
            endif()
            set(siglus_ndk_bin
                "${siglus_ndk_home}/toolchains/llvm/prebuilt/${siglus_ndk_host_tag}/bin")
            if(EXISTS "${siglus_ndk_bin}/clang")
                set(siglus_ndk_api 24)
                # Map the Rust triple to the NDK wrapper naming scheme.
                set(siglus_cc_name "")
                set(siglus_cxx_name "")
                if(rust_triple STREQUAL "aarch64-linux-android")
                    set(siglus_cc_name "aarch64-linux-android${siglus_ndk_api}-clang")
                    set(siglus_cxx_name "aarch64-linux-android${siglus_ndk_api}-clang++")
                elseif(rust_triple STREQUAL "armv7-linux-androideabi")
                    set(siglus_cc_name "armv7a-linux-androideabi${siglus_ndk_api}-clang")
                    set(siglus_cxx_name "armv7a-linux-androideabi${siglus_ndk_api}-clang++")
                elseif(rust_triple STREQUAL "i686-linux-android")
                    set(siglus_cc_name "i686-linux-android${siglus_ndk_api}-clang")
                    set(siglus_cxx_name "i686-linux-android${siglus_ndk_api}-clang++")
                elseif(rust_triple STREQUAL "x86_64-linux-android")
                    set(siglus_cc_name "x86_64-linux-android${siglus_ndk_api}-clang")
                    set(siglus_cxx_name "x86_64-linux-android${siglus_ndk_api}-clang++")
                endif()
                if(NOT "${siglus_cc_name}" STREQUAL "")
                    string(REPLACE "-" "_" siglus_triple_us "${rust_triple}")
                    list(APPEND SIGLUS_PATH_PREFIX
                        "CC_${siglus_triple_us}=${siglus_ndk_bin}/${siglus_cc_name}"
                        "CXX_${siglus_triple_us}=${siglus_ndk_bin}/${siglus_cxx_name}"
                        "AR_${siglus_triple_us}=${siglus_ndk_bin}/llvm-ar")
                endif()
            else()
                message(WARNING
                    "Siglus runtime: NDK toolchain bin not found under "
                    "\"${siglus_ndk_bin}\"; Rust native-dep builds may fail.")
                set(siglus_ndk_bin "")
            endif()
        else()
            message(WARNING
                "Siglus runtime: ANDROID build without ANDROID_NDK_HOME; "
                "Rust native-dep builds may fail.")
        endif()
    endif()

    if(NOT "${siglus_ndk_home}" STREQUAL "")
        string(APPEND siglus_path_leading "${siglus_ndk_bin}:")
    endif()
    if(SIGLUS_TOOLCHAIN_BIN_DIR)
        string(APPEND siglus_path_leading "${SIGLUS_TOOLCHAIN_BIN_DIR}:")
    endif()
    if(CMAKE_HOST_UNIX)
        list(PREPEND SIGLUS_PATH_PREFIX "PATH=${siglus_path_leading}$ENV{PATH}")
    else()
        list(PREPEND SIGLUS_PATH_PREFIX "PATH=${siglus_path_leading}\;$ENV{PATH}")
    endif()

    # Panic = abort keeps unwinding from ever crossing the C boundary while the
    # runtime is still being wired behind the stub provider.
    set(SIGLUS_RUSTFLAGS "-C panic=abort")

    add_custom_target(siglus_rs_cargo_build ALL
        COMMAND ${CMAKE_COMMAND} -E env
            ${SIGLUS_PATH_PREFIX}
            "CARGO_TARGET_DIR=${SIGLUS_CARGO_TARGET_DIR}"
            "RUSTFLAGS=${SIGLUS_RUSTFLAGS}"
            "${CARGO_EXECUTABLE}" rustc
                --manifest-path "${SIGLUS_RS_MANIFEST}"
                --target "${rust_triple}"
                --lib
                ${rust_profile_flag}
                --crate-type staticlib
        BYPRODUCTS "${SIGLUS_STATIC_LIB}"
        USES_TERMINAL
        VERBATIM)

    add_library(${imported_target} STATIC IMPORTED GLOBAL)
    set_target_properties(${imported_target} PROPERTIES
        IMPORTED_LOCATION "${SIGLUS_STATIC_LIB}")
    add_dependencies(${imported_target} siglus_rs_cargo_build)

    # wgpu/winit/kira pull system frameworks on Apple platforms. Linking them
    # here lets the dependency propagate to whichever target consumes the
    # archive, instead of teaching every consumer about Rust's link surface.
    if(APPLE)
        if(IOS)
            list(APPEND SIGLUS_APPLE_FRAMEWORKS UIKit)
        else()
            list(APPEND SIGLUS_APPLE_FRAMEWORKS AppKit)
        endif()
        list(APPEND SIGLUS_APPLE_FRAMEWORKS
            Foundation Metal QuartzCore IOKit Security
            CoreGraphics CoreAudio AudioToolbox AudioUnit)
        set(siglus_framework_flags "")
        foreach(framework IN LISTS SIGLUS_APPLE_FRAMEWORKS)
            list(APPEND siglus_framework_flags
                "-framework" "${framework}")
        endforeach()
        set_property(TARGET ${imported_target} APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES "${siglus_framework_flags}")
    elseif(WIN32)
        set_property(TARGET ${imported_target} APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES
            ntdll user32 gdi32 shell32 ws2_2 bcrypt advapi32 ole32 oleaut32)
    endif()

    set(${imported_target}_FOUND TRUE PARENT_SCOPE)
endfunction()
