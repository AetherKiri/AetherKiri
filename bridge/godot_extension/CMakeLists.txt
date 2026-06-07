cmake_minimum_required(VERSION 3.19)

project(aether_kiri_godot LANGUAGES C CXX)

find_package(unofficial-godot-cpp CONFIG REQUIRED)

if(WEB OR EMSCRIPTEN)
    get_target_property(_godot_cpp_compile_options unofficial::godot::cpp INTERFACE_COMPILE_OPTIONS)
    if(_godot_cpp_compile_options)
        string(REPLACE "-sSUPPORT_LONGJMP=emscripten" "-sSUPPORT_LONGJMP=wasm" _godot_cpp_compile_options "${_godot_cpp_compile_options}")
        string(REPLACE "-sWASM_LEGACY_EXCEPTIONS=0" "-sWASM_LEGACY_EXCEPTIONS=1" _godot_cpp_compile_options "${_godot_cpp_compile_options}")
        string(REPLACE "-fexceptions" "-fwasm-exceptions" _godot_cpp_compile_options "${_godot_cpp_compile_options}")
        string(REPLACE "-fno-exceptions" "-fwasm-exceptions" _godot_cpp_compile_options "${_godot_cpp_compile_options}")
        set_target_properties(unofficial::godot::cpp PROPERTIES
            INTERFACE_COMPILE_OPTIONS "${_godot_cpp_compile_options}"
        )
    endif()
    get_target_property(_godot_cpp_link_options unofficial::godot::cpp INTERFACE_LINK_OPTIONS)
    if(_godot_cpp_link_options)
        string(REPLACE "-sSUPPORT_LONGJMP=emscripten" "-sSUPPORT_LONGJMP=wasm" _godot_cpp_link_options "${_godot_cpp_link_options}")
        string(REPLACE "-sWASM_LEGACY_EXCEPTIONS=0" "-sWASM_LEGACY_EXCEPTIONS=1" _godot_cpp_link_options "${_godot_cpp_link_options}")
        string(REPLACE "-fexceptions" "-fwasm-exceptions" _godot_cpp_link_options "${_godot_cpp_link_options}")
        set_target_properties(unofficial::godot::cpp PROPERTIES
            INTERFACE_LINK_OPTIONS "${_godot_cpp_link_options}"
        )
    endif()
endif()

if(WEB OR EMSCRIPTEN)
    add_executable(aether_kiri_godot src/aether_kiri_godot.cpp)
elseif(IOS)
    add_library(aether_kiri_godot STATIC src/aether_kiri_godot.cpp)
else()
    add_library(aether_kiri_godot SHARED src/aether_kiri_godot.cpp)
endif()

target_compile_features(aether_kiri_godot PRIVATE cxx_std_17)
target_include_directories(aether_kiri_godot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine_api/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/visual/godot
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/visual
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/tjs2
    ${CMAKE_BINARY_DIR}/cpp/core/tjs2/gen
)

if(TARGET engine_api)
    target_link_libraries(aether_kiri_godot PRIVATE engine_api)
endif()
if(TARGET krkr2plugin)
    set_property(TARGET aether_kiri_godot PROPERTY LINK_LIBRARY_OVERRIDE_krkr2plugin WHOLE_ARCHIVE)
endif()

target_link_libraries(aether_kiri_godot PRIVATE unofficial::godot::cpp)

set_target_properties(aether_kiri_godot PROPERTIES
    OUTPUT_NAME "aether_kiri_godot"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path"
)

if(WEB OR EMSCRIPTEN)
    execute_process(
        COMMAND
            ${CMAKE_CXX_COMPILER}
            -fPIC
            -pthread
            -fwasm-exceptions
            -sWASM_LEGACY_EXCEPTIONS=1
            -sSIDE_MODULE=1
            --print-file-name=libc++-mt-legacyexcept.a
        OUTPUT_VARIABLE AETHERKIRI_WEB_LIBCXX_LEGACYEXCEPT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND
            ${CMAKE_CXX_COMPILER}
            -fPIC
            -pthread
            -fwasm-exceptions
            -sWASM_LEGACY_EXCEPTIONS=1
            -sSIDE_MODULE=1
            --print-file-name=libunwind-mt-legacyexcept.a
        OUTPUT_VARIABLE AETHERKIRI_WEB_LIBUNWIND_LEGACYEXCEPT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND
            ${CMAKE_CXX_COMPILER}
            -fPIC
            -pthread
            -fwasm-exceptions
            -sWASM_LEGACY_EXCEPTIONS=1
            -sSIDE_MODULE=1
            --print-file-name=libc++abi-mt-legacyexcept.a
        OUTPUT_VARIABLE AETHERKIRI_WEB_LIBCXXABI_LEGACYEXCEPT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT EXISTS "${AETHERKIRI_WEB_LIBCXX_LEGACYEXCEPT}")
        message(FATAL_ERROR
            "libc++-mt-legacyexcept.a was not found. "
            "Run `embuilder --pic build libc++-mt-legacyexcept` after activating Emscripten."
        )
    endif()
    if(NOT EXISTS "${AETHERKIRI_WEB_LIBUNWIND_LEGACYEXCEPT}")
        message(FATAL_ERROR
            "libunwind-mt-legacyexcept.a was not found. "
            "Run `embuilder --pic build libunwind-mt-legacyexcept` after activating Emscripten."
        )
    endif()
    if(NOT EXISTS "${AETHERKIRI_WEB_LIBCXXABI_LEGACYEXCEPT}")
        message(FATAL_ERROR
            "libc++abi-mt-legacyexcept.a was not found. "
            "Run `embuilder --pic build libc++abi-mt-legacyexcept` after activating Emscripten."
        )
    endif()
    target_sources(aether_kiri_godot PRIVATE
        src/web_longjmp_stubs.cpp
    )
    target_compile_options(aether_kiri_godot PRIVATE
        -pthread
        -msimd128
        -sSUPPORT_LONGJMP=wasm
        -sWASM_LEGACY_EXCEPTIONS=1
        $<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>
    )
    target_link_options(aether_kiri_godot PRIVATE
        -pthread
        -msimd128
        -sSIDE_MODULE=1
        -sSUPPORT_LONGJMP=wasm
        -sWASM_LEGACY_EXCEPTIONS=1
        -fwasm-exceptions
        -Wl,--whole-archive
        "${AETHERKIRI_WEB_LIBCXX_LEGACYEXCEPT}"
        "${AETHERKIRI_WEB_LIBUNWIND_LEGACYEXCEPT}"
        "${AETHERKIRI_WEB_LIBCXXABI_LEGACYEXCEPT}"
        -Wl,--no-whole-archive
    )
    set_target_properties(aether_kiri_godot PROPERTIES
        PREFIX ""
        SUFFIX ".wasm"
    )
endif()
