use std::collections::HashMap;
use std::io::Cursor;

use encoding_rs::SHIFT_JIS;
use serde::{Deserialize, Serialize};

use crate::profile::{GameMenuItemKind, GameProfile};
use crate::vfs::Vfs;
use crate::vm::{
    CharRequest, EffectRequest, PanelRequest, ShakeRequest, StageRequest, TransitionConfig,
};

const WIDTH: u32 = 1280;
const HEIGHT: u32 = 720;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MainMenuAction {
    NewGame,
    LoadGame,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GameMenuAction {
    Consumed,
    Save,
    Load,
    QuickSave,
    System,
    Skip,
    SaveSlot(u8),
    LoadSlot(u8),
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Frame {
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
    pub serial: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SceneKind {
    MainMenu,
    Script,
}

struct CharacterMovePlan {
    id: Option<i32>,
    x: i32,
    y: i32,
    x_mode: i32,
    y_mode: i32,
    duration: i32,
    blend_rate: i32,
    offset: bool,
}

#[derive(Debug, Clone)]
struct CharacterMovement {
    start_x: i32,
    start_y: i32,
    target_x: i32,
    target_y: i32,
    start_blend: i32,
    target_blend: Option<i32>,
    x_mode: i32,
    y_mode: i32,
    duration_ms: u32,
    elapsed_ms: u32,
}

#[derive(Debug, Clone)]
struct MouthOverlay {
    image: Frame,
    x: i32,
    y: i32,
}

#[derive(Debug, Clone)]
struct MouthAnimation {
    actor: String,
    medium: MouthOverlay,
    open: MouthOverlay,
    open_eye: Option<MouthOverlay>,
    closed_eye: Option<MouthOverlay>,
}

#[derive(Debug, Clone)]
struct Character {
    image_name: String,
    image: Frame,
    _secondary_name: Option<String>,
    secondary_image: Option<Frame>,
    _tertiary_name: Option<String>,
    mouth: Option<MouthAnimation>,
    mouth_state: u8,
    blink_enabled: bool,
    blink_elapsed_ms: u32,
    blink_state: bool,
    scaled_size: Option<(u32, u32)>,
    x: i32,
    y: i32,
    order: i32,
    blend_rate: i32,
    visible: bool,
    keep: bool,
    current: bool,
    next: bool,
    sequence: Option<CharacterSequence>,
    movement: Option<CharacterMovement>,
}

#[derive(Debug, Clone)]
struct CharacterSequence {
    frames: u32,
    rate: u32,
    start: u32,
    elapsed_ms: u32,
    loaded_frame: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub(crate) struct ManualCharacterState {
    id: u32,
    image_name: String,
    secondary_name: Option<String>,
    tertiary_name: Option<String>,
    scaled_size: Option<(u32, u32)>,
    x: i32,
    y: i32,
    order: i32,
    blend_rate: i32,
    visible: bool,
    keep: bool,
    current: bool,
    next: bool,
    sequence: Option<(u32, u32, u32, u32, u32)>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub(crate) struct ManualSceneState {
    pub background_rgba: Vec<u8>,
    pub panel: Option<Frame>,
    pub characters: Vec<ManualCharacterState>,
    pub rand_state: u32,
}

#[derive(Debug, Clone)]
struct MessageCharTransition {
    delay_ms: u32,
    duration_units: i32,
    elapsed_ms: u32,
    id: i32,
    target_blend: i32,
    started: bool,
}

#[derive(Debug)]
struct PanelTransition {
    old: Option<Frame>,
    new: Option<Frame>,
    speed: u32,
    elapsed_ms: u32,
}

#[derive(Debug)]
struct ShakeState {
    mode: u8,
    amplitude: i32,
    remaining_units: u32,
    accumulated_ms: u32,
    phase: u32,
    base_rgba: Vec<u8>,
}

#[derive(Debug)]
struct ScrollState {
    start_x: i32,
    start_y: i32,
    target_x: i32,
    target_y: i32,
    speed: i32,
    diagonal: bool,
    duration_ms: Option<u32>,
    elapsed_ms: u32,
}

#[derive(Debug)]
struct TransitionState {
    transition_type: i32,
    old_rgba: Vec<u8>,
    new_rgba: Vec<u8>,
    mask: Option<Vec<u8>>,
    tile_values: Option<Vec<i32>>,
    duration: u32,
    elapsed_ms: u32,
    progress: u32,
}

#[derive(Debug)]
struct StageEffectLayers {
    base: Frame,
    scrolling: Vec<(Frame, i32, i32)>,
    foreground: Vec<(Frame, i32, i32)>,
}

#[derive(Debug)]
struct WScrollState {
    _period: i32,
    speed: i32,
    elapsed_ms: u32,
    ticks: i32,
    previous_velocity: i32,
    remainder: i32,
    offset: i32,
}

#[derive(Debug)]
struct CutinState {
    image: Frame,
    from_left: bool,
    hold: i32,
    elapsed_ms: u32,
    state: u8,
    x: i32,
    velocity: f64,
    factor: f64,
}

#[derive(Debug)]
struct GameMenuState {
    underlay: Vec<u8>,
    left: i32,
    top: i32,
}

#[derive(Debug)]
struct SaveMenuState {
    underlay: Vec<u8>,
    load: bool,
}

#[derive(Debug)]
pub struct SceneSystem {
    profile: &'static GameProfile,
    pub current: SceneKind,
    frame: Frame,
    background_source: Frame,
    background_rgba: Vec<u8>,
    camera_x: i32,
    camera_y: i32,
    scroll: Option<ScrollState>,
    shake: Option<ShakeState>,
    effect_layers: Option<StageEffectLayers>,
    wscroll: Option<WScrollState>,
    cutin: Option<CutinState>,
    characters: HashMap<u32, Character>,
    panel: Option<Frame>,
    panel_transition: Option<PanelTransition>,
    message_char_transitions: Vec<MessageCharTransition>,
    transition: Option<TransitionState>,
    game_menu: Option<GameMenuState>,
    save_menu: Option<SaveMenuState>,
    rand_state: u32,
}

impl SceneSystem {
    pub fn main_menu(vfs: &Vfs, profile: &'static GameProfile) -> Result<Self, String> {
        let data = vfs.read(profile.title_background)?;
        let frame = decode_png(&data)?;
        Ok(Self {
            profile,
            current: SceneKind::MainMenu,
            background_source: frame.clone(),
            background_rgba: frame.rgba.clone(),
            camera_x: 0,
            camera_y: 0,
            scroll: None,
            shake: None,
            effect_layers: None,
            wscroll: None,
            cutin: None,
            frame,
            characters: HashMap::new(),
            panel: None,
            panel_transition: None,
            message_char_transitions: Vec::new(),
            transition: None,
            game_menu: None,
            save_menu: None,
            rand_state: 1,
        })
    }

    pub fn enter_script_scene(&mut self) {
        let serial = self.frame.serial.saturating_add(1);
        let black = [0, 0, 0, 255].repeat((WIDTH * HEIGHT) as usize);
        self.current = SceneKind::Script;
        self.frame = Frame {
            width: WIDTH,
            height: HEIGHT,
            rgba: black.clone(),
            serial,
        };
        self.background_source = self.frame.clone();
        self.background_rgba = black;
        self.camera_x = 0;
        self.camera_y = 0;
        self.scroll = None;
        self.shake = None;
        self.effect_layers = None;
        self.wscroll = None;
        self.cutin = None;
        self.characters.clear();
        self.panel = None;
        self.panel_transition = None;
        self.message_char_transitions.clear();
        self.transition = None;
        self.game_menu = None;
        self.save_menu = None;
    }

    pub fn frame(&self) -> &Frame {
        &self.frame
    }

    pub(crate) fn manual_state(&self) -> ManualSceneState {
        let mut characters = self
            .characters
            .iter()
            .map(|(id, character)| ManualCharacterState {
                id: *id,
                image_name: character.image_name.clone(),
                secondary_name: character._secondary_name.clone(),
                tertiary_name: character._tertiary_name.clone(),
                scaled_size: character.scaled_size,
                x: character.x,
                y: character.y,
                order: character.order,
                blend_rate: character.blend_rate,
                visible: character.visible,
                keep: character.keep,
                current: character.current,
                next: character.next,
                sequence: character.sequence.as_ref().map(|sequence| {
                    (
                        sequence.frames,
                        sequence.rate,
                        sequence.start,
                        sequence.elapsed_ms,
                        sequence.loaded_frame,
                    )
                }),
            })
            .collect::<Vec<_>>();
        characters.sort_by_key(|character| character.id);
        ManualSceneState {
            background_rgba: self.background_rgba.clone(),
            panel: self.panel.clone(),
            characters,
            rand_state: self.rand_state,
        }
    }

    pub fn save_capture(&self) -> Frame {
        let rgba = self
            .save_menu
            .as_ref()
            .map_or_else(|| self.frame.rgba.clone(), |state| state.underlay.clone());
        Frame {
            width: WIDTH,
            height: HEIGHT,
            rgba,
            serial: self.frame.serial,
        }
    }

    pub fn main_menu_action(&self, x: f64, y: f64) -> Option<MainMenuAction> {
        if self.current != SceneKind::MainMenu {
            return None;
        }
        if self.profile.title_new_game.contains(x, y) {
            Some(MainMenuAction::NewGame)
        } else if self.profile.title_load_game.contains(x, y) {
            Some(MainMenuAction::LoadGame)
        } else {
            None
        }
    }

    pub fn game_menu_active(&self) -> bool {
        self.game_menu.is_some() || self.save_menu.is_some()
    }

    pub fn game_menu_pointer_down(
        &mut self,
        vfs: &Vfs,
        x: f64,
        y: f64,
    ) -> Result<Option<GameMenuAction>, String> {
        if self.save_menu.is_some() {
            if let Some(slot) = save_slot_hit(self.profile, x, y) {
                let action = if self.save_menu.as_ref().is_some_and(|state| state.load) {
                    GameMenuAction::LoadSlot(slot)
                } else {
                    GameMenuAction::SaveSlot(slot)
                };
                return Ok(Some(action));
            }
            if self.profile.save_load.close.contains(x, y)
                && let Some(state) = self.save_menu.take()
            {
                self.frame.rgba = state.underlay;
                self.frame.serial = self.frame.serial.saturating_add(1);
            }
            return Ok(Some(GameMenuAction::Consumed));
        }
        if self.current != SceneKind::Script {
            return Ok(None);
        }
        if self.game_menu.is_none() {
            let trigger = self.profile.game_menu.trigger_size;
            if (f64::from(WIDTH) - trigger..f64::from(WIDTH)).contains(&x)
                && (f64::from(HEIGHT) - trigger..f64::from(HEIGHT)).contains(&y)
            {
                self.open_game_menu(vfs)?;
                return Ok(Some(GameMenuAction::Consumed));
            }
            return Ok(None);
        }

        let action = self
            .game_menu
            .as_ref()
            .and_then(|state| game_menu_hit(self.profile, x, y, state.left, state.top));
        if let Some(state) = self.game_menu.take() {
            self.frame.rgba = state.underlay;
            self.frame.serial = self.frame.serial.saturating_add(1);
        }
        Ok(Some(action.unwrap_or(GameMenuAction::Consumed)))
    }

    pub fn populate_save_slots(&mut self, slots: &[(u8, Frame)]) {
        for (slot, frame) in slots {
            if *slot >= 10 || frame.width == 0 || frame.height == 0 {
                continue;
            }
            let layout = &self.profile.save_load;
            let thumbnail = resize_nearest(frame, layout.thumbnail_width, layout.thumbnail_height);
            let row = i32::from(*slot % 5);
            let column = usize::from(*slot >= 5);
            let left = layout.thumbnail_left[column];
            let top = layout.thumbnail_top + row * layout.row_step as i32;
            composite_source_over(&mut self.frame, &thumbnail, left, top);
        }
        self.frame.serial = self.frame.serial.saturating_add(1);
    }

    pub fn restore_save_capture(&mut self, frame: Frame) -> Result<(), String> {
        if frame.width != WIDTH
            || frame.height != HEIGHT
            || frame.rgba.len() != (WIDTH * HEIGHT * 4) as usize
        {
            return Err("manual save scene frame has invalid dimensions".into());
        }
        let serial = self.frame.serial.saturating_add(1);
        self.current = SceneKind::Script;
        self.frame = Frame { serial, ..frame };
        self.background_source = self.frame.clone();
        self.background_rgba = self.frame.rgba.clone();
        self.camera_x = 0;
        self.camera_y = 0;
        self.scroll = None;
        self.shake = None;
        self.effect_layers = None;
        self.wscroll = None;
        self.cutin = None;
        self.characters.clear();
        self.panel = None;
        self.panel_transition = None;
        self.message_char_transitions.clear();
        self.transition = None;
        self.game_menu = None;
        self.save_menu = None;
        Ok(())
    }

    pub(crate) fn restore_manual_state(
        &mut self,
        vfs: &Vfs,
        frame: Frame,
        state: ManualSceneState,
    ) -> Result<(), String> {
        if state.background_rgba.len() != (WIDTH * HEIGHT * 4) as usize {
            return Err("manual save background has invalid dimensions".into());
        }
        self.restore_save_capture(frame)?;
        self.background_rgba = state.background_rgba;
        self.background_source = Frame {
            width: WIDTH,
            height: HEIGHT,
            rgba: self.background_rgba.clone(),
            serial: self.frame.serial,
        };
        self.panel = state.panel;
        self.rand_state = state.rand_state;
        for saved in state.characters {
            let mut image = decode_png(&vfs.read(&saved.image_name)?)?;
            if let Some((width, height)) = saved.scaled_size {
                image = resize_nearest(&image, width, height);
            }
            let mut secondary_image = saved
                .secondary_name
                .as_deref()
                .filter(|name| !name.is_empty())
                .map(|name| vfs.read(name).and_then(|data| decode_png(&data)))
                .transpose()?;
            if let (Some((width, height)), Some(secondary)) =
                (saved.scaled_size, secondary_image.as_mut())
            {
                *secondary = resize_nearest(secondary, width, height);
            }
            let ani_name = saved.image_name.rsplit_once('.').map_or_else(
                || format!("{}.ani", saved.image_name),
                |(stem, _)| format!("{stem}.ani"),
            );
            let mouth = vfs
                .read_optional(&ani_name)?
                .as_deref()
                .map(parse_mouth_animation)
                .transpose()?
                .flatten();
            self.characters.insert(
                saved.id,
                Character {
                    image_name: saved.image_name,
                    image,
                    _secondary_name: saved.secondary_name,
                    secondary_image,
                    _tertiary_name: saved.tertiary_name,
                    mouth,
                    mouth_state: 0,
                    blink_enabled: true,
                    blink_elapsed_ms: 0,
                    blink_state: false,
                    scaled_size: saved.scaled_size,
                    x: saved.x,
                    y: saved.y,
                    order: saved.order,
                    blend_rate: saved.blend_rate,
                    visible: saved.visible,
                    keep: saved.keep,
                    current: saved.current,
                    next: saved.next,
                    sequence: saved.sequence.map(
                        |(frames, rate, start, elapsed_ms, loaded_frame)| CharacterSequence {
                            frames,
                            rate,
                            start,
                            elapsed_ms,
                            loaded_frame,
                        },
                    ),
                    movement: None,
                },
            );
        }
        Ok(())
    }

    pub fn close_save_menu(&mut self) {
        if let Some(state) = self.save_menu.take() {
            self.frame.rgba = state.underlay;
            self.frame.serial = self.frame.serial.saturating_add(1);
        }
    }

    pub fn open_save_menu(&mut self, vfs: &Vfs) -> Result<(), String> {
        self.open_save_load_menu(vfs, false)
    }

    pub fn open_load_menu(&mut self, vfs: &Vfs) -> Result<(), String> {
        self.open_save_load_menu(vfs, true)
    }

    fn open_save_load_menu(&mut self, vfs: &Vfs, load: bool) -> Result<(), String> {
        let underlay = self.frame.rgba.clone();
        let layout = &self.profile.save_load;
        self.frame = decode_png(&vfs.read(layout.base)?)?;
        let heading = if load {
            layout.load_heading
        } else {
            layout.save_heading
        };
        for (name, x, y) in [
            (heading, 0, 0),
            (layout.page_resource, layout.page_x, layout.page_y),
        ] {
            let image = decode_png(&vfs.read(name)?)?;
            composite_source_over(&mut self.frame, &image, x, y);
        }
        self.save_menu = Some(SaveMenuState { underlay, load });
        self.frame.serial = self.frame.serial.saturating_add(1);
        Ok(())
    }

    fn open_game_menu(&mut self, vfs: &Vfs) -> Result<(), String> {
        let underlay = self.frame.rgba.clone();
        let menu = &self.profile.game_menu;
        let back = decode_png(&vfs.read(menu.background)?)?;
        let left = WIDTH as i32 - back.width as i32 - menu.right_margin;
        let top = HEIGHT as i32 - back.height as i32 - menu.bottom_margin;
        composite_source_over(&mut self.frame, &back, left, top);
        for item in menu.items {
            let image = decode_png(&vfs.read(item.resource)?)?;
            composite_source_over(&mut self.frame, &image, left + item.x, top + item.y);
        }
        self.game_menu = Some(GameMenuState {
            underlay,
            left,
            top,
        });
        self.frame.serial = self.frame.serial.saturating_add(1);
        Ok(())
    }

    pub fn shake_active(&self) -> bool {
        self.shake.is_some()
    }

    pub fn start_effect(&mut self, vfs: &Vfs, request: &EffectRequest) -> Result<(), String> {
        match request.kind.as_str() {
            "WScroll2" | "WScrollST" => {
                if request.kind == "WScroll2"
                    && self
                        .effect_layers
                        .as_ref()
                        .is_none_or(|layers| layers.scrolling.is_empty())
                {
                    return Err("WScroll2 requires a scrolling stage layer".into());
                }
                if request.kind == "WScrollST" {
                    let base = request
                        .resource
                        .split_once(',')
                        .map_or(request.resource.as_str(), |(base, _)| base);
                    let normal = decode_png(&vfs.read(&format!("{base}_n.png"))?)?;
                    let scrolling = decode_png(&vfs.read(&format!("{base}_f.png"))?)?;
                    self.background_source = normal.clone();
                    self.background_rgba = crop_background(&normal, 0, 0)?.rgba;
                    self.camera_x = 0;
                    self.camera_y = 0;
                    self.effect_layers = Some(StageEffectLayers {
                        base: normal.clone(),
                        scrolling: vec![(scrolling, 0, 0)],
                        foreground: vec![(normal, 0, 0)],
                    });
                    self.render_panel_frame();
                }
                self.cutin = None;
                self.wscroll = Some(WScrollState {
                    _period: request.first,
                    speed: request.second,
                    elapsed_ms: 0,
                    ticks: 0,
                    previous_velocity: 0,
                    remainder: 0,
                    offset: 0,
                });
                Ok(())
            }
            kind => Err(format!("effect rendering is not implemented: {kind}")),
        }
    }

    pub fn start_cutin(&mut self, vfs: &Vfs, request: &EffectRequest) -> Result<(), String> {
        let image = decode_png(&vfs.read(&request.resource)?)?;
        let from_left = request.second >= 0;
        let factor = (f64::from(request.second).abs() / 100.0).max(1.5);
        let x = if from_left {
            -(image.width as i32)
        } else {
            WIDTH as i32
        };
        self.wscroll = None;
        self.cutin = Some(CutinState {
            image,
            from_left,
            hold: request.first,
            elapsed_ms: 0,
            state: 0,
            x,
            velocity: if from_left { 2.0 } else { -2.0 },
            factor,
        });
        Ok(())
    }

    pub fn fadeout_effect(&mut self) {
        if let Some(cutin) = self.cutin.as_mut()
            && cutin.state == 1
        {
            cutin.state = 2;
            cutin.velocity = if cutin.from_left { 2.0 } else { -2.0 };
        }
    }

    pub fn clear_effect(&mut self) -> Result<(), String> {
        self.wscroll = None;
        self.cutin = None;
        self.background_rgba =
            crop_background(&self.background_source, self.camera_x, self.camera_y)?.rgba;
        self.render_panel_frame();
        Ok(())
    }

    pub fn tick_effect(&mut self, delta_ms: u32) -> Result<(), String> {
        if self.cutin.is_some() {
            return self.tick_cutin(delta_ms);
        }
        let Some(effect) = self.wscroll.as_mut() else {
            return Ok(());
        };
        effect.elapsed_ms = effect.elapsed_ms.saturating_add(delta_ms);
        let steps = effect.elapsed_ms / 10;
        effect.elapsed_ms %= 10;
        let changed = advance_wscroll(effect, steps);
        if changed {
            self.render_wscroll()?;
        }
        Ok(())
    }

    fn tick_cutin(&mut self, delta_ms: u32) -> Result<(), String> {
        let Some(cutin) = self.cutin.as_mut() else {
            return Ok(());
        };
        cutin.elapsed_ms = cutin.elapsed_ms.saturating_add(delta_ms);
        let steps = cutin.elapsed_ms / 40;
        cutin.elapsed_ms %= 40;
        if steps == 0 {
            return Ok(());
        }
        advance_cutin(cutin, steps);
        let complete = cutin.state == 3;
        let x = cutin.x;
        let y = (HEIGHT as i32 - cutin.image.height as i32) / 2;
        let image = cutin.image.clone();
        self.render_panel_frame();
        if complete {
            self.cutin = None;
        } else {
            composite_source_over(&mut self.frame, &image, x, y);
            self.frame.serial = self.frame.serial.saturating_add(1);
        }
        Ok(())
    }

    fn render_wscroll(&mut self) -> Result<(), String> {
        let effect = self
            .wscroll
            .as_ref()
            .ok_or_else(|| "WScroll2 is not active".to_string())?;
        let layers = self
            .effect_layers
            .as_ref()
            .ok_or_else(|| "WScroll2 stage layers are unavailable".to_string())?;
        let mut output = crop_background(&layers.base, self.camera_x, self.camera_y)?;
        for (layer, x, y) in &layers.scrolling {
            composite_wrapped_horizontal(
                &mut output,
                layer,
                self.camera_x
                    .saturating_add(effect.offset)
                    .saturating_sub(*x),
                self.camera_y.saturating_sub(*y),
            );
        }
        for (layer, x, y) in &layers.foreground {
            composite_viewport(
                &mut output,
                layer,
                self.camera_x.saturating_sub(*x),
                self.camera_y.saturating_sub(*y),
            );
        }
        self.background_rgba = output.rgba;
        self.render_panel_frame();
        Ok(())
    }

    pub fn start_shake(&mut self, request: &ShakeRequest) -> Result<(), String> {
        let mode = request
            .mode
            .as_bytes()
            .first()
            .copied()
            .unwrap_or(b'R')
            .to_ascii_uppercase();
        let mode = match mode {
            b'Z' | b'V' | b'H' | b'O' | b'T' => mode,
            b'R' if request.duration > 0 => b'R',
            b'R' => b'D',
            _ if request.duration > 0 => b'R',
            _ => b'D',
        };
        self.shake = Some(ShakeState {
            mode,
            amplitude: request.amplitude,
            remaining_units: request.duration.unsigned_abs(),
            accumulated_ms: 0,
            phase: 0,
            base_rgba: self.frame.rgba.clone(),
        });
        Ok(())
    }

    pub fn tick_shake(&mut self, delta_ms: u32) -> bool {
        let Some(shake) = self.shake.as_mut() else {
            return false;
        };
        shake.accumulated_ms = shake.accumulated_ms.saturating_add(delta_ms);
        let steps = shake.accumulated_ms / 10;
        shake.accumulated_ms %= 10;
        if steps == 0 {
            return false;
        }
        for _ in 0..steps {
            shake.remaining_units = shake.remaining_units.saturating_sub(1);
            if shake.remaining_units == 0 {
                self.frame.rgba.clone_from(&shake.base_rgba);
                self.frame.serial = self.frame.serial.saturating_add(1);
                self.shake = None;
                return true;
            }
            let amplitude = shake.amplitude.unsigned_abs() as i32;
            self.frame.rgba = match shake.mode {
                b'V' => translate_frame(
                    &shake.base_rgba,
                    0,
                    if shake.phase & 1 == 0 {
                        -amplitude
                    } else {
                        amplitude
                    },
                ),
                b'H' => translate_frame(
                    &shake.base_rgba,
                    if shake.phase & 1 == 0 {
                        -amplitude
                    } else {
                        amplitude
                    },
                    0,
                ),
                b'R' => {
                    let direction = msvc_rand(&mut self.rand_state) as u32 % 8;
                    let (x, y) = match direction {
                        0 => (-amplitude, -amplitude),
                        1 => (-amplitude, 0),
                        2 => (amplitude, amplitude),
                        3 => (amplitude, 0),
                        4 => (-amplitude, amplitude),
                        5 => (0, amplitude),
                        6 => (amplitude, -amplitude),
                        _ => (0, -amplitude),
                    };
                    translate_frame(&shake.base_rgba, x, y)
                }
                b'Z' => zoom_inset_frame(&shake.base_rgba, amplitude),
                b'O' => {
                    let random = msvc_rand(&mut self.rand_state);
                    let angle = if amplitude == 0 {
                        0.0
                    } else {
                        f64::from(random % (2 * amplitude) - amplitude).to_radians()
                    };
                    rotate_frame(
                        &shake.base_rgba,
                        WIDTH as f64 / 2.0,
                        HEIGHT as f64 / 2.0,
                        angle,
                    )
                }
                b'T' => {
                    let angle_degrees = f64::from(shake.phase.saturating_add(1)) * 1000.0 / 800.0;
                    if angle_degrees <= f64::from(amplitude) {
                        rotate_frame(
                            &shake.base_rgba,
                            799.0,
                            0.0,
                            (angle_degrees % 180.0).to_radians(),
                        )
                    } else {
                        self.frame.rgba.clone()
                    }
                }
                b'D' => {
                    let index = (msvc_rand(&mut self.rand_state) % 9) as usize;
                    let x = [
                        0, -amplitude, 0, amplitude, -amplitude, 0, amplitude, -amplitude, 0,
                    ][index];
                    let y = [
                        0, -amplitude, -amplitude, -amplitude, 0, 0, 0, amplitude, amplitude,
                    ][index];
                    translate_frame(&shake.base_rgba, x, y)
                }
                _ => unreachable!(),
            };
            shake.phase = shake.phase.saturating_add(1);
            self.frame.serial = self.frame.serial.saturating_add(1);
        }
        false
    }

    pub fn scroll_active(&self) -> bool {
        self.scroll.is_some()
    }

    pub fn start_scroll(&mut self, request: &crate::vm::ScrollRequest) {
        self.scroll = Some(ScrollState {
            start_x: self.camera_x,
            start_y: self.camera_y,
            target_x: request.target_x.unwrap_or(self.camera_x),
            target_y: request.target_y.unwrap_or(self.camera_y),
            speed: request.speed,
            diagonal: request.target_x.is_some() && request.target_y.is_some(),
            duration_ms: None,
            elapsed_ms: 0,
        });
    }

    pub fn start_scroll_xf(&mut self, request: &crate::vm::ScrollXfRequest) {
        let values = request.values;
        self.camera_x = values[0];
        self.camera_y = values[1];
        self.scroll = Some(ScrollState {
            start_x: values[0],
            start_y: values[1],
            target_x: values[6],
            target_y: values[7],
            speed: 0,
            diagonal: true,
            duration_ms: Some(values[8].max(0) as u32),
            elapsed_ms: 0,
        });
    }

    pub fn interrupt_scroll(&mut self) {
        self.scroll = None;
    }

    pub fn tick_scroll(&mut self, delta_ms: u32) -> Result<bool, String> {
        let Some(scroll) = self.scroll.as_mut() else {
            return Ok(false);
        };
        scroll.elapsed_ms = scroll.elapsed_ms.saturating_add(delta_ms);
        let (next_x, next_y) = if let Some(duration_ms) = scroll.duration_ms {
            let progress = if duration_ms == 0 {
                1.0
            } else {
                (f64::from(scroll.elapsed_ms.min(duration_ms)) / f64::from(duration_ms)).min(1.0)
            };
            let interpolate = |start: i32, target: i32| {
                (f64::from(start) + f64::from(target - start) * progress).round() as i32
            };
            (
                interpolate(scroll.start_x, scroll.target_x),
                interpolate(scroll.start_y, scroll.target_y),
            )
        } else {
            let elapsed_units = scroll.elapsed_ms / 10;
            if scroll.diagonal {
                scroll_pair(scroll, elapsed_units)
            } else {
                (
                    scroll_axis(scroll.start_x, scroll.target_x, scroll.speed, elapsed_units),
                    scroll_axis(scroll.start_y, scroll.target_y, scroll.speed, elapsed_units),
                )
            }
        };
        let complete = next_x == scroll.target_x && next_y == scroll.target_y;
        let changed = next_x != self.camera_x || next_y != self.camera_y;
        self.camera_x = next_x;
        self.camera_y = next_y;
        if complete {
            self.scroll = None;
        }
        if changed {
            self.background_rgba =
                crop_background(&self.background_source, self.camera_x, self.camera_y)?.rgba;
            self.render_panel_frame();
        }
        Ok(complete)
    }

    pub fn transition_active(&self) -> bool {
        self.transition.is_some()
    }

    pub fn panel_transition_active(&self) -> bool {
        self.panel_transition.is_some()
    }

    pub fn panel_visible(&self) -> bool {
        self.panel.is_some()
            || self
                .panel_transition
                .as_ref()
                .is_some_and(|transition| transition.new.is_some())
    }

    pub fn apply_panel(&mut self, vfs: &Vfs, request: &PanelRequest) -> Result<(), String> {
        let new = match request.mode {
            0 | 7 => None,
            1 | 2 => {
                let name = if request.image.is_empty() {
                    self.profile.message_panel
                } else {
                    &request.image
                };
                Some(decode_png(&vfs.read(name)?)?)
            }
            mode => return Err(format!("panel mode {mode} is not implemented")),
        };
        self.panel_transition = Some(PanelTransition {
            old: self.panel.clone(),
            new,
            speed: if request.duration > 0 {
                u32::try_from(request.duration).unwrap_or(u32::MAX)
            } else {
                16
            },
            elapsed_ms: 0,
        });
        Ok(())
    }

    pub fn tick_panel_transition(&mut self, delta_ms: u32) -> bool {
        let Some(transition) = self.panel_transition.as_mut() else {
            return false;
        };
        transition.elapsed_ms = transition.elapsed_ms.saturating_add(delta_ms);
        let ticks = transition.elapsed_ms / 10;
        if ticks >= 256 / transition.speed {
            self.panel = transition.new.take();
            self.panel_transition = None;
            self.render_panel_frame();
            return true;
        }
        self.render_panel_frame();
        false
    }

    pub fn apply_char(&mut self, vfs: &Vfs, request: CharRequest) -> Result<(), String> {
        match request {
            CharRequest::Load {
                id,
                image,
                secondary,
                tertiary,
                blink,
            } => {
                let key = id.unsigned_abs();
                let prior = self.characters.remove(&key);
                let mut frame = decode_png(&vfs.read(&image)?)?;
                let secondary_image = secondary
                    .as_deref()
                    .filter(|name| !name.is_empty())
                    .map(|name| vfs.read(name).and_then(|data| decode_png(&data)))
                    .transpose()?;
                let ani_name = image
                    .rsplit_once('.')
                    .map_or_else(|| format!("{image}.ani"), |(stem, _)| format!("{stem}.ani"));
                let mouth = vfs
                    .read_optional(&ani_name)?
                    .as_deref()
                    .map(parse_mouth_animation)
                    .transpose()?
                    .flatten();
                let scaled_size = prior.as_ref().and_then(|character| character.scaled_size);
                if let Some((width, height)) = scaled_size {
                    frame = resize_nearest(&frame, width, height);
                }
                let character = Character {
                    image_name: image,
                    image: frame,
                    _secondary_name: secondary,
                    secondary_image,
                    _tertiary_name: tertiary,
                    mouth,
                    mouth_state: 0,
                    blink_enabled: blink,
                    blink_elapsed_ms: 0,
                    blink_state: false,
                    scaled_size,
                    x: prior.as_ref().map_or(0, |character| character.x),
                    y: prior.as_ref().map_or(0, |character| character.y),
                    order: prior
                        .as_ref()
                        .map_or(4 * key.min(i32::MAX as u32) as i32 + 100, |character| {
                            character.order
                        }),
                    blend_rate: prior.as_ref().map_or(256, |character| character.blend_rate),
                    visible: prior.as_ref().is_none_or(|character| character.visible),
                    keep: prior.as_ref().is_some_and(|character| character.keep),
                    current: prior
                        .as_ref()
                        .map_or(id >= 0, |character| character.current),
                    next: prior.as_ref().map_or(id < 0, |character| character.next),
                    sequence: prior
                        .as_ref()
                        .and_then(|character| character.sequence.clone()),
                    movement: prior
                        .as_ref()
                        .and_then(|character| character.movement.clone()),
                };
                self.characters.insert(key, character);
            }
            CharRequest::Position { id, x, y } => {
                if let Some(character) = self.character_mut(id) {
                    character.x = x;
                    character.y = y;
                }
            }
            CharRequest::Size { id, width, height } => {
                if width > 0
                    && height > 0
                    && let Some(character) = self.character_mut(id)
                {
                    let width = u32::try_from(width).unwrap_or(u32::MAX);
                    let height = u32::try_from(height).unwrap_or(u32::MAX);
                    character.image = resize_nearest(&character.image, width, height);
                    if let Some(secondary) = character.secondary_image.as_mut() {
                        *secondary = resize_nearest(secondary, width, height);
                    }
                    character.scaled_size = Some((width, height));
                }
            }
            CharRequest::Order {
                id,
                order,
                secondary,
            } => {
                if let Some(character) = self.character_mut(id) {
                    character.order = if secondary { order } else { 4 * order + 100 };
                }
            }
            CharRequest::BlendRate { id, rate } => {
                if let Some(character) = self.character_mut(id) {
                    character.blend_rate = rate;
                }
            }
            CharRequest::Visible { id, visible } => {
                if let Some(character) = self.character_mut(id) {
                    character.visible = visible;
                }
            }
            CharRequest::Clear { first, last } => {
                clear_character_range(&mut self.characters, first, last);
            }
            CharRequest::Move {
                id,
                x,
                y,
                x_mode,
                y_mode,
                duration,
                blend_rate,
                offset,
                ..
            } => {
                self.start_character_move(CharacterMovePlan {
                    id: (id >= 0).then_some(id),
                    x,
                    y,
                    x_mode,
                    y_mode,
                    duration,
                    blend_rate,
                    offset,
                });
            }
            CharRequest::AllMove {
                x,
                y,
                duration,
                mode,
            } => {
                self.start_character_move(CharacterMovePlan {
                    id: None,
                    x,
                    y,
                    x_mode: mode,
                    y_mode: mode,
                    duration,
                    blend_rate: -1,
                    offset: true,
                });
            }
            CharRequest::Freeze => {
                self.freeze_characters();
            }
            CharRequest::Movie { .. } => {}
            CharRequest::Sequence {
                id,
                timed,
                frames,
                rate,
                start,
            } => {
                if let Some(character) = self.character_mut(id) {
                    configure_character_sequence(character, timed, frames, rate, start);
                }
            }
            CharRequest::Keep { first, last } => {
                for (key, character) in &mut self.characters {
                    if character.current && character_in_range(*key, first, last) {
                        character.keep = true;
                        character.next = true;
                    }
                }
            }
        }
        Ok(())
    }

    fn freeze_characters(&mut self) {
        let composed = freeze_character_layers(&self.background_rgba, &mut self.characters);
        if let Some(transition) = self.transition.as_mut() {
            transition.new_rgba = composed;
            return;
        }
        self.frame.rgba = composed;
        if let Some(transition) = self.panel_transition.as_ref() {
            render_panel_transition(&mut self.frame.rgba, transition);
        } else if let Some(panel) = &self.panel {
            composite_panel(&mut self.frame.rgba, panel, 255);
        }
        self.frame.serial = self.frame.serial.saturating_add(1);
    }

    pub fn character_movement_active(&self) -> bool {
        self.characters
            .values()
            .any(|character| character.movement.is_some())
    }

    fn start_character_move(&mut self, plan: CharacterMovePlan) {
        for (key, character) in &mut self.characters {
            if plan.id.is_some_and(|id| id.unsigned_abs() != *key) {
                continue;
            }
            let target_x = if plan.offset {
                character.x.saturating_add(plan.x)
            } else {
                plan.x
            };
            let target_y = if plan.offset {
                character.y.saturating_add(plan.y)
            } else {
                plan.y
            };
            if plan.duration <= 0 {
                character.x = target_x;
                character.y = target_y;
                if plan.blend_rate >= 0 {
                    character.blend_rate = plan.blend_rate;
                }
                character.movement = None;
                continue;
            }
            character.movement = Some(CharacterMovement {
                start_x: character.x,
                start_y: character.y,
                target_x,
                target_y,
                start_blend: character.blend_rate,
                target_blend: (plan.blend_rate >= 0).then_some(plan.blend_rate),
                x_mode: plan.x_mode,
                y_mode: plan.y_mode,
                duration_ms: script_units_to_ms(plan.duration),
                elapsed_ms: 0,
            });
        }
    }

    pub fn set_message_char_transitions(&mut self, requests: &[(i32, i32, i32, i32)]) {
        finish_message_char_transitions(&mut self.characters, &mut self.message_char_transitions);
        self.message_char_transitions = requests
            .iter()
            .map(
                |&(delay, id, duration, target_blend)| MessageCharTransition {
                    delay_ms: script_units_to_ms(delay),
                    duration_units: duration,
                    elapsed_ms: 0,
                    id,
                    target_blend,
                    started: false,
                },
            )
            .collect();
    }

    pub fn mouth_debug(&self) -> String {
        let mut entries: Vec<_> = self
            .characters
            .values()
            .map(|character| {
                let (actor, state) = character.mouth.as_ref().map_or(("none", 0), |mouth| {
                    (mouth.actor.as_str(), character.mouth_state)
                });
                format!("{}:{actor}:{state}", character.image_name)
            })
            .collect();
        entries.sort();
        entries.join(",")
    }

    pub fn set_mouth_state(&mut self, actor: Option<&str>, state: u8) {
        let _ = msvc_rand(&mut self.rand_state);
        let target_id = select_mouth_target(&self.characters, actor);
        let mut changed = false;
        for (id, character) in &mut self.characters {
            let target = if Some(*id) == target_id {
                state.min(2)
            } else {
                0
            };
            if character.mouth_state != target {
                character.mouth_state = target;
                changed = true;
            }
        }
        if changed {
            self.refresh_character_frame();
        }
    }

    pub fn tick_characters(&mut self, vfs: &Vfs, delta_ms: u32) -> Result<(), String> {
        let mut changed = advance_characters(vfs, &mut self.characters, delta_ms)?;
        changed |= advance_blinks(&mut self.characters, delta_ms);
        changed |= advance_message_char_transitions(
            &mut self.characters,
            &mut self.message_char_transitions,
            delta_ms,
        );
        if !changed {
            return Ok(());
        }
        self.refresh_character_frame();
        Ok(())
    }

    fn refresh_character_frame(&mut self) {
        let mut composed = compose_characters(&self.background_rgba, &self.characters);
        composite_panel_state(
            &mut composed,
            self.panel_transition.as_ref(),
            self.panel.as_ref(),
        );
        if let Some(transition) = self.transition.as_mut() {
            transition.new_rgba = composed;
        } else {
            self.frame.rgba = composed;
            self.frame.serial = self.frame.serial.saturating_add(1);
        }
    }

    pub fn apply_stage(
        &mut self,
        vfs: &Vfs,
        request: &StageRequest,
        transition: Option<&TransitionConfig>,
    ) -> Result<bool, String> {
        let mut image = decode_png(&vfs.read(&request.image)?)?;
        let base = image.clone();
        let mut scrolling = Vec::with_capacity(request.extras.len());
        let mut foreground = Vec::new();
        for extra in &request.extras {
            let overlay = decode_png(&vfs.read(&extra.name)?)?;
            composite_source_over(&mut image, &overlay, extra.first, extra.second);
            scrolling.push((overlay, extra.first, extra.second));
        }
        if request.base_spec != "*" {
            let (screen, layers) = parse_stage_base(&request.base_spec)?;
            for name in layers {
                if name != "*" && !name.is_empty() {
                    let overlay = decode_png(&vfs.read(name)?)?;
                    composite_source_over(&mut image, &overlay, request.base_x, request.base_y);
                    foreground.push((overlay, request.base_x, request.base_y));
                }
            }
            if !screen.is_empty() && screen != "*" {
                let overlay = decode_png(&vfs.read(screen)?)?;
                composite_source_over(&mut image, &overlay, request.base_x, request.base_y);
                foreground.push((overlay, request.base_x, request.base_y));
            }
        }
        let target = crop_background(&image, request.x, request.y)?;
        self.effect_layers = Some(StageEffectLayers {
            base,
            scrolling,
            foreground,
        });
        self.current = SceneKind::Script;
        self.swap_characters();
        self.background_source = image;
        self.camera_x = request.x;
        self.camera_y = request.y;
        self.scroll = None;
        self.shake = None;
        self.background_rgba = target.rgba;
        let mut target_rgba = compose_characters(&self.background_rgba, &self.characters);
        composite_panel_state(
            &mut target_rgba,
            self.panel_transition.as_ref(),
            self.panel.as_ref(),
        );

        match transition {
            Some(config)
                if config.transition_type == 0
                    || (1..=13).contains(&config.transition_type)
                    || (14..=18).contains(&config.transition_type) =>
            {
                let duration = u32::try_from(config.duration.max(0)).unwrap_or(u32::MAX);
                let mask = if matches!(config.transition_type, 1 | 18) && config.mask.len() > 1 {
                    Some(decode_transition_mask(&vfs.read(&config.mask)?)?)
                } else {
                    None
                };
                let tile_values = (3..=13).contains(&config.transition_type).then(|| {
                    transition_tile_values(config.transition_type - 3, &mut self.rand_state)
                });
                self.transition = Some(TransitionState {
                    transition_type: config.transition_type,
                    old_rgba: self.frame.rgba.clone(),
                    new_rgba: target_rgba,
                    mask,
                    tile_values,
                    duration,
                    elapsed_ms: 0,
                    progress: 0,
                });
                Ok(true)
            }
            Some(config) if transition_is_immediate(config.transition_type) => {
                self.install_frame(target_rgba);
                Ok(false)
            }
            Some(config) => Err(format!(
                "transition type {} is not implemented",
                config.transition_type
            )),
            None => {
                self.install_frame(target_rgba);
                Ok(false)
            }
        }
    }

    pub fn tick_transition(&mut self, delta_ms: u32) -> bool {
        let Some(transition) = self.transition.as_mut() else {
            return false;
        };
        transition.elapsed_ms = transition.elapsed_ms.saturating_add(delta_ms);
        let elapsed_units = transition.elapsed_ms / 10;
        let progress = transition.duration.saturating_mul(elapsed_units) / 10;
        let complete = match transition.transition_type {
            1 => transition.mask.is_none() || progress >= 512,
            2 => progress >= 400,
            3..=13 => transition
                .tile_values
                .as_ref()
                .is_none_or(|values| values.iter().all(|&value| value >= 10 * 576)),
            18 => transition.mask.is_none() || transition.progress > 255,
            _ => progress > transition_completion(transition.transition_type),
        };
        if complete {
            let new_rgba = std::mem::take(&mut transition.new_rgba);
            self.transition = None;
            self.install_frame(new_rgba);
            return true;
        }
        if progress != transition.progress {
            let previous_progress = transition.progress;
            transition.progress = progress;
            render_transition(transition, previous_progress, &mut self.frame.rgba);
            self.frame.serial = self.frame.serial.saturating_add(1);
        }
        false
    }

    fn render_panel_frame(&mut self) {
        self.frame.rgba = compose_characters(&self.background_rgba, &self.characters);
        composite_panel_state(
            &mut self.frame.rgba,
            self.panel_transition.as_ref(),
            self.panel.as_ref(),
        );
        self.frame.serial = self.frame.serial.saturating_add(1);
    }

    fn swap_characters(&mut self) {
        self.characters.retain(|_, character| {
            character.current = character.next;
            character.next = false;
            character.keep = false;
            character.current
        });
    }

    fn character_mut(&mut self, id: i32) -> Option<&mut Character> {
        self.characters.get_mut(&id.unsigned_abs())
    }

    fn install_frame(&mut self, rgba: Vec<u8>) {
        self.frame = Frame {
            width: WIDTH,
            height: HEIGHT,
            rgba,
            serial: self.frame.serial.saturating_add(1),
        };
    }
}

fn rotate_frame(source: &[u8], center_x: f64, center_y: f64, angle: f64) -> Vec<u8> {
    let mut output = vec![0; source.len()];
    for pixel in output.chunks_exact_mut(4) {
        pixel[3] = 255;
    }
    let cosine = angle.cos();
    let sine = angle.sin();
    for target_y in 0..HEIGHT {
        for target_x in 0..WIDTH {
            let x = f64::from(target_x) - center_x;
            let y = f64::from(target_y) - center_y;
            let source_x = (cosine * x + sine * y + center_x) as i32;
            let source_y = (-sine * x + cosine * y + center_y) as i32;
            if !(0..WIDTH as i32).contains(&source_x) || !(0..HEIGHT as i32).contains(&source_y) {
                continue;
            }
            let source_index = ((source_y as u32 * WIDTH + source_x as u32) * 4) as usize;
            let target_index = ((target_y * WIDTH + target_x) * 4) as usize;
            output[target_index..target_index + 4]
                .copy_from_slice(&source[source_index..source_index + 4]);
        }
    }
    output
}

fn translate_frame(source: &[u8], offset_x: i32, offset_y: i32) -> Vec<u8> {
    let mut output = vec![0; source.len()];
    for pixel in output.chunks_exact_mut(4) {
        pixel[3] = 255;
    }
    for source_y in 0..HEIGHT as i32 {
        let target_y = source_y + offset_y;
        if !(0..HEIGHT as i32).contains(&target_y) {
            continue;
        }
        for source_x in 0..WIDTH as i32 {
            let target_x = source_x + offset_x;
            if !(0..WIDTH as i32).contains(&target_x) {
                continue;
            }
            let source_index = ((source_y as u32 * WIDTH + source_x as u32) * 4) as usize;
            let target_index = ((target_y as u32 * WIDTH + target_x as u32) * 4) as usize;
            output[target_index..target_index + 4]
                .copy_from_slice(&source[source_index..source_index + 4]);
        }
    }
    output
}

fn zoom_inset_frame(source: &[u8], amplitude: i32) -> Vec<u8> {
    let inset_x = amplitude.clamp(0, WIDTH.saturating_sub(1) as i32) as u32;
    let inset_y = amplitude.clamp(0, HEIGHT.saturating_sub(1) as i32) as u32;
    let source_width = WIDTH.saturating_sub(inset_x.saturating_mul(2)).max(1);
    let source_height = HEIGHT.saturating_sub(inset_y.saturating_mul(2)).max(1);
    let mut output = vec![0; source.len()];
    for target_y in 0..HEIGHT {
        let source_y = inset_y + target_y.saturating_mul(source_height) / HEIGHT;
        for target_x in 0..WIDTH {
            let source_x = inset_x + target_x.saturating_mul(source_width) / WIDTH;
            let source_index = ((source_y * WIDTH + source_x) * 4) as usize;
            let target_index = ((target_y * WIDTH + target_x) * 4) as usize;
            output[target_index..target_index + 4]
                .copy_from_slice(&source[source_index..source_index + 4]);
        }
    }
    output
}

fn scroll_pair(scroll: &ScrollState, elapsed_units: u32) -> (i32, i32) {
    let delta_x = i64::from(scroll.target_x) - i64::from(scroll.start_x);
    let delta_y = i64::from(scroll.target_y) - i64::from(scroll.start_y);
    let distance = delta_x.abs().max(delta_y.abs());
    if distance == 0 {
        return (scroll.target_x, scroll.target_y);
    }
    let travelled = i64::from(scroll.speed) * i64::from(elapsed_units) / 10;
    if travelled >= distance {
        return (scroll.target_x, scroll.target_y);
    }
    let interpolate = |start: i32, delta: i64| {
        let value = i64::from(start) + delta * travelled / distance;
        i32::try_from(value.clamp(i64::from(i32::MIN), i64::from(i32::MAX))).unwrap_or(start)
    };
    (
        interpolate(scroll.start_x, delta_x),
        interpolate(scroll.start_y, delta_y),
    )
}

fn scroll_axis(start: i32, target: i32, speed: i32, elapsed_units: u32) -> i32 {
    if start == target || speed == 0 {
        return start;
    }
    let offset = i64::from(speed) * i64::from(elapsed_units) / 10;
    let candidate = (i64::from(start) + offset).clamp(i64::from(i32::MIN), i64::from(i32::MAX));
    if speed > 0 {
        i32::try_from(candidate.min(i64::from(target))).unwrap_or(target)
    } else {
        i32::try_from(candidate.max(i64::from(target))).unwrap_or(target)
    }
}

fn clear_character_range(characters: &mut HashMap<u32, Character>, first: i32, last: i32) {
    if first <= 0 && last <= 0 {
        characters.clear();
        return;
    }
    characters.retain(|key, _| !character_in_range(*key, first, last));
}

fn character_in_range(key: u32, first: i32, last: i32) -> bool {
    if first <= 0 && last <= 0 {
        return true;
    }
    let first = first.unsigned_abs();
    let last = last.unsigned_abs();
    key >= first.min(last) && key <= first.max(last)
}

fn configure_character_sequence(
    character: &mut Character,
    timed: bool,
    frames: i32,
    rate: i32,
    start: i32,
) {
    if !timed {
        character.sequence = None;
        return;
    }
    character.sequence = Some(CharacterSequence {
        frames: u32::try_from(frames.max(1)).unwrap_or(u32::MAX),
        rate: u32::try_from(rate.max(1)).unwrap_or(u32::MAX),
        start: u32::try_from(start.max(0)).unwrap_or(u32::MAX),
        elapsed_ms: 0,
        loaded_frame: 0,
    });
}

fn advance_characters(
    vfs: &Vfs,
    characters: &mut HashMap<u32, Character>,
    delta_ms: u32,
) -> Result<bool, String> {
    let mut changed = false;
    for character in characters.values_mut() {
        if advance_character_movement(character, delta_ms) {
            changed = true;
        }
        let Some(sequence) = character.sequence.as_mut() else {
            continue;
        };
        sequence.elapsed_ms = sequence.elapsed_ms.saturating_add(delta_ms);
        let frame = (sequence.start + sequence.elapsed_ms.saturating_mul(sequence.rate) / 1000)
            % sequence.frames;
        if frame == sequence.loaded_frame {
            continue;
        }
        let name = sequence_frame_name(&character.image_name, frame + 1)?;
        let mut image = decode_png(&vfs.read(&name)?)?;
        if let Some((width, height)) = character.scaled_size {
            image = resize_nearest(&image, width, height);
        }
        character.image = image;
        sequence.loaded_frame = frame;
        changed = true;
    }
    Ok(changed)
}

fn script_units_to_ms(value: i32) -> u32 {
    u32::try_from(value.max(0))
        .unwrap_or(u32::MAX)
        .saturating_mul(10)
}

fn finish_message_char_transitions(
    characters: &mut HashMap<u32, Character>,
    transitions: &mut Vec<MessageCharTransition>,
) {
    for transition in transitions.iter() {
        if let Some(character) = characters.get_mut(&transition.id.unsigned_abs()) {
            character.blend_rate = transition.target_blend;
            character.movement = None;
        }
    }
    transitions.clear();
}

fn advance_message_char_transitions(
    characters: &mut HashMap<u32, Character>,
    transitions: &mut Vec<MessageCharTransition>,
    delta_ms: u32,
) -> bool {
    let mut changed = false;
    for transition in transitions.iter_mut() {
        if transition.started {
            continue;
        }
        transition.elapsed_ms = transition.elapsed_ms.saturating_add(delta_ms);
        if transition.elapsed_ms < transition.delay_ms {
            continue;
        }
        let Some(character) = characters.get_mut(&transition.id.unsigned_abs()) else {
            transition.started = true;
            continue;
        };
        let duration_ms = script_units_to_ms(transition.duration_units);
        if duration_ms == 0 {
            character.blend_rate = transition.target_blend;
            character.movement = None;
        } else {
            character.movement = Some(CharacterMovement {
                start_x: character.x,
                start_y: character.y,
                target_x: character.x,
                target_y: character.y,
                start_blend: character.blend_rate,
                target_blend: Some(transition.target_blend),
                x_mode: 0,
                y_mode: 0,
                duration_ms,
                elapsed_ms: 0,
            });
        }
        transition.started = true;
        changed = true;
    }
    transitions.retain(|transition| {
        !transition.started
            || characters
                .get(&transition.id.unsigned_abs())
                .is_some_and(|character| character.movement.is_some())
    });
    changed
}

fn advance_character_movement(character: &mut Character, delta_ms: u32) -> bool {
    let Some(movement) = character.movement.as_mut() else {
        return false;
    };
    movement.elapsed_ms = movement.elapsed_ms.saturating_add(delta_ms);
    let complete = movement.elapsed_ms >= movement.duration_ms;
    let progress = if complete {
        1.0
    } else {
        f64::from(movement.elapsed_ms) / f64::from(movement.duration_ms)
    };
    character.x = movement.start_x
        + ((f64::from(movement.target_x) - f64::from(movement.start_x))
            * movement_progress(progress, movement.x_mode)) as i32;
    character.y = movement.start_y
        + ((f64::from(movement.target_y) - f64::from(movement.start_y))
            * movement_progress(progress, movement.y_mode)) as i32;
    if let Some(target) = movement.target_blend {
        character.blend_rate = movement.start_blend
            + ((f64::from(target) - f64::from(movement.start_blend)) * progress) as i32;
    }
    if complete {
        character.x = movement.target_x;
        character.y = movement.target_y;
        if let Some(target) = movement.target_blend {
            character.blend_rate = target;
        }
        character.movement = None;
    }
    true
}

fn transition_is_immediate(transition_type: i32) -> bool {
    !(0..=18).contains(&transition_type)
}

fn movement_progress(progress: f64, mode: i32) -> f64 {
    match mode {
        1 => progress * progress,
        2 => 2.0 * progress - progress * progress,
        _ => progress,
    }
}

fn resize_nearest(source: &Frame, width: u32, height: u32) -> Frame {
    let mut rgba = vec![0; width as usize * height as usize * 4];
    for y in 0..height {
        let source_y = u64::from(y) * u64::from(source.height) / u64::from(height);
        for x in 0..width {
            let source_x = u64::from(x) * u64::from(source.width) / u64::from(width);
            let source_index = ((source_y as u32 * source.width + source_x as u32) * 4) as usize;
            let target_index = ((y * width + x) * 4) as usize;
            rgba[target_index..target_index + 4]
                .copy_from_slice(&source.rgba[source_index..source_index + 4]);
        }
    }
    Frame {
        width,
        height,
        rgba,
        serial: source.serial.saturating_add(1),
    }
}

fn sequence_frame_name(name: &str, frame: u32) -> Result<String, String> {
    let dot = name
        .rfind('.')
        .ok_or_else(|| format!("sequence image has no extension: {name}"))?;
    if dot < 4
        || !name[dot - 4..dot]
            .bytes()
            .all(|value| value.is_ascii_digit())
    {
        return Err(format!("sequence image has no four-digit suffix: {name}"));
    }
    Ok(format!("{}{:04}{}", &name[..dot - 4], frame, &name[dot..]))
}

fn freeze_character_layers(background: &[u8], characters: &mut HashMap<u32, Character>) -> Vec<u8> {
    let composed = compose_characters(background, characters);
    characters.clear();
    composed
}

fn compose_characters(background: &[u8], characters: &HashMap<u32, Character>) -> Vec<u8> {
    let mut output = background.to_vec();
    let mut ordered: Vec<_> = characters.values().collect();
    ordered.sort_by_key(|character| character.order);
    for character in ordered {
        if !character.current || !character.visible || character.blend_rate <= 0 {
            continue;
        }
        composite_character_surface(&mut output, character, &character.image);
        if let Some(mouth) = &character.mouth {
            if let Some(eye) = character
                .blink_state
                .then_some(mouth.closed_eye.as_ref())
                .flatten()
            {
                let base_left = character.x - character.image.width as i32 / 2;
                composite_character_surface_at(
                    &mut output,
                    character,
                    &eye.image,
                    base_left + eye.x,
                    character.y + eye.y,
                );
            }
            let overlay = match character.mouth_state {
                1 => Some(&mouth.medium),
                2 => Some(&mouth.open),
                _ => None,
            };
            if let Some(overlay) = overlay {
                let base_left = character.x - character.image.width as i32 / 2;
                composite_character_surface_at(
                    &mut output,
                    character,
                    &overlay.image,
                    base_left + overlay.x,
                    character.y + overlay.y,
                );
            }
        }
        if let Some(secondary) = &character.secondary_image {
            composite_character_surface(&mut output, character, secondary);
        }
    }
    output
}

fn composite_character_surface(output: &mut [u8], character: &Character, image: &Frame) {
    let left = character.x - image.width as i32 / 2;
    composite_character_surface_at(output, character, image, left, character.y);
}

fn composite_character_surface_at(
    output: &mut [u8],
    character: &Character,
    image: &Frame,
    left: i32,
    top: i32,
) {
    for source_y in 0..image.height as i32 {
        let target_y = top + source_y;
        if !(0..HEIGHT as i32).contains(&target_y) {
            continue;
        }
        for source_x in 0..image.width as i32 {
            let target_x = left + source_x;
            if !(0..WIDTH as i32).contains(&target_x) {
                continue;
            }
            let source_index = ((source_y as u32 * image.width + source_x as u32) * 4) as usize;
            let target_index = ((target_y as u32 * WIDTH + target_x as u32) * 4) as usize;
            let alpha = u32::from(image.rgba[source_index + 3])
                * character.blend_rate.clamp(0, 256) as u32
                / 256;
            for channel in 0..3 {
                let source = u32::from(image.rgba[source_index + channel]);
                let target = u32::from(output[target_index + channel]);
                output[target_index + channel] =
                    ((source * alpha + target * (255 - alpha)) / 255) as u8;
            }
            output[target_index + 3] = 255;
        }
    }
}

fn transition_completion(transition_type: i32) -> u32 {
    match transition_type {
        0 => 255,
        16 => HEIGHT,
        14 | 15 | 17 => WIDTH,
        _ => unreachable!("only implemented transition types can be active"),
    }
}

fn render_transition(
    transition: &mut TransitionState,
    previous_progress: u32,
    output: &mut Vec<u8>,
) {
    match transition.transition_type {
        0 => blend_frames(
            &transition.old_rgba,
            &transition.new_rgba,
            transition.progress,
            output,
        ),
        1 => {
            let mask = transition
                .mask
                .as_ref()
                .expect("maskless type 1 completes before rendering");
            for (index, &value) in mask.iter().enumerate() {
                let alpha = (u32::from(value) + transition.progress)
                    .saturating_sub(256)
                    .min(255) as i32;
                let pixel = index * 4;
                for channel in 0..3 {
                    let old = i32::from(transition.old_rgba[pixel + channel]);
                    let new = i32::from(transition.new_rgba[pixel + channel]);
                    output[pixel + channel] = (old + (((new - old) * alpha) >> 8)) as u8;
                }
                output[pixel + 3] = 255;
            }
        }
        2 => {
            let (source, index) = if transition.progress < 200 {
                (&transition.old_rgba, transition.progress)
            } else {
                (&transition.new_rgba, 399 - transition.progress)
            };
            let scaled = center_zoom_nearest(source, WIDTH, HEIGHT, transition_zoom_factor(index));
            for (target, scaled) in output.iter_mut().zip(scaled) {
                *target = (*target & scaled) + (((*target ^ scaled) & 0xFE) >> 1);
            }
        }
        3..=13 => render_tile_transition(transition, previous_progress, output),
        14 => {
            output.clone_from(&transition.old_rgba);
            let right = transition.progress.min(WIDTH);
            let bottom = right.saturating_mul(HEIGHT) / WIDTH;
            copy_transition_rect(&transition.new_rgba, output, 0, 0, right, bottom);
        }
        15 => {
            output.clone_from(&transition.old_rgba);
            let right = transition.progress.min(WIDTH);
            let bottom = right.saturating_mul(HEIGHT) / WIDTH;
            copy_transition_rect(&transition.new_rgba, output, 0, 0, right, bottom);
            fill_transition_rect(output, 0, bottom, right, HEIGHT, 0xFF00_0000);
            fill_transition_rect(output, right, 0, WIDTH, bottom, 0xFF00_0000);
        }
        16 => {
            output.clone_from(&transition.old_rgba);
            copy_transition_rect(
                &transition.new_rgba,
                output,
                0,
                0,
                WIDTH,
                transition.progress.min(HEIGHT),
            );
        }
        17 => {
            output.clone_from(&transition.old_rgba);
            copy_transition_rect(
                &transition.new_rgba,
                output,
                0,
                0,
                transition.progress.min(WIDTH),
                HEIGHT,
            );
        }
        18 => {
            let mask = transition
                .mask
                .as_ref()
                .expect("maskless type 18 completes before rendering");
            for (index, &value) in mask.iter().enumerate() {
                let value = u32::from(value);
                if value >= previous_progress && value <= transition.progress {
                    let pixel = index * 4;
                    output[pixel..pixel + 4]
                        .copy_from_slice(&transition.new_rgba[pixel..pixel + 4]);
                }
            }
        }
        _ => unreachable!("only implemented transition types can be rendered"),
    }
}

fn msvc_rand(state: &mut u32) -> i32 {
    *state = state.wrapping_mul(214013).wrapping_add(2531011);
    ((*state >> 16) & 0x7fff) as i32
}

fn transition_tile_values(mode: i32, rand_state: &mut u32) -> Vec<i32> {
    let columns = WIDTH.div_ceil(40) as i32;
    let rows = HEIGHT.div_ceil(40) as i32;
    let mut values = vec![0; (columns * rows) as usize];
    let index = |x: i32, y: i32| (y * columns + x) as usize;
    match mode {
        0 => {}
        1 => {
            for y in 0..rows {
                for x in 0..columns {
                    values[index(x, y)] = -(y * columns + x) * 10;
                }
            }
        }
        2 => {
            let count = columns * rows;
            for y in 0..rows {
                for x in 0..columns {
                    values[index(x, y)] = (1 - count + y * columns + x) * 10;
                }
            }
        }
        3 => {
            for y in 0..rows {
                for x in 0..columns {
                    values[index(x, y)] = -(x * columns + y) * 10;
                }
            }
        }
        4 => {
            for y in 0..rows {
                for x in 0..columns {
                    values[index(x, y)] = -((columns - 1 - x) * columns + (rows - 1 - y)) * 10;
                }
            }
        }
        5 | 6 => {
            for diagonal in 0..(columns + rows) {
                for y in 0..=diagonal {
                    let x = diagonal - y;
                    if x < columns && y < rows {
                        let (x, y) = if mode == 5 {
                            (x, y)
                        } else {
                            (columns - 1 - x, rows - 1 - y)
                        };
                        values[index(x, y)] = -diagonal * (columns + rows) * 10;
                    }
                }
            }
        }
        7 | 8 => {
            for y in 0..rows {
                for x in 0..columns {
                    let (dx, dy) = if mode == 7 {
                        (x.min(columns - x), y.min(rows - y))
                    } else {
                        ((columns / 2 - x).abs(), (rows / 2 - y).abs())
                    };
                    let distance = ((dx * dx + dy * dy) as f64).sqrt() as i32;
                    values[index(x, y)] = -distance * columns * 10;
                }
            }
        }
        9 => {
            let delay_range = columns * rows * 10;
            for value in &mut values {
                *value = -(msvc_rand(rand_state) % delay_range);
            }
        }
        10 => {
            let mut columns_order: Vec<_> = (0..columns).map(|x| x * rows * 10).collect();
            for x in 0..columns {
                let other = msvc_rand(rand_state) % columns;
                columns_order.swap(x as usize, other as usize);
            }
            for x in 0..columns {
                let mut delay = columns_order[x as usize];
                for y in 0..rows {
                    values[index(x, y)] = -delay;
                    delay += columns * 10;
                }
            }
        }
        _ => unreachable!("tile transition mode is 0..=10"),
    }
    values
}

fn render_tile_transition(
    transition: &mut TransitionState,
    previous_progress: u32,
    output: &mut [u8],
) {
    let columns = WIDTH.div_ceil(40);
    let tile_count = (WIDTH.div_ceil(40) * HEIGHT.div_ceil(40)) as i32;
    let delta =
        i32::try_from(transition.progress.saturating_sub(previous_progress)).unwrap_or(i32::MAX);
    let values = transition
        .tile_values
        .as_mut()
        .expect("tile transition requires timing values");
    for (tile, value) in values.iter_mut().enumerate() {
        if *value >= tile_count * 10 {
            continue;
        }
        let previous_phase = *value / tile_count;
        *value = value.saturating_add(delta);
        if (0..=10).contains(&previous_phase) {
            let phase = *value / tile_count;
            if phase != previous_phase {
                let phase = phase.clamp(0, 10) as u32;
                let left = tile as u32 % columns * 40;
                let top = tile as u32 / columns * 40;
                copy_transition_rect(
                    &transition.new_rgba,
                    output,
                    left,
                    top,
                    left + 4 * phase,
                    top + 4 * phase,
                );
            }
        }
    }
}

fn transition_zoom_factor(index: u32) -> f64 {
    1.0 + f64::from(index).powi(2) * 0.002_499_936_87
}

fn center_zoom_nearest(source: &[u8], width: u32, height: u32, factor: f64) -> Vec<u8> {
    if factor <= 1.0 {
        return source.to_vec();
    }
    let source_width = ((f64::from(width) / factor) as u32).max(1);
    let source_height = ((f64::from(height) / factor) as u32).max(1);
    let source_left = (width - source_width) / 2;
    let source_top = (height - source_height) / 2;
    let mut x_offsets = Vec::with_capacity(width as usize);
    let mut error = 1_i64 - i64::from(width);
    for _ in 0..width {
        error += 2 * i64::from(source_width) - 2;
        let advance = if error >= 0 {
            error += 2 - 2 * i64::from(width);
            1_u32
        } else {
            0
        };
        x_offsets.push(advance);
    }

    let mut output = vec![0; source.len()];
    let mut source_y = source_top;
    let mut copy_source_row = true;
    let mut y_error = 1_i64 - i64::from(height);
    for target_y in 0..height {
        let target_start = (target_y * width * 4) as usize;
        if copy_source_row {
            let mut source_x = source_left;
            for (target_x, advance) in x_offsets.iter().copied().enumerate() {
                let source_index = ((source_y * width + source_x) * 4) as usize;
                let target_index = target_start + target_x * 4;
                output[target_index..target_index + 4]
                    .copy_from_slice(&source[source_index..source_index + 4]);
                source_x += advance;
            }
        } else {
            let previous_start = target_start - width as usize * 4;
            let (previous, current) = output.split_at_mut(target_start);
            current[..width as usize * 4].copy_from_slice(&previous[previous_start..target_start]);
        }
        y_error += 2 * i64::from(source_height) - 2;
        if y_error < 0 {
            copy_source_row = false;
        } else {
            source_y += 1;
            y_error += 2 - 2 * i64::from(height);
            copy_source_row = true;
        }
    }
    output
}

fn copy_transition_rect(
    source: &[u8],
    output: &mut [u8],
    left: u32,
    top: u32,
    right: u32,
    bottom: u32,
) {
    for y in top.min(HEIGHT)..bottom.min(HEIGHT) {
        let start = ((y * WIDTH + left.min(WIDTH)) * 4) as usize;
        let end = ((y * WIDTH + right.min(WIDTH)) * 4) as usize;
        output[start..end].copy_from_slice(&source[start..end]);
    }
}

fn fill_transition_rect(
    output: &mut [u8],
    left: u32,
    top: u32,
    right: u32,
    bottom: u32,
    color: u32,
) {
    let pixel = color.to_le_bytes();
    for y in top.min(HEIGHT)..bottom.min(HEIGHT) {
        for x in left.min(WIDTH)..right.min(WIDTH) {
            let index = ((y * WIDTH + x) * 4) as usize;
            output[index..index + 4].copy_from_slice(&pixel);
        }
    }
}

fn save_slot_hit(profile: &GameProfile, x: f64, y: f64) -> Option<u8> {
    let layout = &profile.save_load;
    for row in 0..5_u8 {
        let top = layout.first_row_top + f64::from(row) * layout.row_step;
        if (top..top + layout.row_height).contains(&y) {
            if (layout.left_column.left..layout.left_column.right).contains(&x) {
                return Some(row);
            }
            if (layout.right_column.left..layout.right_column.right).contains(&x) {
                return Some(row + 5);
            }
        }
    }
    None
}

fn game_menu_hit(
    profile: &GameProfile,
    x: f64,
    y: f64,
    left: i32,
    top: i32,
) -> Option<GameMenuAction> {
    let local_x = x - f64::from(left);
    let local_y = y - f64::from(top);
    for item in profile.game_menu.items {
        if (f64::from(item.x)..f64::from(item.x) + item.width).contains(&local_x)
            && (f64::from(item.y)..f64::from(item.y) + item.height).contains(&local_y)
        {
            return Some(match item.kind {
                GameMenuItemKind::Skip => GameMenuAction::Skip,
                GameMenuItemKind::QuickSave => GameMenuAction::QuickSave,
                GameMenuItemKind::System => GameMenuAction::System,
                GameMenuItemKind::Load => GameMenuAction::Load,
                GameMenuItemKind::Save => GameMenuAction::Save,
            });
        }
    }
    None
}

fn composite_panel_state(
    output: &mut [u8],
    transition: Option<&PanelTransition>,
    panel: Option<&Frame>,
) {
    if let Some(transition) = transition {
        render_panel_transition(output, transition);
    } else if let Some(panel) = panel {
        composite_panel(output, panel, 255);
    }
}

fn render_panel_transition(output: &mut [u8], transition: &PanelTransition) {
    let ticks = transition.elapsed_ms / 10;
    let progress = transition.speed.saturating_mul(ticks).min(255) as u8;
    if let Some(old) = &transition.old {
        composite_panel(output, old, 255 - progress);
    }
    if let Some(new) = &transition.new {
        composite_panel(output, new, progress);
    }
}

fn composite_panel(output: &mut [u8], panel: &Frame, opacity: u8) {
    let top = HEIGHT as i32 - panel.height as i32 + 64;
    for source_y in 0..panel.height as i32 {
        let target_y = top + source_y;
        if !(0..HEIGHT as i32).contains(&target_y) {
            continue;
        }
        for source_x in 0..panel.width.min(WIDTH) {
            let source_index = ((source_y as u32 * panel.width + source_x) * 4) as usize;
            let target_index = ((target_y as u32 * WIDTH + source_x) * 4) as usize;
            let alpha = u32::from(panel.rgba[source_index + 3]) * u32::from(opacity) / 255;
            for channel in 0..3 {
                let source = u32::from(panel.rgba[source_index + channel]);
                let target = u32::from(output[target_index + channel]);
                output[target_index + channel] =
                    ((source * alpha + target * (255 - alpha)) / 255) as u8;
            }
            output[target_index + 3] = 255;
        }
    }
}

fn parse_stage_base(spec: &str) -> Result<(&str, Vec<&str>), String> {
    let Some((screen, layers)) = spec.split_once(':') else {
        return Ok((spec, Vec::new()));
    };
    let layers: Vec<_> = layers.split(',').collect();
    if layers.len() != 3 {
        return Err(format!("stage base requires screen:BG,ST,FG, got {spec}"));
    }
    Ok((screen, layers))
}

fn composite_source_over(target: &mut Frame, overlay: &Frame, x: i32, y: i32) {
    for overlay_y in 0..overlay.height as i32 {
        let target_y = y.saturating_add(overlay_y);
        if !(0..target.height as i32).contains(&target_y) {
            continue;
        }
        for overlay_x in 0..overlay.width as i32 {
            let target_x = x.saturating_add(overlay_x);
            if !(0..target.width as i32).contains(&target_x) {
                continue;
            }
            let source_index = ((overlay_y as u32 * overlay.width + overlay_x as u32) * 4) as usize;
            let target_index = ((target_y as u32 * target.width + target_x as u32) * 4) as usize;
            blend_pixel(
                &mut target.rgba[target_index..target_index + 4],
                &overlay.rgba[source_index..source_index + 4],
            );
        }
    }
}

fn advance_cutin(cutin: &mut CutinState, steps: u32) {
    for _ in 0..steps {
        match cutin.state {
            0 => {
                cutin.x = cutin.x.saturating_add(cutin.velocity as i32);
                cutin.velocity *= cutin.factor;
                if (cutin.from_left && cutin.x >= 0) || (!cutin.from_left && cutin.x <= 0) {
                    cutin.x = 0;
                    cutin.state = 1;
                }
            }
            1 => {
                if cutin.hold >= 0 {
                    cutin.hold = cutin.hold.saturating_sub(4);
                    if cutin.hold <= 0 {
                        cutin.state = 2;
                        cutin.velocity = if cutin.from_left { 2.0 } else { -2.0 };
                    }
                }
            }
            2 => {
                cutin.x = cutin.x.saturating_add(cutin.velocity as i32);
                cutin.velocity *= cutin.factor;
                let complete = if cutin.from_left {
                    cutin.x >= cutin.image.width as i32
                } else {
                    cutin.x <= -(cutin.image.width as i32)
                };
                if complete {
                    cutin.state = 3;
                }
            }
            _ => break,
        }
    }
}

fn advance_wscroll(effect: &mut WScrollState, steps: u32) -> bool {
    let mut changed = false;
    for _ in 0..steps {
        effect.ticks = effect.ticks.saturating_add(1);
        let velocity = effect.speed.saturating_mul(effect.ticks) / 10;
        let delta = velocity.saturating_sub(effect.previous_velocity);
        if delta.unsigned_abs() < 2 {
            continue;
        }
        effect.remainder = effect.remainder.saturating_add(delta);
        effect.previous_velocity = velocity;
        if effect.remainder.unsigned_abs() > 5 {
            effect.offset = effect.offset.saturating_add(effect.remainder / 5);
            effect.remainder %= 5;
            changed = true;
        }
    }
    changed
}

fn composite_viewport(target: &mut Frame, overlay: &Frame, source_x: i32, source_y: i32) {
    for target_y in 0..target.height as i32 {
        let overlay_y = source_y.saturating_add(target_y);
        if !(0..overlay.height as i32).contains(&overlay_y) {
            continue;
        }
        for target_x in 0..target.width as i32 {
            let overlay_x = source_x.saturating_add(target_x);
            if !(0..overlay.width as i32).contains(&overlay_x) {
                continue;
            }
            let source_index = ((overlay_y as u32 * overlay.width + overlay_x as u32) * 4) as usize;
            let target_index = ((target_y as u32 * target.width + target_x as u32) * 4) as usize;
            blend_pixel(
                &mut target.rgba[target_index..target_index + 4],
                &overlay.rgba[source_index..source_index + 4],
            );
        }
    }
}

fn composite_wrapped_horizontal(target: &mut Frame, overlay: &Frame, source_x: i32, source_y: i32) {
    if overlay.width == 0 {
        return;
    }
    let width = overlay.width as i32;
    let start_x = source_x.rem_euclid(width);
    for target_y in 0..target.height as i32 {
        let overlay_y = source_y.saturating_add(target_y);
        if !(0..overlay.height as i32).contains(&overlay_y) {
            continue;
        }
        for target_x in 0..target.width as i32 {
            let overlay_x = (start_x + target_x).rem_euclid(width);
            let source_index = ((overlay_y as u32 * overlay.width + overlay_x as u32) * 4) as usize;
            let target_index = ((target_y as u32 * target.width + target_x as u32) * 4) as usize;
            blend_pixel(
                &mut target.rgba[target_index..target_index + 4],
                &overlay.rgba[source_index..source_index + 4],
            );
        }
    }
}

fn blend_pixel(target: &mut [u8], source: &[u8]) {
    let alpha = u32::from(source[3]);
    let inverse = 255 - alpha;
    for channel in 0..3 {
        target[channel] =
            ((u32::from(source[channel]) * alpha + u32::from(target[channel]) * inverse + 127)
                / 255) as u8;
    }
    let target_alpha = u32::from(target[3]);
    target[3] = (alpha + (target_alpha * inverse + 127) / 255).min(255) as u8;
}

fn crop_background(source: &Frame, x: i32, y: i32) -> Result<Frame, String> {
    let x = u32::try_from(x.max(0)).unwrap_or(0);
    let y = u32::try_from(y.max(0)).unwrap_or(0);
    let mut rgba = vec![0; WIDTH as usize * HEIGHT as usize * 4];
    for pixel in rgba.chunks_exact_mut(4) {
        pixel[3] = 255;
    }
    let copy_width = WIDTH.min(source.width.saturating_sub(x));
    let copy_height = HEIGHT.min(source.height.saturating_sub(y));
    let source_stride = source.width as usize * 4;
    let target_stride = WIDTH as usize * 4;
    let copy_bytes = copy_width as usize * 4;
    for row in 0..copy_height as usize {
        let source_start = (y as usize + row) * source_stride + x as usize * 4;
        let target_start = row * target_stride;
        rgba[target_start..target_start + copy_bytes]
            .copy_from_slice(&source.rgba[source_start..source_start + copy_bytes]);
    }
    Ok(Frame {
        width: WIDTH,
        height: HEIGHT,
        rgba,
        serial: 0,
    })
}

fn blend_frames(old: &[u8], new: &[u8], progress: u32, output: &mut [u8]) {
    for ((old_pixel, new_pixel), output_pixel) in old
        .chunks_exact(4)
        .zip(new.chunks_exact(4))
        .zip(output.chunks_exact_mut(4))
    {
        for channel in 0..3 {
            let old_value = i32::from(old_pixel[channel]);
            let difference = i32::from(new_pixel[channel]) - old_value;
            output_pixel[channel] =
                (old_value + ((difference * progress as i32) >> 8)).clamp(0, 255) as u8;
        }
        output_pixel[3] = 255;
    }
}

fn decode_transition_mask(data: &[u8]) -> Result<Vec<u8>, String> {
    let mut decoder = png::Decoder::new(Cursor::new(data));
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let mut reader = decoder
        .read_info()
        .map_err(|e| format!("transition mask PNG header: {e}"))?;
    let mut pixels = vec![
        0;
        reader
            .output_buffer_size()
            .ok_or("transition mask PNG output is too large")?
    ];
    let info = reader
        .next_frame(&mut pixels)
        .map_err(|e| format!("transition mask PNG frame: {e}"))?;
    if info.width != WIDTH || info.height != HEIGHT {
        return Err(format!(
            "transition mask is {}x{}, expected {WIDTH}x{HEIGHT}",
            info.width, info.height
        ));
    }
    if info.color_type != png::ColorType::Grayscale {
        return Err(format!(
            "transition mask is {:?}, expected 8-bit grayscale",
            info.color_type
        ));
    }
    Ok(pixels[..info.buffer_size()]
        .iter()
        .map(|value| !value)
        .collect())
}

fn select_mouth_target(characters: &HashMap<u32, Character>, actor: Option<&str>) -> Option<u32> {
    if let Some(exact) = actor.and_then(|actor| {
        characters.iter().find_map(|(id, character)| {
            character
                .mouth
                .as_ref()
                .is_some_and(|mouth| mouth.actor == actor)
                .then_some(*id)
        })
    }) {
        return Some(exact);
    }
    let mut candidates = characters.iter().filter_map(|(id, character)| {
        (character.visible && character.mouth.is_some()).then_some(*id)
    });
    let first = candidates.next()?;
    candidates.next().is_none().then_some(first)
}

fn advance_blinks(characters: &mut HashMap<u32, Character>, delta_ms: u32) -> bool {
    let mut changed = false;
    for character in characters.values_mut() {
        let Some(mouth) = character.mouth.as_ref() else {
            continue;
        };
        if !character.blink_enabled || mouth.open_eye.is_none() || mouth.closed_eye.is_none() {
            continue;
        }
        character.blink_elapsed_ms = character.blink_elapsed_ms.saturating_add(delta_ms);
        if character.blink_state {
            if character.blink_elapsed_ms >= 100 {
                character.blink_elapsed_ms = 0;
                character.blink_state = false;
                changed = true;
            }
        } else if character.blink_elapsed_ms >= 2800 {
            character.blink_elapsed_ms = 0;
            character.blink_state = true;
            changed = true;
        }
    }
    changed
}

fn parse_mouth_animation(data: &[u8]) -> Result<Option<MouthAnimation>, String> {
    if data.len() < 8 || data[0..2] != [0, 1] {
        return Err("unsupported Minori ANI header".into());
    }
    let count = u16::from_le_bytes(data[2..4].try_into().unwrap()) as usize;
    let mut offset = 8;
    let mut actor = None;
    let mut medium = None;
    let mut open = None;
    let mut open_eye = None;
    let mut closed_eye = None;
    for _ in 0..count {
        let name_end = data[offset..]
            .iter()
            .position(|&value| value == 0)
            .map(|value| offset + value)
            .ok_or("ANI record name has no terminator")?;
        let (name, _, had_errors) = SHIFT_JIS.decode(&data[offset..name_end]);
        if had_errors {
            return Err("ANI record name is invalid Shift-JIS".into());
        }
        offset = name_end + 1;
        if offset + 10 > data.len() {
            return Err("ANI record metadata is truncated".into());
        }
        let width = u16::from_le_bytes(data[offset..offset + 2].try_into().unwrap()) as u32;
        let height = u16::from_le_bytes(data[offset + 2..offset + 4].try_into().unwrap()) as u32;
        let bpp = u16::from_le_bytes(data[offset + 4..offset + 6].try_into().unwrap());
        let x = u16::from_le_bytes(data[offset + 6..offset + 8].try_into().unwrap()) as i32;
        let y = u16::from_le_bytes(data[offset + 8..offset + 10].try_into().unwrap()) as i32;
        offset += 10;
        if bpp != 32 {
            return Err(format!("ANI record uses unsupported {bpp}-bit pixels"));
        }
        let pixel_len = width as usize * height as usize * 4;
        if offset + pixel_len > data.len() {
            return Err("ANI record pixels are truncated".into());
        }
        let mut rgba = Vec::with_capacity(pixel_len);
        for bgra in data[offset..offset + pixel_len].chunks_exact(4) {
            rgba.extend_from_slice(&[bgra[2], bgra[1], bgra[0], bgra[3]]);
        }
        offset += pixel_len;
        let Some((record_actor, kind)) = name.rsplit_once(' ') else {
            continue;
        };
        let overlay = MouthOverlay {
            image: Frame {
                width,
                height,
                rgba,
                serial: 1,
            },
            x,
            y,
        };
        match kind {
            "中口" => {
                actor.get_or_insert_with(|| record_actor.to_string());
                medium = Some(overlay);
            }
            "開き口" => {
                actor.get_or_insert_with(|| record_actor.to_string());
                open = Some(overlay);
            }
            "中目" => open_eye = Some(overlay),
            "閉じ目" => closed_eye = Some(overlay),
            _ => {}
        }
    }
    match (actor, medium, open) {
        (Some(actor), Some(medium), Some(open)) => Ok(Some(MouthAnimation {
            actor,
            medium,
            open,
            open_eye,
            closed_eye,
        })),
        _ => Ok(None),
    }
}

fn decode_png(data: &[u8]) -> Result<Frame, String> {
    let mut decoder = png::Decoder::new(Cursor::new(data));
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let mut reader = decoder
        .read_info()
        .map_err(|e| format!("PNG header: {e}"))?;
    let mut pixels = vec![
        0;
        reader
            .output_buffer_size()
            .ok_or("PNG output is too large")?
    ];
    let info = reader
        .next_frame(&mut pixels)
        .map_err(|e| format!("PNG frame: {e}"))?;
    let source = &pixels[..info.buffer_size()];
    let mut rgba = Vec::with_capacity(info.width as usize * info.height as usize * 4);
    match info.color_type {
        png::ColorType::Rgba => rgba.extend_from_slice(source),
        png::ColorType::Rgb => {
            for rgb in source.chunks_exact(3) {
                rgba.extend_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
            }
        }
        png::ColorType::Grayscale => {
            for value in source {
                rgba.extend_from_slice(&[*value, *value, *value, 255]);
            }
        }
        png::ColorType::GrayscaleAlpha => {
            for value in source.chunks_exact(2) {
                rgba.extend_from_slice(&[value[0], value[0], value[0], value[1]]);
            }
        }
        png::ColorType::Indexed => return Err("PNG palette was not expanded".into()),
    }
    Ok(Frame {
        width: info.width,
        height: info.height,
        rgba,
        serial: 1,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::profile::SUPIPARA_PROFILE;

    fn character(id: u8, current: bool, next: bool) -> Character {
        Character {
            image_name: format!("{id:04}.png"),
            image: Frame {
                width: 1,
                height: 1,
                rgba: vec![id, id, id, 255],
                serial: 1,
            },
            _secondary_name: None,
            secondary_image: None,
            _tertiary_name: None,
            mouth: None,
            mouth_state: 0,
            blink_enabled: true,
            blink_elapsed_ms: 0,
            blink_state: false,
            scaled_size: None,
            x: 0,
            y: 0,
            order: i32::from(id),
            blend_rate: 256,
            visible: true,
            keep: next,
            current,
            next,
            sequence: None,
            movement: None,
        }
    }

    #[test]
    fn title_menu_maps_original_new_and_load_rows() {
        let scene = SceneSystem {
            profile: &SUPIPARA_PROFILE,
            current: SceneKind::MainMenu,
            frame: Frame {
                width: WIDTH,
                height: HEIGHT,
                rgba: Vec::new(),
                serial: 1,
            },
            background_source: Frame {
                width: WIDTH,
                height: HEIGHT,
                rgba: Vec::new(),
                serial: 1,
            },
            background_rgba: Vec::new(),
            camera_x: 0,
            camera_y: 0,
            scroll: None,
            shake: None,
            effect_layers: None,
            wscroll: None,
            cutin: None,
            characters: HashMap::new(),
            panel: None,
            panel_transition: None,
            message_char_transitions: Vec::new(),
            transition: None,
            game_menu: None,
            save_menu: None,
            rand_state: 1,
        };
        assert_eq!(
            scene.main_menu_action(1150.0, 48.0),
            Some(MainMenuAction::NewGame)
        );
        assert_eq!(
            scene.main_menu_action(1150.0, 96.0),
            Some(MainMenuAction::LoadGame)
        );
        assert_eq!(scene.main_menu_action(900.0, 96.0), None);
    }

    #[test]
    fn save_menu_maps_original_two_by_five_slot_grid() {
        assert_eq!(save_slot_hit(&SUPIPARA_PROFILE, 100.0, 100.0), Some(0));
        assert_eq!(save_slot_hit(&SUPIPARA_PROFILE, 500.0, 100.0), Some(5));
        assert_eq!(save_slot_hit(&SUPIPARA_PROFILE, 100.0, 550.0), Some(4));
        assert_eq!(save_slot_hit(&SUPIPARA_PROFILE, 500.0, 550.0), Some(9));
        assert_eq!(save_slot_hit(&SUPIPARA_PROFILE, 430.0, 100.0), None);
    }

    #[test]
    fn game_menu_uses_original_radial_item_coordinates() {
        let left = WIDTH as i32 - 172 - 6;
        let top = HEIGHT as i32 - 169 - 15;
        assert_eq!(
            game_menu_hit(&SUPIPARA_PROFILE, 1147.0, 655.0, left, top),
            Some(GameMenuAction::Save)
        );
        assert_eq!(
            game_menu_hit(&SUPIPARA_PROFILE, 1191.0, 680.0, left, top),
            Some(GameMenuAction::Load)
        );
        assert_eq!(
            game_menu_hit(&SUPIPARA_PROFILE, 1231.0, 582.0, left, top),
            Some(GameMenuAction::QuickSave)
        );
        assert_eq!(
            game_menu_hit(&SUPIPARA_PROFILE, 1236.0, 646.0, left, top),
            Some(GameMenuAction::System)
        );
        assert_eq!(
            game_menu_hit(&SUPIPARA_PROFILE, 1148.0, 596.0, left, top),
            Some(GameMenuAction::Skip)
        );
        assert_eq!(
            game_menu_hit(&SUPIPARA_PROFILE, 300.0, 300.0, left, top),
            None
        );
    }

    #[test]
    fn panel_preserves_the_original_sixty_four_pixel_lower_offset() {
        let panel = Frame {
            width: 1,
            height: 65,
            rgba: vec![255; 65 * 4],
            serial: 0,
        };
        let mut output = vec![0; (WIDTH * HEIGHT * 4) as usize];
        composite_panel_state(&mut output, None, Some(&panel));
        let bottom_left = ((HEIGHT - 1) * WIDTH * 4) as usize;
        assert_eq!(&output[bottom_left..bottom_left + 4], &[255, 255, 255, 255]);
        let previous_left = bottom_left - (WIDTH * 4) as usize;
        assert_eq!(&output[previous_left..previous_left + 4], &[0; 4]);
    }

    #[test]
    fn active_panel_is_preserved_in_scene_transition_targets() {
        let black = [0, 0, 0, 255].repeat((WIDTH * HEIGHT) as usize);
        let panel = Frame {
            width: 1,
            height: 65,
            rgba: vec![255; 65 * 4],
            serial: 1,
        };
        let mut scene = SceneSystem {
            profile: &SUPIPARA_PROFILE,
            current: SceneKind::Script,
            frame: Frame {
                width: WIDTH,
                height: HEIGHT,
                rgba: black.clone(),
                serial: 1,
            },
            background_source: Frame {
                width: WIDTH,
                height: HEIGHT,
                rgba: black.clone(),
                serial: 1,
            },
            background_rgba: black.clone(),
            camera_x: 0,
            camera_y: 0,
            scroll: None,
            shake: None,
            effect_layers: None,
            wscroll: None,
            cutin: None,
            characters: HashMap::new(),
            panel: Some(panel),
            panel_transition: None,
            message_char_transitions: Vec::new(),
            transition: Some(TransitionState {
                transition_type: 0,
                old_rgba: black.clone(),
                new_rgba: black,
                mask: None,
                tile_values: None,
                duration: 10,
                elapsed_ms: 0,
                progress: 0,
            }),
            game_menu: None,
            save_menu: None,
            rand_state: 1,
        };
        scene.refresh_character_frame();
        let target = &scene.transition.as_ref().unwrap().new_rgba;
        let bottom_left = ((HEIGHT - 1) * WIDTH * 4) as usize;
        assert_eq!(&target[bottom_left..bottom_left + 4], &[255; 4]);
    }

    #[test]
    fn wscroll_uses_original_acceleration_and_five_pixel_remainder() {
        let mut effect = WScrollState {
            _period: 60,
            speed: -2,
            elapsed_ms: 0,
            ticks: 0,
            previous_velocity: 0,
            remainder: 0,
            offset: 0,
        };
        assert!(advance_wscroll(&mut effect, 30));
        assert_eq!(effect.offset, -1);
        assert_eq!(effect.remainder, -1);
        assert_eq!(effect.previous_velocity, -6);
    }

    #[test]
    fn wscroll_wraps_from_normalized_source_offset() {
        let mut target = Frame {
            width: 4,
            height: 1,
            rgba: vec![0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255],
            serial: 0,
        };
        let overlay = Frame {
            width: 2,
            height: 1,
            rgba: vec![255, 0, 0, 255, 0, 255, 0, 255],
            serial: 0,
        };
        composite_wrapped_horizontal(&mut target, &overlay, -1, 0);
        assert_eq!(
            target.rgba,
            vec![
                0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 255, 0, 0, 255
            ]
        );
    }

    #[test]
    fn compound_stage_base_preserves_original_screen_and_three_layer_slots() {
        assert_eq!(
            parse_stage_base("screen.png:body.png,*,hair.png").unwrap(),
            ("screen.png", vec!["body.png", "*", "hair.png"])
        );
        assert_eq!(
            parse_stage_base("screen.png").unwrap(),
            ("screen.png", Vec::new())
        );
        assert!(parse_stage_base("screen.png:body.png,hair.png").is_err());
    }

    #[test]
    fn stage_overlay_uses_source_alpha_and_script_position() {
        let mut target = Frame {
            width: 2,
            height: 1,
            rgba: vec![100, 100, 100, 255, 20, 30, 40, 255],
            serial: 0,
        };
        let overlay = Frame {
            width: 1,
            height: 1,
            rgba: vec![200, 100, 0, 128],
            serial: 0,
        };
        composite_source_over(&mut target, &overlay, 1, 0);
        assert_eq!(&target.rgba[..4], &[100, 100, 100, 255]);
        assert_eq!(&target.rgba[4..], &[110, 65, 20, 255]);
    }

    #[test]
    fn stage_swap_keeps_marked_current_characters_and_promotes_next_scene() {
        let old = vec![0, 0, 0, 255];
        let mut scene = SceneSystem {
            profile: &SUPIPARA_PROFILE,
            current: SceneKind::Script,
            frame: Frame {
                width: 1,
                height: 1,
                rgba: old.clone(),
                serial: 1,
            },
            background_source: Frame {
                width: 1,
                height: 1,
                rgba: old.clone(),
                serial: 1,
            },
            background_rgba: old,
            camera_x: 0,
            camera_y: 0,
            scroll: None,
            shake: None,
            effect_layers: None,
            wscroll: None,
            cutin: None,
            characters: HashMap::from([
                (1, character(1, true, true)),
                (2, character(2, true, false)),
                (3, character(3, false, true)),
            ]),
            panel: None,
            panel_transition: None,
            message_char_transitions: Vec::new(),
            transition: None,
            game_menu: None,
            save_menu: None,
            rand_state: 1,
        };
        scene.swap_characters();
        assert!(scene.characters.contains_key(&1));
        assert!(!scene.characters.get(&1).unwrap().keep);
        assert!(!scene.characters.contains_key(&2));
        assert!(scene.characters.contains_key(&3));
        assert!(scene.characters.values().all(|character| character.current));
    }

    #[test]
    fn panel_transition_uses_original_default_speed_and_10ms_ticks() {
        let mut scene = SceneSystem {
            profile: &SUPIPARA_PROFILE,
            current: SceneKind::Script,
            frame: Frame {
                width: WIDTH,
                height: HEIGHT,
                rgba: vec![0; WIDTH as usize * HEIGHT as usize * 4],
                serial: 1,
            },
            background_source: Frame {
                width: WIDTH,
                height: HEIGHT,
                rgba: vec![0; WIDTH as usize * HEIGHT as usize * 4],
                serial: 1,
            },
            background_rgba: vec![0; WIDTH as usize * HEIGHT as usize * 4],
            camera_x: 0,
            camera_y: 0,
            scroll: None,
            shake: None,
            effect_layers: None,
            wscroll: None,
            cutin: None,
            characters: HashMap::new(),
            panel: None,
            panel_transition: Some(PanelTransition {
                old: None,
                new: Some(Frame {
                    width: 1,
                    height: 1,
                    rgba: vec![255, 255, 255, 255],
                    serial: 1,
                }),
                speed: 16,
                elapsed_ms: 0,
            }),
            message_char_transitions: Vec::new(),
            transition: None,
            game_menu: None,
            save_menu: None,
            rand_state: 1,
        };
        assert!(!scene.tick_panel_transition(159));
        assert!(scene.panel_transition_active());
        assert!(scene.tick_panel_transition(1));
        assert!(!scene.panel_transition_active());
        assert!(scene.panel.is_some());
        assert_eq!(scene.frame.rgba.len(), WIDTH as usize * HEIGHT as usize * 4);
    }

    #[test]
    fn shake_translation_uses_black_edges_and_original_msvc_direction_seed() {
        let mut source = vec![0; WIDTH as usize * HEIGHT as usize * 4];
        for pixel in source.chunks_exact_mut(4) {
            pixel[3] = 255;
        }
        let source_index = ((100 * WIDTH + 100) * 4) as usize;
        source[source_index..source_index + 4].copy_from_slice(&[255, 0, 0, 255]);
        let shifted = translate_frame(&source, -10, 0);
        let target_index = ((100 * WIDTH + 90) * 4) as usize;
        assert_eq!(&shifted[target_index..target_index + 4], &[255, 0, 0, 255]);
        assert_eq!(
            &shifted[((100 * WIDTH + 1279) * 4) as usize..][..4],
            &[0, 0, 0, 255]
        );
        assert_eq!(
            rotate_frame(&source, WIDTH as f64 / 2.0, HEIGHT as f64 / 2.0, 0.0,),
            source
        );

        let first = 1u32.wrapping_mul(214013).wrapping_add(2531011);
        assert_eq!(((first >> 16) & 0x7fff) % 8, 1);
    }

    #[test]
    fn scroll_uses_original_ten_millisecond_axis_and_major_distance_formulas() {
        assert_eq!(scroll_axis(700, 150, -15, 100), 550);
        assert_eq!(scroll_axis(700, 150, -15, 367), 150);
        assert_eq!(scroll_axis(1200, 1280, 6, 100), 1260);
        assert_eq!(scroll_axis(1200, 1280, 6, 134), 1280);

        let diagonal = ScrollState {
            start_x: 500,
            start_y: 0,
            target_x: 50,
            target_y: 400,
            speed: 300,
            diagonal: true,
            duration_ms: None,
            elapsed_ms: 0,
        };
        assert_eq!(scroll_pair(&diagonal, 10), (200, 266));
        assert_eq!(scroll_pair(&diagonal, 15), (50, 400));
    }

    #[test]
    fn cutin_enters_holds_until_fadeout_and_exits_with_original_multiplier() {
        let mut cutin = CutinState {
            image: Frame {
                width: 1280,
                height: 720,
                rgba: Vec::new(),
                serial: 0,
            },
            from_left: true,
            hold: -1,
            elapsed_ms: 0,
            state: 0,
            x: -1280,
            velocity: 2.0,
            factor: 15.0,
        };
        advance_cutin(&mut cutin, 4);
        assert_eq!((cutin.state, cutin.x), (1, 0));
        advance_cutin(&mut cutin, 20);
        assert_eq!((cutin.state, cutin.x), (1, 0));
        cutin.state = 2;
        cutin.velocity = 2.0;
        advance_cutin(&mut cutin, 4);
        assert_eq!(cutin.state, 3);
    }

    #[test]
    fn non_timed_character_sequence_stops_automatic_frame_updates() {
        let mut character = character(1, true, true);
        character.sequence = Some(CharacterSequence {
            frames: 4,
            rate: 12,
            start: 0,
            elapsed_ms: 10,
            loaded_frame: 2,
        });
        configure_character_sequence(&mut character, false, 4, 12, 0);
        assert!(character.sequence.is_none());
        assert_eq!(character.image.rgba, [1, 1, 1, 255]);
    }

    #[test]
    fn lip_target_falls_back_only_to_one_visible_animated_character() {
        let overlay = MouthOverlay {
            image: Frame {
                width: 1,
                height: 1,
                rgba: vec![0; 4],
                serial: 1,
            },
            x: 0,
            y: 0,
        };
        let mut alice = character(1, true, false);
        alice.mouth = Some(MouthAnimation {
            actor: "アリス".into(),
            medium: overlay.clone(),
            open: overlay.clone(),
            open_eye: None,
            closed_eye: None,
        });
        let mut characters = HashMap::from([(1, alice)]);
        assert_eq!(select_mouth_target(&characters, Some("alias")), Some(1));

        let mut sakura = character(2, true, false);
        sakura.mouth = Some(MouthAnimation {
            actor: "桜".into(),
            medium: overlay.clone(),
            open: overlay,
            open_eye: None,
            closed_eye: None,
        });
        characters.insert(2, sakura);
        assert_eq!(select_mouth_target(&characters, Some("桜")), Some(2));
        assert_eq!(select_mouth_target(&characters, Some("alias")), None);
    }

    #[test]
    fn ani_mouth_records_decode_bgra_layers_and_original_coordinates() {
        let mut ani = vec![0, 1, 2, 0, 0, 0, 0, 0];
        for (name, x, color) in [
            ("アリス 開き口", 7_u16, [30, 20, 10, 255]),
            ("アリス 中口", 5_u16, [60, 50, 40, 255]),
        ] {
            let (encoded, _, errors) = SHIFT_JIS.encode(name);
            assert!(!errors);
            ani.extend_from_slice(&encoded);
            ani.push(0);
            for value in [1_u16, 1, 32, x, 9] {
                ani.extend_from_slice(&value.to_le_bytes());
            }
            ani.extend_from_slice(&color);
        }
        let mouth = parse_mouth_animation(&ani).unwrap().unwrap();
        assert_eq!(mouth.actor, "アリス");
        assert_eq!((mouth.medium.x, mouth.medium.y), (5, 9));
        assert_eq!(mouth.medium.image.rgba, [40, 50, 60, 255]);
        assert_eq!((mouth.open.x, mouth.open.y), (7, 9));
        assert_eq!(mouth.open.image.rgba, [10, 20, 30, 255]);
    }

    #[test]
    fn secondary_character_surface_composes_after_primary() {
        let mut character = character(1, true, true);
        character.image.rgba = vec![255, 0, 0, 255];
        character.secondary_image = Some(Frame {
            width: 1,
            height: 1,
            rgba: vec![0, 255, 0, 128],
            serial: 1,
        });
        let characters = HashMap::from([(1, character)]);
        let composed = compose_characters(&[0, 0, 0, 255], &characters);
        assert_eq!(composed, [127, 128, 0, 255]);
    }

    #[test]
    fn freeze_bakes_visible_characters_into_the_scene_and_clears_layers() {
        let mut characters = HashMap::from([(1, character(200, true, true))]);
        let frozen = freeze_character_layers(&[0, 0, 0, 255], &mut characters);
        assert_eq!(frozen, [200, 200, 200, 255]);
        assert!(characters.is_empty());
    }

    #[test]
    fn message_trans_delays_and_blends_layers_on_the_original_ten_ms_clock() {
        assert_eq!(script_units_to_ms(65), 650);
        let mut characters = HashMap::from([
            (10, character(10, true, true)),
            (
                11,
                Character {
                    blend_rate: 0,
                    ..character(11, true, true)
                },
            ),
        ]);
        let mut transitions = vec![
            MessageCharTransition {
                delay_ms: script_units_to_ms(70),
                duration_units: 65,
                elapsed_ms: 0,
                id: 10,
                target_blend: 0,
                started: false,
            },
            MessageCharTransition {
                delay_ms: script_units_to_ms(50),
                duration_units: 55,
                elapsed_ms: 0,
                id: 11,
                target_blend: 255,
                started: false,
            },
        ];
        assert!(!advance_message_char_transitions(
            &mut characters,
            &mut transitions,
            499
        ));
        assert!(advance_message_char_transitions(
            &mut characters,
            &mut transitions,
            1
        ));
        assert_eq!(characters[&11].movement.as_ref().unwrap().duration_ms, 550);
        assert!(characters[&10].movement.is_none());
        assert!(advance_message_char_transitions(
            &mut characters,
            &mut transitions,
            200
        ));
        assert_eq!(characters[&10].movement.as_ref().unwrap().duration_ms, 650);

        finish_message_char_transitions(&mut characters, &mut transitions);
        assert_eq!(characters[&10].blend_rate, 0);
        assert_eq!(characters[&11].blend_rate, 255);
        assert!(
            characters
                .values()
                .all(|character| character.movement.is_none())
        );
        assert!(transitions.is_empty());
    }

    #[test]
    fn character_move_uses_original_independent_easing_and_millisecond_duration() {
        let mut character = character(1, true, true);
        character.movement = Some(CharacterMovement {
            start_x: 0,
            start_y: 0,
            target_x: 100,
            target_y: 100,
            start_blend: 256,
            target_blend: Some(0),
            x_mode: 0,
            y_mode: 1,
            duration_ms: 100,
            elapsed_ms: 0,
        });
        assert!(advance_character_movement(&mut character, 50));
        assert_eq!(
            (character.x, character.y, character.blend_rate),
            (50, 25, 128)
        );
        assert!(character.movement.is_some());
        assert!(advance_character_movement(&mut character, 50));
        assert_eq!(
            (character.x, character.y, character.blend_rate),
            (100, 100, 0)
        );
        assert!(character.movement.is_none());
        assert!(!advance_character_movement(&mut character, 1));
        assert_eq!(movement_progress(0.5, 2), 0.75);
    }

    #[test]
    fn character_size_uses_original_floor_mapped_nearest_sampling() {
        let source = Frame {
            width: 2,
            height: 1,
            rgba: vec![255, 0, 0, 255, 0, 255, 0, 255],
            serial: 4,
        };
        let resized = resize_nearest(&source, 3, 1);
        assert_eq!(
            resized.rgba,
            vec![255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255]
        );
        assert_eq!(resized.serial, 5);
    }

    #[test]
    fn sequence_names_replace_the_original_four_digit_frame() {
        assert_eq!(
            sequence_frame_name("petal\\petal_0001.png", 27).unwrap(),
            "petal\\petal_0027.png"
        );
    }

    #[test]
    fn rectangular_transition_types_use_original_regions_and_completion_axes() {
        let old = [255, 0, 0, 255].repeat((WIDTH * HEIGHT) as usize);
        let new = [0, 255, 0, 255].repeat((WIDTH * HEIGHT) as usize);
        let pixel = |rgba: &[u8], x: u32, y: u32| {
            let index = ((y * WIDTH + x) * 4) as usize;
            <[u8; 4]>::try_from(&rgba[index..index + 4]).unwrap()
        };

        for (transition_type, progress, samples) in [
            (
                14,
                640,
                vec![
                    ((100, 100), [0, 255, 0, 255]),
                    ((100, 500), [255, 0, 0, 255]),
                ],
            ),
            (
                15,
                640,
                vec![
                    ((100, 100), [0, 255, 0, 255]),
                    ((100, 500), [0, 0, 0, 255]),
                    ((1000, 100), [0, 0, 0, 255]),
                    ((1000, 500), [255, 0, 0, 255]),
                ],
            ),
            (
                16,
                360,
                vec![
                    ((100, 100), [0, 255, 0, 255]),
                    ((100, 500), [255, 0, 0, 255]),
                ],
            ),
            (
                17,
                640,
                vec![
                    ((100, 100), [0, 255, 0, 255]),
                    ((1000, 100), [255, 0, 0, 255]),
                ],
            ),
        ] {
            let mut transition = TransitionState {
                transition_type,
                old_rgba: old.clone(),
                new_rgba: new.clone(),
                mask: None,
                tile_values: None,
                duration: 1,
                elapsed_ms: 0,
                progress,
            };
            let mut output = old.clone();
            render_transition(&mut transition, 0, &mut output);
            for ((x, y), expected) in samples {
                assert_eq!(pixel(&output, x, y), expected);
            }
        }
        assert_eq!(transition_completion(14), WIDTH);
        assert_eq!(transition_completion(15), WIDTH);
        assert_eq!(transition_completion(16), HEIGHT);
        assert_eq!(transition_completion(17), WIDTH);
    }

    #[test]
    fn random_tile_transitions_share_original_msvc_rand_sequence() {
        let mut state = 1;
        assert_eq!(msvc_rand(&mut state), 41);
        assert_eq!(msvc_rand(&mut state), 18_467);

        let mut state = 1;
        let scattered = transition_tile_values(9, &mut state);
        assert_eq!(&scattered[..2], &[-41, -1187]);

        let mut state = 1;
        let columns = transition_tile_values(10, &mut state);
        let stride = WIDTH.div_ceil(40) as usize;
        assert_eq!(columns[0], -1620);
        assert_eq!(columns[stride], -1940);
        assert_eq!(columns[1], -180);
        assert_eq!(state, 1_423_890_337);
    }

    #[test]
    fn deterministic_tile_transitions_match_original_delay_fields_and_growth() {
        let columns = WIDTH.div_ceil(40) as usize;
        let rows = HEIGHT.div_ceil(40) as usize;
        let at = |values: &[i32], x: usize, y: usize| values[y * columns + x];

        let mut rand_state = 1;
        let row_forward = transition_tile_values(1, &mut rand_state);
        assert_eq!(at(&row_forward, 0, 0), 0);
        assert_eq!(at(&row_forward, columns - 1, rows - 1), -5750);
        let row_reverse = transition_tile_values(2, &mut rand_state);
        assert_eq!(at(&row_reverse, 0, 0), -5750);
        assert_eq!(at(&row_reverse, columns - 1, rows - 1), 0);
        let column_forward = transition_tile_values(3, &mut rand_state);
        assert_eq!(at(&column_forward, 1, 0), -320);
        assert_eq!(at(&column_forward, 0, 1), -10);
        let column_reverse = transition_tile_values(4, &mut rand_state);
        assert_eq!(at(&column_reverse, 0, 0), -10090);
        assert_eq!(at(&column_reverse, columns - 1, rows - 1), 0);
        let diagonal = transition_tile_values(5, &mut rand_state);
        assert_eq!(at(&diagonal, 1, 0), -500);
        assert_eq!(at(&diagonal, 0, 1), -500);
        let diagonal_reverse = transition_tile_values(6, &mut rand_state);
        assert_eq!(at(&diagonal_reverse, columns - 1, rows - 1), 0);
        let edge_radial = transition_tile_values(7, &mut rand_state);
        assert_eq!(at(&edge_radial, 0, 0), 0);
        assert_eq!(at(&edge_radial, columns - 1, rows - 1), -320);
        assert_eq!(at(&edge_radial, columns / 2, rows / 2), -5760);
        let center_radial = transition_tile_values(8, &mut rand_state);
        assert_eq!(at(&center_radial, columns / 2, rows / 2), 0);
        assert_eq!(at(&center_radial, 0, 0), -5760);

        let old = [255, 0, 0, 255].repeat((WIDTH * HEIGHT) as usize);
        let new = [0, 255, 0, 255].repeat((WIDTH * HEIGHT) as usize);
        let mut transition = TransitionState {
            transition_type: 3,
            old_rgba: old.clone(),
            new_rgba: new,
            mask: None,
            tile_values: Some(transition_tile_values(0, &mut rand_state)),
            duration: 1,
            elapsed_ms: 0,
            progress: 576,
        };
        let mut output = old;
        render_transition(&mut transition, 0, &mut output);
        let pixel = |x: u32, y: u32| {
            let index = ((y * WIDTH + x) * 4) as usize;
            <[u8; 4]>::try_from(&output[index..index + 4]).unwrap()
        };
        assert_eq!(pixel(3, 3), [0, 255, 0, 255]);
        assert_eq!(pixel(4, 3), [255, 0, 0, 255]);
        assert_eq!(pixel(40, 0), [0, 255, 0, 255]);
    }

    #[test]
    fn zoom_transition_uses_original_table_nearest_sampling_and_floor_average() {
        assert_eq!(transition_zoom_factor(0), 1.0);
        assert!((transition_zoom_factor(10) - 1.249_993_7).abs() < 1e-7);
        assert!((transition_zoom_factor(199) - 100.0).abs() < 1e-7);

        let mut source = Vec::new();
        for value in 0..16_u8 {
            source.extend_from_slice(&[value, 0, 0, 255]);
        }
        let scaled = center_zoom_nearest(&source, 4, 4, 2.0);
        let red: Vec<_> = scaled.chunks_exact(4).map(|pixel| pixel[0]).collect();
        assert_eq!(red, [5, 5, 6, 6, 5, 5, 6, 6, 9, 9, 10, 10, 9, 9, 10, 10]);

        let old = [100, 100, 100, 255].repeat((WIDTH * HEIGHT) as usize);
        let new = [200, 200, 200, 255].repeat((WIDTH * HEIGHT) as usize);
        let mut transition = TransitionState {
            transition_type: 2,
            old_rgba: old.clone(),
            new_rgba: new,
            mask: None,
            tile_values: None,
            duration: 1,
            elapsed_ms: 0,
            progress: 399,
        };
        let mut output = old;
        render_transition(&mut transition, 398, &mut output);
        assert_eq!(&output[..4], &[150, 150, 150, 255]);
    }

    #[test]
    fn soft_mask_transition_uses_original_shifted_512_step_alpha() {
        let old = [100, 100, 100, 255].repeat((WIDTH * HEIGHT) as usize);
        let new = [200, 200, 200, 255].repeat((WIDTH * HEIGHT) as usize);
        let mut transition = TransitionState {
            transition_type: 1,
            old_rgba: old.clone(),
            new_rgba: new,
            mask: Some(vec![128; (WIDTH * HEIGHT) as usize]),
            tile_values: None,
            duration: 1,
            elapsed_ms: 0,
            progress: 256,
        };
        let mut output = old;
        render_transition(&mut transition, 0, &mut output);
        assert_eq!(&output[..4], &[150, 150, 150, 255]);

        transition.progress = 511;
        render_transition(&mut transition, 256, &mut output);
        assert_eq!(&output[..4], &[199, 199, 199, 255]);
    }

    #[test]
    fn transition_mask_decoder_inverts_original_grayscale_surface() {
        let source = vec![0x2A; (WIDTH * HEIGHT) as usize];
        let mut encoded = Vec::new();
        {
            let mut encoder = png::Encoder::new(&mut encoded, WIDTH, HEIGHT);
            encoder.set_color(png::ColorType::Grayscale);
            encoder.set_depth(png::BitDepth::Eight);
            encoder
                .write_header()
                .unwrap()
                .write_image_data(&source)
                .unwrap();
        }
        let mask = decode_transition_mask(&encoded).unwrap();
        assert_eq!(mask.len(), source.len());
        assert!(mask.iter().all(|&value| value == !0x2A));
    }

    #[test]
    fn mask_transition_copies_only_newly_crossed_inverted_thresholds() {
        let old = [255, 0, 0, 255].repeat((WIDTH * HEIGHT) as usize);
        let new = [0, 255, 0, 255].repeat((WIDTH * HEIGHT) as usize);
        let mut mask = vec![255; (WIDTH * HEIGHT) as usize];
        mask[0] = 10;
        mask[1] = 20;
        let mut transition = TransitionState {
            transition_type: 18,
            old_rgba: old.clone(),
            new_rgba: new,
            mask: Some(mask),
            tile_values: None,
            duration: 1,
            elapsed_ms: 0,
            progress: 15,
        };
        let mut output = old;
        render_transition(&mut transition, 5, &mut output);
        assert_eq!(&output[..4], &[0, 255, 0, 255]);
        assert_eq!(&output[4..8], &[255, 0, 0, 255]);

        transition.progress = 25;
        render_transition(&mut transition, 15, &mut output);
        assert_eq!(&output[4..8], &[0, 255, 0, 255]);
    }

    #[test]
    fn transition_types_outside_original_dispatch_table_swap_immediately() {
        assert!(transition_is_immediate(22));
        assert!(transition_is_immediate(-1));
        assert!(!transition_is_immediate(0));
        assert!(!transition_is_immediate(18));
    }

    #[test]
    fn type_zero_transition_uses_original_10ms_progress_formula() {
        let old = vec![100, 100, 100, 255];
        let new = vec![0, 0, 0, 255];
        let mut output = old.clone();
        blend_frames(&old, &new, 128, &mut output);
        assert_eq!(output, vec![50, 50, 50, 255]);

        let mut scene = SceneSystem {
            profile: &SUPIPARA_PROFILE,
            current: SceneKind::Script,
            frame: Frame {
                width: 1,
                height: 1,
                rgba: old.clone(),
                serial: 1,
            },
            background_source: Frame {
                width: 1,
                height: 1,
                rgba: old.clone(),
                serial: 1,
            },
            background_rgba: old.clone(),
            camera_x: 0,
            camera_y: 0,
            scroll: None,
            shake: None,
            effect_layers: None,
            wscroll: None,
            cutin: None,
            characters: HashMap::new(),
            panel: None,
            panel_transition: None,
            message_char_transitions: Vec::new(),
            transition: Some(TransitionState {
                transition_type: 0,
                old_rgba: old,
                new_rgba: new,
                mask: None,
                tile_values: None,
                duration: 10,
                elapsed_ms: 0,
                progress: 0,
            }),
            game_menu: None,
            save_menu: None,
            rand_state: 1,
        };
        assert!(!scene.tick_transition(2550));
        assert_eq!(scene.transition.as_ref().unwrap().progress, 255);
        assert!(scene.tick_transition(10));
        assert_eq!(scene.frame.rgba, vec![0, 0, 0, 255]);

        scene.frame.rgba = vec![255, 255, 255, 255];
        scene.enter_script_scene();
        assert_eq!(scene.current, SceneKind::Script);
        assert_eq!(scene.frame.width, WIDTH);
        assert_eq!(scene.frame.height, HEIGHT);
        assert!(
            scene
                .frame
                .rgba
                .chunks_exact(4)
                .all(|pixel| pixel == [0, 0, 0, 255])
        );
    }
}
