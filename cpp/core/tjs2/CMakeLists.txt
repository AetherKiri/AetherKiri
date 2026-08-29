cmake_minimum_required(VERSION 3.28)
project(tjs2)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

set(AETHER_TJS_EXCEPTION_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/tjsException.cpp")
aether_krkrz_track_core_source(
    "${AETHER_TJS_EXCEPTION_SOURCE}"
    "common/tjs2/tjsException.cpp")

# Keep the debugger hook and symbol table in the pinned krkrz checkout.  The
# hook source is harmless when KRKRZ_ENABLE_DAP is not defined, so every
# target gets one stable no-op ABI; the socket-backed DAP state machine is
# added only when the parent enables the desktop bridge.
set(AETHER_TJS_DEBUGGER_HOOK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/tjsDebuggerHook.cpp")
set(AETHER_TJS_DEBUGGER_SYMBOLS_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/tjsDebuggerSymbols.cpp")
aether_krkrz_track_core_source(
    "${AETHER_TJS_DEBUGGER_HOOK_SOURCE}"
    "common/tjs2/tjsDebuggerHook.cpp")
aether_krkrz_track_core_source(
    "${AETHER_TJS_DEBUGGER_SYMBOLS_SOURCE}"
    "common/tjs2/tjsDebuggerSymbols.cpp")

set(AETHER_TJS_DEBUGGER_SOURCES
    ${AETHER_TJS_DEBUGGER_HOOK_SOURCE}
    ${AETHER_TJS_DEBUGGER_SYMBOLS_SOURCE})
if(AETHERKIRI_DAP_ENABLED)
    set(AETHER_TJS_DEBUGGER_CORE_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/tjsDebuggerCore.cpp")
    aether_krkrz_track_core_source(
        "${AETHER_TJS_DEBUGGER_CORE_SOURCE}"
        "common/tjs2/tjsDebuggerCore.cpp")
    list(APPEND AETHER_TJS_DEBUGGER_SOURCES
        ${AETHER_TJS_DEBUGGER_CORE_SOURCE})
endif()

add_library(${PROJECT_NAME} STATIC)
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:_DEBUG>)

# TJS2 engine legacy code relies on if(!this) null checks inside member functions.
# This is UB in C++ and modern compilers (GCC/Clang -O2+) will optimize these away.
# This flag prevents that optimization, ensuring null-this checks remain effective.
if(NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -fno-delete-null-pointer-checks)
endif()

set(GEN_CODE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)

if(WINDOWS)
    target_compile_options(${PROJECT_NAME} PRIVATE "/FS")
endif()

if(NOT EXISTS ${GEN_CODE_BINARY_DIR})
    file(MAKE_DIRECTORY ${GEN_CODE_BINARY_DIR})
endif()

#===============================================================#
#======================= generate parser =======================#
#===============================================================#
find_program(BISON_EXECUTABLE NAMES win_bison.exe bison.exe bison
    HINTS
        /opt/homebrew/opt/bison/bin   # Homebrew (Apple Silicon)
        /usr/local/opt/bison/bin      # Homebrew (Intel Mac)
)

set(BISON_FILES
    ${CMAKE_CURRENT_LIST_DIR}/bison/tjs.y
    ${CMAKE_CURRENT_LIST_DIR}/bison/tjsdate.y
    ${CMAKE_CURRENT_LIST_DIR}/bison/tjspp.y
)
set(PARSE_TAB_CPP_FILES)
foreach(BISON_FILE ${BISON_FILES})
    get_filename_component(FILE_NAME ${BISON_FILE} NAME_WE)

    execute_process(
        TIMEOUT 5000
        COMMAND ${BISON_EXECUTABLE} ${BISON_FILE}
        WORKING_DIRECTORY ${GEN_CODE_BINARY_DIR}
        COMMAND_ECHO STDOUT
    )

    add_custom_command(
        OUTPUT
            ${GEN_CODE_BINARY_DIR}/${FILE_NAME}.tab.cpp
            ${GEN_CODE_BINARY_DIR}/${FILE_NAME}.tab.hpp
        DEPENDS ${BISON_FILE}
        COMMAND ${BISON_EXECUTABLE} ${BISON_FILE}
        WORKING_DIRECTORY ${GEN_CODE_BINARY_DIR}
        COMMENT "Generate Bison File From ${BISON_FILE}"
    )

    list(APPEND PARSE_TAB_CPP_FILES ${GEN_CODE_BINARY_DIR}/${FILE_NAME}.tab.cpp)
endforeach()

add_custom_target(GENERATE_CPP_FILES DEPENDS ${PARSE_TAB_CPP_FILES})
add_dependencies(${PROJECT_NAME} GENERATE_CPP_FILES)

install(DIRECTORY "${GEN_CODE_BINARY_DIR}/"
    DESTINATION "include/tjs2/parser"
    FILES_MATCHING PATTERN "*.tab.hpp"
)

#===============================================================#
#==================== generate date word =======================#
#===============================================================#
find_package(Python3)

execute_process(
    TIMEOUT 5000
    COMMAND ${Python3_EXECUTABLE} create_world_map.py ${GEN_CODE_BINARY_DIR}/tjsDateWordMap.inc
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/script
    COMMAND_ECHO STDOUT
)

target_sources(${PROJECT_NAME} PRIVATE
    ${PARSE_TAB_CPP_FILES}
    tjsLex.cpp
    tjsNative.cpp
    tjsRandomGenerator.cpp
    tjsDebug.cpp
    tjsRegExp.cpp
    tjsBinarySerializer.cpp
    tjsInterCodeGen.cpp
    tjsObject.cpp
    tjsConstArrayData.cpp
    tjsUtils.cpp
    ${AETHER_TJS_EXCEPTION_SOURCE}
    tjsDictionary.cpp
    tjsInterCodeExec.cpp
    tjsVariantString.cpp
    tjsScriptBlock.cpp
    tjsMath.cpp
    tjs.cpp
    tjsMessage.cpp
    tjsDisassemble.cpp
    tjsDate.cpp
    tjsError.cpp
    tjsInterface.cpp
    tjsMT19937ar-cok.cpp
    tjsArray.cpp
    tjsByteCodeLoader.cpp
    tjsDateParser.cpp
    tjsGlobalStringMap.cpp
    tjsOctPack.cpp
    tjsNamespace.cpp
    tjsScriptCache.cpp
    tjsScriptRecovery.cpp
    tjsConfig.cpp
    tjsCompileControl.cpp
    tjsObjectExtendable.cpp
    tjsVariant.cpp
    tjsString.cpp
    ${AETHER_TJS_DEBUGGER_SOURCES}
)

install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/"
    DESTINATION "include/tjs2"
    FILES_MATCHING PATTERN "*.h"
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
        $<BUILD_INTERFACE:${GEN_CODE_BINARY_DIR}>
    INTERFACE
        $<INSTALL_INTERFACE:include/tjs2>
        $<INSTALL_INTERFACE:include/tjs2/parser>
)

if(AETHERKIRI_DAP_ENABLED)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/cpp/core/utils"
        "${AETHERKIRI_PICOJSON_INCLUDE_DIR}"
        # The bridged upstream translation units retain their own include
        # spelling (LogIntf.h, CharacterSet.h, ...).  Headers are included
        # after Aether's guarded ABI headers, so these paths only provide
        # leaf declarations and cannot introduce a second runtime owner.
        "${AETHER_KRKRZ_CORE_ROOT}/common/tjs2"
        "${AETHER_KRKRZ_CORE_ROOT}/common/utils"
        "${AETHER_KRKRZ_CORE_ROOT}/common/base")
endif()

find_package(Boost COMPONENTS locale REQUIRED NO_MODULE)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(oniguruma CONFIG QUIET)
set(AETHER_ONIGURUMA_TARGET "")
if(TARGET oniguruma::onig)
    set(AETHER_ONIGURUMA_TARGET oniguruma::onig)
else()
    # Homebrew and several distro packages expose Oniguruma through headers
    # and pkg-config but do not ship a CMake config file. Keep the vcpkg target
    # preferred while providing the same ABI-compatible fallback for native
    # developer builds.
    find_path(AETHER_ONIGURUMA_INCLUDE_DIR NAMES oniguruma.h)
    find_library(AETHER_ONIGURUMA_LIBRARY NAMES onig oniguruma)
    if(NOT AETHER_ONIGURUMA_INCLUDE_DIR OR NOT AETHER_ONIGURUMA_LIBRARY)
        message(FATAL_ERROR
            "Oniguruma was not found. Install the vcpkg oniguruma package or "
            "provide oniguruma.h and libonig.")
    endif()
    add_library(aether_oniguruma UNKNOWN IMPORTED)
    set_target_properties(aether_oniguruma PROPERTIES
        IMPORTED_LOCATION "${AETHER_ONIGURUMA_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${AETHER_ONIGURUMA_INCLUDE_DIR}")
    set(AETHER_ONIGURUMA_TARGET aether_oniguruma)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC
    Boost::locale
    fmt::fmt
    spdlog::spdlog
    ${AETHER_ONIGURUMA_TARGET}
)
