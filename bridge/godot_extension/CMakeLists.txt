cmake_minimum_required(VERSION 3.19)

project(aether_kiri_godot LANGUAGES C CXX)

if(APPLE)
    enable_language(Swift)
endif()

find_package(unofficial-godot-cpp CONFIG REQUIRED)

if(APPLE)
    # godot-cpp publishes Clang-specific debug flags without a language guard.
    # A mixed C++/Swift GDExtension otherwise forwards them to swiftc, which
    # rejects -gdwarf-4 and -g2.
    get_target_property(_godot_cpp_apple_compile_options
        unofficial::godot::cpp INTERFACE_COMPILE_OPTIONS)
    if(_godot_cpp_apple_compile_options)
        string(REPLACE "-gdwarf-4"
            "$<$<COMPILE_LANGUAGE:CXX>:-gdwarf-4>"
            _godot_cpp_apple_compile_options
            "${_godot_cpp_apple_compile_options}")
        string(REPLACE "-g2"
            "$<$<COMPILE_LANGUAGE:CXX>:-g2>"
            _godot_cpp_apple_compile_options
            "${_godot_cpp_apple_compile_options}")
        set_target_properties(unofficial::godot::cpp PROPERTIES
            INTERFACE_COMPILE_OPTIONS "${_godot_cpp_apple_compile_options}"
        )
    endif()

    # godot-cpp also publishes AppleClang's -Wl spelling for Release strip
    # options. Use CMake's driver-neutral LINKER spelling so a mixed target
    # linked by swiftc receives the corresponding -Xlinker arguments.
    get_target_property(_godot_cpp_apple_link_options
        unofficial::godot::cpp INTERFACE_LINK_OPTIONS)
    if(_godot_cpp_apple_link_options)
        string(REPLACE "-Wl,-S" "LINKER:-S"
            _godot_cpp_apple_link_options
            "${_godot_cpp_apple_link_options}")
        string(REPLACE "-Wl,-x" "LINKER:-x"
            _godot_cpp_apple_link_options
            "${_godot_cpp_apple_link_options}")
        string(REPLACE "-Wl,-dead_strip" "LINKER:-dead_strip"
            _godot_cpp_apple_link_options
            "${_godot_cpp_apple_link_options}")
        set_target_properties(unofficial::godot::cpp PROPERTIES
            INTERFACE_LINK_OPTIONS "${_godot_cpp_apple_link_options}"
        )
    endif()

    # The vcpkg include root published by godot-cpp contains C headers whose
    # names overlap iOS SDK headers (notably uuid/uuid.h). Passing that root to
    # Swift breaks the Darwin module under Release whole-module optimization.
    # Keep all godot-cpp headers scoped to its actual C++ consumer.
    foreach(_godot_cpp_include_property IN ITEMS
        INTERFACE_INCLUDE_DIRECTORIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
        get_target_property(_godot_cpp_apple_include_directories
            unofficial::godot::cpp ${_godot_cpp_include_property})
        if(_godot_cpp_apple_include_directories)
            set(_godot_cpp_cxx_include_directories "")
            foreach(_godot_cpp_include_directory IN LISTS
                _godot_cpp_apple_include_directories)
                list(APPEND _godot_cpp_cxx_include_directories
                    "$<$<COMPILE_LANGUAGE:CXX>:${_godot_cpp_include_directory}>")
            endforeach()
            set_target_properties(unofficial::godot::cpp PROPERTIES
                ${_godot_cpp_include_property}
                "${_godot_cpp_cxx_include_directories}"
            )
        endif()
    endforeach()
endif()

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

set(AETHERKIRI_GODOT_SOURCES
    src/aether_runtime_player.cpp
    src/frame_effect_host.cpp
)

if(WEB OR EMSCRIPTEN)
    add_executable(aether_kiri_godot ${AETHERKIRI_GODOT_SOURCES})
elseif(IOS)
    add_library(aether_kiri_godot STATIC ${AETHERKIRI_GODOT_SOURCES})
else()
    add_library(aether_kiri_godot SHARED ${AETHERKIRI_GODOT_SOURCES})
endif()

if(APPLE)
    target_sources(aether_kiri_godot PRIVATE
        src/apple_external_texture.mm
        src/apple_native_file_picker.swift
        src/apple_storekit.swift)
    set_source_files_properties(src/apple_external_texture.mm
        PROPERTIES LANGUAGE CXX COMPILE_FLAGS "-x objective-c++")
    set_property(TARGET aether_kiri_godot PROPERTY Swift_LANGUAGE_VERSION 5)
    target_link_libraries(aether_kiri_godot PRIVATE
        "-framework CoreVideo"
        "-framework Metal"
        "-framework UniformTypeIdentifiers"
        "-framework Security"
        "-framework StoreKit")
endif()

if(APPLE AND NOT IOS)
    find_package(VulkanHeaders CONFIG REQUIRED)
    target_sources(aether_kiri_godot PRIVATE
        src/apple_vulkan_external_texture.mm)
    set_source_files_properties(src/apple_vulkan_external_texture.mm
        PROPERTIES LANGUAGE CXX COMPILE_FLAGS "-x objective-c++")
    target_link_libraries(aether_kiri_godot PRIVATE Vulkan::Headers)
endif()

if(ANDROID)
    target_sources(aether_kiri_godot PRIVATE
        src/android_external_texture.cpp)
    target_link_libraries(aether_kiri_godot PRIVATE
        android vulkan ${CMAKE_DL_LIBS})
endif()

target_compile_features(aether_kiri_godot PRIVATE cxx_std_17)
if(MSVC)
    set_property(TARGET aether_kiri_godot PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_definitions(aether_kiri_godot PRIVATE
        _ITERATOR_DEBUG_LEVEL=0)
    target_compile_options(aether_kiri_godot PRIVATE /U_DEBUG)
endif()
target_compile_definitions(aether_kiri_godot PRIVATE
    $<$<CONFIG:Debug>:DEBUG_ENABLED DEBUG_METHODS_ENABLED HOT_RELOAD_ENABLED>
)
target_include_directories(aether_kiri_godot PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../engine_api/include>
    $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/visual/godot>
    $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/visual>
    $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/core/tjs2>
    $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_BINARY_DIR}/cpp/core/tjs2/gen>
)

if(AETHERKIRI_INTERNAL_ENABLED AND
   COMMAND aetherinternal_extend_godot_extension)
    aetherinternal_extend_godot_extension(
        aether_kiri_godot
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

if(TARGET engine_api)
    target_link_libraries(aether_kiri_godot PRIVATE engine_api)
endif()
if(TARGET aether_onscripter_runtime)
    target_link_libraries(aether_kiri_godot PRIVATE
        aether_onscripter_runtime)
    target_compile_definitions(aether_kiri_godot PRIVATE
        AETHERKIRI_WITH_ONSCRIPTER=1)
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

target_link_libraries(aether_kiri_godot PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,unofficial::godot::cpp>"
)

set(AETHERKIRI_EXTENSION_RPATH "@loader_path")
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    # The staged Linux extension and engine API live next to each other.
    set(AETHERKIRI_EXTENSION_RPATH "$ORIGIN")
endif()

set_target_properties(aether_kiri_godot PROPERTIES
    OUTPUT_NAME "aether_kiri_godot"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${AETHERKIRI_EXTENSION_RPATH}"
)

aetherkiri_limit_runtime_exports(aether_kiri_godot GODOT_EXTENSION)

if(APPLE AND NOT IOS AND TARGET engine_api)
    # Godot exports the libraries referenced by aether_kiri.gdextension from
    # apps/godot_app/bin. Keep that staging directory tied to the actual CMake
    # outputs so an export cannot silently package an older runtime.
    set(_aetherkiri_macos_godot_bin
        "${CMAKE_CURRENT_SOURCE_DIR}/../../apps/godot_app/bin/macos/$<LOWER_CASE:$<CONFIG>>")
    add_custom_command(TARGET aether_kiri_godot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${_aetherkiri_macos_godot_bin}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:aether_kiri_godot>"
            "${_aetherkiri_macos_godot_bin}/libaether_kiri_godot.dylib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:engine_api>"
            "${_aetherkiri_macos_godot_bin}/libengine_api.dylib"
        VERBATIM
    )
endif()

if(WIN32)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" AETHERKIRI_WINDOWS_CONFIG_DIR)
    if(NOT AETHERKIRI_WINDOWS_CONFIG_DIR)
        set(AETHERKIRI_WINDOWS_CONFIG_DIR "$<LOWER_CASE:$<CONFIG>>")
    endif()
    set(AETHERKIRI_WINDOWS_STAGE_DIR
        "${CMAKE_SOURCE_DIR}/apps/godot_app/bin/windows/${AETHERKIRI_WINDOWS_CONFIG_DIR}")
    add_custom_command(TARGET aether_kiri_godot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${AETHERKIRI_WINDOWS_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:aether_kiri_godot>"
                "${AETHERKIRI_WINDOWS_STAGE_DIR}/aether_kiri_godot.dll"
        VERBATIM
    )
    if(TARGET engine_api)
        add_custom_command(TARGET aether_kiri_godot POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:engine_api>"
                    "${AETHERKIRI_WINDOWS_STAGE_DIR}/engine_api.dll"
            VERBATIM
        )
    endif()
endif()

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
