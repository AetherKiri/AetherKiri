cmake_minimum_required(VERSION 3.19)
project(psdfile)

set(AETHERKIRI_PSDFILE_PACKAGE_ROOT
    "${CMAKE_SOURCE_DIR}/packages/psdfile")
set(AETHERKIRI_PSDPARSE_ROOT
    "${AETHERKIRI_PSDFILE_PACKAGE_ROOT}/external/psdparse")

if(NOT EXISTS "${AETHERKIRI_PSDFILE_PACKAGE_ROOT}/psdclass.cpp" OR
   NOT EXISTS "${AETHERKIRI_PSDPARSE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "packages/psdfile is incomplete; run: "
        "git submodule update --init --recursive packages/psdfile")
endif()

set(PSDPARSE_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(PSDPARSE_BUILD_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${AETHERKIRI_PSDPARSE_ROOT}"
    "${CMAKE_CURRENT_BINARY_DIR}/psdparse")

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
    psdclass.cpp
    psdclass_loadstream.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${AETHERKIRI_PSDPARSE_ROOT}")
target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin psdparse)
