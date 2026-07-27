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
        add_subdirectory(
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/runtime"
            "${CMAKE_BINARY_DIR}/kagura-runtime"
            EXCLUDE_FROM_ALL)
        target_include_directories(kagura_runtime PUBLIC
            "${AETHERKIRI_KAGURA_SOURCE_DIR}/include")
        if(ANDROID OR (UNIX AND NOT APPLE))
            target_link_libraries(kagura_runtime PUBLIC ${CMAKE_DL_LIBS})
        endif()
    endif()

    target_link_libraries("${target_name}" PRIVATE kagura_runtime)
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
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
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

    foreach(internal_source IN LISTS ARGN)
        if(NOT EXISTS "${internal_source}")
            message(FATAL_ERROR
                "Internal protection source does not exist: ${internal_source}")
        endif()
        set_property(SOURCE "${internal_source}" APPEND PROPERTY
            COMPILE_OPTIONS
                "-fpass-plugin=${obfuscator_plugin}"
                "SHELL:-mllvm -kagura-config=${obfuscation_policy}"
                "SHELL:-mllvm -kagura-build-id=${AETHERKIRI_OBFUSCATION_BUILD_ID}"
        )
        set_property(SOURCE "${internal_source}" APPEND PROPERTY
            COMPILE_DEFINITIONS AETHERKIRI_KAGURA_RUNTIME=1)
    endforeach()

    message(STATUS
        "Internal code obfuscation: ${target_name} (${ARGC} arguments)")
endfunction()
