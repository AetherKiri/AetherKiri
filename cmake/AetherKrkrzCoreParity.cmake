# Standalone parity targets for selected krkrz_dev core algorithms.
#
# These targets deliberately do not link any Aether core library and do not
# add the upstream core CMake project.  They compile the upstream reference
# and SIMD translation units into isolated executables, making it possible to
# validate future Aether adapters without creating a second set of runtime
# symbols in the product.

function(aether_krkrz_add_core_parity_targets)
    if(NOT AETHER_KRKRZ_DEV_ROOT)
        message(FATAL_ERROR "AETHER_KRKRZ_DEV_ROOT is not configured")
    endif()

    set(_core_root "${AETHER_KRKRZ_DEV_ROOT}/src/core")
    if(NOT EXISTS "${_core_root}/tests/simd_parity_test.cpp" OR
       NOT EXISTS "${_core_root}/tests/sound_parity_test.cpp")
        message(FATAL_ERROR
            "krkrz_dev core parity sources are missing under ${_core_root}")
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arch)
    set(_is_x86 OFF)
    set(_is_arm OFF)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)x86_64($|;)")
        set(_is_x86 ON)
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)(arm64|aarch64)($|;)")
        set(_is_arm ON)
    elseif(_arch MATCHES "^(x86_64|amd64|x64|i[3-6]86|x86)$")
        set(_is_x86 ON)
    elseif(_arch MATCHES "^(aarch64|arm64|armv[0-9].*|arm)$")
        set(_is_arm ON)
    endif()

    if(NOT _is_x86 AND NOT _is_arm)
        message(STATUS
            "krkrz core parity targets skipped on unsupported architecture: "
            "${CMAKE_SYSTEM_PROCESSOR}")
        return()
    endif()

    set(_common_includes
        "${_core_root}/common/environ"
        "${_core_root}/common/tjs2"
        "${_core_root}/common/base"
        "${_core_root}/common/extension"
        "${_core_root}/common/utils"
        "${_core_root}/common/visual"
        "${_core_root}/common/visual/gl"
        "${_core_root}/common/visual/IA32"
        "${_core_root}/common/sound"
        "${_core_root}/external"
        "${_core_root}/win32/vcproj"
        "${_core_root}/win32/environ")
    set(_common_definitions
        UNICODE
        _UNICODE
        __GENERIC__
        TVP_DONT_AUTOLOAD_PROVINCE
        TVP_DONT_AUTOLOAD_MASK)

    if(_is_x86)
        add_executable(aether_krkrz_visual_parity
            "${_core_root}/tests/simd_parity_test.cpp"
            "${_core_root}/common/visual/tvpgl.c"
            "${_core_root}/common/visual/gl/blend_function.cpp"
            "${_core_root}/common/visual/cpu_detect.cpp"
            "${_core_root}/common/visual/gl/blend_function_sse2.cpp"
            "${_core_root}/common/visual/gl/blend_function_avx2.cpp"
            "${_core_root}/common/visual/gl/adjust_color_sse2.cpp"
            "${_core_root}/common/visual/gl/boxblur_sse2.cpp"
            "${_core_root}/common/visual/gl/colorfill_sse2.cpp"
            "${_core_root}/common/visual/gl/colorfill_avx2.cpp"
            "${_core_root}/common/visual/gl/colormap_sse2.cpp"
            "${_core_root}/common/visual/gl/colormap_avx2.cpp"
            "${_core_root}/common/visual/gl/pixelformat_sse2.cpp"
            "${_core_root}/common/visual/gl/tlg_sse2.cpp"
            "${_core_root}/common/visual/gl/univtrans_sse2.cpp"
            "${_core_root}/common/visual/gl/x86simdutil.cpp"
            "${_core_root}/common/visual/gl/x86simdutilAVX2.cpp"
            "${_core_root}/common/visual/IA32/detect_cpu.cpp"
            "${_core_root}/common/visual/IA32/tvpgl_ia32_intf.c")
        target_include_directories(aether_krkrz_visual_parity PRIVATE
            ${_common_includes})
        target_compile_definitions(aether_krkrz_visual_parity PRIVATE
            ${_common_definitions}
            KRKRZ_STANDALONE_TEST
            KRKRZ_TEST_HAS_X86)

        add_executable(aether_krkrz_sound_parity
            "${_core_root}/tests/sound_parity_test.cpp"
            "${_core_root}/common/sound/MathAlgorithms.cpp"
            "${_core_root}/common/sound/RealFFT.cpp"
            "${_core_root}/common/sound/MathAlgorithms_SSE.cpp"
            "${_core_root}/common/sound/RealFFT_SSE.cpp"
            "${_core_root}/common/sound/xmmlib.cpp")
        target_include_directories(aether_krkrz_sound_parity PRIVATE
            ${_common_includes})
        target_compile_definitions(aether_krkrz_sound_parity PRIVATE
            ${_common_definitions})

        if(MSVC)
            set(_avx2_flags "/arch:AVX2")
            set(_ssse3_flags "")
        else()
            set(_avx2_flags "-mavx2;-mfma")
            set(_ssse3_flags "-mssse3")
        endif()
        set_source_files_properties(
            "${_core_root}/common/visual/gl/blend_function_avx2.cpp"
            "${_core_root}/common/visual/gl/colormap_avx2.cpp"
            "${_core_root}/common/visual/gl/colorfill_avx2.cpp"
            "${_core_root}/common/visual/gl/x86simdutilAVX2.cpp"
            PROPERTIES COMPILE_OPTIONS "${_avx2_flags}")
        if(_ssse3_flags)
            set_source_files_properties(
                "${_core_root}/common/visual/gl/blend_function_sse2.cpp"
                "${_core_root}/common/visual/gl/pixelformat_sse2.cpp"
                PROPERTIES COMPILE_OPTIONS "${_ssse3_flags}")
        endif()
        set_source_files_properties(
            "${_core_root}/common/sound/MathAlgorithms_SSE.cpp"
            "${_core_root}/common/sound/RealFFT_SSE.cpp"
            "${_core_root}/common/sound/xmmlib.cpp"
            PROPERTIES COMPILE_OPTIONS "${_ssse3_flags}")

    elseif(_is_arm)
        add_executable(aether_krkrz_visual_parity
            "${_core_root}/tests/simd_parity_test.cpp"
            "${_core_root}/common/visual/tvpgl.c"
            "${_core_root}/common/visual/gl/blend_function.cpp"
            "${_core_root}/common/visual/cpu_detect.cpp"
            "${_core_root}/common/visual/gl/blend_function_neon.cpp"
            "${_core_root}/common/visual/gl/adjust_color_neon.cpp"
            "${_core_root}/common/visual/gl/colormap_neon.cpp"
            "${_core_root}/common/visual/gl/colorfill_neon.cpp"
            "${_core_root}/common/visual/gl/pixelformat_neon.cpp")
        target_include_directories(aether_krkrz_visual_parity PRIVATE
            ${_common_includes})
        target_compile_definitions(aether_krkrz_visual_parity PRIVATE
            ${_common_definitions}
            KRKRZ_STANDALONE_TEST
            KRKRZ_TEST_HAS_NEON)

        add_executable(aether_krkrz_sound_parity
            "${_core_root}/tests/sound_parity_test.cpp"
            "${_core_root}/common/sound/MathAlgorithms.cpp"
            "${_core_root}/common/sound/RealFFT.cpp"
            "${_core_root}/common/sound/MathAlgorithms_NEON.cpp"
            "${_core_root}/common/sound/RealFFT_NEON.cpp")
        target_include_directories(aether_krkrz_sound_parity PRIVATE
            ${_common_includes})
        target_compile_definitions(aether_krkrz_sound_parity PRIVATE
            ${_common_definitions})
    endif()

    if(UNIX AND NOT APPLE)
        target_link_libraries(aether_krkrz_visual_parity PRIVATE m)
        target_link_libraries(aether_krkrz_sound_parity PRIVATE m)
    endif()
    add_test(NAME aether_krkrz_visual_parity
        COMMAND aether_krkrz_visual_parity)
    add_test(NAME aether_krkrz_sound_parity
        COMMAND aether_krkrz_sound_parity)
endfunction()
