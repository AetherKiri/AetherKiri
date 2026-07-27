include_guard(GLOBAL)

set(AETHERKIRI_OBFUSCATION_POLICY
    "${CMAKE_CURRENT_LIST_DIR}/internal_obfuscation.json")

function(aetherkiri_link_kagura_runtime target_name)
    if(NOT EXISTS "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/CMakeLists.txt" OR
       NOT EXISTS "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/ios/jailbreak_detection.c")
        message(FATAL_ERROR
            "Kagura runtime sources are missing: "
            "${AETHERKIRI_KAGURA_SOURCE_DIR}")
    endif()

    if(NOT TARGET kagura_runtime)
        add_library(kagura_runtime STATIC
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/core/aes.c"
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/core/zero_buf.c"
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/anti_debug/anti_debug.c"
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/anti_debug/breakpoint_detection.c"
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/anti_debug/hook_detection.c"
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/anti_debug/emulator_detection.c"
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime/ios/jailbreak_detection.c")
        target_include_directories(kagura_runtime PUBLIC
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/include")
        set_target_properties(kagura_runtime PROPERTIES
            C_STANDARD 11
            POSITION_INDEPENDENT_CODE ON)
        if(ANDROID OR (UNIX AND NOT APPLE))
            target_link_libraries(kagura_runtime PUBLIC ${CMAKE_DL_LIBS})
        endif()
    endif()

    target_link_libraries("${target_name}" PRIVATE kagura_runtime)
endfunction()

function(aetherkiri_enable_internal_runtime_protection target_name)
    if(NOT AETHERKIRI_ENABLE_RUNTIME_PROTECTION)
        return()
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        return()
    endif()
    if(WEB OR EMSCRIPTEN)
        message(FATAL_ERROR "Kagura runtime protection is unavailable for Web")
    endif()

    aetherkiri_link_kagura_runtime("${target_name}")
    foreach(internal_source IN LISTS ARGN)
        if(NOT EXISTS "${internal_source}")
            message(FATAL_ERROR
                "Internal runtime protection source does not exist: "
                "${internal_source}")
        endif()
        set_property(SOURCE "${internal_source}" APPEND PROPERTY
            COMPILE_DEFINITIONS
                AETHERKIRI_KAGURA_RUNTIME=1
                AETHERKIRI_KAGURA_FLA=1)
    endforeach()
endfunction()

function(aetherkiri_protect_internal_sources target_name)
    if(NOT AETHERKIRI_ENABLE_CODE_OBFUSCATION)
        return()
    endif()

    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(STATUS
            "Internal code obfuscation: disabled for ${CMAKE_BUILD_TYPE}")
        return()
    endif()

    if(WEB OR EMSCRIPTEN)
        message(FATAL_ERROR
            "Internal code obfuscation is not supported for Web builds")
    endif()
    if(NOT ANDROID AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR
            "Internal code obfuscation requires upstream Clang; "
            "AppleClang and MSVC cannot load the pinned LLVM pass plugin")
    endif()
    if(NOT EXISTS "${AETHERKIRI_OBFUSCATOR_PLUGIN}")
        message(FATAL_ERROR
            "AETHERKIRI_OBFUSCATOR_PLUGIN does not exist: "
            "${AETHERKIRI_OBFUSCATOR_PLUGIN}")
    endif()
    if(NOT EXISTS "${AETHERKIRI_OBFUSCATION_POLICY}")
        message(FATAL_ERROR
            "Internal obfuscation policy is missing: "
            "${AETHERKIRI_OBFUSCATION_POLICY}")
    endif()

    file(TO_CMAKE_PATH "${AETHERKIRI_OBFUSCATOR_PLUGIN}"
         obfuscator_plugin)
    file(TO_CMAKE_PATH "${AETHERKIRI_OBFUSCATION_POLICY}"
         obfuscation_policy)

    aetherkiri_link_kagura_runtime("${target_name}")

    if(ANDROID)
        set(android_launcher
            "${CMAKE_SOURCE_DIR}/.github/scripts/android-kagura-fla-launcher.sh")
        if(NOT EXISTS "${android_launcher}")
            message(FATAL_ERROR
                "Android Kagura FLA launcher is missing: ${android_launcher}")
        endif()
        if(NOT EXISTS "${AETHERKIRI_ANDROID_OPT}")
            message(FATAL_ERROR
                "AETHERKIRI_ANDROID_OPT does not exist: ${AETHERKIRI_ANDROID_OPT}")
        endif()
        if(NOT EXISTS "${AETHERKIRI_ANDROID_LLC}")
            message(FATAL_ERROR
                "AETHERKIRI_ANDROID_LLC does not exist: ${AETHERKIRI_ANDROID_LLC}")
        endif()
        set_property(TARGET "${target_name}" PROPERTY
            CXX_COMPILER_LAUNCHER
                "${android_launcher};${AETHERKIRI_ANDROID_LLC};${AETHERKIRI_ANDROID_OPT};${obfuscator_plugin}")
    endif()

    foreach(internal_source IN LISTS ARGN)
        if(NOT EXISTS "${internal_source}")
            message(FATAL_ERROR
                "Internal protection source does not exist: ${internal_source}")
        endif()
        if(NOT ANDROID)
            set_property(SOURCE "${internal_source}" APPEND PROPERTY
                COMPILE_OPTIONS
                    "-fpass-plugin=${obfuscator_plugin}"
                    "SHELL:-mllvm -kagura-config=${obfuscation_policy}"
                    "SHELL:-mllvm -kagura-build-id=${AETHERKIRI_OBFUSCATION_BUILD_ID}"
                    "SHELL:-mllvm -kagura-metrics"
            )
        endif()
        if(ANDROID)
            set_property(SOURCE "${internal_source}" APPEND PROPERTY
                COMPILE_DEFINITIONS
                    AETHERKIRI_KAGURA_RUNTIME=1
                    AETHERKIRI_KAGURA_FLA=1)
        else()
            set_property(SOURCE "${internal_source}" APPEND PROPERTY
                COMPILE_DEFINITIONS AETHERKIRI_KAGURA_RUNTIME=1)
        endif()
    endforeach()

    message(STATUS
        "Internal code obfuscation: ${target_name} (${ARGC} arguments)")
endfunction()
