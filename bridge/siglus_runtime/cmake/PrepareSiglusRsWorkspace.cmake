# PrepareSiglusRsWorkspace.cmake — build-time source overlay for the
# packages/siglus_rs submodule (run in cmake -P script mode).
#
# The submodule stays untouched. Instead, a pristine copy is materialized in
# the build tree ("workspace"), the files from overlay/files/ are dropped in,
# and the diffs from overlay/patches/*.patch are applied with `git apply`.
#
# Inputs (passed with -D):
#   SIGLUS_RS_SRC       pristine submodule checkout
#   SIGLUS_RS_WORKSPACE destination workspace (created/refreshed)
#   SIGLUS_OVERLAY_DIR  directory containing files/ and patches/
#
# Refresh policy: a marker file records a fingerprint of (overlay contents +
# upstream source tree). Any drift wipes the workspace and reapplies from
# scratch. Patch failures are fatal so overlay/upstream divergence surfaces
# loudly instead of silently building unpatched sources.

set(SIGLUS_MARKER_FILE "${SIGLUS_RS_WORKSPACE}/.aetherkiri-overlay-stamp")

find_program(SIGLUS_GIT_EXECUTABLE git)
if(NOT SIGLUS_GIT_EXECUTABLE)
    message(FATAL_ERROR
        "siglus_rs overlay requires `git` on PATH to apply patches.")
endif()

# ---------------------------------------------------------------------------
# 1. Fingerprint the inputs.
# ---------------------------------------------------------------------------
set(SIGLUS_COPY_ITEMS
    "${SIGLUS_RS_SRC}/Cargo.toml"
    "${SIGLUS_RS_SRC}/Cargo.lock"
    "${SIGLUS_RS_SRC}/crates")

set(fingerprint "")
foreach(item IN LISTS SIGLUS_COPY_ITEMS)
    if(IS_DIRECTORY "${item}")
        file(GLOB_RECURSE item_files LIST_DIRECTORIES false "${item}/*")
    else()
        set(item_files "${item}")
    endif()
    foreach(f IN LISTS item_files)
        file(TIMESTAMP "${f}" f_mtime)
        file(SIZE "${f}" f_size)
        string(APPEND fingerprint "${f}|${f_size}|${f_mtime}\n")
    endforeach()
endforeach()

file(GLOB overlay_files
    LIST_DIRECTORIES false
    "${SIGLUS_OVERLAY_DIR}/files/*")
foreach(f IN LISTS overlay_files)
    file(SHA512 "${f}" f_hash)
    string(APPEND fingerprint "file:${f}:${f_hash}\n")
endforeach()

file(GLOB overlay_patches
    LIST_DIRECTORIES false
    "${SIGLUS_OVERLAY_DIR}/patches/*.patch")
list(SORT overlay_patches)
foreach(f IN LISTS overlay_patches)
    file(SHA512 "${f}" f_hash)
    string(APPEND fingerprint "patch:${f}:${f_hash}\n")
endforeach()

string(SHA512 fingerprint "${fingerprint}")

# Include this script's own content so prepare-logic changes always force a
# workspace rebuild.
file(SHA512 "${CMAKE_SCRIPT_MODE_FILE}" self_content_hash)
set(fingerprint "${fingerprint}${self_content_hash}")

if(EXISTS "${SIGLUS_MARKER_FILE}")
    file(READ "${SIGLUS_MARKER_FILE}" stamped_fingerprint)
    if(stamped_fingerprint STREQUAL fingerprint)
        message(STATUS "siglus_rs workspace up to date")
        return()
    endif()
endif()

message(STATUS "Preparing siglus_rs workspace at ${SIGLUS_RS_WORKSPACE}")

# ---------------------------------------------------------------------------
# 2. Fresh pristine copy.
# ---------------------------------------------------------------------------
if(EXISTS "${SIGLUS_RS_WORKSPACE}")
    file(REMOVE_RECURSE "${SIGLUS_RS_WORKSPACE}")
endif()
file(MAKE_DIRECTORY "${SIGLUS_RS_WORKSPACE}")
foreach(item IN LISTS SIGLUS_COPY_ITEMS)
    get_filename_component(item_name "${item}" NAME)
    file(COPY "${item}" DESTINATION "${SIGLUS_RS_WORKSPACE}")
endforeach()

# ---------------------------------------------------------------------------
# 3. Drop in new files.
# ---------------------------------------------------------------------------
if(overlay_files)
    file(COPY ${overlay_files}
         DESTINATION "${SIGLUS_RS_WORKSPACE}/crates/siglus_scene_vm/src/")
endif()

# ---------------------------------------------------------------------------
# 3b. Neutralize gitignore interference.
# ---------------------------------------------------------------------------
# The workspace sits inside the AetherKiri work tree (out/ is ignored there)
# and carries siglus_rs' own .gitignore files; git apply --no-index silently
# skips ignored paths. Remove every .gitignore and turn the workspace into
# its own repository root so patch application is unambiguous.
file(GLOB_RECURSE workspace_gitignores LIST_DIRECTORIES false
    "${SIGLUS_RS_WORKSPACE}/.gitignore")
foreach(f IN LISTS workspace_gitignores)
    file(REMOVE "${f}")
endforeach()
execute_process(
    COMMAND "${SIGLUS_GIT_EXECUTABLE}" init -q
    WORKING_DIRECTORY "${SIGLUS_RS_WORKSPACE}"
    RESULT_VARIABLE init_result)
if(NOT init_result EQUAL 0)
    message(FATAL_ERROR "git init failed in ${SIGLUS_RS_WORKSPACE}")
endif()

# ---------------------------------------------------------------------------
# 4. Apply patches (verify first so a failed apply never leaves a half-patched
# workspace behind).
# ---------------------------------------------------------------------------
foreach(patch IN LISTS overlay_patches)
    get_filename_component(patch_name "${patch}" NAME)
    execute_process(
        COMMAND "${SIGLUS_GIT_EXECUTABLE}" apply --check
                --whitespace=nowarn "${patch}"
        WORKING_DIRECTORY "${SIGLUS_RS_WORKSPACE}"
        RESULT_VARIABLE check_result
        ERROR_VARIABLE check_output)
    if(NOT check_result EQUAL 0)
        message(FATAL_ERROR
            "siglus_rs overlay patch '${patch_name}' no longer applies to "
            "packages/siglus_rs. The submodule was probably updated; rebase "
            "the patch series in bridge/siglus_runtime/overlay/. Git said:\n"
            "${check_output}")
    endif()
    execute_process(
        COMMAND "${SIGLUS_GIT_EXECUTABLE}" apply --whitespace=nowarn "${patch}"
        WORKING_DIRECTORY "${SIGLUS_RS_WORKSPACE}"
        RESULT_VARIABLE apply_result
        ERROR_VARIABLE apply_output)
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR
            "siglus_rs overlay patch '${patch_name}' passed --check but "
            "failed to apply:\n${apply_output}")
    endif()
    message(STATUS "Applied siglus_rs overlay patch ${patch_name}")
endforeach()

# file(COPY) preserves source mtimes, which can be older than a previous
# cargo build in the shared target dir; cargo would then reuse stale artifacts
# despite changed sources. Bump every file so cargo sees a fresh tree.
file(GLOB_RECURSE workspace_files LIST_DIRECTORIES false
    "${SIGLUS_RS_WORKSPACE}/*")
list(FILTER workspace_files EXCLUDE REGEX "/\\.git/")
foreach(f IN LISTS workspace_files)
    file(TOUCH "${f}")
endforeach()

file(WRITE "${SIGLUS_MARKER_FILE}" "${fingerprint}")
