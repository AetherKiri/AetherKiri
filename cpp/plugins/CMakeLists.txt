cmake_minimum_required(VERSION 3.19)
project(krkr2plugin)

include("${CMAKE_SOURCE_DIR}/cmake/AetherKrkrzIntegration.cmake")

# layerExSave keeps Aether's Layer/TJS wrapper and storage boundary, but its
# portable PNG and TLG5 codecs are compiled directly from the pinned upstream
# sources.  Do not copy these files into the parent repository.
aether_krkrz_require_source("layerExSave/LodePNG/lodepng.cpp")
aether_krkrz_require_source("layerExSave/LodePNG/lodepng.h")
aether_krkrz_require_source("layerExSave/tlg5/slide.cpp")
aether_krkrz_require_source("layerExSave/tlg5/slide.h")

# These low-risk leaf plugins have one implementation path: a small Aether
# ABI/module-name bridge that includes the business translation unit from the
# pinned krkrz_dev submodule. Historical local copies were deliberately
# removed so a stale build option cannot select a second implementation.
aether_krkrz_require_source("layerExAreaAverage/main.cpp")
aether_krkrz_require_source("layerExRaster/main.cpp")
aether_krkrz_require_source("layerExRaster/layerExBase.hpp")
aether_krkrz_require_source("layerExImage/layerExBase.hpp")
aether_krkrz_require_source("layerExImage/LayerExImage.cpp")
aether_krkrz_require_source("layerExImage/Main.cpp")
aether_krkrz_require_source("layerExLongExposure/Main.cpp")
aether_krkrz_require_source("getSample/main.cpp")
aether_krkrz_require_source("layerExBTOA/Main.cpp")
aether_krkrz_require_source("shrinkCopy/main.cpp")

# The extNagano sources are self-contained transition algorithms. The two
# rule-provider variants (3duniversal/imagewipe) require texture methods that
# are not present in Aether's scan-line ABI, so they remain on the Aether
# fallback provider; the other algorithms are detached and wrapped by
# upstream_bridge/extNaganoProviders.cpp.
set(AETHER_EXTNAGANO_UPSTREAM_SOURCES
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/blurfade.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/book.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/flutter.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/honeyturn.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/morphing.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/multiripple.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/rgbfade.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/scanline.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/spin.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/extNagano/zoomfade.cpp")
foreach(_aether_extnagano_source IN LISTS AETHER_EXTNAGANO_UPSTREAM_SOURCES)
    if(NOT EXISTS "${_aether_extnagano_source}")
        message(FATAL_ERROR
            "Selected extNagano upstream source is missing: "
            "${_aether_extnagano_source}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_aether_extnagano_source}")
endforeach()
set(AETHER_LAYEREX_AREA_AVERAGE_SOURCE upstream_bridge/layerExAreaAverage.cpp)
set(AETHER_LAYEREX_RASTER_SOURCE upstream_bridge/layerExRaster.cpp)
set(AETHER_LAYEREX_LONG_EXPOSURE_SOURCE
    upstream_bridge/layerExLongExposure.cpp)
set(AETHER_GET_SAMPLE_SOURCE upstream_bridge/getSample.cpp)
set(AETHER_LAYEREX_BTOA_SOURCE upstream_bridge/layerExBTOA.cpp)
set(AETHER_LAYEREX_IMAGE_SOURCE upstream_bridge/layerExImage.cpp)
set(AETHER_SHRINK_COPY_SOURCE upstream_bridge/shrinkCopy.cpp)

# ---------------------------------------------------------------------------
# SQLite owner and CLIP Studio (.clip) adapter
# ---------------------------------------------------------------------------
# clipfile requires SQLite's deserialize API. Instead of linking a private
# second SQLite next to Aether's historical 3.7 amalgamation, use one modern
# process-wide SQLite owner for the legacy TJS API, XP3 VFS, and CLIP parser.
# Prefer the pinned vcpkg package in normal builds (it is recorded in
# vcpkg.json).  A modern system SQLite is accepted for developer checkouts
# when it exposes the deserialize API, and a hash-verified 3.45.1 FetchContent
# fallback remains available when neither provider exists. The owner is
# configured unconditionally because the legacy sqlite3 plugin is part of the
# product even when CLIP is disabled.
set(AETHER_SQLITE_INCLUDE_DIR "")
find_package(unofficial-sqlite3 CONFIG QUIET)
if(TARGET unofficial::sqlite3::sqlite3)
    add_library(aether_sqlite3 INTERFACE)
    target_link_libraries(aether_sqlite3 INTERFACE unofficial::sqlite3::sqlite3)
    target_compile_definitions(aether_sqlite3 INTERFACE
        SQLITE_ENABLE_DESERIALIZE=1
        SQLITE_THREADSAFE=1)
    set(AETHER_SQLITE_PROVIDER "vcpkg")
else()
    # Homebrew and several Linux distributions do not provide a CMake config
    # file.  Probe their headers/libraries before downloading a second copy;
    # the header check deliberately rejects old SQLite versions without
    # sqlite3_deserialize().
    find_path(_aether_system_sqlite_include
        NAMES sqlite3.h
        HINTS "$ENV{SQLITE_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/sqlite"
              /opt/homebrew/opt/sqlite /usr/local/opt/sqlite /usr)
    find_library(_aether_system_sqlite_library
        NAMES sqlite3
        HINTS "$ENV{SQLITE_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/sqlite"
              /opt/homebrew/opt/sqlite /usr/local/opt/sqlite /usr)
    set(_aether_system_sqlite_has_deserialize FALSE)
    if(_aether_system_sqlite_include)
        file(READ "${_aether_system_sqlite_include}/sqlite3.h"
             _aether_sqlite_header_text)
        string(FIND "${_aether_sqlite_header_text}" "sqlite3_deserialize("
               _aether_deserialize_pos)
        if(NOT _aether_deserialize_pos EQUAL -1)
            set(_aether_system_sqlite_has_deserialize TRUE)
        endif()
    endif()
    if(_aether_system_sqlite_has_deserialize AND
       _aether_system_sqlite_library)
        add_library(aether_sqlite3 INTERFACE)
        target_include_directories(aether_sqlite3 INTERFACE
            "${_aether_system_sqlite_include}")
        target_link_libraries(aether_sqlite3 INTERFACE
            "${_aether_system_sqlite_library}")
        target_compile_definitions(aether_sqlite3 INTERFACE
            SQLITE_ENABLE_DESERIALIZE=1
            SQLITE_THREADSAFE=1)
        set(AETHER_SQLITE_INCLUDE_DIR "${_aether_system_sqlite_include}")
        set(AETHER_SQLITE_PROVIDER "system")
    else()
        include(FetchContent)
        FetchContent_Declare(aether_sqlite_source
            URL https://sqlite.org/2024/sqlite-amalgamation-3450100.zip
            URL_HASH SHA256=5592243caf28b2cdef41e6ab58d25d653dfc53deded8450eb66072c929f030c4
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_GetProperties(aether_sqlite_source)
        if(NOT aether_sqlite_source_POPULATED)
            # The SQLite amalgamation is a source archive without a
            # CMakeLists.txt.  MakeAvailable() would try to add the archive as
            # a subdirectory and fail before the owner target is created;
            # Populate() deliberately performs only the verified download and
            # extraction.  CMP0169 is scoped so projects embedding Aether do
            # not inherit a global policy change.
            cmake_policy(PUSH)
            if(POLICY CMP0169)
                cmake_policy(SET CMP0169 OLD)
            endif()
            FetchContent_Populate(aether_sqlite_source)
            cmake_policy(POP)
        endif()
        file(GLOB _aether_sqlite_sources
             "${aether_sqlite_source_SOURCE_DIR}/sqlite3.c"
             "${aether_sqlite_source_SOURCE_DIR}/*/sqlite3.c")
        file(GLOB _aether_sqlite_headers
             "${aether_sqlite_source_SOURCE_DIR}/sqlite3.h"
             "${aether_sqlite_source_SOURCE_DIR}/*/sqlite3.h")
        list(LENGTH _aether_sqlite_sources _aether_sqlite_source_count)
        list(LENGTH _aether_sqlite_headers _aether_sqlite_header_count)
        if(NOT _aether_sqlite_source_count EQUAL 1 OR
           NOT _aether_sqlite_header_count EQUAL 1)
            message(FATAL_ERROR
                "The pinned SQLite 3.45.1 fallback amalgamation was not found under "
                "${aether_sqlite_source_SOURCE_DIR}")
        endif()
        list(GET _aether_sqlite_sources 0 AETHER_SQLITE_SOURCE)
        list(GET _aether_sqlite_headers 0 AETHER_SQLITE_HEADER)
        get_filename_component(AETHER_SQLITE_DIR "${AETHER_SQLITE_SOURCE}" DIRECTORY)
        set(AETHER_SQLITE_INCLUDE_DIR "${AETHER_SQLITE_DIR}")

        add_library(aether_sqlite3 STATIC "${AETHER_SQLITE_SOURCE}")
        target_include_directories(aether_sqlite3 PUBLIC "${AETHER_SQLITE_DIR}")
        target_compile_definitions(aether_sqlite3 PUBLIC
            SQLITE_ENABLE_DESERIALIZE=1
            SQLITE_THREADSAFE=1)
        if(ANDROID)
            target_compile_definitions(aether_sqlite3 PRIVATE SQLITE_DISABLE_UMASK)
        endif()
        if(APPLE)
            target_compile_definitions(aether_sqlite3 PRIVATE SQLITE_WITHOUT_ZONEMALLOC)
        endif()
        set_property(SOURCE "${AETHER_SQLITE_SOURCE}" APPEND PROPERTY
            OBJECT_DEPENDS "${AETHER_SQLITE_HEADER}")
        set(AETHER_SQLITE_PROVIDER "pinned-fetch")
    endif()
endif()
message(STATUS "Aether SQLite owner: ${AETHER_SQLITE_PROVIDER}")

# ---------------------------------------------------------------------------
# krkr_richtext / Minikin hybrid adapter
# ---------------------------------------------------------------------------
# Richtext is a portable layout/rendering library, but its upstream plug-in
# CMake project also builds a private ICU/HarfBuzz stack and registers a second
# tp_stub.  Reuse only the source libraries here and bind them to the
# process-wide Aether FreeType/ICU/HarfBuzz owners.  The existing Aether text
# renderer remains the compatibility path for games that do not use the
# RichText classes; no runtime switch or duplicate registry is introduced.
set(AETHER_RICHTEXT_ENABLED OFF)
set(AETHER_RICHTEXT_BRIDGE_SOURCES)
if(AETHERKIRI_ENABLE_RICHTEXT AND NOT WEB AND NOT EMSCRIPTEN AND NOT IOS)
    set(AETHER_RICHTEXT_ROOT "${AETHER_KRKRZ_PLUGIN_ROOT}/krkr_richtext")
    set(AETHER_RICHTEXT_LIB_ROOT "${AETHER_RICHTEXT_ROOT}/richtext")
    set(AETHER_RICHTEXT_MINIKIN_ROOT
        "${AETHER_RICHTEXT_LIB_ROOT}/ext/minikin")

    foreach(_aether_richtext_required
            "krkr_richtext/src/main.cpp"
            "krkr_richtext/src/HostFontBackend.cpp"
            "krkr_richtext/src/HostFontBackend.hpp"
            "krkr_richtext/src/EscapeConverter.cpp"
            "krkr_richtext/src/EscapeConverter.hpp"
            "krkr_richtext/src/LicensesGen.cpp"
            "krkr_richtext/richtext/CMakeLists.txt"
            "krkr_richtext/richtext/include/richtext.hpp"
            "krkr_richtext/richtext/include/richtext/FontBackend.hpp"
            "krkr_richtext/richtext/ext/minikin/CMakeLists.txt"
            "krkr_richtext/richtext/ext/minikin/libs/_stub.h")
        aether_krkrz_require_source("${_aether_richtext_required}")
    endforeach()

    # Keep the source list explicit at the library boundary.  A configure
    # dependency is attached to every selected file so a submodule update
    # cannot leave a stale object in an incremental build.
    set(_aether_richtext_sources
        "FontFace.cpp"
        "FontManager.cpp"
        "GlyphwareBackend.cpp"
        "Raster.cpp"
        "TextStyle.cpp"
        "Appearance.cpp"
        "TextLayout.cpp"
        "ParagraphLayout.cpp"
        "GlyphRenderer.cpp"
        "TextRenderer.cpp"
        "StyledLayout.cpp"
        "TagParser.cpp"
        "TextureAtlas.cpp"
        "TimingInfo.cpp")
    set(_aether_richtext_minikin_sources)
    file(GLOB _aether_richtext_minikin_sources CONFIGURE_DEPENDS
         "${AETHER_RICHTEXT_MINIKIN_ROOT}/libs/minikin/*.cpp")
    if(NOT _aether_richtext_minikin_sources)
        message(FATAL_ERROR
            "krkrz richtext Minikin sources are missing under ${AETHER_RICHTEXT_MINIKIN_ROOT}")
    endif()
    foreach(_aether_richtext_source IN LISTS _aether_richtext_sources)
        aether_krkrz_require_source(
            "krkr_richtext/richtext/src/${_aether_richtext_source}")
    endforeach()
    foreach(_aether_minikin_source IN LISTS _aether_richtext_minikin_sources)
        file(RELATIVE_PATH _aether_minikin_relative
             "${AETHER_KRKRZ_PLUGIN_ROOT}" "${_aether_minikin_source}")
        aether_krkrz_require_source("${_aether_minikin_relative}")
    endforeach()

    # Resolve the three external owners without invoking the upstream
    # FetchContent projects.  vcpkg targets are preferred; pkg-config is a
    # host fallback for Homebrew/Linux developer checkouts.
    find_package(Freetype QUIET)
    set(_aether_richtext_freetype_target "")
    if(TARGET Freetype::Freetype)
        set(_aether_richtext_freetype_target Freetype::Freetype)
    elseif(TARGET freetype)
        set(_aether_richtext_freetype_target freetype)
    endif()

    set(_aether_richtext_harfbuzz_target "")
    find_package(harfbuzz CONFIG QUIET)
    foreach(_aether_hb_candidate harfbuzz::harfbuzz HarfBuzz::HarfBuzz harfbuzz)
        if(NOT _aether_richtext_harfbuzz_target AND
           TARGET ${_aether_hb_candidate})
            set(_aether_richtext_harfbuzz_target ${_aether_hb_candidate})
        endif()
    endforeach()
    find_path(AETHER_RICHTEXT_HARFBUZZ_INCLUDE_DIR
        NAMES hb.h PATH_SUFFIXES harfbuzz)
    find_library(AETHER_RICHTEXT_HARFBUZZ_LIBRARY NAMES harfbuzz)
    if(NOT _aether_richtext_harfbuzz_target AND
       AETHER_RICHTEXT_HARFBUZZ_LIBRARY)
        set(_aether_richtext_harfbuzz_target
            "${AETHER_RICHTEXT_HARFBUZZ_LIBRARY}")
    endif()

    set(_aether_richtext_icu_targets)
    find_package(ICU QUIET COMPONENTS uc i18n)
    foreach(_aether_icu_candidate ICU::uc ICU::i18n ICU::data)
        if(TARGET ${_aether_icu_candidate})
            list(APPEND _aether_richtext_icu_targets
                 ${_aether_icu_candidate})
        endif()
    endforeach()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        if(NOT TARGET ICU::uc)
            pkg_check_modules(AETHER_RICHTEXT_ICUUC QUIET IMPORTED_TARGET
                              icu-uc)
            if(TARGET PkgConfig::AETHER_RICHTEXT_ICUUC)
                list(APPEND _aether_richtext_icu_targets
                     PkgConfig::AETHER_RICHTEXT_ICUUC)
            endif()
        endif()
        if(NOT TARGET ICU::i18n)
            pkg_check_modules(AETHER_RICHTEXT_ICUI18N QUIET IMPORTED_TARGET
                              icu-i18n)
            if(TARGET PkgConfig::AETHER_RICHTEXT_ICUI18N)
                list(APPEND _aether_richtext_icu_targets
                     PkgConfig::AETHER_RICHTEXT_ICUI18N)
            endif()
        endif()
    endif()
    list(REMOVE_DUPLICATES _aether_richtext_icu_targets)

    if(_aether_richtext_freetype_target AND
       _aether_richtext_harfbuzz_target AND
       _aether_richtext_icu_targets)
        add_library(aether_krkrz_richtext_freetype INTERFACE)
        target_link_libraries(aether_krkrz_richtext_freetype INTERFACE
                              ${_aether_richtext_freetype_target})

        add_library(aether_krkrz_richtext_harfbuzz INTERFACE)
        target_link_libraries(aether_krkrz_richtext_harfbuzz INTERFACE
                              ${_aether_richtext_harfbuzz_target})
        if(AETHER_RICHTEXT_HARFBUZZ_INCLUDE_DIR)
            target_include_directories(aether_krkrz_richtext_harfbuzz INTERFACE
                "${AETHER_RICHTEXT_HARFBUZZ_INCLUDE_DIR}")
        endif()

        add_library(aether_krkrz_richtext_icu INTERFACE)
        target_link_libraries(aether_krkrz_richtext_icu INTERFACE
                              ${_aether_richtext_icu_targets})

        add_library(aether_krkrz_minikin STATIC
            ${_aether_richtext_minikin_sources})
        target_compile_features(aether_krkrz_minikin PUBLIC cxx_std_17)
        target_compile_definitions(aether_krkrz_minikin PUBLIC
            WAMSOFT_MODIFIED)
        target_include_directories(aether_krkrz_minikin PUBLIC
            "${AETHER_RICHTEXT_MINIKIN_ROOT}/include"
            "${AETHER_RICHTEXT_MINIKIN_ROOT}/libs")
        if(MSVC)
            target_compile_options(aether_krkrz_minikin PRIVATE
                "/FI${AETHER_RICHTEXT_MINIKIN_ROOT}/libs/_stub.h")
        else()
            target_compile_options(aether_krkrz_minikin PRIVATE
                -include "${AETHER_RICHTEXT_MINIKIN_ROOT}/libs/_stub.h")
        endif()
        target_link_libraries(aether_krkrz_minikin PUBLIC
            aether_krkrz_richtext_freetype
            aether_krkrz_richtext_icu
            aether_krkrz_richtext_harfbuzz)

        set(_aether_richtext_library_sources)
        foreach(_aether_richtext_source IN LISTS _aether_richtext_sources)
            list(APPEND _aether_richtext_library_sources
                 "${AETHER_RICHTEXT_LIB_ROOT}/src/${_aether_richtext_source}")
        endforeach()
        add_library(aether_krkrz_richtext_lib STATIC
            ${_aether_richtext_library_sources})
        target_compile_features(aether_krkrz_richtext_lib PUBLIC cxx_std_17)
        target_include_directories(aether_krkrz_richtext_lib PUBLIC
            "${AETHER_RICHTEXT_LIB_ROOT}/include"
            "${AETHER_RICHTEXT_MINIKIN_ROOT}/include"
            "${AETHER_RICHTEXT_MINIKIN_ROOT}/libs")
        target_link_libraries(aether_krkrz_richtext_lib PUBLIC
            aether_krkrz_minikin)

        set(AETHER_RICHTEXT_BRIDGE_SOURCES
            upstream_bridge/krkrz_richtext_main.cpp
            upstream_bridge/krkrz_richtext_host_font.cpp
            upstream_bridge/krkrz_richtext_escape.cpp
            upstream_bridge/krkrz_richtext_licenses.cpp)
        aether_krkrz_track_source(
            "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/krkrz_richtext_main.cpp"
            "krkr_richtext/src/main.cpp")
        aether_krkrz_track_source(
            "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/krkrz_richtext_host_font.cpp"
            "krkr_richtext/src/HostFontBackend.cpp")
        aether_krkrz_track_source(
            "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/krkrz_richtext_escape.cpp"
            "krkr_richtext/src/EscapeConverter.cpp")
        aether_krkrz_track_source(
            "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/krkrz_richtext_licenses.cpp"
            "krkr_richtext/src/LicensesGen.cpp")
        set(AETHER_RICHTEXT_ENABLED ON)
        message(STATUS
            "krkrz richtext adapter: enabled (Aether FontService + shared ICU/HarfBuzz)")
    else()
        # Richtext is an additive provider.  A host without its native
        # shaping stack must keep the existing TextRenderBase path usable;
        # fail-open here instead of making an otherwise valid Aether build
        # impossible to configure.  CI's vcpkg profile supplies all three
        # owners, so the adapter remains enabled in the supported product
        # builds and this branch is an explicit, observable fallback.
        message(WARNING
            "krkrz richtext adapter unavailable (need FreeType, HarfBuzz and "
            "ICU); keeping Aether TextRenderBase compatibility path")
    endif()
elseif(AETHERKIRI_ENABLE_RICHTEXT)
    message(STATUS
        "krkrz richtext adapter: disabled for web/emscripten/iOS target")
endif()

# clipfile is the previously optional plugin whose portable business
# implementation is complete enough to ship. Compile clipparse directly from
# the nested submodule and keep only the stream/storage/registration bridge in
# Aether.
set(AETHER_CLIPFILE_ENABLED OFF)
if(AETHERKIRI_ENABLE_CLIPFILE AND NOT WEB AND NOT EMSCRIPTEN)
    set(AETHER_CLIPFILE_ROOT "${AETHER_KRKRZ_PLUGIN_ROOT}/clipfile")
    set(AETHER_CLIPPARSE_ROOT
        "${AETHER_CLIPFILE_ROOT}/external/clipparse/clipparse")
    foreach(_aether_clipfile_source
            "clipfile/clipclass.cpp"
            "clipfile/clipclass.h"
            "clipfile/clipwriter.cpp"
            "clipfile/main.cpp"
            "clipfile/external/clipparse/clipparse/clipbase.h"
            "clipfile/external/clipparse/clipparse/clipfile.h"
            "clipfile/external/clipparse/clipparse/clipfile.cpp"
            "clipfile/external/clipparse/clipparse/clipcomposite.cpp"
            "clipfile/external/clipparse/clipparse/clipencode.h"
            "clipfile/external/clipparse/clipparse/clipencode.cpp"
            "clipfile/external/clipparse/clipparse/clipwrite.h"
            "clipfile/external/clipparse/clipparse/clipwrite.cpp"
            "clipfile/external/clipparse/clipparse/clipvalidate.cpp")
        aether_krkrz_require_source("${_aether_clipfile_source}")
    endforeach()

    find_package(ZLIB REQUIRED)
    add_library(aether_krkrz_clipparse STATIC
        "${AETHER_CLIPPARSE_ROOT}/clipfile.cpp"
        "${AETHER_CLIPPARSE_ROOT}/clipcomposite.cpp"
        "${AETHER_CLIPPARSE_ROOT}/clipencode.cpp"
        "${AETHER_CLIPPARSE_ROOT}/clipwrite.cpp"
        "${AETHER_CLIPPARSE_ROOT}/clipvalidate.cpp")
    target_compile_features(aether_krkrz_clipparse PUBLIC cxx_std_17)
    target_include_directories(aether_krkrz_clipparse PUBLIC
        "${AETHER_CLIPFILE_ROOT}/external/clipparse"
        "${AETHER_CLIPPARSE_ROOT}")
    target_link_libraries(aether_krkrz_clipparse PUBLIC
        aether_sqlite3
        ZLIB::ZLIB)

    set(AETHER_CLIPFILE_ENABLED ON)
elseif(AETHERKIRI_ENABLE_CLIPFILE)
    message(STATUS
        "krkrz clipfile adapter: disabled for web/emscripten target")
endif()

if(AETHER_USE_KRKRZ_OPTIONAL_PLUGINS)
    if(NOT EXISTS "${AETHER_KRKRZ_DEV_ROOT}/.git" AND
       NOT EXISTS "${AETHER_KRKRZ_DEV_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "AETHER_USE_KRKRZ_OPTIONAL_PLUGINS requires an initialized "
            "krkrz_dev checkout at ${AETHER_KRKRZ_DEV_ROOT}")
    endif()
    # The CLIP parser/writer and (on supported native hosts) richtext adapter
    # are linked above.  The remaining projects pull in platform/rendering SDKs
    # and old registration contracts; they remain source-validated until
    # dedicated adapters and tests exist.
    foreach(_aether_krkrz_optional_source
            "layerExVector/main.cpp"
            "krkreffekseer/src/Effect.cpp"
            "krkrthreepp/src/main.cpp")
        aether_krkrz_require_source("${_aether_krkrz_optional_source}")
    endforeach()
    message(STATUS
        "krkrz_dev optional plugin sources validated; "
        "layerExVector uses the Aether LayerExDraw compatibility bridge; "
        "richtext is enabled where its native dependencies are available")
endif()

add_library(${PROJECT_NAME} STATIC)

# Keep the include scanner honest when a pinned upstream translation unit
# changes in place. The bridge remains the only product source; the object
# dependency merely triggers a rebuild.
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_AREA_AVERAGE_SOURCE}"
    "layerExAreaAverage/main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_RASTER_SOURCE}"
    "layerExRaster/main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_LONG_EXPOSURE_SOURCE}"
    "layerExLongExposure/Main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_GET_SAMPLE_SOURCE}"
    "getSample/main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_BTOA_SOURCE}"
    "layerExBTOA/Main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_IMAGE_SOURCE}"
    "layerExImage/Main.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_LAYEREX_IMAGE_SOURCE}"
    "layerExImage/LayerExImage.cpp")
aether_krkrz_track_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/${AETHER_SHRINK_COPY_SOURCE}"
    "shrinkCopy/main.cpp")

target_sources(${PROJECT_NAME} PRIVATE
    scriptsEx.cpp
    kbadDataPack.cpp
    binaryStream.cpp
    base64.cpp
    encode.cpp
    expat.cpp
    imagesaver.cpp
    lineParser.cpp
    memfile.cpp
    json/jsonPlugin.cpp
    tomlPlugin.cpp
    minizip.cpp
    qrcode.cpp
    qrcode/QR_Encode.cpp
    sqlitePlugin.cpp
    sqliteXp3Vfs.cpp
    portableResourceBundle.cpp
    portableResourceObjects.cpp
    portableRegistry.cpp
    portableLayerEffects.cpp
    win32dialog.cpp
    dirlist.cpp
    csvParser.cpp
    layerExMovie.cpp
    varfile.cpp
    saveStruct.cpp
    getabout.cpp
    addFont.cpp
    wutcwf.cpp
    windowEx.cpp
    menuExt.cpp
    ${AETHER_GET_SAMPLE_SOURCE}
    kirikiroid2.cpp
    layerExPerspective.cpp
    fftgraph.cpp
    LayerExBase.cpp
    layerExSaveCompat.cpp
    upstream_bridge/layerExSaveCodecs.cpp
    "${AETHER_KRKRZ_PLUGIN_ROOT}/layerExSave/LodePNG/lodepng.cpp"
    "${AETHER_KRKRZ_PLUGIN_ROOT}/layerExSave/tlg5/slide.cpp"
    xp3filter.cpp
    ${AETHER_SHRINK_COPY_SOURCE}
    extrans.cpp
    extrans_precise/mosaic.cpp
    extrans_precise/ripple.cpp
    extrans_precise/rotatebase.cpp
    extrans_precise/rotatetrans.cpp
    extrans_precise/turn.cpp
    extrans_precise/turntrans_table.cpp
    extrans_precise/wave.cpp
    extNagano.cpp
    multiimage.cpp
    alphamovie.cpp
    ${AETHER_LAYEREX_BTOA_SOURCE}
    ${AETHER_LAYEREX_IMAGE_SOURCE}
    ${AETHER_LAYEREX_RASTER_SOURCE}
    ${AETHER_LAYEREX_AREA_AVERAGE_SOURCE}
    ${AETHER_LAYEREX_LONG_EXPOSURE_SOURCE}
    wfBasicEffectCompat.cpp
    wfTypicalDSPCompat.cpp
    packinone.cpp
    clipboardEx.cpp
    compatLegacyPlugins.cpp
    compatMediaLayerPlugins.cpp
    portablePreRenderedFont.cpp
    portableHttpServer.cpp
    portableWindowMessaging.cpp
    portableSignatureCheck.cpp
    krkrzLayerExVectorCompat.cpp
    compatSystemPlugins.cpp
    stubs/dummy_plugin_stubs.cpp
    pluginAnchors.cpp
    fstat/main.cpp
    ${AETHER_RICHTEXT_BRIDGE_SOURCES}
)

if(AETHER_RICHTEXT_ENABLED)
    target_link_libraries(${PROJECT_NAME} PRIVATE aether_krkrz_richtext_lib)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        AETHERKIRI_RICHTEXT_ENABLED=1)
endif()

if(AETHER_CLIPFILE_ENABLED)
    aether_krkrz_track_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/clipfile_clipclass.cpp"
        "clipfile/clipclass.cpp")
    aether_krkrz_track_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/clipfile_clipwriter.cpp"
        "clipfile/clipwriter.cpp")
    aether_krkrz_track_source(
        "${CMAKE_CURRENT_SOURCE_DIR}/upstream_bridge/clipfile_main.cpp"
        "clipfile/main.cpp")
    target_sources(${PROJECT_NAME} PRIVATE
        upstream_bridge/clipfile_clipclass.cpp
        upstream_bridge/clipfile_clipwriter.cpp
        upstream_bridge/clipfile_main.cpp)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${AETHER_CLIPFILE_ROOT}"
        "${AETHER_CLIPPARSE_ROOT}")
    target_link_libraries(${PROJECT_NAME} PRIVATE aether_krkrz_clipparse)
    message(STATUS
        "krkrz clipfile adapter: enabled (shared SQLite owner + upstream clipparse)")
endif()

target_sources(${PROJECT_NAME} PRIVATE
    upstream_bridge/extNaganoProviders.cpp
    ${AETHER_EXTNAGANO_UPSTREAM_SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    AETHER_KRKRZ_EXTNAGANO_UPSTREAM=1)
# Aether's local tp_stub intentionally omits the generated calling-convention
# macro. The upstream files expect it to expand to an empty token on
# non-Windows hosts; apply that definition only to these selected sources.
set_source_files_properties(${AETHER_EXTNAGANO_UPSTREAM_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS "TJS_INTF_METHOD=")

if(BUILD_GPU_BRIDGE)
    target_sources(${PROJECT_NAME} PRIVATE krkrgles.cpp)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${KRKR2CORE_PATH}/
    ${KRKR2CORE_PATH}/base
    ${KRKR2CORE_PATH}/base/win32
    ${KRKR2CORE_PATH}/environ
    ${KRKR2CORE_PATH}/environ/win32
    ${KRKR2CORE_PATH}/environ/android
    ${KRKR2CORE_PATH}/environ/sdl
    ${KRKR2CORE_PATH}/msg
    ${KRKR2CORE_PATH}/msg/win32
    ${KRKR2CORE_PATH}/extension
    ${KRKR2CORE_PATH}/sound
    ${KRKR2CORE_PATH}/sound/win32
    ${KRKR2CORE_PATH}/tjs2
    ${KRKR2CORE_PATH}/utils
    ${KRKR2CORE_PATH}/utils/win32
    ${KRKR2CORE_PATH}/visual
    ${KRKR2CORE_PATH}/visual/ARM
    ${KRKR2CORE_PATH}/visual/win32
    ${KRKR2CORE_PATH}/visual/RenderScript/rs
)

add_definitions(-D__STDC_CONSTANT_MACROS -DTJS_TEXT_OUT_CRLF)

if(ANDROID)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SQLITE_DISABLE_UMASK)
endif()

if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SQLITE_WITHOUT_ZONEMALLOC)
endif()

# The public target only owns the script-compatible fallback.  The optional
# AetherInternal package adds the Cubism SDK and the native Live2D loaders and
# renderers to this target without replacing the rest of krkr2plugin.
if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_live2d)
    aetherinternal_extend_live2d(${PROJECT_NAME})
else()
    target_sources(${PROJECT_NAME} PRIVATE krkrlive2d_stub.cpp)
endif()
if(AETHERKIRI_INTERNAL_ENABLED AND
   COMMAND aetherinternal_extend_legacy_plugins)
    aetherinternal_extend_legacy_plugins(${PROJECT_NAME})
endif()

if(AETHERKIRI_INTERNAL_ENABLED AND COMMAND aetherinternal_extend_krkr2plugin)
    aetherinternal_extend_krkr2plugin(${PROJECT_NAME})
else()
    target_sources(${PROJECT_NAME} PRIVATE
        tjsNs0DataPack.cpp
        textrender.cpp
        drawDeviceD2DCompat.cpp
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${KRKR2PLUGIN_PATH} ${KRKR2PLUGIN_PATH}/simplebinder)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${AETHER_KRKRZ_PLUGIN_ROOT}/layerExSave")
# Wrappers include upstream translation units by plugin-relative path so an
# alternate pinned checkout can be supplied with -DAETHER_KRKRZ_DEV_ROOT=...
# without editing source files.
target_include_directories(${PROJECT_NAME} PRIVATE
    "${AETHER_KRKRZ_PLUGIN_ROOT}")
target_link_libraries(${PROJECT_NAME} PUBLIC krkr2core)

add_subdirectory(psdfile)
add_subdirectory(psbfile)
if(NOT WEB AND NOT EMSCRIPTEN)
    add_subdirectory(layerex_draw)
endif()
add_subdirectory(motionplayer)
add_subdirectory(kagparserex)
add_subdirectory(extkagparser)

#add_subdirectory(json)
#add_subdirectory(steam)
#add_subdirectory(DrawDeviceForSteam)
target_link_libraries(${PROJECT_NAME} PUBLIC
        psdfile
        psbfile
        motionplayer
        kagparserex
        extkagparser
#        json steam DrawDeviceForSteam
)
if(NOT WEB AND NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PUBLIC layerExDraw)
endif()

find_package(fmt CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)

# httprequest/xmlhttprequest use the normalized libcurl owner when one is
# available. The source keeps local Storage requests and an explicit
# fail-closed network path for minimal/web builds where curl is absent.
if(DEFINED AETHERKIRI_CURL_TARGET AND AETHERKIRI_CURL_TARGET AND
   TARGET "${AETHERKIRI_CURL_TARGET}")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${AETHERKIRI_CURL_TARGET}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE AETHERKIRI_HAS_CURL=1)
endif()

# sigcheck.dll uses the same SHA-256/RSA-PSS wire format as krkrz.  OpenSSL is
# already a transitive dependency of the normal curl profile, but resolve it
# explicitly here so the feature is available to embedders that provide a
# different HTTP transport.  The source keeps a strict fail-closed path when
# no crypto provider is present (not a fabricated verification result).
set(AETHERKIRI_OPENSSL_CRYPTO_TARGET "")
find_package(OpenSSL CONFIG QUIET COMPONENTS Crypto)
if(TARGET OpenSSL::Crypto)
    set(AETHERKIRI_OPENSSL_CRYPTO_TARGET OpenSSL::Crypto)
endif()
if(NOT AETHERKIRI_OPENSSL_CRYPTO_TARGET)
    find_package(OpenSSL QUIET COMPONENTS Crypto)
    if(TARGET OpenSSL::Crypto)
        set(AETHERKIRI_OPENSSL_CRYPTO_TARGET OpenSSL::Crypto)
    endif()
endif()
if(AETHERKIRI_OPENSSL_CRYPTO_TARGET)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${AETHERKIRI_OPENSSL_CRYPTO_TARGET})
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        AETHERKIRI_HAS_OPENSSL=1)
    message(STATUS "sigcheck adapter: OpenSSL RSA-PSS verifier enabled")
else()
    message(STATUS
        "sigcheck adapter: crypto provider unavailable; verification fails closed")
endif()

find_package(expat CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE expat::expat)

find_package(ZLIB REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)

# sqlite3.dll, sqlite3_xp3_vfs.dll and clipfile share one modern SQLite
# implementation. Keeping one owner avoids duplicate sqlite globals while
# preserving the legacy TJS API.
target_link_libraries(${PROJECT_NAME} PRIVATE aether_sqlite3)

# The legacy TJS minizip plugin includes the compatibility headers directly,
# so the normalized provider must be on this target as well as on the core
# archive libraries.  Keeping it as an interface dependency propagates the
# build-tree header shim without introducing another ZIP implementation.
target_link_libraries(${PROJECT_NAME} PRIVATE ${AETHERKIRI_MINIZIP_TARGET})
