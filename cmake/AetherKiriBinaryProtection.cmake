include_guard(GLOBAL)

set(AETHERKIRI_OBFUSCATION_POLICY
    "${CMAKE_CURRENT_LIST_DIR}/internal_obfuscation.json")

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

    foreach(internal_source IN LISTS ARGN)
        if(NOT EXISTS "${internal_source}")
            message(FATAL_ERROR
                "Internal protection source does not exist: ${internal_source}")
        endif()
        set_property(SOURCE "${internal_source}"
            TARGET_DIRECTORY ${target_name}
            APPEND PROPERTY COMPILE_OPTIONS
                "-fvisibility=hidden"
                "-fvisibility-inlines-hidden"
                "-fpass-plugin=${obfuscator_plugin}"
                "-mllvm"
                "-kagura-config=${obfuscation_policy}"
                "-mllvm"
                "-kagura-fla"
                "-mllvm"
                "-kagura-bcf"
                "-mllvm"
                "-kagura-bcf-prob=30"
                "-mllvm"
                "-kagura-bcf-iter=1"
                "-mllvm"
                "-kagura-str"
                "-mllvm"
                "-kagura-metrics"
                "-mllvm"
                "-kagura-dwarf=strip"
                "-mllvm"
                "-kagura-build-id=${AETHERKIRI_OBFUSCATION_BUILD_ID}"
        )
    endforeach()

    message(STATUS
        "Internal code obfuscation: ${target_name} (${ARGC} arguments)")
endfunction()
