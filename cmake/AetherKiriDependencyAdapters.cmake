# Host dependency adapters used by the Aether build.
#
# The product normally resolves these libraries from the pinned vcpkg
# manifest.  Developer machines (and downstream embedders) often have a
# system package instead, with a different CMake package name or no package
# file at all.  These helpers normalize only the target boundary; they do not
# vendor a second copy of a dependency or change the runtime owner.

include_guard(GLOBAL)

function(_aetherkiri_native_host_fallback_allowed out_var)
    # CONFIG packages are resolved through the active toolchain and are safe
    # for every target.  Raw Homebrew/pkg-config probes are only valid for a
    # native host build; using them while cross-compiling would silently link
    # macOS libraries into Android/iOS/Web artifacts.
    if(CMAKE_CROSSCOMPILING)
        set(${out_var} FALSE PARENT_SCOPE)
    else()
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(_aetherkiri_make_imported_library target include_dir library)
    if(TARGET "${target}")
        return()
    endif()
    add_library("${target}" UNKNOWN IMPORTED GLOBAL)
    set_target_properties("${target}" PROPERTIES
        IMPORTED_LOCATION "${library}"
        INTERFACE_INCLUDE_DIRECTORIES "${include_dir}")
endfunction()

function(aetherkiri_prepare_minizip)
    if(DEFINED AETHERKIRI_MINIZIP_TARGET AND
       TARGET "${AETHERKIRI_MINIZIP_TARGET}")
        return()
    endif()

    # vcpkg's legacy compatibility package is the preferred provider for the
    # Aether archive code.  krkrz's own plugin uses the modern MINIZIP target;
    # both expose the same compatibility API, so normalize it below.
    find_package(unofficial-minizip CONFIG QUIET)
    if(TARGET unofficial::minizip::minizip)
        set(AETHERKIRI_MINIZIP_TARGET unofficial::minizip::minizip
            CACHE INTERNAL "Normalized minizip target")
        set(AETHERKIRI_MINIZIP_INCLUDE_DIR "" CACHE INTERNAL
            "Normalized minizip compatibility include directory")
        return()
    endif()

    find_package(minizip-ng CONFIG QUIET)
    set(_aether_minizip_target "")
    foreach(_candidate MINIZIP::minizip-ng minizip-ng minizip)
        if(NOT _aether_minizip_target AND TARGET "${_candidate}")
            set(_aether_minizip_target "${_candidate}")
        endif()
    endforeach()

    _aetherkiri_native_host_fallback_allowed(
        _aether_minizip_allow_host_fallback)
    if(NOT _aether_minizip_target AND
       _aether_minizip_allow_host_fallback)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(AETHER_MINIZIP_PC QUIET IMPORTED_TARGET
                              minizip-ng)
            if(TARGET PkgConfig::AETHER_MINIZIP_PC)
                set(_aether_minizip_target PkgConfig::AETHER_MINIZIP_PC)
            endif()
        endif()
    endif()

    if(NOT _aether_minizip_target AND
       _aether_minizip_allow_host_fallback)
        find_path(_aether_minizip_system_include
            NAMES mz.h minizip-ng/mz.h minizip/mz.h
            HINTS "$ENV{MINIZIP_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/minizip-ng"
                  /opt/homebrew/opt/minizip-ng /usr/local/opt/minizip-ng
            PATH_SUFFIXES include include/minizip-ng)
        find_library(_aether_minizip_library
            NAMES minizip-ng minizip
            HINTS "$ENV{MINIZIP_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/minizip-ng"
                  /opt/homebrew/opt/minizip-ng /usr/local/opt/minizip-ng
            PATH_SUFFIXES lib)
        if(_aether_minizip_system_include AND _aether_minizip_library)
            _aetherkiri_make_imported_library(aether_minizip_system
                "${_aether_minizip_system_include}" "${_aether_minizip_library}")
            set(_aether_minizip_target aether_minizip_system)
        endif()
    endif()

    if(NOT _aether_minizip_target)
        set(AETHERKIRI_MINIZIP_TARGET "" CACHE INTERNAL
            "Normalized minizip target")
        set(AETHERKIRI_MINIZIP_INCLUDE_DIR "" CACHE INTERNAL
            "Normalized minizip compatibility include directory")
        message(FATAL_ERROR
            "A compatible minizip provider is required. Install the vcpkg "
            "unofficial-minizip package or minizip-ng (with pkg-config).")
    endif()

    # Aether's historical archive sources include <minizip/*.h>, while modern
    # minizip-ng installs those headers below <minizip-ng/>.  Generate a tiny
    # build-tree compatibility directory rather than copying headers into the
    # repository.  The upstream plugin keeps using its native <mz_*.h> path.
    set(_aether_minizip_target_include "")
    set(_aether_minizip_header_prefix "")
    if(TARGET "${_aether_minizip_target}")
        get_target_property(_aether_minizip_includes
            "${_aether_minizip_target}" INTERFACE_INCLUDE_DIRECTORIES)
        foreach(_include IN LISTS _aether_minizip_includes)
            if(_include AND EXISTS "${_include}/minizip-ng/mz.h")
                set(_aether_minizip_target_include "${_include}")
                set(_aether_minizip_header_prefix "minizip-ng/")
                break()
            elseif(_include AND EXISTS "${_include}/mz.h")
                set(_aether_minizip_target_include "${_include}")
                set(_aether_minizip_header_prefix "")
                break()
            endif()
        endforeach()
    endif()
    if(NOT _aether_minizip_target_include AND
       DEFINED _aether_minizip_system_include)
        set(_aether_minizip_target_include
            "${_aether_minizip_system_include}")
        if(EXISTS "${_aether_minizip_system_include}/minizip-ng/mz.h")
            set(_aether_minizip_header_prefix "minizip-ng/")
        endif()
    endif()

    set(_aether_minizip_compat_dir
        "${CMAKE_BINARY_DIR}/generated/minizip-compat")
    file(MAKE_DIRECTORY "${_aether_minizip_compat_dir}/minizip")
    foreach(_header ioapi.h zip.h unzip.h)
        if(_aether_minizip_target_include AND
           EXISTS "${_aether_minizip_target_include}/${_aether_minizip_header_prefix}${_header}")
            if(_header STREQUAL "ioapi.h")
                # The modern header intentionally leaves zlib and the old
                # zlib_filefunc64_32_def compatibility struct to callers.
                # Aether's historical ZIP reader uses that private struct,
                # so provide it in the generated (never committed) shim.
                file(WRITE "${_aether_minizip_compat_dir}/minizip/${_header}"
                     "#pragma once\n"
                     "#include <zlib.h>\n"
                     "#include <${_aether_minizip_header_prefix}${_header}>\n"
                     "#ifndef AETHER_ZLIB_FILEFUNC64_32_COMPAT\n"
                     "#define AETHER_ZLIB_FILEFUNC64_32_COMPAT 1\n"
                     "typedef struct zlib_filefunc64_32_def_s {\n"
                     "    zlib_filefunc64_def zfile_func64;\n"
                     "    open_file_func zopen32_file;\n"
                     "    tell_file_func ztell32_file;\n"
                     "    seek_file_func zseek32_file;\n"
                     "} zlib_filefunc64_32_def;\n"
                     "#endif\n")
            else()
                file(WRITE "${_aether_minizip_compat_dir}/minizip/${_header}"
                     "#pragma once\n#include <${_aether_minizip_header_prefix}${_header}>\n")
            endif()
        endif()
    endforeach()

    add_library(aether_minizip INTERFACE)
    target_link_libraries(aether_minizip INTERFACE "${_aether_minizip_target}")
    if(_aether_minizip_target_include)
        target_include_directories(aether_minizip INTERFACE
            "${_aether_minizip_target_include}")
    endif()
    if(EXISTS "${_aether_minizip_compat_dir}/minizip/ioapi.h")
        target_include_directories(aether_minizip INTERFACE
            "${_aether_minizip_compat_dir}")
    endif()

    set(AETHERKIRI_MINIZIP_TARGET aether_minizip CACHE INTERNAL
        "Normalized minizip target")
    set(AETHERKIRI_MINIZIP_INCLUDE_DIR "${_aether_minizip_compat_dir}"
        CACHE INTERNAL "Normalized minizip compatibility include directory")
    message(STATUS "Aether minizip provider: ${_aether_minizip_target}")
endfunction()

function(_aetherkiri_alias_interface alias_name target_name)
    if(NOT TARGET "${alias_name}" AND TARGET "${target_name}")
        add_library("${alias_name}" ALIAS "${target_name}")
    endif()
endfunction()

function(_aetherkiri_make_pkg_interface target_name pkg_name)
    if(TARGET "${target_name}")
        return()
    endif()
    if(NOT PkgConfig_FOUND)
        return()
    endif()
    string(MAKE_C_IDENTIFIER "${target_name}" _aether_pkg_id)
    pkg_check_modules(${_aether_pkg_id} QUIET IMPORTED_TARGET "${pkg_name}")
    if(TARGET PkgConfig::${_aether_pkg_id})
        add_library("${target_name}" INTERFACE)
        target_link_libraries("${target_name}" INTERFACE
            PkgConfig::${_aether_pkg_id})
    endif()
endfunction()

function(_aetherkiri_make_host_library target_name header_name library_name
                                         formula_name)
    if(TARGET "${target_name}" OR CMAKE_CROSSCOMPILING)
        return()
    endif()
    unset(_aether_host_include CACHE)
    unset(_aether_host_library CACHE)
    unset(_aether_host_include)
    unset(_aether_host_library)
    find_path(_aether_host_include
        NAMES "${header_name}"
        HINTS "$ENV{${formula_name}_ROOT}"
              "$ENV{HOMEBREW_PREFIX}/opt/${formula_name}"
              "/opt/homebrew/opt/${formula_name}"
              "/usr/local/opt/${formula_name}"
        PATH_SUFFIXES include include/opus)
    find_library(_aether_host_library
        NAMES "${library_name}" "lib${library_name}"
        HINTS "$ENV{${formula_name}_ROOT}"
              "$ENV{HOMEBREW_PREFIX}/opt/${formula_name}"
              "/opt/homebrew/opt/${formula_name}"
              "/usr/local/opt/${formula_name}"
        PATH_SUFFIXES lib)
    if(_aether_host_include AND _aether_host_library)
        _aetherkiri_make_imported_library("${target_name}"
            "${_aether_host_include}" "${_aether_host_library}")
    endif()
endfunction()

function(_aetherkiri_add_homebrew_pkgconfig)
    if(CMAKE_CROSSCOMPILING)
        return()
    endif()
    set(_aether_pc_paths)
    foreach(_prefix
            "$ENV{HOMEBREW_PREFIX}"
            /opt/homebrew
            /usr/local)
        if(_prefix AND EXISTS "${_prefix}/opt")
            foreach(_formula libarchive libvorbis opus opusfile openal-soft
                             lz4 ffmpeg)
                if(EXISTS "${_prefix}/opt/${_formula}/lib/pkgconfig")
                    list(APPEND _aether_pc_paths
                         "${_prefix}/opt/${_formula}/lib/pkgconfig")
                endif()
            endforeach()
        endif()
    endforeach()
    if(_aether_pc_paths)
        string(JOIN ":" _aether_pc_path ${_aether_pc_paths})
        if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
            set(_aether_pc_path
                "${_aether_pc_path}:$ENV{PKG_CONFIG_PATH}")
        endif()
        set(ENV{PKG_CONFIG_PATH} "${_aether_pc_path}")
    endif()
endfunction()

function(aetherkiri_prepare_sound_codecs)
    # Do not replace package-provided targets.  This function only fills the
    # naming gaps seen on system package managers (notably Homebrew), where
    # pkg-config is available but the vcpkg-style CONFIG files are not.
    find_package(Vorbis CONFIG QUIET)
    find_package(Opus CONFIG QUIET)
    find_package(OpusFile CONFIG QUIET)
    find_package(OpenAL CONFIG QUIET)
    _aetherkiri_native_host_fallback_allowed(
        _aether_sound_allow_host_fallback)
    if(_aether_sound_allow_host_fallback)
        _aetherkiri_add_homebrew_pkgconfig()
        find_package(PkgConfig QUIET)
    else()
        set(PkgConfig_FOUND FALSE)
    endif()

    if(NOT TARGET Vorbis::vorbis)
        _aetherkiri_make_pkg_interface(aether_vorbis vorbis)
        _aetherkiri_alias_interface(Vorbis::vorbis aether_vorbis)
    endif()
    if(NOT TARGET Vorbis::vorbisfile)
        _aetherkiri_make_pkg_interface(aether_vorbisfile vorbisfile)
        _aetherkiri_alias_interface(Vorbis::vorbisfile aether_vorbisfile)
    endif()
    if(NOT TARGET Vorbis::vorbisenc)
        _aetherkiri_make_pkg_interface(aether_vorbisenc vorbisenc)
        _aetherkiri_alias_interface(Vorbis::vorbisenc aether_vorbisenc)
    endif()
    if(NOT TARGET Opus::opus)
        _aetherkiri_make_pkg_interface(aether_opus opus)
        _aetherkiri_alias_interface(Opus::opus aether_opus)
    endif()
    if(NOT TARGET OpusFile::opusfile)
        _aetherkiri_make_pkg_interface(aether_opusfile opusfile)
        _aetherkiri_alias_interface(OpusFile::opusfile aether_opusfile)
    endif()
    if(NOT TARGET OpenAL::OpenAL)
        _aetherkiri_make_pkg_interface(aether_openal openal)
        _aetherkiri_alias_interface(OpenAL::OpenAL aether_openal)
    endif()

    # Homebrew's codec formulae are often keg-only and its pkg-config files
    # can be hidden behind a toolchain-provided pkgconf.  Resolve the same
    # ABI directly as a native-host fallback instead of making developers
    # install a second copy or weakening the sound target.
    if(_aether_sound_allow_host_fallback)
        _aetherkiri_make_host_library(aether_vorbis vorbis/codec.h vorbis
                                      libvorbis)
        _aetherkiri_make_host_library(aether_vorbisfile vorbis/vorbisfile.h
                                      vorbisfile libvorbis)
        _aetherkiri_make_host_library(aether_vorbisenc vorbis/vorbisenc.h
                                      vorbisenc libvorbis)
        _aetherkiri_make_host_library(aether_opus opus/opus.h opus opus)
        _aetherkiri_make_host_library(aether_opusfile opusfile.h opusfile
                                      opusfile)
        _aetherkiri_make_host_library(aether_openal AL/al.h openal
                                      openal-soft)
        _aetherkiri_alias_interface(Vorbis::vorbis aether_vorbis)
        _aetherkiri_alias_interface(Vorbis::vorbisfile aether_vorbisfile)
        _aetherkiri_alias_interface(Vorbis::vorbisenc aether_vorbisenc)
        _aetherkiri_alias_interface(Opus::opus aether_opus)
        _aetherkiri_alias_interface(OpusFile::opusfile aether_opusfile)
        _aetherkiri_alias_interface(OpenAL::OpenAL aether_openal)
    endif()

    set(_aether_missing_codecs)
    foreach(_aether_codec_target
            Vorbis::vorbis Vorbis::vorbisfile Vorbis::vorbisenc
            Opus::opus OpusFile::opusfile OpenAL::OpenAL)
        if(NOT TARGET "${_aether_codec_target}")
            list(APPEND _aether_missing_codecs "${_aether_codec_target}")
        endif()
    endforeach()
    if(_aether_missing_codecs)
        string(JOIN ", " _aether_missing_text ${_aether_missing_codecs})
        message(FATAL_ERROR
            "Audio codec providers are missing: ${_aether_missing_text}. "
            "Install the vcpkg manifest dependencies or system packages "
            "with pkg-config (vorbis, opus, opusfile, openal).")
    endif()
    message(STATUS "Aether sound codec providers: normalized")
endfunction()

function(aetherkiri_prepare_lz4)
    if(TARGET lz4::lz4)
        return()
    endif()

    # Prefer the toolchain package used by release CI.  Homebrew intentionally
    # ships no lz4Config.cmake, so native developer builds need the equivalent
    # pkg-config/raw-library boundary normalized to the same target name.
    find_package(lz4 CONFIG QUIET)
    if(TARGET lz4::lz4)
        return()
    endif()

    _aetherkiri_native_host_fallback_allowed(_aether_lz4_allow_host_fallback)
    if(_aether_lz4_allow_host_fallback)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(AETHER_LZ4_PC QUIET IMPORTED_TARGET liblz4)
        endif()
        if(TARGET PkgConfig::AETHER_LZ4_PC)
            add_library(aether_lz4 INTERFACE)
            target_link_libraries(aether_lz4 INTERFACE
                PkgConfig::AETHER_LZ4_PC)
            add_library(lz4::lz4 ALIAS aether_lz4)
            message(STATUS "Aether lz4 provider: pkg-config")
            return()
        endif()

        find_path(AETHER_LZ4_INCLUDE_DIR NAMES lz4.h
            HINTS "$ENV{LZ4_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/lz4"
                  /opt/homebrew/opt/lz4 /usr/local/opt/lz4
            PATH_SUFFIXES include)
        find_library(AETHER_LZ4_LIBRARY NAMES lz4
            HINTS "$ENV{LZ4_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/lz4"
                  /opt/homebrew/opt/lz4 /usr/local/opt/lz4
            PATH_SUFFIXES lib)
        if(AETHER_LZ4_INCLUDE_DIR AND AETHER_LZ4_LIBRARY)
            _aetherkiri_make_imported_library(aether_lz4_system
                "${AETHER_LZ4_INCLUDE_DIR}" "${AETHER_LZ4_LIBRARY}")
            add_library(lz4::lz4 ALIAS aether_lz4_system)
            message(STATUS "Aether lz4 provider: ${AETHER_LZ4_LIBRARY}")
            return()
        endif()
    endif()

    message(FATAL_ERROR
        "A compatible lz4 provider is required. Install the vcpkg lz4 "
        "package or, for a native host build, liblz4 with pkg-config.")
endfunction()

function(aetherkiri_prepare_libarchive)
    if(DEFINED AETHERKIRI_LIBARCHIVE_TARGET AND
       TARGET "${AETHERKIRI_LIBARCHIVE_TARGET}")
        return()
    endif()

    # Prefer a toolchain/config package.  This keeps cross builds on the
    # target triplet and avoids accidentally linking host archives.
    find_package(LibArchive CONFIG QUIET)
    if(TARGET LibArchive::LibArchive)
        set(AETHERKIRI_LIBARCHIVE_TARGET LibArchive::LibArchive CACHE INTERNAL
            "Normalized libarchive target")
        return()
    endif()

    # The vcpkg libarchive port intentionally installs only a pkg-config
    # file (there is no LibArchiveConfig.cmake).  CMake's module-mode finder
    # understands that layout and, when a vcpkg/toolchain root is active,
    # resolves the target-triplet headers and archive instead of probing the
    # host filesystem.  Keep CONFIG first for downstream providers that do
    # ship a modern package config, then use the module as the portable
    # cross-build path.
    find_package(LibArchive QUIET)
    if(TARGET LibArchive::LibArchive)
        set(AETHERKIRI_LIBARCHIVE_TARGET LibArchive::LibArchive CACHE INTERNAL
            "Normalized libarchive target")
        return()
    endif()
    foreach(_candidate LibArchive::libarchive libarchive::libarchive
                       archive::archive)
        if(TARGET "${_candidate}")
            set(AETHERKIRI_LIBARCHIVE_TARGET "${_candidate}" CACHE INTERNAL
                "Normalized libarchive target")
            return()
        endif()
    endforeach()

    _aetherkiri_native_host_fallback_allowed(
        _aether_libarchive_allow_host_fallback)
    if(_aether_libarchive_allow_host_fallback)
        _aetherkiri_add_homebrew_pkgconfig()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(AETHER_LIBARCHIVE_PC QUIET IMPORTED_TARGET
                              libarchive)
            if(TARGET PkgConfig::AETHER_LIBARCHIVE_PC)
                set(AETHERKIRI_LIBARCHIVE_TARGET
                    PkgConfig::AETHER_LIBARCHIVE_PC CACHE INTERNAL
                    "Normalized libarchive target")
                return()
            endif()
        endif()

        find_path(_aether_libarchive_include
            NAMES archive.h
            HINTS "$ENV{LIBARCHIVE_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/libarchive"
                  /opt/homebrew/opt/libarchive /usr/local/opt/libarchive
            PATH_SUFFIXES include)
        find_library(_aether_libarchive_library
            NAMES archive libarchive
            HINTS "$ENV{LIBARCHIVE_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/libarchive"
                  /opt/homebrew/opt/libarchive /usr/local/opt/libarchive
            PATH_SUFFIXES lib)
        if(_aether_libarchive_include AND _aether_libarchive_library)
            _aetherkiri_make_imported_library(aether_libarchive_system
                "${_aether_libarchive_include}"
                "${_aether_libarchive_library}")
            set(AETHERKIRI_LIBARCHIVE_TARGET aether_libarchive_system
                CACHE INTERNAL "Normalized libarchive target")
            return()
        endif()
    endif()

    set(AETHERKIRI_LIBARCHIVE_TARGET "" CACHE INTERNAL
        "Normalized libarchive target")
    message(FATAL_ERROR
        "A compatible libarchive provider is required. Install the vcpkg "
        "libarchive package or a system libarchive development package.")
endfunction()

# Normalize libcurl for the script-visible httprequest/xmlhttprequest
# compatibility adapters. HTTP is optional at the dependency boundary: a
# build without curl still has local Storage requests, but network requests
# must fail closed instead of shelling out to an unquoted command. vcpkg/config
# targets are preferred; native developers may use a Homebrew/pkg-config/raw
# library fallback. Cross builds never consume host curl binaries.
function(aetherkiri_prepare_curl)
    if(DEFINED AETHERKIRI_CURL_TARGET)
        return()
    endif()

    set(_aether_curl_target "")
    find_package(CURL CONFIG QUIET)
    foreach(_candidate CURL::libcurl CURL::libcurl_static
                       CURL::libcurl_shared)
        if(NOT _aether_curl_target AND TARGET "${_candidate}")
            set(_aether_curl_target "${_candidate}")
        endif()
    endforeach()

    _aetherkiri_native_host_fallback_allowed(_aether_curl_allow_host)
    if(NOT _aether_curl_target AND _aether_curl_allow_host)
        # CMake's module mode handles Homebrew's keg-only installation on
        # recent CMake versions. Prefer its imported target when available.
        find_package(CURL QUIET)
        foreach(_candidate CURL::libcurl CURL::libcurl_static
                           CURL::libcurl_shared)
            if(NOT _aether_curl_target AND TARGET "${_candidate}")
                set(_aether_curl_target "${_candidate}")
            endif()
        endforeach()
        if(NOT _aether_curl_target)
            _aetherkiri_add_homebrew_pkgconfig()
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(AETHER_CURL_PC QUIET IMPORTED_TARGET
                                  libcurl)
                if(TARGET PkgConfig::AETHER_CURL_PC)
                    set(_aether_curl_target PkgConfig::AETHER_CURL_PC)
                endif()
            endif()
        endif()
        if(NOT _aether_curl_target)
            find_path(_aether_curl_include NAMES curl/curl.h
                HINTS "$ENV{CURL_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/curl"
                      /opt/homebrew/opt/curl /usr/local/opt/curl
                PATH_SUFFIXES include)
            find_library(_aether_curl_library NAMES curl libcurl
                HINTS "$ENV{CURL_ROOT}" "$ENV{HOMEBREW_PREFIX}/opt/curl"
                      /opt/homebrew/opt/curl /usr/local/opt/curl
                PATH_SUFFIXES lib)
            if(_aether_curl_include AND _aether_curl_library)
                _aetherkiri_make_imported_library(aether_curl_system
                    "${_aether_curl_include}" "${_aether_curl_library}")
                set(_aether_curl_target aether_curl_system)
            endif()
        endif()
    endif()

    if(_aether_curl_target)
        # Imported targets returned from find_package() can be directory
        # scoped when this helper is called from a function. Publish a normal
        # interface owner so child directories can link it reliably.
        if(NOT TARGET aether_curl)
            add_library(aether_curl INTERFACE)
            target_link_libraries(aether_curl INTERFACE
                                  "${_aether_curl_target}")
        endif()
        set(AETHERKIRI_CURL_TARGET "aether_curl" CACHE INTERNAL
            "Normalized libcurl target")
        message(STATUS "Aether curl provider: ${_aether_curl_target}")
    else()
        set(AETHERKIRI_CURL_TARGET "" CACHE INTERNAL
            "Normalized libcurl target")
        message(STATUS
            "Aether curl provider: unavailable (network compatibility will fail closed)")
    endif()
endfunction()
