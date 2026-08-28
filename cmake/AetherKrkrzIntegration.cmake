# Helpers for consuming selected business sources from the pinned krkrz_dev
# submodule without importing its standalone build/ABI.

set(AETHER_KRKRZ_DEV_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/krkrz_dev"
    CACHE PATH "Pinned krkrz_dev checkout used for source-level reuse")
set(AETHER_KRKRZ_PLUGIN_ROOT
    "${AETHER_KRKRZ_DEV_ROOT}/src/plugins")
set(AETHER_KRKRZ_CORE_ROOT
    "${AETHER_KRKRZ_DEV_ROOT}/src/core")

function(aether_krkrz_require_source relative_path)
    set(source "${AETHER_KRKRZ_PLUGIN_ROOT}/${relative_path}")
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR
            "krkrz_dev source is missing: ${source}. Run "
            "git submodule update --init --recursive third_party/krkrz_dev")
    endif()
    # A submodule update must cause a reconfigure when a selected source is
    # added or removed, even though the parent CMake file did not change.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
endfunction()

function(aether_krkrz_track_source wrapper relative_source)
    if(NOT EXISTS "${wrapper}")
        message(FATAL_ERROR "Aether krkrz wrapper is missing: ${wrapper}")
    endif()
    aether_krkrz_require_source("${relative_source}")
    set_property(SOURCE "${wrapper}" APPEND PROPERTY OBJECT_DEPENDS
        "${AETHER_KRKRZ_PLUGIN_ROOT}/${relative_source}")
endfunction()

# Require a source from the nested krkrz core checkout without importing the
# upstream core project, registry, or ABI.  Core leaf sources use the Aether
# public headers through the consuming target's include paths.
function(aether_krkrz_require_core_source relative_path)
    set(source "${AETHER_KRKRZ_CORE_ROOT}/${relative_path}")
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR
            "krkrz_dev core source is missing: ${source}. Run "
            "git submodule update --init --recursive third_party/krkrz_dev")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
endfunction()

function(aether_krkrz_track_core_source wrapper relative_source)
    if(NOT EXISTS "${wrapper}")
        message(FATAL_ERROR "Aether krkrz core wrapper is missing: ${wrapper}")
    endif()
    aether_krkrz_require_core_source("${relative_source}")
    set_property(SOURCE "${wrapper}" APPEND PROPERTY OBJECT_DEPENDS
        "${AETHER_KRKRZ_CORE_ROOT}/${relative_source}")
endfunction()
