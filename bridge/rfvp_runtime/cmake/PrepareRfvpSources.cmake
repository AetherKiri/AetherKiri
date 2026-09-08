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
        "[workspace]\nresolver = \"3\"\nmembers = [\"crates/*\"]\n[profile.dev]\ndebug = 0\nopt-level = 1\nincremental = false\n[profile.dev.package.rfvp]\nopt-level = 3\n[profile.release]\ndebug = 0\nlto = \"thin\"\ncodegen-units = 1\n")
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
    # V1 drops active motions and text/VM wait linkage on load. Extend new RFVS
    # payloads with a V2 envelope without changing the original V1 wire layout.
    set(motion_src "${src}/subsystem/resources/motion_manager")
    foreach(file alpha normal_move rotation_move s2_move z_move v3d anim snow lip)
        file(READ "${motion_src}/${file}.rs" content)
        string(REGEX REPLACE "#\\[derive\\(([^)]*)\\)\\]"
            "#[derive(\\1, serde::Serialize, serde::Deserialize)]" content "${content}")
        file(WRITE "${motion_src}/${file}.rs" "${content}")
    endforeach()
    foreach(pair "alpha;AlphaMotionContainer" "normal_move;MoveMotionContainer"
            "rotation_move;RotationMotionContainer" "s2_move;ScaleMotionContainer"
            "z_move;ZMotionContainer" "snow;SnowMotionContainer" "lip;LipMotionContainer")
        list(GET pair 0 file)
        list(GET pair 1 type)
        rfvp_replace("${motion_src}/${file}.rs" "pub struct ${type} {"
            "#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]\npub struct ${type} {")
    endforeach()
    rfvp_replace("${motion_src}/anim.rs" "#[derive(Debug, Default,"
        "#[derive(Debug, Default, Clone,")
    foreach(field flakes flake_ptrs)
        rfvp_replace("${motion_src}/snow.rs" "    pub ${field}:"
            "    #[serde(with = \"crate::subsystem::save_state::host_array\")]\n    pub ${field}:")
    endforeach()
    foreach(pair "motion_snapshot;subsystem/resources/motion_manager/mod.rs"
            "text_snapshot;subsystem/resources/text_manager.rs" "save_snapshot;subsystem/save_state.rs"
            "soft_render_host;soft_render/renderer.rs")
        list(GET pair 0 helper)
        list(GET pair 1 target)
        get_filename_component(target_dir "${src}/${target}" DIRECTORY)
        file(COPY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../rust/${helper}.rs" DESTINATION "${target_dir}")
        file(APPEND "${src}/${target}" "\ninclude!(\"${helper}.rs\");\n")
    endforeach()
    rfvp_replace("${src}/soft_render/renderer.rs"
        "    framebuffer: SoftFramebuffer,\n    stats: SoftRendererStats,"
        "    framebuffer: SoftFramebuffer,\n    stats: SoftRendererStats,\n    host_gpu: Option<HostGpuRenderer>,")
    rfvp_replace("${src}/soft_render/renderer.rs"
        "            stats: SoftRendererStats::default(),\n        })"
        "            stats: SoftRendererStats::default(),\n            host_gpu: None,\n        })")
    rfvp_replace("${src}/soft_render/renderer.rs"
        "        self.stats = SoftRendererStats::default();\n        self.framebuffer.clear_rgba(0, 0, 0, 255);"
        "        self.stats = SoftRendererStats::default();\n        if !self.begin_host_gpu_frame() {\n            self.framebuffer.clear_rgba(0, 0, 0, 255);\n        }")
    rfvp_replace("${src}/soft_render/renderer.rs"
        "        let graphs = motion.graphs();\n        let snow_motions = motion.snow_motions();"
        "        let graphs = motion.graphs();\n        self.prune_host_gpu_graphs(graphs);\n        let snow_motions = motion.snow_motions();")
    rfvp_replace("${src}/soft_render/renderer.rs"
        "        }\n\n        Ok(())\n    }\n\n    fn dissolve_color"
        "        }\n\n        self.finish_host_gpu_frame();\n        Ok(())\n    }\n\n    fn dissolve_color")
    rfvp_replace("${src}/soft_render/renderer.rs"
        "        self.raster_triangle(v0, v1, v2, texture);\n        self.raster_triangle(v2, v1, v3, texture);"
        "        if !self.try_host_gpu_quad(v0, v1, v2, v3, texture) {\n            self.raster_triangle(v0, v1, v2, texture);\n            self.raster_triangle(v2, v1, v3, texture);\n        }")
    # The common axis-aligned case needs one clipped rectangle, not two
    # overlapping triangle scans. Keep rotated/sheared quads on the old path.
    rfvp_replace("${src}/soft_render/renderer.rs"
        "        let p0 = model.transform_point3(vec3(0.0, dst_h, 0.0)).truncate();"
        "        if color.w <= 0.0 { return Ok(()); }\n        let p0 = model.transform_point3(vec3(0.0, dst_h, 0.0)).truncate();")
    rfvp_replace("${src}/soft_render/renderer.rs" "        let v0 = Vertex {"
        "        if self.try_host_axis_quad(p1, p3, p0, uv0, uv1, color, texture) {\n            self.stats.quad_count += 1;\n            self.stats.draw_calls += 1;\n            return Ok(());\n        }\n        let v0 = Vertex {")
    set(save_state "${src}/subsystem/save_state.rs")
    rfvp_replace("${save_state}" "pub struct SaveStateSnapshotV1 {"
        "pub struct SaveStateSnapshotV1 {\n    #[serde(skip)]\n    pub(crate) host_playback: Option<HostPlaybackSnapshot>,")
    rfvp_replace("${save_state}" "        SaveStateSnapshotV1 {\n            version: 1,"
        "        SaveStateSnapshotV1 {\n            host_playback: Some(HostPlaybackSnapshot::capture(game_data)),\n            version: 1,")
    rfvp_replace("${save_state}" "        tm.apply_snapshot_v1(&self.vm);"
        "        tm.apply_snapshot_v1(&self.vm);\n        if let Some(playback) = &self.host_playback { playback.apply(game_data); }")
    rfvp_replace("${save_state}" "        .reject_trailing_bytes()"
        "        .reject_trailing_bytes()\n        .with_limit(MAX_STATE_PAYLOAD_BYTES as u64)")
    rfvp_replace("${save_state}" "let payload = bincode_opts()\n            .serialize(snap)"
        "let payload = encode_host_snapshot(snap)")
    rfvp_replace("${save_state}" "let snap: SaveStateSnapshotV1 = bincode_opts()\n            .deserialize(payload)"
        "let snap: SaveStateSnapshotV1 = decode_host_snapshot(payload)")
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
    # SePlayer::new materializes several 256-element arrays even when GameData
    # itself is initialized in place. Those temporaries overflow the macOS
    # async-startup thread's stack. Collect the large slot buffers directly on
    # the heap; boxed slices retain fixed lengths and the existing slot indices.
    set(se_player "${src}/audio_player/se_player.rs")
    foreach(type TrackHandle "Option<StaticSoundHandle>" "Option<StaticSoundData>" "Option<String>")
        rfvp_replace("${se_player}" "[${type}; SE_SLOT_COUNT]" "Box<[${type}]>")
    endforeach()
    rfvp_replace("${se_player}" "let se_tracks = [(); SE_SLOT_COUNT].map(|_| {"
        "let se_tracks = (0..SE_SLOT_COUNT).map(|_| {")
    rfvp_replace("${se_player}" ".expect(\"Failed to create se track\")\n        });"
        ".expect(\"Failed to create se track\")\n        }).collect::<Vec<_>>().into_boxed_slice();")
    foreach(field se_slots se_datas se_names)
        rfvp_replace("${se_player}" "${field}: [(); SE_SLOT_COUNT].map(|_| None),"
            "${field}: (0..SE_SLOT_COUNT).map(|_| None).collect::<Vec<_>>().into_boxed_slice(),")
    endforeach()
    foreach(player Bgm Se)
        string(TOLOWER "${player}" lower)
        rfvp_replace("${src}/audio_player/${lower}_player.rs"
            "impl ${player}Player {"
            "impl ${player}Player {\n    pub fn pause_host(&mut self, paused: bool) {\n        for track in self.${lower}_tracks.iter_mut() {\n            if paused { track.pause(Tween::default()); } else { track.resume(Tween::default()); }\n        }\n    }")
    endforeach()
endfunction()
