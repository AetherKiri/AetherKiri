# ggml enables -march=native unless it detects a reproducible/cross build.
# When producing x86_64 on an Apple Silicon host, clang expands that flag to
# apple-m1 and rejects it for the x86 target. Scope the reproducible-build hint
# to ggml so the rest of the dependency graph keeps its existing ABI.
set(_aether_had_source_date_epoch FALSE)
if(DEFINED ENV{SOURCE_DATE_EPOCH})
    set(_aether_had_source_date_epoch TRUE)
    set(_aether_source_date_epoch "$ENV{SOURCE_DATE_EPOCH}")
endif()
set(ENV{SOURCE_DATE_EPOCH} "1")

set(_aether_overlay_port_dir "${CURRENT_PORT_DIR}")
set(CURRENT_PORT_DIR "${VCPKG_ROOT_DIR}/ports/ggml")
include("${VCPKG_ROOT_DIR}/ports/ggml/portfile.cmake")
set(CURRENT_PORT_DIR "${_aether_overlay_port_dir}")

if(_aether_had_source_date_epoch)
    set(ENV{SOURCE_DATE_EPOCH} "${_aether_source_date_epoch}")
else()
    unset(ENV{SOURCE_DATE_EPOCH})
endif()
