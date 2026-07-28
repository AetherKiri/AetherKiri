cmake_minimum_required(VERSION 3.19)
project(tvpgl_simd LANGUAGES CXX)

if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    string(REGEX REPLACE "(^| )[/-]RTC[^ ]*" "" CMAKE_CXX_FLAGS_DEBUG
           "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(SIMD_PATH ${CMAKE_CURRENT_LIST_DIR})
set(SIMD_SOURCE_FILES
    ${SIMD_PATH}/tvpgl_simd_init.cpp
    ${SIMD_PATH}/tvpgl_simd_copy.cpp
    ${SIMD_PATH}/tvpgl_simd_alpha_blend.cpp
    ${SIMD_PATH}/tvpgl_simd_arithmetic_blend.cpp
    ${SIMD_PATH}/tvpgl_simd_const_blend.cpp
    ${SIMD_PATH}/tvpgl_simd_premul_blend.cpp
    ${SIMD_PATH}/tvpgl_simd_ps_blend.cpp
    ${SIMD_PATH}/tvpgl_simd_ps_blend2.cpp
    ${SIMD_PATH}/tvpgl_simd_convert.cpp
    ${SIMD_PATH}/tvpgl_simd_misc.cpp
    ${SIMD_PATH}/tvpgl_simd_blur.cpp
)

add_library(${PROJECT_NAME} STATIC ${SIMD_SOURCE_FILES})

# Highway dependency via vcpkg
find_package(hwy CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE hwy::hwy)

# Include paths: public for tvpgl_simd_init.h, private for engine headers
target_include_directories(${PROJECT_NAME} PUBLIC ${SIMD_PATH})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/..      # visual/ (for tvpgl.h)
    ${CMAKE_CURRENT_LIST_DIR}/../gl   # visual/gl/ (for blend headers)
)

# Need tjs2 for tjsTypes.h
target_link_libraries(${PROJECT_NAME} PRIVATE tjs2)

# C++17 required by Highway
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# Static dispatch: compile only for the current target architecture
# ARM64 always has NEON; x86_64 targets SSE4.1+ or AVX2
target_compile_definitions(${PROJECT_NAME} PRIVATE
    HWY_COMPILE_ONLY_STATIC=1
)

# CRITICAL: Highway SIMD intrinsics MUST be compiled with optimization,
# even in Debug builds. Without at least -O2, all hn::Mul/Add/ShiftRight/etc
# remain as function calls instead of inline SIMD instructions, causing
# massive performance degradation (~20x slower than scalar C code).
# This is standard practice for all Highway/SIMD projects.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        # MSVC's Debug defaults include /RTC1, which cannot be combined with
        # optimization. Keep PDB symbols while disabling runtime checks only
        # for this performance-critical SIMD target.
        target_compile_options(${PROJECT_NAME} PRIVATE /O2 /Zi)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -O2 -g)
    endif()
endif()
