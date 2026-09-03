use std::collections::{HashMap, HashSet};
use std::ffi::{CStr, CString, c_char, c_void};
use std::fs;
use std::io::Cursor;
use std::mem::size_of;
use std::path::{Path, PathBuf};
use std::ptr;

use lewton::inside_ogg::OggStreamReader;

use crate::persistence::{
    ManualSaveData, load_manual_slot, load_system_dat, save_manual_slot, save_system_dat,
};
use crate::profile::{GameProfile, detect_game_profile};
use crate::scene::{Frame, GameMenuAction, MainMenuAction, SceneKind, SceneSystem};
use crate::script::Program;
use crate::vfs::Vfs;
use crate::vm::{
    BgmRequest, CharRequest, EffectRequest, ManualVmState, MessageRequest, MovieRequest,
    PanelRequest, SeRequest, SelectOption, StageRequest, TransitionConfig, Vm, VmEvent,
    infer_bgm_before,
};

const API_VERSION: u32 = 0x0100_0000;
const OK: i32 = 0;
const INVALID_ARGUMENT: i32 = -1;
const INVALID_STATE: i32 = -2;
const NOT_SUPPORTED: i32 = -3;
const IO_ERROR: i32 = -4;
const PIXEL_RGBA8888: u32 = 1;
const INPUT_POINTER_DOWN: u32 = 1;
const INPUT_KEY_DOWN: u32 = 5;

#[repr(C)]
pub struct EngineCreateDesc {
    struct_size: u32,
    api_version: u32,
    writable_path_utf8: *const c_char,
    cache_path_utf8: *const c_char,
    user_data: *mut c_void,
    reserved_u64: [u64; 4],
    reserved_ptr: [*mut c_void; 4],
}

#[repr(C)]
pub struct EngineFrameDesc {
    struct_size: u32,
    width: u32,
    height: u32,
    stride_bytes: u32,
    pixel_format: u32,
    frame_serial: u64,
    reserved_u64: [u64; 4],
    reserved_ptr: [*mut c_void; 4],
}

#[repr(C)]
pub struct EngineInputEvent {
    struct_size: u32,
    event_type: u32,
    timestamp_micros: u64,
    x: f64,
    y: f64,
    delta_x: f64,
    delta_y: f64,
    pointer_id: i32,
    button: i32,
    key_code: i32,
    modifiers: i32,
    unicode_codepoint: u32,
    reserved_u32: u32,
    reserved_u64: [u64; 2],
    reserved_ptr: [*mut c_void; 2],
}

#[repr(C)]
pub struct EngineRuntimeHost {
    struct_size: u32,
    api_version: u32,
    user_data: *mut c_void,
    log: Option<unsafe extern "C" fn(*mut c_void, u32, *const c_char, *const c_char)>,
    monotonic_time_micros: Option<unsafe extern "C" fn(*mut c_void) -> u64>,
    platform_request: Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char)>,
    reserved_u64: [u64; 4],
    reserved_ptr: [*mut c_void; 3],
}

type Unary = Option<unsafe extern "C" fn(*mut c_void) -> i32>;

#[repr(C)]
pub struct Provider {
    struct_size: u32,
    api_version: u32,
    runtime_id_utf8: *const c_char,
    display_name_utf8: *const c_char,
    priority: i32,
    provider_user_data: *mut c_void,
    probe: Option<unsafe extern "C" fn(*mut c_void, *const c_char) -> i32>,
    create: Option<
        unsafe extern "C" fn(
            *mut c_void,
            *const EngineRuntimeHost,
            *const EngineCreateDesc,
            *mut *mut c_void,
        ) -> i32,
    >,
    destroy: Option<unsafe extern "C" fn(*mut c_void)>,
    open_game: Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char) -> i32>,
    tick: Option<unsafe extern "C" fn(*mut c_void, u32) -> i32>,
    pause: Unary,
    resume: Unary,
    set_option: Option<unsafe extern "C" fn(*mut c_void, *const c_void) -> i32>,
    set_surface_size: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> i32>,
    get_frame_desc: Option<unsafe extern "C" fn(*mut c_void, *mut EngineFrameDesc) -> i32>,
    read_frame_rgba: Option<unsafe extern "C" fn(*mut c_void, *mut c_void, usize) -> i32>,
    get_godot_native_frame_texture:
        Option<unsafe extern "C" fn(*mut c_void, *mut u64, *mut u32, *mut u32, *mut u64) -> i32>,
    get_host_native_window: Option<unsafe extern "C" fn(*mut c_void, *mut *mut c_void) -> i32>,
    get_host_native_view: Option<unsafe extern "C" fn(*mut c_void, *mut *mut c_void) -> i32>,
    send_input: Option<unsafe extern "C" fn(*mut c_void, *const c_void) -> i32>,
    get_main_menu_json:
        Option<unsafe extern "C" fn(*mut c_void, *mut c_char, u32, *mut u32) -> i32>,
    activate_menu_item: Option<unsafe extern "C" fn(*mut c_void, *const c_char) -> i32>,
    set_render_target_iosurface: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32) -> i32>,
    set_render_target_surface:
        Option<unsafe extern "C" fn(*mut c_void, *mut c_void, u32, u32) -> i32>,
    get_frame_rendered_flag: Option<unsafe extern "C" fn(*mut c_void, *mut u32) -> i32>,
    get_renderer_info: Option<unsafe extern "C" fn(*mut c_void, *mut c_char, u32) -> i32>,
    get_memory_stats: Option<unsafe extern "C" fn(*mut c_void, *mut c_void) -> i32>,
    get_plugin_debug_info:
        Option<unsafe extern "C" fn(*mut c_void, *mut c_char, u32, *mut u32) -> i32>,
    get_last_error: Option<unsafe extern "C" fn(*mut c_void) -> *const c_char>,
    reserved_u64: [u64; 8],
    submit_platform_response:
        Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char) -> i32>,
    get_text_input_state: Option<unsafe extern "C" fn(*mut c_void, *mut u32) -> i32>,
    get_godot_presentation_state: Option<unsafe extern "C" fn(*mut c_void, *mut u32) -> i32>,
    reserved_ptr: [*mut c_void; 5],
}

struct Runtime {
    profile: Option<&'static GameProfile>,
    vfs: Option<Vfs>,
    vm: Option<Vm>,
    pending_transition: Option<TransitionConfig>,
    pending_stage: Option<StageRequest>,
    pending_panel: Option<PanelRequest>,
    current_message: Option<MessageRequest>,
    current_voice: Option<String>,
    current_bgm: Option<BgmRequest>,
    current_effect: Option<EffectRequest>,
    effect_fading_out: bool,
    current_se: [Option<SeRequest>; 3],
    current_movie: Option<MovieRequest>,
    voice_lip: Option<VoiceLipState>,
    message_reveal: Option<MessageRevealState>,
    message_icon_path: Option<PathBuf>,
    movie_data: Vec<u8>,
    movie_path: Option<PathBuf>,
    audio_paths: [Option<PathBuf>; 5],
    writable_path: PathBuf,
    cache_path: PathBuf,
    host_user_data: *mut c_void,
    platform_request: Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char)>,
    persistence_pending: bool,
    pending_save_slot: Option<u8>,
    persisted_globals: HashMap<String, i32>,
    scene: Option<SceneSystem>,
    error: CString,
    failed: bool,
    delivered_serial: u64,
}

struct VoiceLipState {
    samples: Vec<i16>,
    sample_rate: u32,
    channels: usize,
    elapsed_ms: u64,
    analyzed_blocks: usize,
    previous_amplitude: u32,
    mouth_state: u8,
    actor: Option<String>,
}

impl VoiceLipState {
    fn decode(data: &[u8], actor: Option<String>) -> Result<Self, String> {
        let mut reader = OggStreamReader::new(Cursor::new(data))
            .map_err(|error| format!("voice OGG header: {error}"))?;
        let sample_rate = reader.ident_hdr.audio_sample_rate;
        let channels = usize::from(reader.ident_hdr.audio_channels);
        let mut samples = Vec::new();
        while let Some(packet) = reader
            .read_dec_packet_itl()
            .map_err(|error| format!("voice OGG packet: {error}"))?
        {
            samples.extend_from_slice(&packet);
        }
        Ok(Self {
            samples,
            sample_rate,
            channels,
            elapsed_ms: 0,
            analyzed_blocks: 0,
            previous_amplitude: 0,
            mouth_state: 0,
            actor,
        })
    }

    fn advance(&mut self, delta_ms: u32) -> (u8, bool) {
        self.elapsed_ms = self.elapsed_ms.saturating_add(u64::from(delta_ms));
        let elapsed_frames =
            usize::try_from(self.elapsed_ms.saturating_mul(u64::from(self.sample_rate)) / 1000)
                .unwrap_or(usize::MAX);
        let total_frames = self.samples.len() / self.channels;
        let completed_blocks = elapsed_frames.min(total_frames) / 4096;
        if completed_blocks > self.analyzed_blocks {
            // The original 44.1 kHz mono DirectSound path publishes one
            // waveform value per 0x2000-byte (4096-frame) buffer segment.
            let block = completed_blocks - 1;
            let start = block * 4096 * self.channels;
            let end = ((block + 1) * 4096 * self.channels).min(self.samples.len());
            let amplitude = self.samples[start..end]
                .iter()
                .map(|sample| u32::from(sample.unsigned_abs()))
                .max()
                .unwrap_or(0);
            self.analyzed_blocks = completed_blocks;
            self.mouth_state = if amplitude >= 0x320 {
                if amplitude > self.previous_amplitude {
                    if self.mouth_state == 0 { 1 } else { 2 }
                } else if self.mouth_state != 1 || amplitude >= self.previous_amplitude / 2 {
                    1
                } else {
                    0
                }
            } else if self.mouth_state == 2 {
                1
            } else {
                0
            };
            self.previous_amplitude = amplitude;
        }
        (self.mouth_state, elapsed_frames >= total_frames)
    }
}

struct MessageRevealState {
    steps_remaining: u32,
    elapsed_ms: u32,
    started: bool,
}

impl MessageRevealState {
    fn advance(&mut self, delta_ms: u32) -> bool {
        if !self.started {
            self.started = true;
            self.steps_remaining = self.steps_remaining.saturating_sub(1);
            return self.steps_remaining == 0;
        }
        self.elapsed_ms = self.elapsed_ms.saturating_add(delta_ms);
        let steps = self.elapsed_ms / 100;
        self.elapsed_ms %= 100;
        self.steps_remaining = self.steps_remaining.saturating_sub(steps);
        self.steps_remaining == 0
    }
}

struct AudioHostRequest<'a> {
    slot: usize,
    name: &'a str,
    data: Option<&'a [u8]>,
    looped: bool,
    volume: i32,
    pan: i32,
    fade_in: i32,
    fade_out: i32,
}

impl AudioHostRequest<'_> {
    fn argument(&self, path: &str) -> CString {
        safe_cstring(&format!(
            "{}\t{path}\t{}\t{}\t{}\t{}\t{}\t{}",
            self.slot,
            u8::from(self.looped),
            self.volume.clamp(0, 100),
            self.pan.clamp(-100, 100),
            self.name,
            self.fade_in.max(0),
            self.fade_out.max(0)
        ))
    }
}

impl Runtime {
    fn save_manual_game(&self, slot: u8, name: &str) -> Result<(), String> {
        let vm = self
            .vm
            .as_ref()
            .ok_or("manual save requires an active VM")?;
        let frame = self
            .scene
            .as_ref()
            .ok_or("manual save requires an active scene")?
            .save_capture();
        let vm_state = vm.manual_state()?;
        let save = ManualSaveData {
            name: name.to_string(),
            script: vm_state.script,
            pc: Some(vm_state.pc),
            message: self.current_message.clone(),
            bgm: self.current_bgm.clone(),
            scene: self.scene.as_ref().map(SceneSystem::manual_state),
            globals: vm_state.globals,
            locals: vm_state.locals,
            control_enabled: vm_state.control_enabled,
            skip_enabled: vm_state.skip_enabled,
            seen_messages: vm_state.seen_messages.into_iter().collect(),
            width: frame.width,
            height: frame.height,
            rgba: frame.rgba,
        };
        save_manual_slot(
            &self
                .writable_path
                .join("save")
                .join(format!("slot-{:02}.sav", slot + 1)),
            &save,
        )
    }

    fn load_manual_game(&mut self, slot: u8) -> Result<(), String> {
        let path = self
            .writable_path
            .join("save")
            .join(format!("slot-{:02}.sav", slot + 1));
        let save =
            load_manual_slot(&path)?.ok_or_else(|| format!("save slot {} is empty", slot + 1))?;
        let pc = save
            .pc
            .ok_or("legacy save slot has no resumable program counter")?;
        let program = load_program(
            self.vfs.as_ref().ok_or("game VFS is not mounted")?,
            &save.script,
        )?;
        let active_message_id = save.message.as_ref().map(|message| message.id);
        let saved_bgm = save.bgm.clone().or_else(|| infer_bgm_before(&program, pc));
        let vm = Vm::from_manual_state(
            ManualVmState {
                script: save.script,
                pc,
                globals: save.globals,
                locals: save.locals,
                control_enabled: save.control_enabled,
                skip_enabled: save.skip_enabled,
                active_message_id,
                seen_messages: save.seen_messages.into_iter().collect::<HashSet<_>>(),
            },
            program,
        )?;
        let frame = Frame {
            width: save.width,
            height: save.height,
            rgba: save.rgba,
            serial: 0,
        };
        let scene = self
            .scene
            .as_mut()
            .ok_or("manual load requires an active scene")?;
        if let Some(state) = save.scene {
            scene.restore_manual_state(
                self.vfs.as_ref().ok_or("game VFS is not mounted")?,
                frame,
                state,
            )?;
        } else {
            scene.restore_save_capture(frame)?;
        }
        self.vm = Some(vm);
        self.current_message = save.message;
        self.message_reveal = None;
        self.voice_lip = None;
        self.current_voice = None;
        if let Some(message) = self.current_message.as_ref() {
            let presentation = message_display_text(&message.text)?;
            let speaker = message_speaker_text(&message.speaker);
            self.request_message(speaker.as_deref(), Some(&presentation.text), &[], 0)?;
        } else {
            self.request_message(None, None, &[], 0)?;
        }
        self.request_save_slots(&[])?;
        if let Some(bgm) = saved_bgm {
            let data = self
                .vfs
                .as_ref()
                .ok_or("game VFS is not mounted")?
                .read(&bgm.name)?;
            self.request_audio(AudioHostRequest {
                slot: 0,
                name: &bgm.name,
                data: Some(&data),
                looped: true,
                volume: bgm.volume,
                pan: 0,
                fade_in: 0,
                fade_out: 0,
            })?;
            self.current_bgm = Some(bgm);
        } else {
            self.request_audio(AudioHostRequest {
                slot: 0,
                name: "*",
                data: None,
                looped: false,
                volume: 100,
                pan: 0,
                fade_in: 0,
                fade_out: 0,
            })?;
            self.current_bgm = None;
        }
        Ok(())
    }

    fn save_globals(&mut self) -> Result<(), String> {
        let globals = self
            .vm
            .as_ref()
            .map(Vm::globals_snapshot)
            .unwrap_or_default();
        save_system_dat(&self.writable_path.join("system.dat"), &globals)?;
        self.persisted_globals = globals;
        self.persistence_pending = false;
        Ok(())
    }

    fn fail(&mut self, message: impl AsRef<str>, result: i32) -> i32 {
        self.error = safe_cstring(message.as_ref());
        self.failed = true;
        result
    }

    fn clear_movie(&mut self) {
        if let Some(path) = self.movie_path.take() {
            let _ = fs::remove_file(path);
        }
        self.current_movie = None;
        self.movie_data.clear();
    }

    fn clear_audio(&mut self) {
        for path in &mut self.audio_paths {
            if let Some(path) = path.take() {
                let _ = fs::remove_file(path);
            }
        }
    }

    fn request_message(
        &mut self,
        speaker: Option<&str>,
        text: Option<&str>,
        reveal_steps: &[u32],
        total_steps: u32,
    ) -> Result<(), String> {
        if self.message_icon_path.is_none() {
            let icon = self
                .profile
                .ok_or("message icon requires a game profile")?
                .message_wait;
            let data = self
                .vfs
                .as_ref()
                .ok_or("message icon requires a mounted VFS")?
                .read(icon.resource)?;
            let path = self.cache_path.join("minori-message-icon.png");
            fs::create_dir_all(&self.cache_path)
                .map_err(|error| format!("create message cache: {error}"))?;
            fs::write(&path, data).map_err(|error| format!("write message icon: {error}"))?;
            self.message_icon_path = Some(path);
        }
        let callback = self
            .platform_request
            .ok_or_else(|| "runtime host has no platform request callback".to_string())?;
        let argument = match text {
            Some(text) => {
                let schedule = reveal_steps
                    .iter()
                    .map(u32::to_string)
                    .collect::<Vec<_>>()
                    .join(",");
                safe_cstring(&format!(
                    "{}\t{text}\t{total_steps}\t{schedule}",
                    speaker.unwrap_or_default()
                ))
            }
            None => safe_cstring(""),
        };
        unsafe {
            callback(
                self.host_user_data,
                c"minori_message".as_ptr(),
                argument.as_ptr(),
            );
        }
        self.set_message_icon_visible(text.is_some() && total_steps == 0)?;
        Ok(())
    }

    fn set_message_icon_visible(&self, visible: bool) -> Result<(), String> {
        let Some(path) = self.message_icon_path.as_ref() else {
            return Ok(());
        };
        let callback = self
            .platform_request
            .ok_or_else(|| "runtime host has no platform request callback".to_string())?;
        let wait = self
            .profile
            .ok_or("message icon requires a game profile")?
            .message_wait;
        let argument = safe_cstring(&format!(
            "message_wait\t{}\t{}\t{}\t{}\t{}\t{}",
            path.to_string_lossy(),
            wait.frames,
            wait.interval_ms,
            wait.x,
            wait.y,
            u8::from(visible)
        ));
        unsafe {
            callback(
                self.host_user_data,
                c"presentation_sprite".as_ptr(),
                argument.as_ptr(),
            );
        }
        Ok(())
    }

    fn request_select(&self, options: &[SelectOption]) -> Result<(), String> {
        let callback = self
            .platform_request
            .ok_or_else(|| "runtime host has no platform request callback".to_string())?;
        let value = options
            .iter()
            .map(|option| option.text.as_str())
            .collect::<Vec<_>>()
            .join("\n");
        let argument = safe_cstring(&value);
        unsafe {
            callback(
                self.host_user_data,
                c"minori_select".as_ptr(),
                argument.as_ptr(),
            );
        }
        Ok(())
    }

    fn request_save_name(&self, slot: u8) -> Result<(), String> {
        let callback = self
            .platform_request
            .ok_or_else(|| "runtime host has no platform request callback".to_string())?;
        let argument = safe_cstring(&format!("Save data\tEnter a name for slot {}\t", slot + 1));
        unsafe {
            callback(
                self.host_user_data,
                c"text_input".as_ptr(),
                argument.as_ptr(),
            );
        }
        Ok(())
    }

    fn request_save_slots(&self, slots: &[(u8, String)]) -> Result<(), String> {
        let callback = self
            .platform_request
            .ok_or_else(|| "runtime host has no platform request callback".to_string())?;
        let layout = &self
            .profile
            .ok_or("save slot labels require a game profile")?
            .save_load;
        let value = slots
            .iter()
            .map(|(slot, name)| {
                let row = f64::from(*slot % 5);
                let column = usize::from(*slot >= 5);
                let x =
                    f64::from(layout.thumbnail_left[column] + layout.thumbnail_width as i32 + 6);
                let y = layout.first_row_top + row * layout.row_step + 10.0;
                let right = if column == 0 {
                    layout.left_column.right
                } else {
                    layout.right_column.right
                };
                let width = (right - x - 10.0).max(1.0);
                let height = (layout.row_height - 20.0).max(1.0);
                format!("{slot}\t{x}\t{y}\t{width}\t{height}\t{name}")
            })
            .collect::<Vec<_>>()
            .join("\n");
        let argument = safe_cstring(&value);
        unsafe {
            callback(
                self.host_user_data,
                c"minori_save_slots".as_ptr(),
                argument.as_ptr(),
            );
        }
        Ok(())
    }

    fn populate_save_slots(&mut self) -> Result<(), String> {
        let mut labels = Vec::new();
        let mut frames = Vec::new();
        for slot in 0..10_u8 {
            let path = self
                .writable_path
                .join("save")
                .join(format!("slot-{:02}.sav", slot + 1));
            if let Some(save) = load_manual_slot(&path)? {
                labels.push((slot, save.name));
                frames.push((
                    slot,
                    Frame {
                        width: save.width,
                        height: save.height,
                        rgba: save.rgba,
                        serial: 0,
                    },
                ));
            }
        }
        self.scene
            .as_mut()
            .ok_or("save slots require an active scene")?
            .populate_save_slots(&frames);
        self.request_save_slots(&labels)
    }

    fn reveal_message(&self) -> Result<(), String> {
        let callback = self
            .platform_request
            .ok_or_else(|| "runtime host has no platform request callback".to_string())?;
        unsafe {
            callback(
                self.host_user_data,
                c"minori_message_reveal".as_ptr(),
                c"".as_ptr(),
            );
        }
        self.set_message_icon_visible(true)?;
        Ok(())
    }

    fn request_audio(&mut self, request: AudioHostRequest<'_>) -> Result<(), String> {
        let callback = self
            .platform_request
            .ok_or_else(|| "runtime host has no platform request callback".to_string())?;
        if let Some(path) = self.audio_paths[request.slot].take() {
            let _ = fs::remove_file(path);
        }
        let path = if let Some(data) = request.data {
            fs::create_dir_all(&self.cache_path)
                .map_err(|error| format!("create audio cache: {error}"))?;
            let path = self.cache_path.join(format!(
                "minori-audio-{}-{}.ogg",
                std::process::id(),
                request.slot
            ));
            fs::write(&path, data).map_err(|error| format!("write audio cache: {error}"))?;
            self.audio_paths[request.slot] = Some(path.clone());
            path.to_string_lossy().into_owned()
        } else {
            String::new()
        };
        let argument = request.argument(&path);
        unsafe {
            callback(
                self.host_user_data,
                c"minori_audio".as_ptr(),
                argument.as_ptr(),
            );
        }
        Ok(())
    }

    fn play_voice_resource(&mut self, name: &str, actor: Option<String>) -> Result<(), String> {
        if name == "*" {
            self.request_audio(AudioHostRequest {
                slot: 1,
                name: "*",
                data: None,
                looped: false,
                volume: 100,
                pan: 0,
                fade_in: 0,
                fade_out: 0,
            })?;
            self.current_voice = None;
            self.voice_lip = None;
            if let Some(scene) = self.scene.as_mut() {
                scene.set_mouth_state(None, 0);
            }
            return Ok(());
        }
        if name.is_empty() {
            return Ok(());
        }
        let data = self
            .vfs
            .as_ref()
            .ok_or("game VFS is not mounted")?
            .read(name)?;
        self.request_audio(AudioHostRequest {
            slot: 1,
            name,
            data: Some(&data),
            looped: false,
            volume: 100,
            pan: 0,
            fade_in: 0,
            fade_out: 0,
        })?;
        self.current_voice = Some(name.to_string());
        self.voice_lip = Some(VoiceLipState::decode(&data, actor)?);
        Ok(())
    }

    fn request_movie(&mut self, request: MovieRequest, data: Vec<u8>) -> Result<(), String> {
        let callback = self
            .platform_request
            .ok_or_else(|| "runtime host has no platform request callback".to_string())?;
        self.clear_movie();
        fs::create_dir_all(&self.cache_path)
            .map_err(|error| format!("create movie cache: {error}"))?;
        let path = self.cache_path.join(format!(
            "minori-movie-{}-{}.avi",
            std::process::id(),
            request.id
        ));
        fs::write(&path, &data).map_err(|error| format!("write movie cache: {error}"))?;
        let argument = safe_cstring(&format!(
            "{}\t{}\t{}\t{}\t{}\t{}",
            path.to_string_lossy(),
            request.width,
            request.height,
            request.x,
            request.y,
            u8::from(request.skippable)
        ));
        self.movie_path = Some(path);
        self.movie_data = data;
        self.current_movie = Some(request);
        unsafe {
            callback(
                self.host_user_data,
                c"minori_movie".as_ptr(),
                argument.as_ptr(),
            );
        }
        Ok(())
    }
}

#[derive(Debug, PartialEq, Eq)]
struct MessagePresentation {
    text: String,
    transitions: Vec<(i32, i32, i32, i32)>,
    reveal_steps: Vec<u32>,
    total_steps: u32,
}

fn message_speaker_text(speaker: &str) -> Option<String> {
    if speaker.is_empty() || speaker.starts_with('#') {
        return None;
    }
    let name = if speaker.starts_with('@') {
        "？？？"
    } else {
        speaker.split_once('=').map_or(speaker, |(_, name)| name)
    };
    (!name.is_empty()).then(|| format!("【{name}】"))
}

fn message_lip_actor(speaker: &str) -> Option<String> {
    if speaker.is_empty() || speaker.starts_with('#') {
        return None;
    }
    let speaker = speaker.strip_prefix('@').unwrap_or(speaker).trim();
    let name = speaker
        .split_once('=')
        .map_or(speaker, |(_, name)| name)
        .trim();
    (!name.is_empty()).then(|| name.to_string())
}

fn message_display_text(text: &str) -> Result<MessagePresentation, String> {
    let mut output = String::with_capacity(text.len());
    let mut transitions = Vec::new();
    let mut reveal_steps = Vec::new();
    let mut total_steps = 0_u32;
    let mut chars = text.chars();
    while let Some(ch) = chars.next() {
        if ch != '\\' {
            output.push(ch);
            total_steps = total_steps.saturating_add(1);
            reveal_steps.push(total_steps);
            continue;
        }
        let Some(control) = chars.next() else {
            return Err("message ends with an incomplete control code".to_string());
        };
        match control {
            // CTextDrawer::drawText at 0x41D7A0 consumes these without
            // submitting either byte to the glyph renderer.
            'a' | 'v' => total_steps = total_steps.saturating_add(1),
            'n' => {
                output.push('\n');
                total_steps = total_steps.saturating_add(1);
                reveal_steps.push(total_steps);
            }
            'N' => {
                output.push('\n');
                reveal_steps.push(total_steps);
            }
            'x' => {
                if chars.next() != Some('{') {
                    return Err("message control \\x requires a braced subcommand".to_string());
                }
                let mut command = String::new();
                let mut closed = false;
                for next in chars.by_ref() {
                    if next == '}' {
                        closed = true;
                        break;
                    }
                    command.push(next);
                }
                if !closed {
                    return Err("message control \\x has no closing brace".to_string());
                }
                let fields: Vec<_> = command.split(',').collect();
                if fields.len() != 5 || fields[0] != "trans" {
                    return Err(format!("message subcommand is not implemented: {command}"));
                }
                let mut values = [0_i32; 4];
                for (index, field) in fields[1..].iter().enumerate() {
                    values[index] = field.parse::<i32>().map_err(|_| {
                        format!("message trans argument is not an integer: {field}")
                    })?;
                }
                transitions.push((values[0], values[1], values[2], values[3]));
                total_steps = total_steps.saturating_add(1);
            }
            other => return Err(format!("message control \\{other} is not implemented")),
        }
    }
    Ok(MessagePresentation {
        text: output,
        transitions,
        reveal_steps,
        total_steps,
    })
}

fn safe_cstring(value: &str) -> CString {
    CString::new(value.replace('\0', " ")).unwrap()
}

unsafe fn runtime<'a>(opaque: *mut c_void) -> Option<&'a mut Runtime> {
    unsafe { (opaque as *mut Runtime).as_mut() }
}

unsafe extern "C" fn probe(_: *mut c_void, root: *const c_char) -> i32 {
    if root.is_null() {
        return 0;
    }
    let path = unsafe { CStr::from_ptr(root) }.to_string_lossy();
    let root = Path::new(path.as_ref());
    if root.join("scr.paz").is_file()
        && root.join("sys.paz").is_file()
        && detect_game_profile(root).is_some()
    {
        98
    } else {
        0
    }
}

unsafe extern "C" fn create(
    _: *mut c_void,
    host: *const EngineRuntimeHost,
    desc: *const EngineCreateDesc,
    output: *mut *mut c_void,
) -> i32 {
    if host.is_null() || desc.is_null() || output.is_null() {
        return INVALID_ARGUMENT;
    }
    if unsafe { (*host).api_version } != API_VERSION {
        return NOT_SUPPORTED;
    }
    let writable_path = if unsafe { (*desc).writable_path_utf8 }.is_null() {
        std::env::temp_dir()
    } else {
        PathBuf::from(
            unsafe { CStr::from_ptr((*desc).writable_path_utf8) }
                .to_string_lossy()
                .as_ref(),
        )
    };
    let cache_path = if unsafe { (*desc).cache_path_utf8 }.is_null() {
        std::env::temp_dir()
    } else {
        PathBuf::from(
            unsafe { CStr::from_ptr((*desc).cache_path_utf8) }
                .to_string_lossy()
                .as_ref(),
        )
    };
    let value = Box::new(Runtime {
        profile: None,
        vfs: None,
        vm: None,
        pending_transition: None,
        pending_stage: None,
        pending_panel: None,
        current_message: None,
        current_voice: None,
        current_bgm: None,
        current_effect: None,
        effect_fading_out: false,
        current_se: [None, None, None],
        current_movie: None,
        voice_lip: None,
        message_reveal: None,
        message_icon_path: None,
        movie_data: Vec::new(),
        movie_path: None,
        audio_paths: [None, None, None, None, None],
        writable_path,
        cache_path,
        host_user_data: unsafe { (*host).user_data },
        platform_request: unsafe { (*host).platform_request },
        persistence_pending: false,
        pending_save_slot: None,
        persisted_globals: HashMap::new(),
        scene: None,
        error: safe_cstring(""),
        failed: false,
        delivered_serial: 0,
    });
    unsafe {
        *output = Box::into_raw(value) as *mut c_void;
    }
    OK
}

unsafe extern "C" fn destroy(opaque: *mut c_void) {
    if !opaque.is_null() {
        unsafe {
            let mut runtime = Box::from_raw(opaque as *mut Runtime);
            runtime.clear_movie();
            runtime.clear_audio();
            drop(runtime);
        }
    }
}

unsafe extern "C" fn open_game(opaque: *mut c_void, root: *const c_char, _: *const c_char) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    if root.is_null() {
        return runtime.fail("game root is null", INVALID_ARGUMENT);
    }
    let path = unsafe { CStr::from_ptr(root) }.to_string_lossy();
    let root_path = Path::new(path.as_ref());
    let result = detect_game_profile(root_path)
        .ok_or_else(|| "no Minori game profile matches this installation".to_string())
        .and_then(|profile| {
            let vfs = Vfs::mount_game_with_profile(root_path, profile)?;
            let scene = SceneSystem::main_menu(&vfs, profile)?;
            Ok((profile, vfs, scene))
        });
    match result {
        Ok((profile, vfs, scene)) => {
            runtime.profile = Some(profile);
            runtime.vfs = Some(vfs);
            runtime.vm = None;
            runtime.pending_transition = None;
            runtime.pending_stage = None;
            runtime.pending_panel = None;
            runtime.current_message = None;
            runtime.current_voice = None;
            runtime.current_bgm = None;
            runtime.current_effect = None;
            runtime.effect_fading_out = false;
            runtime.current_se = [None, None, None];
            runtime.voice_lip = None;
            runtime.message_reveal = None;
            runtime.message_icon_path = None;
            runtime.clear_movie();
            runtime.clear_audio();
            runtime.persistence_pending = false;
            runtime.pending_save_slot = None;
            runtime.error = safe_cstring("");
            runtime.persisted_globals =
                match load_system_dat(&runtime.writable_path.join("system.dat")) {
                    Ok(globals) => globals,
                    Err(error) => {
                        runtime.error = safe_cstring(&error);
                        HashMap::new()
                    }
                };
            runtime.scene = Some(scene);
            runtime.delivered_serial = 0;
            runtime.failed = false;
            OK
        }
        Err(error) => runtime.fail(error, IO_ERROR),
    }
}

unsafe extern "C" fn tick(opaque: *mut c_void, delta_ms: u32) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    if runtime.failed {
        return INVALID_STATE;
    }
    if runtime
        .scene
        .as_ref()
        .is_some_and(SceneSystem::game_menu_active)
    {
        runtime.error = safe_cstring("");
        return OK;
    }
    if runtime
        .message_reveal
        .as_mut()
        .is_some_and(|reveal| reveal.advance(delta_ms))
    {
        runtime.message_reveal = None;
        if let Err(error) = runtime.set_message_icon_visible(true) {
            return runtime.fail(error, INVALID_STATE);
        }
    }
    if let Some(voice_lip) = runtime.voice_lip.as_mut() {
        let actor = voice_lip.actor.clone();
        let (mouth_state, finished) = voice_lip.advance(delta_ms);
        if let Some(scene) = runtime.scene.as_mut() {
            scene.set_mouth_state(actor.as_deref(), mouth_state);
        }
        if finished {
            runtime.voice_lip = None;
            runtime.current_voice = None;
            if let Some(scene) = runtime.scene.as_mut() {
                scene.set_mouth_state(None, 0);
            }
        }
    }
    if let Some(scene) = runtime.scene.as_mut() {
        match scene.tick_scroll(delta_ms) {
            Ok(true) => {
                if let Some(vm) = runtime.vm.as_mut() {
                    vm.complete_scroll();
                }
            }
            Ok(false) => {}
            Err(error) => return runtime.fail(error, INVALID_STATE),
        }
    }

    if let Some(scene) = runtime.scene.as_mut()
        && let Err(error) = scene.tick_effect(delta_ms)
    {
        return runtime.fail(error, INVALID_STATE);
    }

    if let (Some(vfs), Some(scene)) = (&runtime.vfs, &mut runtime.scene)
        && let Err(error) = scene.tick_characters(vfs, delta_ms)
    {
        return runtime.fail(error, INVALID_STATE);
    }
    if runtime
        .scene
        .as_ref()
        .is_some_and(|scene| !scene.character_movement_active())
        && let Some(vm) = runtime.vm.as_mut()
    {
        vm.complete_char_move();
    }
    if runtime
        .scene
        .as_ref()
        .is_some_and(SceneSystem::panel_transition_active)
    {
        let completed = runtime
            .scene
            .as_mut()
            .is_some_and(|scene| scene.tick_panel_transition(delta_ms));
        if completed {
            let panel_visible = runtime
                .scene
                .as_ref()
                .is_some_and(SceneSystem::panel_visible);
            if let Some(vm) = runtime.vm.as_mut() {
                vm.complete_panel();
            }
            if !panel_visible {
                runtime.message_reveal = None;
                if let Err(error) = runtime.request_message(None, None, &[], 0) {
                    return runtime.fail(error, INVALID_STATE);
                }
            }
        }
        runtime.error = safe_cstring("");
        return OK;
    }
    if runtime
        .scene
        .as_ref()
        .is_some_and(SceneSystem::transition_active)
    {
        let completed = runtime
            .scene
            .as_mut()
            .is_some_and(|scene| scene.tick_transition(delta_ms));
        if completed && let Some(vm) = runtime.vm.as_mut() {
            vm.complete_transition();
        }
        runtime.error = safe_cstring("");
        return OK;
    }

    if runtime
        .scene
        .as_ref()
        .is_some_and(SceneSystem::shake_active)
        && let Some(scene) = runtime.scene.as_mut()
    {
        scene.tick_shake(delta_ms);
    }

    let result = match (&runtime.vfs, &mut runtime.vm) {
        (Some(vfs), Some(vm)) => vm.tick(delta_ms, |name| load_program(vfs, name)),
        _ => Ok(Vec::new()),
    };
    match result {
        Ok(events) => {
            for event in events {
                match event {
                    VmEvent::ApplyEffect(request) => match request.kind.as_str() {
                        "" | "*" => {
                            if let Some(scene) = runtime.scene.as_mut()
                                && let Err(error) = scene.clear_effect()
                            {
                                return runtime.fail(error, INVALID_STATE);
                            }
                            runtime.current_effect = None;
                            runtime.effect_fading_out = false;
                        }
                        "fadeout" => {
                            runtime.effect_fading_out = runtime.current_effect.is_some();
                            if let Some(scene) = runtime.scene.as_mut() {
                                scene.fadeout_effect();
                            }
                        }
                        "WScroll2" | "WScrollST" => {
                            let result = runtime
                                .vfs
                                .as_ref()
                                .ok_or_else(|| "game VFS is not mounted".to_string())
                                .and_then(|vfs| {
                                    runtime
                                        .scene
                                        .as_mut()
                                        .ok_or_else(|| {
                                            "scene system is not initialized".to_string()
                                        })
                                        .and_then(|scene| scene.start_effect(vfs, &request))
                                });
                            if let Err(error) = result {
                                return runtime.fail(error, INVALID_STATE);
                            }
                            runtime.current_effect = Some(request);
                        }
                        "Cutin" => {
                            let result = runtime
                                .vfs
                                .as_ref()
                                .ok_or_else(|| "game VFS is not mounted".to_string())
                                .and_then(|vfs| {
                                    runtime
                                        .scene
                                        .as_mut()
                                        .ok_or_else(|| {
                                            "scene system is not initialized".to_string()
                                        })?
                                        .start_cutin(vfs, &request)
                                });
                            if let Err(error) = result {
                                return runtime.fail(error, INVALID_STATE);
                            }
                            runtime.current_effect = Some(request);
                            runtime.effect_fading_out = false;
                        }
                        _ => {
                            return runtime.fail(
                                format!("unsupported effect kind: {}", request.kind),
                                INVALID_STATE,
                            );
                        }
                    },
                    VmEvent::ApplyChar(request) => {
                        let blocking = matches!(
                            &request,
                            CharRequest::Move { blocking: true, .. } | CharRequest::AllMove { .. }
                        );
                        let result = runtime
                            .vfs
                            .as_ref()
                            .ok_or_else(|| "game VFS is not mounted".to_string())
                            .and_then(|vfs| {
                                runtime
                                    .scene
                                    .as_mut()
                                    .ok_or_else(|| "scene system is not initialized".to_string())?
                                    .apply_char(vfs, request)
                            });
                        if let Err(error) = result {
                            return runtime.fail(error, INVALID_STATE);
                        }
                        if blocking
                            && runtime
                                .scene
                                .as_ref()
                                .is_some_and(SceneSystem::character_movement_active)
                            && let Some(vm) = runtime.vm.as_mut()
                        {
                            vm.begin_char_move();
                        }
                    }
                    VmEvent::PlayMovie(request) => {
                        let result = runtime
                            .vfs
                            .as_ref()
                            .ok_or_else(|| "game VFS is not mounted".to_string())
                            .and_then(|vfs| vfs.read(&request.name));
                        match result {
                            Ok(data)
                                if data.starts_with(b"RIFF")
                                    && data.get(8..12) == Some(b"AVI ") =>
                            {
                                if let Err(error) = runtime.request_movie(request, data) {
                                    return runtime.fail(error, INVALID_STATE);
                                }
                            }
                            Ok(_) => {
                                return runtime.fail(
                                    format!("movie is not a RIFF AVI: {}", request.name),
                                    INVALID_STATE,
                                );
                            }
                            Err(error) => return runtime.fail(error, INVALID_STATE),
                        }
                    }
                    VmEvent::PlayBgm(request) => {
                        if request.name == "*" {
                            if let Err(error) = runtime.request_audio(AudioHostRequest {
                                slot: 0,
                                name: "*",
                                data: None,
                                looped: false,
                                volume: 100,
                                pan: 0,
                                fade_in: 0,
                                fade_out: request.fade_out,
                            }) {
                                return runtime.fail(error, INVALID_STATE);
                            }
                            runtime.current_bgm = None;
                        } else if runtime.current_bgm.as_ref().is_some_and(|current| {
                            current.name == request.name && current.volume == request.volume
                        }) {
                            // The original returns before touching the active stream.
                        } else {
                            let result = runtime
                                .vfs
                                .as_ref()
                                .ok_or_else(|| "game VFS is not mounted".to_string())
                                .and_then(|vfs| vfs.read(&request.name));
                            let data = match result {
                                Ok(data) => data,
                                Err(error) => return runtime.fail(error, INVALID_STATE),
                            };
                            if let Err(error) = runtime.request_audio(AudioHostRequest {
                                slot: 0,
                                name: &request.name,
                                data: Some(&data),
                                looped: true,
                                volume: request.volume,
                                pan: 0,
                                fade_in: request.fade_in,
                                fade_out: request.fade_out,
                            }) {
                                return runtime.fail(error, INVALID_STATE);
                            }
                            runtime.current_bgm = Some(request);
                        }
                    }
                    VmEvent::PlaySe(request) => {
                        let slot = usize::from(request.channel.saturating_sub(1));
                        let host_slot = slot + 2;
                        if request.name == "*" {
                            if let Err(error) = runtime.request_audio(AudioHostRequest {
                                slot: host_slot,
                                name: "*",
                                data: None,
                                looped: false,
                                volume: 100,
                                pan: 0,
                                fade_in: 0,
                                fade_out: 0,
                            }) {
                                return runtime.fail(error, INVALID_STATE);
                            }
                            runtime.current_se[slot] = None;
                        } else {
                            let result = runtime
                                .vfs
                                .as_ref()
                                .ok_or_else(|| "game VFS is not mounted".to_string())
                                .and_then(|vfs| vfs.read(&request.name));
                            let data = match result {
                                Ok(data) => data,
                                Err(error) => return runtime.fail(error, INVALID_STATE),
                            };
                            if let Err(error) = runtime.request_audio(AudioHostRequest {
                                slot: host_slot,
                                name: &request.name,
                                data: Some(&data),
                                looped: request.looped,
                                volume: request.volume,
                                pan: request.pan,
                                fade_in: 0,
                                fade_out: 0,
                            }) {
                                return runtime.fail(error, INVALID_STATE);
                            }
                            runtime.current_se[slot] = Some(request);
                        }
                    }
                    VmEvent::PlayVoice(request) => {
                        if let Err(error) = runtime.play_voice_resource(&request.name, None) {
                            return runtime.fail(error, INVALID_STATE);
                        }
                    }
                    VmEvent::Select(options) => {
                        if let Err(error) = runtime.request_select(&options) {
                            return runtime.fail(error, INVALID_STATE);
                        }
                    }
                    VmEvent::SetPanel(request) => {
                        let result = runtime
                            .vfs
                            .as_ref()
                            .ok_or_else(|| "game VFS is not mounted".to_string())
                            .and_then(|vfs| {
                                runtime
                                    .scene
                                    .as_mut()
                                    .ok_or_else(|| "scene system is not initialized".to_string())?
                                    .apply_panel(vfs, &request)
                            });
                        if let Err(error) = result {
                            return runtime.fail(error, INVALID_STATE);
                        }
                        runtime.pending_panel = Some(request);
                    }
                    VmEvent::SetTransition(config) => {
                        runtime.pending_transition = Some(config);
                    }
                    VmEvent::StartShake(request) => {
                        let result = runtime
                            .scene
                            .as_mut()
                            .ok_or_else(|| "scene is not initialized".to_string())
                            .and_then(|scene| scene.start_shake(&request));
                        if let Err(error) = result {
                            return runtime.fail(error, INVALID_STATE);
                        }
                    }
                    VmEvent::StartScroll(request) => {
                        let Some(scene) = runtime.scene.as_mut() else {
                            return runtime.fail("scene is not initialized", INVALID_STATE);
                        };
                        scene.start_scroll(&request);
                    }
                    VmEvent::StartScrollXf(request) => {
                        let Some(scene) = runtime.scene.as_mut() else {
                            return runtime.fail("scene is not initialized", INVALID_STATE);
                        };
                        scene.start_scroll_xf(&request);
                    }
                    VmEvent::SetStage(request) => {
                        let result = runtime
                            .vfs
                            .as_ref()
                            .ok_or_else(|| "game VFS is not mounted".to_string())
                            .and_then(|vfs| {
                                runtime
                                    .scene
                                    .as_mut()
                                    .ok_or_else(|| "scene system is not initialized".to_string())?
                                    .apply_stage(vfs, &request, runtime.pending_transition.as_ref())
                            });
                        match result {
                            Ok(true) => {
                                if let Some(vm) = runtime.vm.as_mut() {
                                    vm.begin_transition();
                                }
                            }
                            Ok(false) => {}
                            Err(error) => return runtime.fail(error, INVALID_STATE),
                        }
                        runtime.pending_stage = Some(request);
                    }
                    VmEvent::ScriptChanged(_) => {}
                    VmEvent::PersistGlobals => {
                        // CommandChain ignores Variable::save's return value at
                        // 0x446B06, so persistence failure cannot stop NEXT.
                        runtime.persistence_pending = true;
                        if let Err(error) = runtime.save_globals() {
                            runtime.error = safe_cstring(&error);
                        }
                    }
                    VmEvent::EndMessage => {
                        runtime.message_reveal = None;
                        if let Err(error) = runtime.request_message(None, None, &[], 0) {
                            return runtime.fail(error, INVALID_STATE);
                        }
                    }
                    VmEvent::EndScroll { interrupt } => {
                        let active = runtime
                            .scene
                            .as_ref()
                            .is_some_and(SceneSystem::scroll_active);
                        if active {
                            if interrupt {
                                if let Some(scene) = runtime.scene.as_mut() {
                                    scene.interrupt_scroll();
                                }
                            } else if let Some(vm) = runtime.vm.as_mut() {
                                vm.begin_scroll();
                            }
                        }
                    }
                    VmEvent::Message(message) => {
                        let lip_actor = message_lip_actor(&message.speaker);
                        if let Err(error) = runtime.play_voice_resource(&message.voice, lip_actor) {
                            return runtime.fail(error, INVALID_STATE);
                        }
                        let presentation = match message_display_text(&message.text) {
                            Ok(presentation) => presentation,
                            Err(error) => return runtime.fail(error, INVALID_STATE),
                        };
                        let panel_visible = runtime
                            .scene
                            .as_ref()
                            .is_some_and(SceneSystem::panel_visible);
                        if let Some(scene) = runtime.scene.as_mut() {
                            scene.set_message_char_transitions(&presentation.transitions);
                        }
                        let speaker = message_speaker_text(&message.speaker);
                        let display_text = panel_visible.then_some(presentation.text.as_str());
                        let display_speaker = panel_visible.then_some(speaker.as_deref()).flatten();
                        runtime.message_reveal =
                            (presentation.total_steps > 0).then_some(MessageRevealState {
                                steps_remaining: presentation.total_steps,
                                elapsed_ms: 0,
                                started: false,
                            });
                        if let Err(error) = runtime.request_message(
                            display_speaker,
                            display_text,
                            &presentation.reveal_steps,
                            presentation.total_steps,
                        ) {
                            return runtime.fail(error, INVALID_STATE);
                        }
                        runtime.current_message = Some(message);
                    }
                }
            }
            runtime.error = safe_cstring("");
            OK
        }
        Err(error) => runtime.fail(error, INVALID_STATE),
    }
}
unsafe extern "C" fn unary(opaque: *mut c_void) -> i32 {
    if opaque.is_null() {
        INVALID_ARGUMENT
    } else {
        OK
    }
}
unsafe extern "C" fn set_option(opaque: *mut c_void, option: *const c_void) -> i32 {
    if opaque.is_null() || option.is_null() {
        INVALID_ARGUMENT
    } else {
        OK
    }
}
unsafe extern "C" fn set_surface_size(opaque: *mut c_void, width: u32, height: u32) -> i32 {
    if opaque.is_null() || width == 0 || height == 0 {
        INVALID_ARGUMENT
    } else {
        OK
    }
}

unsafe extern "C" fn get_frame_desc(opaque: *mut c_void, output: *mut EngineFrameDesc) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    if output.is_null() || unsafe { (*output).struct_size } < size_of::<EngineFrameDesc>() as u32 {
        return INVALID_ARGUMENT;
    }
    let Some(frame) = runtime.scene.as_ref().map(|scene| scene.frame()) else {
        return runtime.fail("startup scene is not ready", INVALID_STATE);
    };
    unsafe {
        (*output).width = frame.width;
        (*output).height = frame.height;
        (*output).stride_bytes = frame.width * 4;
        (*output).pixel_format = PIXEL_RGBA8888;
        (*output).frame_serial = frame.serial;
    }
    OK
}

unsafe extern "C" fn read_frame(
    opaque: *mut c_void,
    output: *mut c_void,
    output_size: usize,
) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    if output.is_null() {
        return INVALID_ARGUMENT;
    }
    let Some(frame) = runtime.scene.as_ref().map(|scene| scene.frame()) else {
        return runtime.fail("startup scene is not ready", INVALID_STATE);
    };
    if output_size < frame.rgba.len() {
        return runtime.fail("RGBA frame output buffer is too small", INVALID_ARGUMENT);
    }
    unsafe {
        ptr::copy_nonoverlapping(frame.rgba.as_ptr(), output as *mut u8, frame.rgba.len());
    }
    runtime.delivered_serial = frame.serial;
    OK
}

fn load_program(vfs: &Vfs, name: &str) -> Result<Program, String> {
    let bytes = vfs.read(name)?;
    let (source, _, had_errors) = encoding_rs::SHIFT_JIS.decode(&bytes);
    if had_errors {
        return Err(format!("{name}: invalid Shift-JIS script"));
    }
    Program::parse(&source).map_err(|error| format!("{name}: {error}"))
}

fn start_new_game(runtime: &mut Runtime) -> Result<(), String> {
    let profile = runtime.profile.ok_or("game profile is not selected")?;
    let vfs = runtime.vfs.as_ref().ok_or("game VFS is not mounted")?;
    let program = load_program(vfs, profile.entry_script)?;
    runtime
        .scene
        .as_mut()
        .ok_or("startup scene is not ready")?
        .enter_script_scene();
    let mut vm = Vm::new(profile.entry_script, program);
    vm.replace_globals(runtime.persisted_globals.clone());
    runtime.vm = Some(vm);
    runtime.error = safe_cstring("");
    runtime.failed = false;
    Ok(())
}

fn open_load_menu(runtime: &mut Runtime) -> Result<(), String> {
    let vfs = runtime.vfs.as_ref().ok_or("game VFS is not mounted")?;
    runtime
        .scene
        .as_mut()
        .ok_or("startup scene is not ready")?
        .open_load_menu(vfs)?;
    runtime.populate_save_slots()
}

unsafe extern "C" fn send_input(opaque: *mut c_void, event: *const c_void) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    if event.is_null() {
        return INVALID_ARGUMENT;
    }
    if runtime.failed {
        return INVALID_STATE;
    }
    let event = unsafe { &*(event as *const EngineInputEvent) };
    if event.struct_size < size_of::<EngineInputEvent>() as u32 {
        return runtime.fail("input event struct is too small", INVALID_ARGUMENT);
    }

    if event.event_type == INPUT_POINTER_DOWN {
        let main_action = runtime
            .scene
            .as_ref()
            .and_then(|scene| scene.main_menu_action(event.x, event.y));
        match main_action {
            Some(MainMenuAction::NewGame) => {
                return match start_new_game(runtime) {
                    Ok(()) => OK,
                    Err(error) => runtime.fail(error, INVALID_STATE),
                };
            }
            Some(MainMenuAction::LoadGame) => {
                return match open_load_menu(runtime) {
                    Ok(()) => OK,
                    Err(error) => runtime.fail(error, INVALID_STATE),
                };
            }
            None => {}
        }
    }
    if event.event_type == INPUT_POINTER_DOWN {
        let game_menu_action = match (&runtime.vfs, &mut runtime.scene) {
            (Some(vfs), Some(scene)) => scene.game_menu_pointer_down(vfs, event.x, event.y),
            _ => Ok(None),
        };
        match game_menu_action {
            Ok(Some(GameMenuAction::Save)) => {
                let result = match (&runtime.vfs, &mut runtime.scene) {
                    (Some(vfs), Some(scene)) => scene.open_save_menu(vfs),
                    _ => Err("save menu requires an open game".into()),
                };
                return match result.and_then(|()| runtime.populate_save_slots()) {
                    Ok(()) => OK,
                    Err(error) => runtime.fail(error, INVALID_STATE),
                };
            }
            Ok(Some(GameMenuAction::Load)) => {
                return match open_load_menu(runtime) {
                    Ok(()) => OK,
                    Err(error) => runtime.fail(error, INVALID_STATE),
                };
            }
            Ok(Some(GameMenuAction::SaveSlot(slot))) => {
                if runtime.pending_save_slot.is_some() {
                    return OK;
                }
                if let Err(error) = runtime.request_save_name(slot) {
                    return runtime.fail(error, INVALID_STATE);
                }
                runtime.pending_save_slot = Some(slot);
                return OK;
            }
            Ok(Some(GameMenuAction::LoadSlot(slot))) => {
                return match runtime.load_manual_game(slot) {
                    Ok(()) => OK,
                    Err(error)
                        if error.contains("is empty")
                            || error.contains("legacy save slot has no resumable") =>
                    {
                        runtime.error = safe_cstring(&error);
                        OK
                    }
                    Err(error) => runtime.fail(error, INVALID_STATE),
                };
            }
            Ok(Some(
                GameMenuAction::Consumed
                | GameMenuAction::QuickSave
                | GameMenuAction::System
                | GameMenuAction::Skip,
            )) => return OK,
            Ok(None) => {}
            Err(error) => return runtime.fail(error, INVALID_STATE),
        }
    }
    if matches!(event.event_type, INPUT_POINTER_DOWN | INPUT_KEY_DOWN) {
        if runtime.message_reveal.is_some() {
            runtime.message_reveal = None;
            if let Err(error) = runtime.reveal_message() {
                return runtime.fail(error, INVALID_STATE);
            }
            return OK;
        }
        if let Some(vm) = runtime.vm.as_mut() {
            vm.advance_input();
        }
    }
    OK
}

unsafe extern "C" fn rendered_flag(opaque: *mut c_void, output: *mut u32) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    if output.is_null() {
        return INVALID_ARGUMENT;
    }
    let Some(frame) = runtime.scene.as_ref().map(|scene| scene.frame()) else {
        unsafe {
            *output = 0;
        }
        return INVALID_STATE;
    };
    unsafe {
        *output = u32::from(frame.serial != runtime.delivered_serial);
    }
    OK
}

unsafe fn copy_string(value: &str, output: *mut c_char, size: u32, written: *mut u32) -> i32 {
    if output.is_null() || size == 0 {
        return INVALID_ARGUMENT;
    }
    let bytes = value.as_bytes();
    let count = bytes.len().min(size as usize - 1);
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), output as *mut u8, count);
        *output.add(count) = 0;
        if !written.is_null() {
            *written = count as u32;
        }
    }
    if count == bytes.len() {
        OK
    } else {
        INVALID_ARGUMENT
    }
}

unsafe extern "C" fn renderer_info(opaque: *mut c_void, output: *mut c_char, size: u32) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    let scene = match runtime.scene.as_ref().map(|scene| scene.current) {
        Some(SceneKind::MainMenu) => "main_menu",
        Some(SceneKind::Script) => "script",
        None => "none",
    };
    unsafe {
        copy_string(
            &format!("backend=minori-rust scene={scene} frame=rgba8 source=paz"),
            output,
            size,
            ptr::null_mut(),
        )
    }
}

unsafe extern "C" fn debug_info(
    opaque: *mut c_void,
    output: *mut c_char,
    size: u32,
    written: *mut u32,
) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    let (script, state) = runtime
        .vm
        .as_ref()
        .map_or(("none", "none".to_string()), |vm| {
            (
                vm.current_script().unwrap_or("none"),
                format!("{:?}", vm.state()),
            )
        });
    let scene = match runtime.scene.as_ref().map(|scene| scene.current) {
        Some(SceneKind::MainMenu) => "main_menu",
        Some(SceneKind::Script) => "script",
        None => "none",
    };
    let frame = runtime.scene.as_ref().map(|scene| scene.frame());
    let frame_serial = frame.map_or(0, |frame| frame.serial);
    let first_pixel = frame
        .and_then(|frame| frame.rgba.get(..4))
        .map_or_else(|| "none".to_string(), |pixel| format!("{:02x?}", pixel));
    let message = runtime
        .current_message
        .as_ref()
        .map_or("none".to_string(), |message| message.id.to_string());
    let voice = runtime.current_voice.as_deref().unwrap_or("none");
    let bgm = runtime
        .current_bgm
        .as_ref()
        .map_or("none", |request| request.name.as_str());
    let movie = runtime
        .current_movie
        .as_ref()
        .map_or("none", |request| request.name.as_str());
    let se = runtime.current_se[0]
        .as_ref()
        .map_or("none", |request| request.name.as_str());
    let se2 = runtime.current_se[1]
        .as_ref()
        .map_or("none", |request| request.name.as_str());
    let se3 = runtime.current_se[2]
        .as_ref()
        .map_or("none", |request| request.name.as_str());
    let effect = runtime
        .current_effect
        .as_ref()
        .map_or("none", |request| request.kind.as_str());
    let lip_actor = runtime
        .voice_lip
        .as_ref()
        .and_then(|lip| lip.actor.as_deref())
        .unwrap_or("none");
    let mouths = runtime
        .scene
        .as_ref()
        .map_or_else(String::new, SceneSystem::mouth_debug);
    let value = format!(
        "runtime=minori language=rust vfs=paz-v2 vm=pc-stack-waits scene={scene} script={script} state={state} message={message} voice={voice} lip_actor={lip_actor} mouths={mouths} bgm={bgm} se={se} se2={se2} se3={se3} movie={movie} effect={effect} persistence_pending={} frame={frame_serial} delivered={} pixel={first_pixel} error={}",
        runtime.persistence_pending,
        runtime.delivered_serial,
        runtime.error.to_string_lossy()
    );
    unsafe { copy_string(&value, output, size, written) }
}

unsafe extern "C" fn submit_platform_response(
    opaque: *mut c_void,
    operation: *const c_char,
    argument: *const c_char,
) -> i32 {
    let Some(runtime) = (unsafe { runtime(opaque) }) else {
        return INVALID_ARGUMENT;
    };
    if operation.is_null() || argument.is_null() {
        return INVALID_ARGUMENT;
    }
    let operation = unsafe { CStr::from_ptr(operation) }.to_string_lossy();
    let argument = unsafe { CStr::from_ptr(argument) }.to_string_lossy();
    if operation == "text_input" {
        let Some(slot) = runtime.pending_save_slot.take() else {
            return runtime.fail("unexpected text_input response", INVALID_STATE);
        };
        if argument == "result=cancel" {
            return OK;
        }
        let Some(name) = argument.strip_prefix("result=ok\t") else {
            return runtime.fail("invalid text_input response", INVALID_ARGUMENT);
        };
        return match runtime.save_manual_game(slot, name) {
            Ok(()) => {
                if let Some(scene) = runtime.scene.as_mut() {
                    scene.close_save_menu();
                }
                if let Err(error) = runtime.request_save_slots(&[]) {
                    return runtime.fail(error, INVALID_STATE);
                }
                OK
            }
            Err(error) => runtime.fail(error, IO_ERROR),
        };
    }
    if operation == "minori_select" {
        let Some(index) = argument
            .strip_prefix("index=")
            .and_then(|value| value.parse::<usize>().ok())
        else {
            return runtime.fail("invalid minori_select response", INVALID_ARGUMENT);
        };
        let Some(vm) = runtime.vm.as_mut() else {
            return runtime.fail("select response without a VM", INVALID_STATE);
        };
        return match vm.complete_select(index) {
            Ok(()) => OK,
            Err(error) => runtime.fail(error, INVALID_ARGUMENT),
        };
    }
    if operation == "minori_audio" {
        return if argument.starts_with("result=error") {
            runtime.fail(argument.as_ref(), INVALID_STATE)
        } else {
            OK
        };
    }
    if operation != "minori_movie" {
        return NOT_SUPPORTED;
    }
    if argument.starts_with("result=error") {
        return runtime.fail(argument.as_ref(), INVALID_STATE);
    }
    if !matches!(argument.as_ref(), "result=complete" | "result=skipped") {
        return runtime.fail(
            format!("invalid minori movie response: {argument}"),
            INVALID_ARGUMENT,
        );
    }
    if let Some(vm) = runtime.vm.as_mut() {
        vm.complete_movie();
    }
    runtime.clear_movie();
    OK
}

unsafe extern "C" fn last_error(opaque: *mut c_void) -> *const c_char {
    unsafe { runtime(opaque) }.map_or(c"Minori runtime handle is null".as_ptr(), |value| {
        value.error.as_ptr()
    })
}

#[cfg(test)]
mod tests {
    use super::{
        AudioHostRequest, MessageRevealState, VoiceLipState, message_display_text,
        message_lip_actor, message_speaker_text,
    };

    #[test]
    fn message_speaker_uses_original_mode_one_brackets_and_prefix_rules() {
        assert_eq!(
            message_speaker_text("アリス").as_deref(),
            Some("【アリス】")
        );
        assert_eq!(
            message_speaker_text("voice=アリス").as_deref(),
            Some("【アリス】")
        );
        assert_eq!(
            message_speaker_text("@hidden").as_deref(),
            Some("【？？？】")
        );
        assert_eq!(message_speaker_text("#　　　"), None);
        assert_eq!(message_speaker_text(""), None);
    }

    #[test]
    fn message_lip_actor_uses_the_original_visible_speaker_identity() {
        assert_eq!(message_lip_actor("アリス").as_deref(), Some("アリス"));
        assert_eq!(message_lip_actor("voice=アリス").as_deref(), Some("アリス"));
        assert_eq!(message_lip_actor("@hidden").as_deref(), Some("hidden"));
        assert_eq!(message_lip_actor("@　桜").as_deref(), Some("桜"));
        assert_eq!(message_lip_actor("#　　　"), None);
    }

    #[test]
    fn voice_lip_state_uses_original_pcm_threshold_and_rise_fall_states() {
        let mut lip = VoiceLipState {
            samples: [900_i16, 1000, 900, 100]
                .into_iter()
                .flat_map(|sample| std::iter::repeat_n(sample, 4096))
                .collect(),
            sample_rate: 44_100,
            channels: 1,
            elapsed_ms: 0,
            analyzed_blocks: 0,
            previous_amplitude: 0,
            mouth_state: 0,
            actor: Some("アリス".into()),
        };
        assert_eq!(lip.advance(92), (0, false));
        assert_eq!(lip.advance(1), (1, false));
        assert_eq!(lip.advance(93), (2, false));
        assert_eq!(lip.advance(93), (1, false));
        assert_eq!(lip.advance(93), (0, true));
    }

    #[test]
    fn message_display_text_consumes_only_reversed_c_text_drawer_controls() {
        let presentation = message_display_text("\\a\\vfirst\\Nsecond\\nthird").unwrap();
        assert_eq!(presentation.text, "first\nsecond\nthird");
        assert!(presentation.transitions.is_empty());
        assert_eq!(presentation.total_steps, 19);
        assert_eq!(presentation.reveal_steps.len(), 18);
        assert_eq!(&presentation.reveal_steps[..6], &[3, 4, 5, 6, 7, 7]);
        assert_eq!(
            message_display_text("before\\pafter").unwrap_err(),
            "message control \\p is not implemented"
        );
        assert!(message_display_text("incomplete\\").is_err());
    }

    #[test]
    fn message_trans_subcommand_preserves_original_delay_layer_duration_and_blend() {
        let presentation =
            message_display_text("\\x{trans,70,10,65,0}\\x{trans,50,11,55,255}dialogue").unwrap();
        assert_eq!(presentation.text, "dialogue");
        assert_eq!(
            presentation.transitions,
            vec![(70, 10, 65, 0), (50, 11, 55, 255)]
        );
        assert_eq!(presentation.total_steps, 10);
        assert_eq!(presentation.reveal_steps.first(), Some(&3));
        assert!(message_display_text("\\x{unknown,1,2,3,4}text").is_err());
        assert!(message_display_text("\\x{trans,1,no,3,4}text").is_err());
    }

    #[test]
    fn message_reveal_uses_immediate_first_step_then_one_hundred_ms_steps() {
        let mut reveal = MessageRevealState {
            steps_remaining: 3,
            elapsed_ms: 0,
            started: false,
        };
        assert!(!reveal.advance(16));
        assert_eq!(reveal.steps_remaining, 2);
        assert!(!reveal.advance(99));
        assert_eq!(reveal.steps_remaining, 2);
        assert!(!reveal.advance(1));
        assert_eq!(reveal.steps_remaining, 1);
        assert!(reveal.advance(100));
    }

    #[test]
    fn audio_host_payload_preserves_slots_mix_and_original_fade_intervals() {
        let request = AudioHostRequest {
            slot: 3,
            name: "effect.ogg",
            data: None,
            looped: true,
            volume: 125,
            pan: -150,
            fade_in: 2,
            fade_out: 10,
        };
        assert_eq!(
            request.argument("/tmp/effect.ogg").to_str().unwrap(),
            "3\t/tmp/effect.ogg\t1\t100\t-100\teffect.ogg\t2\t10"
        );
    }
}

static mut PROVIDER: Provider = Provider {
    struct_size: size_of::<Provider>() as u32,
    api_version: API_VERSION,
    runtime_id_utf8: c"minori".as_ptr(),
    display_name_utf8: c"Minori (Rust)".as_ptr(),
    priority: 95,
    provider_user_data: ptr::null_mut(),
    probe: Some(probe),
    create: Some(create),
    destroy: Some(destroy),
    open_game: Some(open_game),
    tick: Some(tick),
    pause: Some(unary),
    resume: Some(unary),
    set_option: Some(set_option),
    set_surface_size: Some(set_surface_size),
    get_frame_desc: Some(get_frame_desc),
    read_frame_rgba: Some(read_frame),
    get_godot_native_frame_texture: None,
    get_host_native_window: None,
    get_host_native_view: None,
    send_input: Some(send_input),
    get_main_menu_json: None,
    activate_menu_item: None,
    set_render_target_iosurface: None,
    set_render_target_surface: None,
    get_frame_rendered_flag: Some(rendered_flag),
    get_renderer_info: Some(renderer_info),
    get_memory_stats: None,
    get_plugin_debug_info: Some(debug_info),
    get_last_error: Some(last_error),
    reserved_u64: [0; 8],
    submit_platform_response: Some(submit_platform_response),
    get_text_input_state: None,
    get_godot_presentation_state: None,
    reserved_ptr: [ptr::null_mut(); 5],
};

unsafe extern "C" {
    fn engine_register_runtime_provider(provider: *const Provider) -> i32;
}

/// Registers the process-lifetime provider descriptor with engine_api.
///
/// # Safety
///
/// The linked engine_api must expose the v1 provider ABI used by `Provider`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn aetherkiri_minori_register_runtime_provider() -> i32 {
    unsafe { engine_register_runtime_provider(&raw const PROVIDER) }
}
