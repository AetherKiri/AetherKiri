cmake_minimum_required(VERSION 3.28)
project(aether_rfvp_runtime LANGUAGES CXX)

get_filename_component(AETHERKIRI_RFVP_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${AETHERKIRI_RFVP_PROJECT_ROOT}/packages/rfvp/crates/rfvp/Cargo.toml")
    message(FATAL_ERROR "Initialize packages/rfvp with git submodule update --init packages/rfvp")
endif()
if(ANDROID OR EMSCRIPTEN OR WEB)
    message(FATAL_ERROR "The embedded rfvp provider currently targets native Apple and desktop builds only")
endif()
find_program(RFVP_CARGO cargo REQUIRED)
find_program(RFVP_RUSTC rustc REQUIRED)
execute_process(COMMAND "${RFVP_RUSTC}" -vV OUTPUT_VARIABLE rust_version COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([^\n]+)" unused "${rust_version}")
set(AETHERKIRI_RFVP_RUST_TARGET "${CMAKE_MATCH_1}" CACHE STRING "Rust target triple for rfvp")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES arch_count)
    if(NOT arch_count EQUAL 1)
        message(FATAL_ERROR "Build rfvp once per architecture before combining universal libraries")
    endif()
    if(IOS)
        if(CMAKE_OSX_SYSROOT STREQUAL "iphonesimulator")
            if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
                set(AETHERKIRI_RFVP_RUST_TARGET "aarch64-apple-ios-sim")
            elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
                set(AETHERKIRI_RFVP_RUST_TARGET "x86_64-apple-ios")
            endif()
        elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
            set(AETHERKIRI_RFVP_RUST_TARGET "aarch64-apple-ios")
        endif()
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(AETHERKIRI_RFVP_RUST_TARGET "aarch64-apple-darwin")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(AETHERKIRI_RFVP_RUST_TARGET "x86_64-apple-darwin")
    endif()
endif()
include(cmake/PrepareRfvpSources.cmake)
set(rfvp_prepared "${CMAKE_CURRENT_BINARY_DIR}/prepared")
aetherkiri_prepare_rfvp("${AETHERKIRI_RFVP_PROJECT_ROOT}" "${rfvp_prepared}")
set(rfvp_features "soft-render-core,audio,native-video,random,image-formats,zlib-flate2")
set(rfvp_profile debug)
set(rfvp_profile_args)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(rfvp_profile release)
    set(rfvp_profile_args --release)
endif()
if(WIN32)
    set(rfvp_library "${rfvp_prepared}/target/${AETHERKIRI_RFVP_RUST_TARGET}/${rfvp_profile}/rfvp.lib")
else()
    set(rfvp_library "${rfvp_prepared}/target/${AETHERKIRI_RFVP_RUST_TARGET}/${rfvp_profile}/librfvp.a")
endif()
file(GLOB_RECURSE rfvp_inputs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/*.rs"
    "${AETHERKIRI_RFVP_PROJECT_ROOT}/packages/rfvp/crates/*/src/*.rs"
    "${AETHERKIRI_RFVP_PROJECT_ROOT}/packages/rfvp/crates/*/Cargo.toml")
list(APPEND rfvp_inputs "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrepareRfvpSources.cmake"
    "${AETHERKIRI_RFVP_PROJECT_ROOT}/apps/godot_app/assets/fonts/aetherkiri-runtime-cjk.otf")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${rfvp_inputs})
add_custom_command(OUTPUT "${rfvp_library}"
    COMMAND "${RFVP_CARGO}" build --locked --manifest-path "${rfvp_prepared}/Cargo.toml"
        -p rfvp --lib --no-default-features --features "${rfvp_features}"
        --target "${AETHERKIRI_RFVP_RUST_TARGET}" ${rfvp_profile_args}
    DEPENDS ${rfvp_inputs}
    WORKING_DIRECTORY "${rfvp_prepared}" VERBATIM USES_TERMINAL)
add_custom_target(aether_rfvp_rust_build DEPENDS "${rfvp_library}")
add_library(aether_rfvp_rust STATIC IMPORTED GLOBAL)
set_target_properties(aether_rfvp_rust PROPERTIES IMPORTED_LOCATION "${rfvp_library}")
add_dependencies(aether_rfvp_rust aether_rfvp_rust_build)
add_library(aether_rfvp_runtime STATIC src/rfvp_runtime_provider.cpp)
target_compile_features(aether_rfvp_runtime PUBLIC cxx_std_17)
set_target_properties(aether_rfvp_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(aether_rfvp_runtime PUBLIC include
    "${AETHERKIRI_RFVP_PROJECT_ROOT}/bridge/engine_api/include"
    PRIVATE
    "${AETHERKIRI_RFVP_PROJECT_ROOT}/cpp/core/visual/godot")
target_link_libraries(aether_rfvp_runtime PUBLIC aether_rfvp_rust)
if(APPLE)
    target_link_libraries(aether_rfvp_runtime PUBLIC
        "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation"
        "-framework Foundation" "-framework Security" "-framework SystemConfiguration")
elseif(WIN32)
    target_link_libraries(aether_rfvp_runtime PUBLIC ws2_32 userenv bcrypt ntdll ole32 avrt)
else()
    target_link_libraries(aether_rfvp_runtime PUBLIC pthread dl m asound)
endif()
if(PROJECT_IS_TOP_LEVEL OR ENABLE_TESTS)
    enable_testing()
    add_executable(rfvp-provider-test tests/provider.cpp)
    target_link_libraries(rfvp-provider-test PRIVATE aether_rfvp_runtime)
    target_compile_definitions(rfvp-provider-test PRIVATE
        RFVP_TEST_DEMO="${AETHERKIRI_RFVP_PROJECT_ROOT}/packages/rfvp/win95_painter_demo")
    add_test(NAME rfvp-provider COMMAND rfvp-provider-test)
    set_tests_properties(rfvp-provider PROPERTIES TIMEOUT 60)
    add_test(NAME rfvp-host-regressions
        COMMAND "${RFVP_CARGO}" test --locked --manifest-path "${rfvp_prepared}/Cargo.toml"
            -p rfvp --lib --no-default-features --features "${rfvp_features}"
            --target "${AETHERKIRI_RFVP_RUST_TARGET}" ${rfvp_profile_args}
            host_ -- --test-threads=1)
    set_tests_properties(rfvp-host-regressions PROPERTIES TIMEOUT 180)
endif()
