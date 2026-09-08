// SPDX-License-Identifier: MPL-2.0
// AetherKiri's embedded host for the pinned rfvp runtime. Compiled inside the
// build-tree copy of rfvp so no private Rust layout crosses the C ABI.
use crate::{
    font::Font,
    script::{
        global::GLOBAL,
        parser::{Nls, Parser},
    },
    soft_render::{create_soft_renderer, PixelFormat, SoftRenderer},
    subsystem::{
        anzu_scene::AnzuScene,
        global_savedata::{save_global_savedata_v1, try_load_global_savedata_v1},
        resources::{
            input_manager::KeyCode, motion_manager::DissolveType, thread_manager::ThreadManager,
            vfs::Vfs, window::Window,
        },
        save_state::SaveStateSnapshotV1,
        scene::{SceneAction, SceneMachine},
        scheduler::Scheduler,
        world::GameData,
    },
    vm_runner::VmRunner,
};
use anyhow::{anyhow, ensure, Context, Result};
use std::{
    collections::VecDeque,
    ffi::{c_char, c_void, CStr, CString},
    fs,
    panic::{catch_unwind, AssertUnwindSafe},
    path::{Path, PathBuf},
    ptr,
    str::FromStr,
    sync::Mutex,
    time::{Duration, Instant},
};

const MAX_SCRIPT: u64 = 64 * 1024 * 1024;
const HOST_GPU_CALLBACKS_API_VERSION: u32 = 0x01000000;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct HostGpuRect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct HostGpuPoint {
    pub x: f64,
    pub y: f64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct HostGpuCallbacks {
    pub(crate) struct_size: u32,
    pub(crate) api_version: u32,
    pub create_rgba: Option<unsafe extern "C" fn(u32, u32, *const c_void, u32) -> u64>,
    pub release_texture: Option<unsafe extern "C" fn(u64)>,
    pub update_rgba:
        Option<unsafe extern "C" fn(u64, *const c_void, u32, *const HostGpuRect) -> bool>,
    pub clear_rgba: Option<unsafe extern "C" fn(u64, u32, *const HostGpuRect) -> bool>,
    pub draw_triangles: Option<
        unsafe extern "C" fn(
            u64,
            u64,
            u32,
            *const HostGpuRect,
            *const HostGpuPoint,
            *const HostGpuPoint,
            f32,
            u32,
        ) -> bool,
    >,
    pub read_rgba: Option<unsafe extern "C" fn(u64, *mut c_void, usize, u32) -> bool>,
    pub begin_batch: Option<unsafe extern "C" fn() -> u64>,
    pub end_batch: Option<unsafe extern "C" fn(u64) -> bool>,
    pub flush: Option<unsafe extern "C" fn() -> bool>,
}
impl HostGpuCallbacks {
    fn valid(&self) -> bool {
        self.struct_size >= std::mem::size_of::<Self>() as u32
            && self.api_version == HOST_GPU_CALLBACKS_API_VERSION
            && self.create_rgba.is_some()
            && self.release_texture.is_some()
            && self.update_rgba.is_some()
            && self.clear_rgba.is_some()
            && self.draw_triangles.is_some()
            && self.read_rgba.is_some()
            && self.begin_batch.is_some()
            && self.end_batch.is_some()
            && self.flush.is_some()
    }
}

static ROOTS: Mutex<Option<(PathBuf, PathBuf)>> = Mutex::new(None);
pub fn game_root() -> Option<PathBuf> {
    ROOTS.lock().unwrap().as_ref().map(|r| r.0.clone())
}
pub fn save_root() -> Option<PathBuf> {
    ROOTS.lock().unwrap().as_ref().map(|r| r.1.clone())
}
struct RootsGuard;
impl Drop for RootsGuard {
    fn drop(&mut self) {
        *ROOTS.lock().unwrap() = None;
    }
}
impl RootsGuard {
    fn install(game: &Path, script: &Path, nls: Nls, writable: &Path) -> Result<Self> {
        let mut roots = ROOTS.lock().unwrap();
        ensure!(roots.is_none(), "Only one rfvp game may be open at a time");
        // Stable path identity, not DefaultHasher's implementation-dependent hash.
        let identity = format!("{}\0{}\0{nls:?}", game.display(), script.display());
        let hash = identity.bytes().fold(0xcbf29ce484222325u64, |h, b| {
            (h ^ b as u64).wrapping_mul(0x100000001b3)
        });
        let saves = writable.join("rfvp").join(format!("{hash:016x}"));
        fs::create_dir_all(saves.join("save"))?;
        *roots = Some((game.to_owned(), saves));
        Ok(Self)
    }
}

static LOGS: Mutex<VecDeque<(u32, String)>> = Mutex::new(VecDeque::new());
struct Logger;
static LOGGER: Logger = Logger;
impl log::Log for Logger {
    fn enabled(&self, m: &log::Metadata) -> bool {
        m.level() <= log::Level::Info
    }
    fn log(&self, r: &log::Record) {
        if !self.enabled(r.metadata()) {
            return;
        }
        let mut text = format!("{}: {}", r.target(), r.args());
        if text.len() > 3500 {
            let mut end = 3500;
            while !text.is_char_boundary(end) {
                end -= 1;
            }
            text.truncate(end);
        }
        if let Ok(mut logs) = LOGS.lock() {
            if logs.len() == 256 {
                logs.pop_front();
            }
            logs.push_back((
                match r.level() {
                    log::Level::Error => 5,
                    log::Level::Warn => 4,
                    log::Level::Info => 3,
                    log::Level::Debug => 2,
                    log::Level::Trace => 1,
                },
                text,
            ));
        }
    }
    fn flush(&self) {}
}

fn script_path(path: &Path, startup: &str) -> Result<(PathBuf, PathBuf)> {
    let explicit = if path.is_file() {
        Some(path.to_owned())
    } else {
        None
    };
    let root = fs::canonicalize(if explicit.is_some() {
        path.parent().unwrap()
    } else {
        path
    })?;
    ensure!(root.is_dir(), "Game root is not a directory");
    if !startup.is_empty() {
        let candidate = fs::canonicalize(root.join(startup))?;
        ensure!(
            candidate.starts_with(&root),
            "Startup script must be inside the game directory"
        );
        ensure!(
            candidate
                .extension()
                .is_some_and(|e| e.eq_ignore_ascii_case("hcb")),
            "Select an HCB script"
        );
        return Ok((root, candidate));
    }
    if let Some(file) = explicit {
        if file
            .extension()
            .is_some_and(|e| e.eq_ignore_ascii_case("hcb"))
        {
            return Ok((root, fs::canonicalize(file)?));
        }
        ensure!(
            file.extension()
                .is_some_and(|e| e.eq_ignore_ascii_case("exe")),
            "Select an HCB script, an executable beside it, or a game directory"
        );
    }
    let mut candidates = Vec::new();
    for entry in fs::read_dir(&root)? {
        let path = entry?.path();
        if path.is_file()
            && path
                .extension()
                .is_some_and(|e| e.eq_ignore_ascii_case("hcb"))
        {
            candidates.push(path);
        }
    }
    ensure!(
        candidates.len() == 1,
        "Expected one HCB script; select a specific HCB when several exist"
    );
    Ok((root, candidates.remove(0)))
}
fn parse(path: &Path, nls: Nls) -> Result<Parser> {
    let size = fs::metadata(path)?.len();
    ensure!(
        size >= 17 && size <= MAX_SCRIPT,
        "Invalid or oversized HCB script"
    );
    let parser = Parser::new(path, nls)?;
    ensure!(
        parser.entry_point >= 4
            && parser.entry_point < parser.sys_desc_offset
            && parser.sys_desc_offset < size as u32
            && parser.get_game_mode() <= 15,
        "Invalid HCB entry point or resolution"
    );
    Ok(parser)
}

struct Core {
    world: Box<GameData>,
    parser: Parser,
    vm: VmRunner,
    scene: SceneMachine,
    scheduler: Scheduler,
    renderer: SoftRenderer,
    size: (u32, u32),
    serial: u64,
    paused: bool,
    paused_at: Option<Instant>,
    dissolve: DissolveType,
    dissolve2: bool,
    // Rust drops fields in declaration order: paths outlive the world and audio.
    _roots: RootsGuard,
}
impl Core {
    fn new(
        path: &Path,
        startup: &str,
        writable: &Path,
        font: &str,
        nls: Nls,
        gpu: Option<HostGpuCallbacks>,
        require_gpu: bool,
    ) -> Result<Self> {
        ensure!(
            !writable.as_os_str().is_empty(),
            "A writable save directory is required"
        );
        let (root, script) = script_path(path, startup)?;
        let parser = parse(&script, nls)?;
        let roots = RootsGuard::install(&root, &script, nls, writable)?;
        crate::subsystem::components::syscalls::legacy::reset_host_state();
        crate::subsystem::components::syscalls::utils::take_pending_exit_dialog_request();
        let mut storage = Box::<GameData>::new_uninit();
        // Upstream in-place initialization avoids placing the large world on
        // the host's (especially mobile) stack.
        let mut world = unsafe {
            GameData::init_default_in_place(storage.as_mut_ptr());
            storage.assume_init()
        };
        world.vfs = Vfs::new(nls)?;
        world.nls = nls;
        let size = parser.get_screen_size();
        world.set_window(Window::new(size, 1.0));
        world.set_can_fullscreen(false);
        if !font.is_empty() {
            let bytes = fs::read(font).context("Read runtime font")?;
            world
                .fontface_manager
                .set_host_font(Font::from_vec(bytes).map_err(|_| anyhow!("Invalid runtime font"))?);
        }
        world.fontface_manager.init_fontface()?;
        GLOBAL.lock().unwrap().init_with(
            parser.get_non_volatile_global_count(),
            parser.get_volatile_global_count(),
        );
        try_load_global_savedata_v1(&mut world).context("Load rfvp global save")?;
        let mut vm = VmRunner::new(ThreadManager::new());
        vm.start_main(parser.get_entry_point());
        let mut scene = SceneMachine {
            current_scene: Some(Box::<AnzuScene>::default()),
        };
        scene.apply_scene_action(SceneAction::Start, &mut world);
        let mut renderer = create_soft_renderer(size.0, size.1, PixelFormat::Rgba8)?;
        let gpu_installed = gpu.is_some_and(|callbacks| renderer.install_host_gpu(callbacks));
        ensure!(
            !require_gpu || gpu_installed,
            "Godot GPU Bridge is unavailable for rfvp"
        );
        if gpu.is_some() && !gpu_installed {
            log::warn!("rfvp GPU renderer initialization failed; using CPU fallback");
        }
        Ok(Self {
            world,
            parser,
            vm,
            scene,
            scheduler: Scheduler::default(),
            renderer,
            size,
            serial: 0,
            paused: false,
            paused_at: None,
            dissolve: DissolveType::None,
            dissolve2: false,
            _roots: roots,
        })
    }
    fn step(&mut self, delta: u32) -> Result<i32> {
        if self.paused {
            return Ok(0);
        }
        let gd = &mut *self.world;
        gd.motion_manager.text_manager.set_render_scale(1.0);
        gd.time_mut_ref()
            .set_external_delta(Duration::from_millis(delta as u64));
        gd.time_mut_ref().frame();
        gd.timer_manager.tick(delta);
        gd.inputs_manager.begin_frame();
        gd.video_manager.tick(&mut gd.motion_manager)?;
        let dissolve = gd.motion_manager.get_dissolve_type();
        let dissolve2 = gd.motion_manager.is_dissolve2_transitioning();
        let done = (self.dissolve != DissolveType::None
            && self.dissolve != DissolveType::Static
            && (dissolve == DissolveType::None || dissolve == DissolveType::Static))
            || (self.dissolve2 && !dissolve2);
        self.dissolve = dissolve;
        self.dissolve2 = dissolve2;
        gd.set_current_thread(0);
        gd.set_halt(false);
        if done {
            self.vm.tick(gd, &mut self.parser, 0)?;
        }
        self.vm.tick(gd, &mut self.parser, delta as u64)?;
        // Legacy native dialogs need a host UI. Report the boundary instead of
        // leaving the VM waiting forever for an invisible standalone window.
        ensure!(crate::subsystem::components::syscalls::legacy::take_pending_save_load_request().is_none(),
            "This game requires an rfvp legacy native save/load dialog, which is not supported by this host yet");
        if crate::subsystem::components::syscalls::utils::take_pending_exit_dialog_request() {
            return Ok(1);
        }
        self.scene.apply_scene_action(SceneAction::Update, gd);
        self.scheduler.execute(gd);
        self.scene.apply_scene_action(SceneAction::LateUpdate, gd);
        self.scene.apply_scene_action(SceneAction::EndFrame, gd);
        self.renderer.render_frame(&gd.motion_manager)?;
        gd.inputs_manager.frame_reset();
        self.serial = self.serial.wrapping_add(1).max(1);
        self.save_capture()?;
        Ok(
            if self.world.get_lock_scripter() && self.world.get_main_thread_exited() {
                1
            } else {
                0
            },
        )
    }
    fn save_capture(&mut self) -> Result<()> {
        let gd = &mut *self.world;
        let nls = gd.nls;
        if let Some((slot, w, h)) = gd.save_manager.pending_save_capture() {
            ensure!(
                w > 0 && h > 0 && w <= 4096 && h <= 4096,
                "Invalid save thumbnail size"
            );
            ensure!(
                self.renderer.sync_host_gpu_framebuffer(),
                "Read rfvp GPU frame for save thumbnail"
            );
            let source = image::RgbaImage::from_raw(
                self.size.0,
                self.size.1,
                self.renderer.framebuffer().pixels().to_vec(),
            )
            .unwrap();
            let thumbnail =
                image::imageops::resize(&source, w, h, image::imageops::FilterType::Triangle);
            let state = SaveStateSnapshotV1::capture(gd);
            if slot == u32::MAX {
                gd.save_manager.finalize_local_savedata_prepare(
                    nls,
                    w,
                    h,
                    thumbnail.as_raw(),
                    Some(&state),
                )?;
            } else {
                gd.save_manager
                    .finalize_save_write(nls, w, h, thumbnail.as_raw(), Some(&state))?;
                save_global_savedata_v1(gd)?;
                gd.save_manager.consume_save_write_result();
            }
        }
        if gd.save_manager.try_commit_local_savedata(nls)? {
            save_global_savedata_v1(gd)?;
            // Match upstream's application loop: completing one write must
            // release the request so a later menu SaveWrite can commit again.
            // Keep local_saved intact; it is the pre-menu gameplay snapshot.
            gd.save_manager.consume_save_write_result();
        }
        Ok(())
    }
    fn pause(&mut self, paused: bool) -> Result<i32> {
        if self.paused == paused {
            return Ok(0);
        }
        let elapsed = if paused {
            self.paused_at = Some(Instant::now());
            Duration::ZERO
        } else {
            self.paused_at
                .take()
                .map_or(Duration::ZERO, |t| t.elapsed())
        };
        self.world.video_manager.pause_host(paused, elapsed);
        self.world.bgm_player_mut().pause_host(paused);
        self.world.se_player_mut().pause_host(paused);
        self.world.inputs_manager.cancel_host_input();
        self.paused = paused;
        Ok(0)
    }
}
impl Drop for Core {
    fn drop(&mut self) {
        let gd = &mut *self.world;
        gd.video_manager.stop(&mut gd.motion_manager);
        if let Err(e) = save_global_savedata_v1(gd) {
            log::error!("Save on shutdown: {e:#}");
        }
    }
}

struct Session {
    core: Option<Core>,
    gpu: Option<HostGpuCallbacks>,
    error: CString,
    poisoned: bool,
}
impl Session {
    fn core(&mut self) -> Result<&mut Core> {
        self.core
            .as_mut()
            .ok_or_else(|| anyhow!("No rfvp game is open"))
    }
}
unsafe fn string<'a>(p: *const c_char) -> Result<&'a str> {
    if p.is_null() {
        return Ok("");
    }
    Ok(CStr::from_ptr(p).to_str()?)
}
unsafe fn call(p: *mut c_void, f: impl FnOnce(&mut Session) -> Result<i32>) -> i32 {
    let Some(s) = (p as *mut Session).as_mut() else {
        return -1;
    };
    if s.poisoned {
        return -5;
    }
    match catch_unwind(AssertUnwindSafe(|| f(s))) {
        Ok(Ok(code)) => {
            s.error = CString::default();
            code
        }
        Ok(Err(e)) => {
            s.error = CString::new(format!("{e:#}").replace('\0', " ")).unwrap();
            -4
        }
        Err(_) => {
            s.poisoned = true;
            s.error = CString::new("rfvp panicked; close this runtime before retrying").unwrap();
            -5
        }
    }
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_new(gpu_callbacks: *const HostGpuCallbacks) -> *mut c_void {
    let _ = log::set_logger(&LOGGER).map(|_| log::set_max_level(log::LevelFilter::Info));
    let gpu = gpu_callbacks
        .as_ref()
        .copied()
        .filter(HostGpuCallbacks::valid);
    Box::into_raw(Box::new(Session {
        core: None,
        gpu,
        error: CString::default(),
        poisoned: false,
    }))
    .cast()
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_free(p: *mut c_void) {
    if !p.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(p as *mut Session))));
    }
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_probe(path: *const c_char) -> i32 {
    catch_unwind(|| {
        let (root, script) = script_path(Path::new(string(path)?), "")?;
        let _ = root;
        parse(&script, Nls::ShiftJIS)?;
        Ok::<_, anyhow::Error>(95)
    })
    .ok()
    .and_then(Result::ok)
    .unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_open(
    p: *mut c_void,
    path: *const c_char,
    script: *const c_char,
    writable: *const c_char,
    font: *const c_char,
    encoding: *const c_char,
    renderer: *const c_char,
) -> i32 {
    call(p, |s| {
        ensure!(s.core.is_none(), "Close the current game before reopening");
        let renderer = string(renderer)?;
        let (gpu, require_gpu) = match renderer {
            "auto" => (s.gpu, false),
            "gpu" => (s.gpu, true),
            "cpu" => (None, false),
            _ => return Err(anyhow!("rfvp renderer must be auto, gpu or cpu")),
        };
        s.core = Some(Core::new(
            Path::new(string(path)?),
            string(script)?,
            Path::new(string(writable)?),
            string(font)?,
            Nls::from_str(string(encoding)?)?,
            gpu,
            require_gpu,
        )?);
        Ok(0)
    })
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_tick(p: *mut c_void, delta: u32) -> i32 {
    call(p, |s| s.core()?.step(delta))
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_pause(p: *mut c_void, paused: u32) -> i32 {
    call(p, |s| s.core()?.pause(paused != 0))
}
fn key(code: i32) -> Option<KeyCode> {
    Some(match code {
        16 => KeyCode::Shift,
        17 => KeyCode::Ctrl,
        13 => KeyCode::Enter,
        27 => KeyCode::Esc,
        32 => KeyCode::Space,
        9 => KeyCode::Tab,
        37 => KeyCode::LeftArrow,
        38 => KeyCode::UpArrow,
        39 => KeyCode::RightArrow,
        40 => KeyCode::DownArrow,
        112 => KeyCode::F1,
        113 => KeyCode::F2,
        114 => KeyCode::F3,
        115 => KeyCode::F4,
        116 => KeyCode::F5,
        117 => KeyCode::F6,
        118 => KeyCode::F7,
        119 => KeyCode::F8,
        120 => KeyCode::F9,
        121 => KeyCode::F10,
        122 => KeyCode::F11,
        123 => KeyCode::F12,
        _ => return None,
    })
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_input(
    p: *mut c_void,
    kind: u32,
    x: f64,
    y: f64,
    button: i32,
    code: i32,
    modifiers: u32,
    wheel: f64,
) -> i32 {
    call(p, |s| {
        let core = s.core()?;
        if core.paused {
            return Ok(0);
        }
        ensure!(
            x.is_finite() && y.is_finite() && wheel.is_finite(),
            "Invalid input coordinates"
        );
        let input = &mut core.world.inputs_manager;
        if modifiers & (1 << 30) != 0 {
            input.cancel_host_input();
            return Ok(0);
        }
        if (1..=4).contains(&kind) {
            input.notify_mouse_move(x as i32, y as i32);
            input.set_mouse_in(
                x >= 0.0 && y >= 0.0 && x < core.size.0 as f64 && y < core.size.1 as f64,
            );
        }
        let mouse = if button == 1 {
            KeyCode::MouseRight
        } else {
            KeyCode::MouseLeft
        };
        match kind {
            1 => input.notify_mouse_down(mouse),
            2 => (),
            3 => input.notify_mouse_up(mouse),
            4 => input.notify_mouse_wheel(wheel.round().clamp(-1200.0, 1200.0) as i32),
            5 => {
                if let Some(key) = key(code) {
                    input.notify_keycode_down(key, false);
                }
            }
            6 => {
                if let Some(key) = key(code) {
                    input.notify_keycode_up(key);
                }
            }
            8 => {
                input.notify_keycode_down(KeyCode::Esc, false);
                input.notify_keycode_up(KeyCode::Esc);
            }
            _ => return Ok(-3),
        }
        Ok(0)
    })
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_frame(
    p: *mut c_void,
    w: *mut u32,
    h: *mut u32,
    serial: *mut u64,
) -> i32 {
    if w.is_null() || h.is_null() || serial.is_null() {
        return -1;
    }
    call(p, |s| {
        let c = s.core()?;
        *w = c.size.0;
        *h = c.size.1;
        *serial = c.serial;
        Ok(0)
    })
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_gpu_frame(
    p: *mut c_void,
    texture: *mut u64,
    w: *mut u32,
    h: *mut u32,
    serial: *mut u64,
) -> i32 {
    if texture.is_null() || w.is_null() || h.is_null() || serial.is_null() {
        return -1;
    }
    call(p, |s| {
        let c = s.core()?;
        let Some(handle) = c.renderer.host_gpu_texture() else {
            return Ok(-3);
        };
        *texture = handle;
        *w = c.size.0;
        *h = c.size.1;
        *serial = c.serial;
        Ok(0)
    })
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_read(p: *mut c_void, output: *mut c_void, size: usize) -> i32 {
    if output.is_null() {
        return -1;
    }
    call(p, |s| {
        let renderer = &mut s.core()?.renderer;
        ensure!(renderer.sync_host_gpu_framebuffer(), "Read rfvp GPU frame");
        let bytes = renderer.framebuffer().pixels();
        ensure!(size >= bytes.len(), "Frame buffer is too small");
        ptr::copy_nonoverlapping(bytes.as_ptr(), output.cast(), bytes.len());
        Ok(0)
    })
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_error(p: *mut c_void) -> *const c_char {
    (p as *const Session)
        .as_ref()
        .map_or(ptr::null(), |s| s.error.as_ptr())
}
#[no_mangle]
pub unsafe extern "C" fn aether_rfvp_log(output: *mut c_char, size: usize) -> u32 {
    if output.is_null() || size < 4096 {
        return 0;
    }
    if let Ok(mut logs) = LOGS.lock() {
        if let Some((level, text)) = logs.pop_front() {
            ptr::copy_nonoverlapping(text.as_ptr(), output.cast(), text.len());
            *output.add(text.len()) = 0;
            return level;
        }
    }
    *output = 0;
    0
}
