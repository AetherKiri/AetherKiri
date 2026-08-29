# Fallback FindFFMPEG module for hosts where FFmpeg is provided by the
# operating system instead of a vcpkg port.  The vcpkg toolchain's module is
# searched first (the root project appends this directory), so this is only
# used when no toolchain package is available.

include(FindPackageHandleStandardArgs)

if(FFMPEG_FOUND)
    return()
endif()

set(_aether_ffmpeg_components
    avutil avcodec avformat avfilter swscale swresample avdevice postproc)
set(_aether_ffmpeg_headers
    libavutil/avutil.h
    libavcodec/avcodec.h
    libavformat/avformat.h
    libavfilter/avfilter.h
    libswscale/swscale.h
    libswresample/swresample.h
    libavdevice/avdevice.h
    libpostproc/postprocess.h)

find_package(PkgConfig QUIET)

# Callers that only ask whether FFmpeg is available (for example the private
# package probe) still need a useful, linkable baseline.  Probe the libraries
# used by the movie/audio paths when no explicit component list is supplied.
if(NOT FFMPEG_FIND_COMPONENTS)
    set(FFMPEG_FIND_COMPONENTS avutil avcodec avformat swresample)
endif()

set(FFMPEG_INCLUDE_DIRS)
set(FFMPEG_LIBRARY_DIRS)
set(FFMPEG_LIBRARIES)
set(FFMPEG_VERSION)
set(_aether_ffmpeg_missing)

foreach(_aether_ffmpeg_component IN LISTS FFMPEG_FIND_COMPONENTS)
    list(FIND _aether_ffmpeg_components "${_aether_ffmpeg_component}" _aether_ffmpeg_index)
    if(_aether_ffmpeg_index LESS 0)
        list(APPEND _aether_ffmpeg_missing "${_aether_ffmpeg_component}")
        continue()
    endif()
    list(GET _aether_ffmpeg_headers ${_aether_ffmpeg_index} _aether_ffmpeg_header)
    set(_aether_ffmpeg_pkg "lib${_aether_ffmpeg_component}")

    set(_aether_ffmpeg_pkg_target "")
    if(PkgConfig_FOUND)
        string(REPLACE "-" "_" _aether_ffmpeg_pc_suffix
               "${_aether_ffmpeg_component}")
        set(_aether_ffmpeg_pc_var "PC_FFMPEG_${_aether_ffmpeg_pc_suffix}")
        pkg_check_modules(${_aether_ffmpeg_pc_var} QUIET IMPORTED_TARGET
                          "${_aether_ffmpeg_pkg}")
        if(${_aether_ffmpeg_pc_var}_FOUND)
            set(FFMPEG_${_aether_ffmpeg_component}_FOUND TRUE)
            set(_aether_ffmpeg_pkg_target
                "PkgConfig::${_aether_ffmpeg_pc_var}")
            list(APPEND FFMPEG_INCLUDE_DIRS
                 ${${_aether_ffmpeg_pc_var}_INCLUDE_DIRS})
            list(APPEND FFMPEG_LIBRARY_DIRS
                 ${${_aether_ffmpeg_pc_var}_LIBRARY_DIRS})
            if(${_aether_ffmpeg_pc_var}_VERSION AND NOT FFMPEG_VERSION)
                set(FFMPEG_VERSION ${${_aether_ffmpeg_pc_var}_VERSION})
            endif()
        endif()
    endif()

    if(_aether_ffmpeg_pkg_target)
        list(APPEND FFMPEG_LIBRARIES "${_aether_ffmpeg_pkg_target}")
        continue()
    endif()

    find_path(_aether_ffmpeg_include_${_aether_ffmpeg_component}
        NAMES "${_aether_ffmpeg_header}"
        PATH_SUFFIXES include
        HINTS "$ENV{FFMPEG_ROOT}" "$ENV{HOMEBREW_PREFIX}"
              /opt/homebrew/opt/ffmpeg /opt/homebrew
              /usr/local/opt/ffmpeg /usr/local)
    find_library(_aether_ffmpeg_library_${_aether_ffmpeg_component}
        NAMES "${_aether_ffmpeg_component}" "lib${_aether_ffmpeg_component}"
        PATH_SUFFIXES lib
        HINTS "$ENV{FFMPEG_ROOT}" "$ENV{HOMEBREW_PREFIX}"
              /opt/homebrew/opt/ffmpeg /opt/homebrew
              /usr/local/opt/ffmpeg /usr/local)
    if(_aether_ffmpeg_include_${_aether_ffmpeg_component} AND
       _aether_ffmpeg_library_${_aether_ffmpeg_component})
        set(FFMPEG_${_aether_ffmpeg_component}_FOUND TRUE)
        list(APPEND FFMPEG_INCLUDE_DIRS
             "${_aether_ffmpeg_include_${_aether_ffmpeg_component}}")
        get_filename_component(_aether_ffmpeg_library_dir
            "${_aether_ffmpeg_library_${_aether_ffmpeg_component}}" DIRECTORY)
        list(APPEND FFMPEG_LIBRARY_DIRS "${_aether_ffmpeg_library_dir}")
        list(APPEND FFMPEG_LIBRARIES
             "${_aether_ffmpeg_library_${_aether_ffmpeg_component}}")
    else()
        set(FFMPEG_${_aether_ffmpeg_component}_FOUND FALSE)
        list(APPEND _aether_ffmpeg_missing "${_aether_ffmpeg_component}")
    endif()
endforeach()

list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
list(REMOVE_DUPLICATES FFMPEG_LIBRARY_DIRS)
list(REMOVE_DUPLICATES FFMPEG_LIBRARIES)
if(NOT _aether_ffmpeg_missing)
    set(FFMPEG_FOUND TRUE)
endif()

set(FFMPEG_INCLUDE_DIRS "${FFMPEG_INCLUDE_DIRS}")
set(FFMPEG_LIBRARY_DIRS "${FFMPEG_LIBRARY_DIRS}")
set(FFMPEG_LIBRARIES "${FFMPEG_LIBRARIES}")
find_package_handle_standard_args(FFMPEG
    REQUIRED_VARS FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARIES
    HANDLE_COMPONENTS
    VERSION_VAR FFMPEG_VERSION)
mark_as_advanced(FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARY_DIRS FFMPEG_LIBRARIES)
