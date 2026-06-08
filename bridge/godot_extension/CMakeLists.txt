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
target_compile_definitions(aether_kiri_godot PRIVATE
    $<$<CONFIG:Debug>:DEBUG_ENABLED DEBUG_METHODS_ENABLED HOT_RELOAD_ENABLED>
)
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
    if(WEB OR EMSCRIPTEN)
        # krkr2plugin has cyclic static dependencies with sub-plugin targets.
        # Force-load the archive by path exactly once; leave target-based
        # krkr2plugin links as normal archives so repeated dependency scans do
        # not produce duplicate symbols under wasm-ld.
        add_dependencies(aether_kiri_godot krkr2plugin)
        target_link_libraries(aether_kiri_godot PRIVATE
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,$<TARGET_FILE:krkr2plugin>>"
        )
    else()
        set_property(TARGET aether_kiri_godot PROPERTY LINK_LIBRARY_OVERRIDE_krkr2plugin WHOLE_ARCHIVE)
    endif()
endif()

if(ANDROID)
    target_link_libraries(aether_kiri_godot PRIVATE log)
endif()

target_link_libraries(aether_kiri_godot PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,unofficial::godot::cpp>"
)

set_target_properties(aether_kiri_godot PROPERTIES
    OUTPUT_NAME "aether_kiri_godot"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path"
)

if(WEB OR EMSCRIPTEN)
    function(aetherkiri_find_web_pic_system_archive out_var archive_name)
        execute_process(
            COMMAND
                ${CMAKE_CXX_COMPILER}
                -pthread
                -fwasm-exceptions
                -sWASM_LEGACY_EXCEPTIONS=1
                -sSIDE_MODULE=1
                --print-file-name=${archive_name}
            OUTPUT_VARIABLE _archive_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        get_filename_component(_archive_dir "${_archive_path}" DIRECTORY)
        set(_pic_archive_path "${_archive_dir}/pic/${archive_name}")
        if(EXISTS "${_pic_archive_path}")
            set(${out_var} "${_pic_archive_path}" PARENT_SCOPE)
        elseif(EXISTS "${_archive_path}" AND _archive_path MATCHES "/pic/")
            set(${out_var} "${_archive_path}" PARENT_SCOPE)
        else()
            string(REGEX REPLACE "\\.a$" "" _embuilder_name "${archive_name}")
            message(FATAL_ERROR
                "${archive_name} PIC archive was not found. "
                "Run `embuilder --pic build ${_embuilder_name}` after activating Emscripten."
            )
        endif()
    endfunction()

    aetherkiri_find_web_pic_system_archive(AETHERKIRI_WEB_LIBCXX_LEGACYEXCEPT libc++-mt-legacyexcept.a)
    aetherkiri_find_web_pic_system_archive(AETHERKIRI_WEB_LIBUNWIND_LEGACYEXCEPT libunwind-mt-legacyexcept.a)
    aetherkiri_find_web_pic_system_archive(AETHERKIRI_WEB_LIBCXXABI_LEGACYEXCEPT libc++abi-mt-legacyexcept.a)
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
