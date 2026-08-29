cmake_minimum_required(VERSION 3.19)
project(core_base_module LANGUAGES CXX)

set(BASE_PATH ${CMAKE_CURRENT_LIST_DIR})

set(BASE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(BASE_D3DEMOTE_RESOURCE ${BASE_PATH}/resources/D3DEmote.tjs)
set(BASE_D3DEMOTE_SOURCE ${BASE_GENERATED_DIR}/D3DEmote_tjs.cpp)
file(MAKE_DIRECTORY ${BASE_GENERATED_DIR})
file(READ ${BASE_D3DEMOTE_RESOURCE} BASE_D3DEMOTE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,"
       BASE_D3DEMOTE_BYTES "${BASE_D3DEMOTE_HEX}")
configure_file(${BASE_PATH}/resources/D3DEmote_tjs.cpp.in
               ${BASE_D3DEMOTE_SOURCE} @ONLY)

set(BASE_SOURCE_FILES
    ${BASE_PATH}/7zArchive.cpp
    ${BASE_PATH}/BinaryStream.cpp
    ${BASE_PATH}/CharacterSet.cpp
    ${BASE_PATH}/EventIntf.cpp
    ${BASE_PATH}/ScriptMgnIntf.cpp
    ${BASE_PATH}/StorageIntf.cpp
    ${BASE_PATH}/SysInitIntf.cpp
    ${BASE_PATH}/SystemIntf.cpp
    ${BASE_PATH}/TARArchive.cpp
    ${BASE_PATH}/TextStream.cpp
    ${BASE_PATH}/UtilStreams.cpp
    ${BASE_PATH}/XP3Archive.cpp
    ${BASE_PATH}/XP3ArchiveCxDecoder.cpp
    ${BASE_PATH}/ZIPArchive.cpp
    ${BASE_PATH}/KAGParser.cpp
    ${BASE_PATH}/MsgIntf.cpp

    ${BASE_PATH}/impl/EventImpl.cpp
    ${BASE_PATH}/impl/FileSelector.cpp
    ${BASE_PATH}/impl/NativeEventQueue.cpp
    ${BASE_PATH}/impl/ScriptMgnImpl.cpp
    ${BASE_PATH}/impl/StorageImpl.cpp
    ${BASE_PATH}/impl/SysInitImpl.cpp
    ${BASE_PATH}/impl/SystemImpl.cpp
    ${BASE_PATH}/impl/MsgImpl.cpp
    ${BASE_D3DEMOTE_SOURCE}
)

set(BASE_HEADERS_DIR
        ${BASE_PATH}/
        ${BASE_PATH}/impl
)

add_library(${PROJECT_NAME} STATIC ${BASE_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${BASE_HEADERS_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_visual_module core_plugin_module core_environ_module core_extension_module core_sound_module core_utils_module)

find_package(uchardet CONFIG REQUIRED)
# The RARLab decoder is preferred when the vcpkg package is available.  On
# developer hosts without that SDK, libarchive still provides RAR4/RAR5
# extraction, so make the specialised decoder optional instead of making the
# entire core impossible to configure.  The source uses the same callback
# contract in either case; only the fast/native RAR path is conditional.
find_package(unrar CONFIG QUIET)
set(AETHERKIRI_HAS_UNRAR OFF)
set(AETHERKIRI_UNRAR_TARGET "")
if(TARGET unrar::unrar)
    set(AETHERKIRI_HAS_UNRAR ON)
    set(AETHERKIRI_UNRAR_TARGET unrar::unrar)
elseif(TARGET unofficial::unrar::unrar)
    set(AETHERKIRI_HAS_UNRAR ON)
    set(AETHERKIRI_UNRAR_TARGET unofficial::unrar::unrar)
endif()
if(NOT AETHERKIRI_HAS_UNRAR)
    message(STATUS
        "unrar SDK not found; archive extraction will use libarchive's RAR provider")
endif()
if(NOT DEFINED AETHERKIRI_LIBARCHIVE_TARGET OR
   NOT TARGET "${AETHERKIRI_LIBARCHIVE_TARGET}")
    include("${CMAKE_SOURCE_DIR}/cmake/AetherKiriDependencyAdapters.cmake")
    aetherkiri_prepare_libarchive()
endif()
find_package(LibXml2 REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
if(NOT DEFINED AETHERKIRI_MINIZIP_TARGET OR
   NOT TARGET "${AETHERKIRI_MINIZIP_TARGET}")
    include("${CMAKE_SOURCE_DIR}/cmake/AetherKiriDependencyAdapters.cmake")
    aetherkiri_prepare_minizip()
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    uchardet::libuchardet
    ${AETHERKIRI_LIBARCHIVE_TARGET}
    LibXml2::LibXml2
    zstd::libzstd

    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    ${AETHERKIRI_MINIZIP_TARGET}
)

if(AETHERKIRI_HAS_UNRAR)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${AETHERKIRI_UNRAR_TARGET})
    target_compile_definitions(${PROJECT_NAME} PRIVATE AETHERKIRI_HAS_UNRAR=1)
endif()

if(APPLE)
    # The static libarchive package on macOS requires libb2 for RAR5 checksum
    # support, but FindLibArchive does not propagate that dependency.
    find_library(LIBB2_LIBRARY NAMES b2 libb2)
    if(LIBB2_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBB2_LIBRARY})
    endif()
endif()
