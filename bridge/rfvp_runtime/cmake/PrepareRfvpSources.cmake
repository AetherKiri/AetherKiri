# The pinned MPL-2.0 sources remain untouched. Host-specific adaptations live
# here and in rust/aetherkiri_host.rs and are applied to a build-tree copy.
function(rfvp_replace file before after)
    file(READ "${file}" content)
    string(FIND "${content}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "rfvp overlay no longer matches ${file}: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" content "${content}")
    file(WRITE "${file}" "${content}")
endfunction()

function(aetherkiri_prepare_rfvp root output)
    # This directory contains only generated copies, never the git submodule.
    if(NOT output STREQUAL "${CMAKE_CURRENT_BINARY_DIR}/prepared")
        message(FATAL_ERROR "Refusing to refresh rfvp sources outside its build directory")
    endif()
    file(REMOVE_RECURSE "${output}/crates")
    foreach(crate rfvp rfvp-bitmap na_wmv_player na_mpeg2_decoder anzu-hal)
        file(COPY "${root}/packages/rfvp/crates/${crate}"
             DESTINATION "${output}/crates"
             PATTERN "target" EXCLUDE PATTERN "fonts" EXCLUDE)
    endforeach()
    file(COPY "${root}/packages/rfvp/LICENSE" "${root}/packages/rfvp/README.md"
         DESTINATION "${output}")
    file(WRITE "${output}/Cargo.toml"
        "[workspace]\nresolver = \"3\"\nmembers = [\"crates/*\"]\n[profile.dev]\ndebug = 0\nopt-level = 1\nincremental = false\n[profile.release]\ndebug = 0\n")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Cargo.lock" "${output}/Cargo.lock" COPYONLY)
    set(src "${output}/crates/rfvp/src")
    rfvp_replace("${output}/crates/rfvp/Cargo.toml"
        "crate-type = [\"rlib\", \"cdylib\"]" "crate-type = [\"rlib\", \"staticlib\"]")
    file(COPY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../rust/aetherkiri_host.rs" DESTINATION "${src}")
    file(APPEND "${src}/lib.rs" "\npub mod aetherkiri_host;\n")
    foreach(os ios android)
        rfvp_replace("${src}/lib.rs"
            "#[cfg(all(not(feature = \"no_std\"), target_os = \"${os}\"))]\nmod ${os}_host;"
            "#[cfg(all(feature = \"gpu-render\", target_os = \"${os}\"))]\nmod ${os}_host;")
    endforeach()
    rfvp_replace("${src}/utils/file.rs" "pub fn app_base_path() -> PathBuilder {"
        "pub fn app_base_path() -> PathBuilder {\n    if let Some(root) = crate::aetherkiri_host::game_root() { return PathBuilder { path_buff: root }; }")
    file(APPEND "${src}/utils/file.rs" "\npub fn save_base_path() -> PathBuilder {\n    PathBuilder { path_buff: crate::aetherkiri_host::save_root().expect(\"rfvp save root not installed\") }\n}\n")
    foreach(file subsystem/resources/save_manager.rs subsystem/global_savedata.rs)
        rfvp_replace("${src}/${file}" "app_base_path" "save_base_path")
    endforeach()
    # Video extraction is host-owned too; never create caches in the game tree
    # or fall back to the process working directory.
    rfvp_replace("${src}/subsystem/components/syscalls/movie.rs"
        "let preferred = app_base_path()" "let preferred = crate::utils::file::save_base_path()")
    rfvp_replace("${src}/subsystem/components/syscalls/movie.rs"
        "std::env::current_dir()\n        .unwrap_or_else(|_| PathBuf::from(\".\"))"
        "crate::utils::file::save_base_path().get_path().clone()")
    file(APPEND "${src}/subsystem/components/syscalls/legacy.rs" [=[

pub fn reset_host_state() {
    LEGACY_CHR_TABLE.lock().unwrap().clear();
    LEGACY_TEXT_STATE.lock().unwrap().clear();
    *LEGACY_CONFIG_STATE.lock().unwrap() = LegacyConfigState::default();
    *LEGACY_UI_STATE.lock().unwrap() = LegacyUiState::default();
}
]=])
    # Never embed the upstream Microsoft fonts. Reuse AetherKiri's OFL font.
    file(MAKE_DIRECTORY "${src}/subsystem/resources/fonts")
    configure_file("${root}/apps/godot_app/assets/fonts/aetherkiri-runtime-cjk.otf"
        "${src}/subsystem/resources/fonts/aetherkiri.otf" COPYONLY)
    foreach(font MSGOTHIC.TTF MSMINCHO.TTF MS-PGothic.ttf MS-PMincho-2.ttf)
        rfvp_replace("${src}/subsystem/resources/text_manager.rs"
            "include_bytes!(\"./fonts/${font}\")" "include_bytes!(\"./fonts/aetherkiri.otf\")")
    endforeach()
    rfvp_replace("${src}/subsystem/resources/text_manager.rs"
        "impl FontEnumerator {\n    pub fn new() -> Self {"
        "impl FontEnumerator {\n    pub fn set_host_font(&mut self, font: Font) {\n        self.default_font = font.clone(); self.sys_ms_gothic = font.clone();\n        self.sys_ms_mincho = font.clone(); self.sys_ms_pgothic = font.clone(); self.sys_ms_pmincho = font;\n    }\n    pub fn new() -> Self {")
    rfvp_replace("${src}/subsystem/resources/input_manager.rs"
        "impl InputManager {\n    pub fn new() -> Self {"
        "impl InputManager {\n    pub fn cancel_host_input(&mut self) {\n        let (mask, x, y, inside) = (self.control_is_masked, self.get_cursor_x(), self.get_cursor_y(), self.get_cursor_in());\n        *self = Self::new(); self.control_is_masked = mask; self.notify_mouse_move(x, y); self.set_mouse_in(inside);\n    }\n    pub fn new() -> Self {")
    file(COPY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../rust/video_host.rs"
        DESTINATION "${src}/subsystem/resources")
    file(APPEND "${src}/subsystem/resources/videoplayer.rs" "\ninclude!(\"video_host.rs\");\n")
    foreach(player Bgm Se)
        string(TOLOWER "${player}" lower)
        rfvp_replace("${src}/audio_player/${lower}_player.rs"
            "impl ${player}Player {"
            "impl ${player}Player {\n    pub fn pause_host(&mut self, paused: bool) {\n        for track in &mut self.${lower}_tracks {\n            if paused { track.pause(Tween::default()); } else { track.resume(Tween::default()); }\n        }\n    }")
    endforeach()
endfunction()
