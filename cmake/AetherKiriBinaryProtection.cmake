include_guard(GLOBAL)

set(AETHERKIRI_OBFUSCATION_POLICY
    "${CMAKE_CURRENT_LIST_DIR}/internal_obfuscation.json")

function(aetherkiri_limit_runtime_exports target_name export_surface)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "Cannot protect missing runtime target: ${target_name}")
    endif()

    if(WEB OR EMSCRIPTEN OR IOS)
        return()
    endif()

    if(NOT MSVC)
        target_compile_options("${target_name}" PRIVATE
            "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>>:-fvisibility=hidden>"
            "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>>:-fvisibility-inlines-hidden>"
        )
    endif()

    if(APPLE)
        if(export_surface STREQUAL "ENGINE_API")
            set(export_list
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/engine_api_apple.exports")
        elseif(export_surface STREQUAL "GODOT_EXTENSION")
            set(export_list
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/godot_extension_apple.exports")
        else()
            message(FATAL_ERROR
                "Unknown runtime export surface: ${export_surface}")
        endif()
        target_link_options("${target_name}" PRIVATE
            "$<$<CONFIG:Release>:LINKER:-dead_strip>"
            "$<$<CONFIG:Release>:LINKER:SHELL:-exported_symbols_list ${export_list}>"
        )
        set_property(TARGET "${target_name}" APPEND PROPERTY
            LINK_DEPENDS "${export_list}")
    elseif(UNIX)
        if(export_surface STREQUAL "ENGINE_API")
            set(version_script
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/engine_api_elf.map")
        elseif(export_surface STREQUAL "GODOT_EXTENSION")
            set(version_script
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/godot_extension_elf.map")
        else()
            message(FATAL_ERROR
                "Unknown runtime export surface: ${export_surface}")
        endif()
        target_link_options("${target_name}" PRIVATE
            "$<$<CONFIG:Release>:-Wl,--gc-sections>"
            "$<$<CONFIG:Release>:-Wl,--version-script=${version_script}>"
            "$<$<CONFIG:Release>:-Wl,--exclude-libs,ALL>"
        )
        set_property(TARGET "${target_name}" APPEND PROPERTY
            LINK_DEPENDS "${version_script}")
    endif()
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
