cmake_minimum_required(VERSION 3.19)
project(psbfile)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
    PSBValue.cpp
    PSBFile.cpp
    PSBFileExtension.cpp
    PSBMedia.cpp
    PSBMediaRegistry.cpp
    types/PimgType.cpp
    types/MotionType.cpp
    types/PimgType.cpp
    types/ScnType.cpp
    types/ImageType.cpp
    types/MmoType.cpp
    types/ArchiveType.cpp
    types/SoundArchiveType.cpp
    resources/ImageMetadata.cpp
)

find_package(ZLIB REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
    krkr2plugin
    ZLIB::ZLIB
)

# PSB/PIMG metadata and resource lookup sits directly on the story advance
# path for motion-heavy games. Keep Debug symbols, but optimize this parser
# and its TJS adapters so large scene transitions do not pay -Og overhead.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -O2)
endif()

if(AETHERKIRI_INTERNAL_ENABLED)
    aetherinternal_extend_psbfile(
        ${PROJECT_NAME}
        "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
