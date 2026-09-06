use std::collections::{HashMap, HashSet};

use crate::command::CommandKind;
use crate::script::{Program, Statement};

const WAIT_UNIT_MS: u32 = 10;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CharRequest {
    Load {
        id: i32,
        image: String,
        secondary: Option<String>,
        tertiary: Option<String>,
        blink: bool,
    },
    Position {
        id: i32,
        x: i32,
        y: i32,
    },
    Size {
        id: i32,
        width: i32,
        height: i32,
    },
    Order {
        id: i32,
        order: i32,
        secondary: bool,
    },
    BlendRate {
        id: i32,
        rate: i32,
    },
    Visible {
        id: i32,
        visible: bool,
    },
    Clear {
        first: i32,
        last: i32,
    },
    Move {
        id: i32,
        x: i32,
        y: i32,
        x_mode: i32,
        y_mode: i32,
        duration: i32,
        blocking: bool,
        blend_rate: i32,
        offset: bool,
    },
    AllMove {
        x: i32,
        y: i32,
        duration: i32,
        mode: i32,
    },
    Freeze,
    Movie {
        name: String,
        skippable: bool,
    },
    Sequence {
        id: i32,
        timed: bool,
        frames: i32,
        rate: i32,
        start: i32,
    },
    Keep {
        first: i32,
        last: i32,
    },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EffectRequest {
    pub kind: String,
    pub resource: String,
    pub first: i32,
    pub second: i32,
    pub third: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MessageRequest {
    pub id: i32,
    pub voice: String,
    pub speaker: String,
    pub text: String,
    pub read: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MovieRequest {
    pub id: i32,
    pub name: String,
    pub width: i32,
    pub height: i32,
    pub skippable: bool,
    pub x: i32,
    pub y: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BgmRequest {
    pub name: String,
    pub fade_in: i32,
    pub fade_out: i32,
    pub volume: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScrollRequest {
    pub target_x: Option<i32>,
    pub target_y: Option<i32>,
    pub speed: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScrollXfRequest {
    pub values: [i32; 10],
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SelectOption {
    pub text: String,
    pub label: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SeRequest {
    pub channel: u8,
    pub name: String,
    pub looped: bool,
    pub first: i32,
    pub second: i32,
    pub volume: i32,
    pub pan: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VoiceRequest {
    pub name: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PanelRequest {
    pub mode: i32,
    pub duration: i32,
    pub image: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ShakeRequest {
    pub mode: String,
    pub amplitude: i32,
    pub duration: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StageExtra {
    pub name: String,
    pub first: i32,
    pub second: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StageRequest {
    pub base_spec: String,
    pub base_x: i32,
    pub base_y: i32,
    pub image: String,
    pub x: i32,
    pub y: i32,
    pub extras: Vec<StageExtra>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TransitionConfig {
    pub transition_type: i32,
    pub mask: String,
    pub duration: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum VmEvent {
    ApplyChar(CharRequest),
    ApplyEffect(EffectRequest),
    EndMessage,
    EndScroll { interrupt: bool },
    Message(MessageRequest),
    PlayMovie(MovieRequest),
    SetPanel(PanelRequest),
    StartShake(ShakeRequest),
    StartScroll(ScrollRequest),
    StartScrollXf(ScrollXfRequest),
    PersistGlobals,
    ScriptChanged(String),
    SetStage(StageRequest),
    SetTransition(TransitionConfig),
    Select(Vec<SelectOption>),
    PlayBgm(BgmRequest),
    PlaySe(SeRequest),
    PlayVoice(VoiceRequest),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum VmState {
    Ready,
    PendingChain { path: String },
    WaitingTransition,
    WaitingTimer { remaining_ms: u32 },
    WaitingInput,
    WaitingMovie,
    WaitingPanel,
    WaitingSelect { labels: Vec<String> },
    WaitingScroll,
    WaitingCharMove,
    Finished,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct ManualVmState {
    pub script: String,
    pub pc: usize,
    pub globals: HashMap<String, i32>,
    pub locals: HashMap<String, i32>,
    pub control_enabled: bool,
    pub skip_enabled: bool,
    pub active_message_id: Option<i32>,
    pub seen_messages: HashSet<i32>,
}

#[derive(Debug, Clone, Copy)]
struct AtLayer {
    id: i32,
    x: i32,
    y: i32,
}

#[derive(Debug)]
struct ScriptFrame {
    name: String,
    program: Program,
    pc: usize,
}

impl ScriptFrame {
    fn new(name: String, program: Program) -> Self {
        Self {
            name,
            program,
            pc: 0,
        }
    }
}

#[derive(Debug)]
pub struct Vm {
    frames: Vec<ScriptFrame>,
    globals: HashMap<String, i32>,
    locals: HashMap<String, i32>,
    state: VmState,
    control_enabled: bool,
    skip_enabled: bool,
    active_message_id: Option<i32>,
    seen_messages: HashSet<i32>,
    at_layers: HashMap<String, AtLayer>,
}

impl Vm {
    pub fn new(name: impl Into<String>, program: Program) -> Self {
        Self {
            frames: vec![ScriptFrame::new(name.into(), program)],
            globals: HashMap::new(),
            locals: HashMap::new(),
            state: VmState::Ready,
            control_enabled: false,
            skip_enabled: false,
            active_message_id: None,
            seen_messages: HashSet::new(),
            at_layers: HashMap::new(),
        }
    }

    pub fn state(&self) -> &VmState {
        &self.state
    }

    pub fn current_script(&self) -> Option<&str> {
        self.frames.last().map(|frame| frame.name.as_str())
    }

    pub fn global(&self, name: &str) -> Option<i32> {
        self.globals.get(name).copied()
    }

    pub fn globals_snapshot(&self) -> HashMap<String, i32> {
        self.globals.clone()
    }

    pub(crate) fn manual_state(&self) -> Result<ManualVmState, String> {
        if self.frames.len() != 1 {
            return Err("manual save during an included script is not supported".into());
        }
        let frame = &self.frames[0];
        Ok(ManualVmState {
            script: frame.name.clone(),
            pc: frame.pc,
            globals: self.globals.clone(),
            locals: self.locals.clone(),
            control_enabled: self.control_enabled,
            skip_enabled: self.skip_enabled,
            active_message_id: self.active_message_id,
            seen_messages: self.seen_messages.clone(),
        })
    }

    pub(crate) fn from_manual_state(
        state: ManualVmState,
        program: Program,
    ) -> Result<Self, String> {
        if state.pc > program.statements.len() {
            return Err("manual save program counter is outside the script".into());
        }
        Ok(Self {
            frames: vec![ScriptFrame {
                name: state.script,
                program,
                pc: state.pc,
            }],
            globals: state.globals,
            locals: state.locals,
            state: VmState::WaitingInput,
            control_enabled: state.control_enabled,
            skip_enabled: state.skip_enabled,
            active_message_id: state.active_message_id,
            seen_messages: state.seen_messages,
            at_layers: HashMap::new(),
        })
    }

    pub fn replace_globals(&mut self, globals: HashMap<String, i32>) {
        self.globals = globals;
    }

    pub fn local(&self, name: &str) -> Option<i32> {
        self.locals.get(name).copied()
    }

    pub fn advance_input(&mut self) {
        if self.state == VmState::WaitingInput {
            self.state = VmState::Ready;
        }
    }

    pub fn complete_movie(&mut self) {
        if self.state == VmState::WaitingMovie {
            self.state = VmState::Ready;
        }
    }

    pub fn complete_select(&mut self, index: usize) -> Result<(), String> {
        let VmState::WaitingSelect { labels } = &self.state else {
            return Err("select response received while VM is not waiting".into());
        };
        let label = labels
            .get(index)
            .cloned()
            .ok_or_else(|| format!("select index {index} is outside {} options", labels.len()))?;
        self.jump(&label)?;
        self.state = VmState::Ready;
        Ok(())
    }

    pub fn complete_panel(&mut self) {
        if self.state == VmState::WaitingPanel {
            self.state = VmState::Ready;
        }
    }

    pub fn begin_char_move(&mut self) {
        self.state = VmState::WaitingCharMove;
    }

    pub fn complete_char_move(&mut self) {
        if self.state == VmState::WaitingCharMove {
            self.state = VmState::Ready;
        }
    }

    pub fn begin_scroll(&mut self) {
        self.state = VmState::WaitingScroll;
    }

    pub fn complete_scroll(&mut self) {
        if self.state == VmState::WaitingScroll {
            self.state = VmState::Ready;
        }
    }

    pub fn begin_transition(&mut self) {
        self.state = VmState::WaitingTransition;
    }

    pub fn complete_transition(&mut self) {
        if self.state == VmState::WaitingTransition {
            self.state = VmState::Ready;
        }
    }

    pub fn tick<F>(&mut self, delta_ms: u32, mut load_script: F) -> Result<Vec<VmEvent>, String>
    where
        F: FnMut(&str) -> Result<Program, String>,
    {
        let mut events = Vec::new();
        match self.state.clone() {
            VmState::PendingChain { path } => {
                let program =
                    load_script(&path).map_err(|error| format!("chain {path}: {error}"))?;
                self.frames.clear();
                self.locals.clear();
                self.frames.push(ScriptFrame::new(path.clone(), program));
                self.state = VmState::Ready;
                events.push(VmEvent::ScriptChanged(path));
            }
            VmState::WaitingTimer { remaining_ms } if delta_ms < remaining_ms => {
                self.state = VmState::WaitingTimer {
                    remaining_ms: remaining_ms - delta_ms,
                };
                return Ok(Vec::new());
            }
            VmState::WaitingTimer { .. } => self.state = VmState::Ready,
            VmState::WaitingInput
            | VmState::WaitingMovie
            | VmState::WaitingPanel
            | VmState::WaitingSelect { .. }
            | VmState::WaitingScroll
            | VmState::WaitingCharMove
            | VmState::WaitingTransition
            | VmState::Finished => {
                return Ok(Vec::new());
            }
            VmState::Ready => {}
        }

        loop {
            let Some(frame) = self.frames.last_mut() else {
                self.state = VmState::Finished;
                break;
            };
            if frame.pc >= frame.program.statements.len() {
                self.frames.pop();
                if self.frames.is_empty() {
                    self.state = VmState::Finished;
                    break;
                }
                continue;
            }

            let statement = frame.program.statements[frame.pc].clone();
            frame.pc += 1;
            match statement {
                Statement::Empty => continue,
                Statement::Comment | Statement::Label(_) => {}
                Statement::Control { name, arguments } => {
                    self.execute_at_control(&name, &arguments, &mut events)?;
                }
                Statement::Include(path) => {
                    let program =
                        load_script(&path).map_err(|error| format!("include {path}: {error}"))?;
                    self.frames.push(ScriptFrame::new(path.clone(), program));
                    events.push(VmEvent::ScriptChanged(path));
                }
                Statement::Message(message) => self.emit_message(
                    MessageRequest {
                        id: 0,
                        voice: String::new(),
                        speaker: String::new(),
                        text: message,
                        read: false,
                    },
                    &mut events,
                ),
                Statement::Command { kind, arguments } => {
                    self.execute_command(kind, arguments, &mut events)?;
                }
            }
            break;
        }
        Ok(events)
    }

    fn execute_at_control(
        &mut self,
        name: &str,
        arguments: &[String],
        events: &mut Vec<VmEvent>,
    ) -> Result<(), String> {
        match name {
            "define" => {
                if arguments.len() != 4 {
                    return Err(format!(
                        "@define requires NAME ID X Y, got {}",
                        arguments.join(" ")
                    ));
                }
                self.at_layers.insert(
                    arguments[0].clone(),
                    AtLayer {
                        id: self.resolve(&arguments[1]),
                        x: self.resolve(&arguments[2]),
                        y: self.resolve(&arguments[3]),
                    },
                );
            }
            "load" => {
                if !(2..=5).contains(&arguments.len()) {
                    return Err(format!(
                        "@load requires NAME IMAGE [X Y BLEND], got {}",
                        arguments.join(" ")
                    ));
                }
                let mut layer = self
                    .at_layers
                    .get(&arguments[0])
                    .copied()
                    .ok_or_else(|| format!("@load uses undefined layer {}", arguments[0]))?;
                if let Some(x) = arguments.get(2).filter(|value| value.as_str() != "*") {
                    layer.x = self.resolve(x);
                }
                if let Some(y) = arguments.get(3).filter(|value| value.as_str() != "*") {
                    layer.y = self.resolve(y);
                }
                self.at_layers.insert(arguments[0].clone(), layer);
                let blink = !arguments[1].starts_with("[noblink:");
                let image = strip_resource_prefix(&arguments[1]);
                events.push(VmEvent::ApplyChar(CharRequest::Load {
                    id: layer.id,
                    image,
                    secondary: None,
                    tertiary: None,
                    blink,
                }));
                events.push(VmEvent::ApplyChar(CharRequest::Position {
                    id: layer.id,
                    x: layer.x,
                    y: layer.y,
                }));
                if let Some(blend) = arguments.get(4) {
                    events.push(VmEvent::ApplyChar(CharRequest::BlendRate {
                        id: layer.id,
                        rate: self.resolve(blend).clamp(0, 256),
                    }));
                }
            }
            "stage" => events.push(VmEvent::SetStage(parse_stage(arguments)?)),
            "moveofs" => {
                if arguments.len() != 7 {
                    return Err(format!(
                        "@moveofs requires NAME X Y XMODE YMODE DURATION BLEND, got {}",
                        arguments.join(" ")
                    ));
                }
                let layer = self
                    .at_layers
                    .get(&arguments[0])
                    .copied()
                    .ok_or_else(|| format!("@moveofs uses undefined layer {}", arguments[0]))?;
                events.push(VmEvent::ApplyChar(CharRequest::Move {
                    id: layer.id,
                    x: self.resolve(&arguments[1]),
                    y: self.resolve(&arguments[2]),
                    x_mode: move_mode(&arguments[3]),
                    y_mode: move_mode(&arguments[4]),
                    duration: self.resolve(&arguments[5]),
                    blocking: false,
                    blend_rate: self.resolve(&arguments[6]).clamp(0, 256),
                    offset: true,
                }));
            }
            "update" | "finish_all" => {}
            _ => {}
        }
        Ok(())
    }

    fn execute_command(
        &mut self,
        command: CommandKind,
        arguments: Vec<String>,
        events: &mut Vec<VmEvent>,
    ) -> Result<(), String> {
        match command {
            CommandKind::SetGlobal => {
                let (variable, value) = self.parse_assignment(&arguments)?;
                self.globals.insert(variable, value);
            }
            CommandKind::Set => {
                let (variable, value) = self.parse_assignment(&arguments)?;
                self.locals.insert(variable, value);
            }
            CommandKind::Goto => {
                let label = one_argument(command.name(), &arguments)?;
                self.jump(label)?;
            }
            CommandKind::If => self.execute_if(&arguments)?,
            CommandKind::Wait => {
                let value = self.resolve(arguments.first().ok_or("wait requires a duration")?);
                let units = u32::try_from(value.max(0)).unwrap_or(u32::MAX);
                self.state = VmState::WaitingTimer {
                    remaining_ms: units.saturating_mul(WAIT_UNIT_MS),
                };
            }
            CommandKind::Chain => {
                let path = one_argument(command.name(), &arguments)?.to_owned();
                // Both Chain::execute at 0x446AD0 and End::execute at 0x448560
                // call the shared message/read-state finalizer at 0x456FA0.
                self.end_message(events);
                events.push(VmEvent::PersistGlobals);
                self.state = VmState::PendingChain { path };
            }
            CommandKind::End => self.end_message(events),
            CommandKind::EndScroll => {
                let value = arguments.first().map_or("", String::as_str);
                let first = value.as_bytes().first().copied().unwrap_or_default();
                events.push(VmEvent::EndScroll {
                    interrupt: matches!(first, b'1'..=b'9' | b't' | b'T'),
                });
            }
            CommandKind::HScroll | CommandKind::VScroll => {
                if arguments.len() < 2 {
                    return Err(format!(
                        "{} requires TARGET SPEED, got {}",
                        command.name(),
                        arguments.join(" ")
                    ));
                }
                let target = self.resolve(&arguments[0]);
                events.push(VmEvent::StartScroll(ScrollRequest {
                    target_x: (command == CommandKind::HScroll).then_some(target),
                    target_y: (command == CommandKind::VScroll).then_some(target),
                    speed: parse_optional_i32(&arguments, 1, 10),
                }));
            }
            CommandKind::Scroll => {
                if arguments.len() < 3 {
                    return Err(format!(
                        "scroll requires X Y SPEED, got {}",
                        arguments.join(" ")
                    ));
                }
                events.push(VmEvent::StartScroll(ScrollRequest {
                    target_x: Some(self.resolve(&arguments[0])),
                    target_y: Some(self.resolve(&arguments[1])),
                    speed: parse_optional_i32(&arguments, 2, 10),
                }));
            }
            CommandKind::ScrollXf => {
                if arguments.len() != 10 {
                    return Err(format!(
                        "scrollXF requires 10 integer fields, got {}",
                        arguments.len()
                    ));
                }
                let mut values = [0; 10];
                for (slot, argument) in values.iter_mut().zip(&arguments) {
                    *slot = self.resolve(argument);
                }
                events.push(VmEvent::StartScrollXf(ScrollXfRequest { values }));
            }
            CommandKind::Select => {
                if arguments.is_empty() {
                    return Err("select requires at least one TEXT:LABEL option".into());
                }
                let options: Vec<_> = arguments
                    .iter()
                    .take(4)
                    .map(|argument| {
                        let (text, label) = argument
                            .split_once(':')
                            .ok_or_else(|| format!("select option has no label: {argument}"))?;
                        if text.is_empty() || label.is_empty() {
                            return Err(format!("select option is empty: {argument}"));
                        }
                        Ok(SelectOption {
                            text: text.to_owned(),
                            label: label.to_owned(),
                        })
                    })
                    .collect::<Result<_, String>>()?;
                self.state = VmState::WaitingSelect {
                    labels: options.iter().map(|option| option.label.clone()).collect(),
                };
                events.push(VmEvent::Select(options));
            }
            CommandKind::ShakeScreen => {
                if arguments.len() != 3 {
                    return Err(format!(
                        "shakeScreen requires MODE AMPLITUDE DURATION, got {}",
                        arguments.join(" ")
                    ));
                }
                events.push(VmEvent::StartShake(ShakeRequest {
                    mode: arguments[0].clone(),
                    amplitude: self.resolve(&arguments[1]),
                    duration: self.resolve(&arguments[2]),
                }));
            }
            CommandKind::PlayBgm => {
                if arguments.is_empty() {
                    return Err("playBGM requires a resource name".into());
                }
                events.push(VmEvent::PlayBgm(BgmRequest {
                    name: arguments[0].clone(),
                    fade_in: parse_optional_i32(&arguments, 1, 2),
                    fade_out: parse_optional_i32(&arguments, 2, 2),
                    volume: parse_optional_i32(&arguments, 3, 100),
                }));
            }
            CommandKind::PlaySe | CommandKind::PlaySe2 | CommandKind::PlaySe3 => {
                if arguments.is_empty() {
                    return Err("playSE requires a resource name".into());
                }
                let (name, volume, pan) = parse_audio_spec(&arguments[0]);
                events.push(VmEvent::PlaySe(SeRequest {
                    channel: match command {
                        CommandKind::PlaySe => 1,
                        CommandKind::PlaySe2 => 2,
                        CommandKind::PlaySe3 => 3,
                        _ => unreachable!(),
                    },
                    name,
                    looped: arguments
                        .get(1)
                        .is_some_and(|value| value.as_bytes().first() == Some(&b't')),
                    first: parse_optional_i32(&arguments, 2, 2),
                    second: parse_optional_i32(&arguments, 3, 2),
                    volume,
                    pan,
                }));
            }
            CommandKind::PlayVoice => {
                let name = one_argument(command.name(), &arguments)?.to_owned();
                events.push(VmEvent::PlayVoice(VoiceRequest { name }));
            }
            CommandKind::Pragma => {
                let mode = one_argument(command.name(), &arguments)?;
                match mode {
                    "monologue" => self.skip_to_monologue_end()?,
                    "skip_disable" => self.skip_enabled = false,
                    "skip_enable" => self.skip_enabled = true,
                    "disable_control" => self.control_enabled = false,
                    "enable_control" => self.control_enabled = true,
                    "clear_global_variables" => self.globals.clear(),
                    "clear_local_variables" => self.locals.clear(),
                    _ => return Err(format!("unsupported pragma mode {mode}")),
                }
            }
            CommandKind::Message => {
                if arguments.len() != 4 {
                    return Err(format!(
                        "message requires ID VOICE SPEAKER TEXT, got {} fields",
                        arguments.len()
                    ));
                }
                let id = self.resolve(&arguments[0]);
                let read = self.seen_messages.contains(&id);
                let (voice, _volume, _pan) = parse_audio_spec(&arguments[1]);
                self.emit_message(
                    MessageRequest {
                        id,
                        voice,
                        speaker: arguments[2].clone(),
                        text: arguments[3].clone(),
                        read,
                    },
                    events,
                );
            }
            CommandKind::Char => {
                let request = self.parse_char(&arguments)?;
                if let CharRequest::Movie { name, skippable } = request {
                    events.push(VmEvent::PlayMovie(MovieRequest {
                        id: 0,
                        name,
                        width: 1280,
                        height: 720,
                        skippable,
                        x: 0,
                        y: 0,
                    }));
                } else {
                    events.push(VmEvent::ApplyChar(request));
                }
            }
            CommandKind::Effect => {
                events.push(VmEvent::ApplyEffect(EffectRequest {
                    kind: arguments.first().cloned().unwrap_or_default(),
                    resource: arguments.get(1).cloned().unwrap_or_default(),
                    first: parse_optional_i32(&arguments, 2, -1),
                    second: parse_optional_i32(&arguments, 3, -1),
                    third: parse_optional_i32(&arguments, 4, -1),
                }));
            }
            CommandKind::Movie => {
                if arguments.len() < 5 {
                    return Err(format!(
                        "movie requires ID NAME WIDTH HEIGHT FLAG, got {}",
                        arguments.join(" ")
                    ));
                }
                let flag = arguments[4].as_bytes().first().copied().unwrap_or(b't');
                events.push(VmEvent::PlayMovie(MovieRequest {
                    id: self.resolve(&arguments[0]),
                    name: arguments[1].clone(),
                    width: arguments[2].parse().unwrap_or(1280),
                    height: arguments[3].parse().unwrap_or(720),
                    skippable: !matches!(flag, b'0' | b'f' | b'F'),
                    x: parse_optional_i32(&arguments, 5, 0),
                    y: parse_optional_i32(&arguments, 6, 0),
                }));
                self.state = VmState::WaitingMovie;
            }
            CommandKind::Panel => {
                if arguments.is_empty() {
                    return Err("panel requires at least a mode".into());
                }
                events.push(VmEvent::SetPanel(PanelRequest {
                    mode: self.resolve(&arguments[0]),
                    duration: arguments
                        .get(1)
                        .and_then(|value| value.parse().ok())
                        .unwrap_or(-1),
                    image: arguments.get(2).cloned().unwrap_or_default(),
                }));
                self.state = VmState::WaitingPanel;
            }
            CommandKind::Stage => {
                events.push(VmEvent::SetStage(parse_stage(&arguments)?));
            }
            CommandKind::Transition => {
                if arguments.len() < 3 {
                    return Err(format!(
                        "transition requires at least 3 arguments, got {}",
                        arguments.join(" ")
                    ));
                }
                events.push(VmEvent::SetTransition(TransitionConfig {
                    transition_type: arguments[0].parse().unwrap_or(0),
                    mask: arguments[1].clone(),
                    duration: arguments[2].parse().unwrap_or(0),
                }));
            }
            command => {
                return Err(format!(
                    "registered command .{} is not implemented (original parse={:#x}, execute={:#x})",
                    command.name(),
                    command.original_handler().parse,
                    command.original_handler().execute
                ));
            }
        }
        Ok(())
    }

    fn parse_char(&self, arguments: &[String]) -> Result<CharRequest, String> {
        let mode = arguments
            .first()
            .ok_or("char requires a subcommand")?
            .as_str();
        let value = |index: usize| {
            arguments
                .get(index)
                .map_or(0, |argument| self.resolve(argument))
        };
        let require = |count: usize| {
            if arguments.len() < count {
                Err(format!(
                    "char {mode} requires {} arguments, got {}",
                    count - 1,
                    arguments.join(" ")
                ))
            } else {
                Ok(())
            }
        };
        match mode {
            "load" => {
                require(3)?;
                Ok(CharRequest::Load {
                    id: value(1),
                    image: strip_resource_prefix(&arguments[2]),
                    secondary: arguments.get(3).map(|value| strip_resource_prefix(value)),
                    tertiary: arguments.get(4).map(|value| strip_resource_prefix(value)),
                    blink: !arguments[2].starts_with("[noblink:"),
                })
            }
            "pos" => {
                require(4)?;
                Ok(CharRequest::Position {
                    id: value(1),
                    x: value(2),
                    y: value(3),
                })
            }
            "size" => {
                require(4)?;
                Ok(CharRequest::Size {
                    id: value(1),
                    width: value(2),
                    height: value(3),
                })
            }
            "order" | "order2" => {
                require(3)?;
                Ok(CharRequest::Order {
                    id: value(1),
                    order: value(2),
                    secondary: mode == "order2",
                })
            }
            "blendrate" => {
                require(3)?;
                Ok(CharRequest::BlendRate {
                    id: value(1),
                    rate: value(2).clamp(0, 256),
                })
            }
            "visible" => {
                require(3)?;
                Ok(CharRequest::Visible {
                    id: value(1),
                    visible: arguments[2] != "off",
                })
            }
            "clear" | "keep" => {
                let first = arguments.get(1).map_or(-1, |value| self.resolve(value));
                let last = arguments.get(2).map_or(first, |value| self.resolve(value));
                if mode == "clear" {
                    Ok(CharRequest::Clear { first, last })
                } else {
                    Ok(CharRequest::Keep { first, last })
                }
            }
            "move" | "moveofs" => {
                require(7)?;
                Ok(CharRequest::Move {
                    id: value(1),
                    x: value(2),
                    y: value(3),
                    x_mode: move_mode(&arguments[4]),
                    y_mode: move_mode(&arguments[5]),
                    duration: value(6),
                    blocking: arguments.get(7).is_some_and(|value| value == "on"),
                    blend_rate: arguments
                        .get(8)
                        .map_or(-1, |value| self.resolve(value))
                        .clamp(-1, 256),
                    offset: mode == "moveofs",
                })
            }
            "allmove" => {
                require(5)?;
                Ok(CharRequest::AllMove {
                    x: value(1),
                    y: value(2),
                    duration: value(3),
                    mode: move_mode(&arguments[4]),
                })
            }
            "freeze" => Ok(CharRequest::Freeze),
            "seq" => {
                require(3)?;
                let timed = arguments[2].starts_with(['t', 'T']);
                Ok(CharRequest::Sequence {
                    id: value(1),
                    timed,
                    frames: arguments.get(3).map_or(1, |value| self.resolve(value)),
                    rate: arguments.get(4).map_or(1, |value| self.resolve(value)),
                    start: arguments.get(5).map_or(0, |value| self.resolve(value)),
                })
            }
            "movie" => {
                require(3)?;
                Ok(CharRequest::Movie {
                    name: arguments[2].clone(),
                    skippable: arguments
                        .get(3)
                        .is_none_or(|value| !matches!(value.as_str(), "f" | "F")),
                })
            }
            _ => Err(format!("unsupported char subcommand {mode}")),
        }
    }

    fn parse_assignment(&self, arguments: &[String]) -> Result<(String, i32), String> {
        if !matches!(arguments.len(), 3 | 5) || arguments[1] != "=" {
            return Err(format!(
                "assignment requires NAME = VALUE or NAME = LEFT OP RIGHT, got {}",
                arguments.join(" ")
            ));
        }
        let value = if arguments.len() == 3 {
            self.resolve(&arguments[2])
        } else {
            let left = self.resolve(&arguments[2]);
            let right = self.resolve(&arguments[4]);
            match arguments[3].as_str() {
                "|" => left | right,
                "&" => left & right,
                "+" => left.wrapping_add(right),
                "-" => left.wrapping_sub(right),
                "*" => left.wrapping_mul(right),
                "/" if right != 0 => left.wrapping_div(right),
                "%" if right != 0 => left.wrapping_rem(right),
                "/" | "%" => return Err("assignment division by zero".into()),
                operator => return Err(format!("unsupported assignment operator {operator}")),
            }
        };
        Ok((arguments[0].clone(), value))
    }

    fn execute_if(&mut self, arguments: &[String]) -> Result<(), String> {
        if arguments.len() != 4 {
            return Err(format!(
                "if requires LEFT OP RIGHT LABEL, got {}",
                arguments.join(" ")
            ));
        }
        let left = self.resolve(&arguments[0]);
        let right = self.resolve(&arguments[2]);
        let condition = match arguments[1].as_str() {
            "==" => left == right,
            "!=" => left != right,
            ">" => left > right,
            "<" => left < right,
            ">=" => left >= right,
            "<=" => left <= right,
            operator => return Err(format!("unsupported if operator {operator}")),
        };
        // CommandIf::execute at 0x449010 falls through when true and jumps on false.
        if !condition {
            self.jump(&arguments[3])?;
        }
        Ok(())
    }

    fn jump(&mut self, label: &str) -> Result<(), String> {
        let frame = self.frames.last_mut().ok_or("goto without a script")?;
        let target = frame
            .program
            .labels
            .get(label)
            .copied()
            .ok_or_else(|| format!("{}: unknown label {label}", frame.name))?;
        frame.pc = target;
        Ok(())
    }

    fn resolve(&self, value: &str) -> i32 {
        value
            .parse::<i32>()
            .ok()
            .or_else(|| self.locals.get(value).copied())
            .or_else(|| self.globals.get(value).copied())
            .unwrap_or(0)
    }

    fn skip_to_monologue_end(&mut self) -> Result<(), String> {
        let frame = self.frames.last_mut().ok_or("monologue without a script")?;
        let target = frame.program.statements[frame.pc..]
            .iter()
            .position(|statement| match statement {
                Statement::Message(text) => text.starts_with("monologue_end"),
                Statement::Command {
                    kind: CommandKind::Message,
                    arguments,
                } => arguments
                    .get(3)
                    .is_some_and(|text| text.starts_with("monologue_end")),
                _ => false,
            })
            .ok_or_else(|| format!("{}: monologue_end not found", frame.name))?;
        frame.pc += target;
        Ok(())
    }

    fn end_message(&mut self, events: &mut Vec<VmEvent>) {
        if let Some(id) = self.active_message_id.take() {
            self.seen_messages.insert(id);
        }
        events.push(VmEvent::EndMessage);
    }

    fn emit_message(&mut self, message: MessageRequest, events: &mut Vec<VmEvent>) {
        self.active_message_id = Some(message.id);
        events.push(VmEvent::Message(message));
        self.state = VmState::WaitingInput;
    }
}

fn parse_audio_spec(value: &str) -> (String, i32, i32) {
    let Some(open) = value.find('[') else {
        return (value.to_string(), 100, 0);
    };
    let name = value[..open].to_string();
    let Some(close) = value[open + 1..].find(']') else {
        return (name, 100, 0);
    };
    let mut values = value[open + 1..open + 1 + close].split(',');
    let volume = values
        .next()
        .and_then(|part| part.parse().ok())
        .unwrap_or(100)
        .clamp(0, 100);
    let pan = values
        .next()
        .and_then(|part| part.parse().ok())
        .unwrap_or(0)
        .clamp(-100, 100);
    (name, volume, pan)
}

fn parse_optional_i32(arguments: &[String], index: usize, fallback: i32) -> i32 {
    arguments
        .get(index)
        .and_then(|value| value.parse().ok())
        .unwrap_or(fallback)
}

fn move_mode(value: &str) -> i32 {
    match value {
        "add" => 1,
        "sub" => 2,
        _ => 0,
    }
}

fn strip_resource_prefix(value: &str) -> String {
    value
        .strip_prefix('[')
        .and_then(|rest| rest.find(']').map(|end| &rest[end + 1..]))
        .unwrap_or(value)
        .to_string()
}

fn parse_stage(arguments: &[String]) -> Result<StageRequest, String> {
    if arguments.len() < 4 {
        return Err(format!(
            "stage requires at least 4 arguments, got {}",
            arguments.join(" ")
        ));
    }

    let mut cursor = 1usize;
    let mut extra_count = ((arguments.len() - 4) / 2).min(10);
    let (mut base_x, mut base_y) = (0, 0);
    if !arguments[1].contains('.')
        && !arguments[2].contains('.')
        && let (Ok(x), Ok(y)) = (arguments[1].parse(), arguments[2].parse())
    {
        base_x = x;
        base_y = y;
        cursor = 3;
        extra_count = extra_count.saturating_sub(1);
    }
    if cursor + 2 >= arguments.len() {
        return Err(format!(
            "stage image tuple is incomplete: {}",
            arguments.join(" ")
        ));
    }

    let image = strip_resource_prefix(&arguments[cursor]);
    let x = arguments[cursor + 1].parse().unwrap_or(0);
    let y = arguments[cursor + 2].parse().unwrap_or(0);
    cursor += 3;
    let mut extras = Vec::with_capacity(extra_count);
    for _ in 0..extra_count {
        if cursor + 1 >= arguments.len() {
            break;
        }
        let values = &arguments[cursor + 1];
        let (first, second) = values
            .split_once(',')
            .map_or((values.as_str(), values.as_str()), |pair| pair);
        extras.push(StageExtra {
            name: strip_resource_prefix(&arguments[cursor]),
            first: first.parse().unwrap_or(0),
            second: second.parse().unwrap_or(0),
        });
        cursor += 2;
    }

    Ok(StageRequest {
        base_spec: arguments[0].clone(),
        base_x,
        base_y,
        image,
        x,
        y,
        extras,
    })
}

pub(crate) fn infer_bgm_before(program: &Program, pc: usize) -> Option<BgmRequest> {
    let mut current = None;
    for statement in program.statements.iter().take(pc) {
        if let Statement::Command {
            kind: CommandKind::PlayBgm,
            arguments,
        } = statement
            && let Some(name) = arguments.first()
        {
            current = (name != "*").then(|| BgmRequest {
                name: name.clone(),
                fade_in: parse_optional_i32(arguments, 1, 2),
                fade_out: parse_optional_i32(arguments, 2, 2),
                volume: parse_optional_i32(arguments, 3, 100),
            });
        }
    }
    current
}

fn one_argument<'a>(command: &str, arguments: &'a [String]) -> Result<&'a str, String> {
    if arguments.len() != 1 {
        return Err(format!(
            "{command} requires one argument, got {}",
            arguments.join(" ")
        ));
    }
    Ok(&arguments[0])
}

#[cfg(test)]
mod tests {
    use super::*;

    fn program(source: &str) -> Program {
        Program::parse(source).unwrap()
    }

    fn no_scripts(_: &str) -> Result<Program, String> {
        Err("unexpected script load".into())
    }

    #[test]
    fn char_parser_matches_all_original_modes_and_defaults() {
        let vm = Vm::new("char.sc", program(".end"));
        assert_eq!(
            vm.parse_char(&words("load -10 image.png mask.png extra.png"))
                .unwrap(),
            CharRequest::Load {
                id: -10,
                image: "image.png".into(),
                secondary: Some("mask.png".into()),
                tertiary: Some("extra.png".into()),
                blink: true,
            }
        );
        assert_eq!(
            vm.parse_char(&words("pos -10 640 -50")).unwrap(),
            CharRequest::Position {
                id: -10,
                x: 640,
                y: -50,
            }
        );
        assert_eq!(
            vm.parse_char(&words("size 10 320 720")).unwrap(),
            CharRequest::Size {
                id: 10,
                width: 320,
                height: 720,
            }
        );
        assert_eq!(
            vm.parse_char(&words("order 10 4")).unwrap(),
            CharRequest::Order {
                id: 10,
                order: 4,
                secondary: false,
            }
        );
        assert_eq!(
            vm.parse_char(&words("order2 10 900000")).unwrap(),
            CharRequest::Order {
                id: 10,
                order: 900000,
                secondary: true,
            }
        );
        assert_eq!(
            vm.parse_char(&words("blendrate 10 999")).unwrap(),
            CharRequest::BlendRate { id: 10, rate: 256 }
        );
        assert_eq!(
            vm.parse_char(&words("visible 10 off")).unwrap(),
            CharRequest::Visible {
                id: 10,
                visible: false,
            }
        );
        assert_eq!(
            vm.parse_char(&words("clear")).unwrap(),
            CharRequest::Clear {
                first: -1,
                last: -1
            }
        );
        assert_eq!(
            vm.parse_char(&words("moveofs 10 2 3 add sub 40 on 300"))
                .unwrap(),
            CharRequest::Move {
                id: 10,
                x: 2,
                y: 3,
                x_mode: 1,
                y_mode: 2,
                duration: 40,
                blocking: true,
                blend_rate: 256,
                offset: true,
            }
        );
        assert_eq!(
            vm.parse_char(&words("allmove 2 3 40 add")).unwrap(),
            CharRequest::AllMove {
                x: 2,
                y: 3,
                duration: 40,
                mode: 1,
            }
        );
        assert_eq!(
            vm.parse_char(&words("freeze")).unwrap(),
            CharRequest::Freeze
        );
        assert_eq!(
            vm.parse_char(&words("seq -10 t 360 12 0")).unwrap(),
            CharRequest::Sequence {
                id: -10,
                timed: true,
                frames: 360,
                rate: 12,
                start: 0,
            }
        );
        assert_eq!(
            vm.parse_char(&words("movie @@ev ev00_0050.avi")).unwrap(),
            CharRequest::Movie {
                name: "ev00_0050.avi".into(),
                skippable: true,
            }
        );
    }

    fn words(value: &str) -> Vec<String> {
        value.split_whitespace().map(str::to_owned).collect()
    }

    #[test]
    fn shake_screen_parser_preserves_all_corpus_modes() {
        for (source, mode, amplitude, duration) in [
            (".shakeScreen H 50 10", "H", 50, 10),
            (".shakeScreen V 60 20", "V", 60, 20),
            (".shakeScreen Z 80 20", "Z", 80, 20),
            (".shakeScreen R 30 50", "R", 30, 50),
        ] {
            let mut vm = Vm::new("shake.sc", program(source));
            assert_eq!(
                vm.tick(0, no_scripts).unwrap(),
                vec![VmEvent::StartShake(ShakeRequest {
                    mode: mode.into(),
                    amplitude,
                    duration,
                })]
            );
        }
    }

    #[test]
    fn scroll_family_parser_preserves_axes_speed_and_end_mode() {
        let mut vm = Vm::new(
            "scroll.sc",
            program(
                ".vscroll 150 -15\n.hscroll 1280 6\n.scroll 50 400 300\n.endScroll f\n.endScroll T",
            ),
        );
        let expected = [
            VmEvent::StartScroll(ScrollRequest {
                target_x: None,
                target_y: Some(150),
                speed: -15,
            }),
            VmEvent::StartScroll(ScrollRequest {
                target_x: Some(1280),
                target_y: None,
                speed: 6,
            }),
            VmEvent::StartScroll(ScrollRequest {
                target_x: Some(50),
                target_y: Some(400),
                speed: 300,
            }),
            VmEvent::EndScroll { interrupt: false },
            VmEvent::EndScroll { interrupt: true },
        ];
        for event in expected {
            assert_eq!(vm.tick(0, no_scripts).unwrap(), vec![event]);
        }
        vm.begin_scroll();
        assert_eq!(vm.state(), &VmState::WaitingScroll);
        vm.complete_scroll();
        assert_eq!(vm.state(), &VmState::Ready);
        vm.begin_char_move();
        assert_eq!(vm.state(), &VmState::WaitingCharMove);
        vm.complete_char_move();
        assert_eq!(vm.state(), &VmState::Ready);
    }

    #[test]
    fn scroll_xf_parser_uses_the_original_ten_field_shape() {
        let mut vm = Vm::new(
            "scrollxf.sc",
            program(".scrollXF 0 720 0 0 1280 720 1280 0 1250 0"),
        );
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![VmEvent::StartScrollXf(ScrollXfRequest {
                values: [0, 720, 0, 0, 1280, 720, 1280, 0, 1250, 0],
            })]
        );
    }
    #[test]
    fn select_waits_and_jumps_to_the_chosen_label() {
        let mut vm = Vm::new(
            "select.sc",
            program(
                ".select Stay:STAY Walk:WALK\n.set RESULT = 0\n.label STAY\n.set RESULT = 1\n.goto DONE\n.label WALK\n.set RESULT = 2\n.label DONE\n.end",
            ),
        );
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![VmEvent::Select(vec![
                SelectOption {
                    text: "Stay".into(),
                    label: "STAY".into(),
                },
                SelectOption {
                    text: "Walk".into(),
                    label: "WALK".into(),
                },
            ])]
        );
        assert_eq!(
            vm.state(),
            &VmState::WaitingSelect {
                labels: vec!["STAY".into(), "WALK".into()]
            }
        );
        assert!(vm.tick(0, no_scripts).unwrap().is_empty());
        vm.complete_select(1).unwrap();
        for _ in 0..8 {
            vm.tick(0, no_scripts).unwrap();
        }
        assert_eq!(vm.local("RESULT"), Some(2));
    }

    #[test]
    fn select_truncates_to_the_original_four_option_limit() {
        let mut vm = Vm::new("select.sc", program(".select A:A B:B C:C D:D E:E"));
        let events = vm.tick(0, no_scripts).unwrap();
        let VmEvent::Select(options) = &events[0] else {
            panic!("expected select event");
        };
        assert_eq!(options.len(), 4);
    }

    #[test]
    fn select_rejects_missing_labels() {
        let mut missing = Vm::new("select.sc", program(".select Broken"));
        assert!(
            missing
                .tick(0, no_scripts)
                .unwrap_err()
                .contains("no label")
        );
    }

    #[test]
    fn set_commands_support_all_original_binary_operators() {
        let mut vm = Vm::new(
            "set.sc",
            program(
                ".set A = 6\n.set OR = A | 1\n.set AND = A & 3\n.set ADD = A + 4\n.set SUB = A - 8\n.set MUL = A * 3\n.set DIV = A / 2\n.set MOD = A % 4\n.setGlobal G = ADD + MUL",
            ),
        );
        for _ in 0..9 {
            vm.tick(0, no_scripts).unwrap();
        }
        assert_eq!(vm.local("OR"), Some(7));
        assert_eq!(vm.local("AND"), Some(2));
        assert_eq!(vm.local("ADD"), Some(10));
        assert_eq!(vm.local("SUB"), Some(-2));
        assert_eq!(vm.local("MUL"), Some(18));
        assert_eq!(vm.local("DIV"), Some(3));
        assert_eq!(vm.local("MOD"), Some(2));
        assert_eq!(vm.global("G"), Some(28));

        let mut zero = Vm::new("zero.sc", program(".set X = 1 / 0"));
        assert_eq!(
            zero.tick(0, no_scripts).unwrap_err(),
            "assignment division by zero"
        );
    }

    #[test]
    fn executes_variables_and_ida_verified_false_branch() {
        let mut vm = Vm::new(
            "branch.sc",
            program(
                ".setGlobal CLEAR = 0\n.if CLEAR == 1 YES\n.set X = 7\n.goto DONE\n.label YES\n.set X = 9\n.label DONE\n.end",
            ),
        );
        for _ in 0..16 {
            vm.tick(0, no_scripts).unwrap();
            if vm.state() == &VmState::Finished {
                break;
            }
        }
        assert_eq!(vm.global("CLEAR"), Some(0));
        assert_eq!(vm.local("X"), Some(9));
        assert_eq!(vm.state(), &VmState::Finished);
    }

    #[test]
    fn legacy_save_bgm_is_inferred_from_commands_before_the_checkpoint() {
        let program = program(
            ".playBGM first.ogg * * 80\n.message 1 voice actor text\n.playBGM second.ogg\n",
        );
        assert_eq!(
            infer_bgm_before(&program, 2),
            Some(BgmRequest {
                name: "first.ogg".into(),
                fade_in: 2,
                fade_out: 2,
                volume: 80,
            })
        );
    }

    #[test]
    fn manual_state_restores_the_next_program_counter_and_wait_boundary() {
        let source = ".set FLAG = 1\n.message 10 voice actor text\n.set FLAG = 2\n";
        let mut vm = Vm::new("scene.sc", program(source));
        vm.tick(0, |_| Err("unexpected script load".into()))
            .unwrap();
        vm.tick(0, |_| Err("unexpected script load".into()))
            .unwrap();
        assert_eq!(vm.state(), &VmState::WaitingInput);
        let state = vm.manual_state().unwrap();
        let mut restored = Vm::from_manual_state(state, program(source)).unwrap();
        restored.advance_input();
        restored
            .tick(0, |_| Err("unexpected script load".into()))
            .unwrap();
        assert_eq!(restored.local("FLAG"), Some(2));
    }

    #[test]
    fn executes_one_command_per_original_engine_update() {
        let mut vm = Vm::new("step.sc", program(".set A = 1\n.set B = 2\n.end"));
        vm.tick(0, no_scripts).unwrap();
        assert_eq!(vm.local("A"), Some(1));
        assert_eq!(vm.local("B"), None);
        assert_eq!(vm.state(), &VmState::Ready);

        vm.tick(0, no_scripts).unwrap();
        assert_eq!(vm.local("B"), Some(2));
        assert_eq!(vm.state(), &VmState::Ready);
    }

    #[test]
    fn effect_parser_preserves_every_corpus_shape_and_defaults() {
        let mut vm = Vm::new(
            "effect.sc",
            program(
                ".effect *\n.effect fadeout\n.effect WScroll2 * 60 -8\n.effect Cutin ev01_087a01.png -1 1500",
            ),
        );
        let expected = [
            EffectRequest {
                kind: "*".into(),
                resource: "".into(),
                first: -1,
                second: -1,
                third: -1,
            },
            EffectRequest {
                kind: "fadeout".into(),
                resource: "".into(),
                first: -1,
                second: -1,
                third: -1,
            },
            EffectRequest {
                kind: "WScroll2".into(),
                resource: "*".into(),
                first: 60,
                second: -8,
                third: -1,
            },
            EffectRequest {
                kind: "Cutin".into(),
                resource: "ev01_087a01.png".into(),
                first: -1,
                second: 1500,
                third: -1,
            },
        ];
        for request in expected {
            assert_eq!(
                vm.tick(0, no_scripts).unwrap(),
                vec![VmEvent::ApplyEffect(request)]
            );
        }
    }

    #[test]
    fn movie_parser_matches_both_corpus_flags_and_blocks_for_playback() {
        let mut skippable = Vm::new("movie.sc", program(".movie 9989 sppl_01_op.avi 1280 720 t"));
        assert_eq!(
            skippable.tick(0, no_scripts).unwrap(),
            vec![VmEvent::PlayMovie(MovieRequest {
                id: 9989,
                name: "sppl_01_op.avi".into(),
                width: 1280,
                height: 720,
                skippable: true,
                x: 0,
                y: 0,
            })]
        );
        assert_eq!(skippable.state(), &VmState::WaitingMovie);
        skippable.complete_movie();
        assert_eq!(skippable.state(), &VmState::Ready);

        let mut unskippable = Vm::new("movie.sc", program(".movie 9889 sppl_01_op.avi 1280 720 f"));
        let events = unskippable.tick(0, no_scripts).unwrap();
        assert!(matches!(
            &events[0],
            VmEvent::PlayMovie(MovieRequest {
                skippable: false,
                ..
            })
        ));
    }

    #[test]
    fn play_bgm_parser_matches_corpus_defaults_and_wildcards() {
        let mut full = Vm::new("bgm.sc", program(".playBGM majyo015_02.ogg * * 90"));
        assert_eq!(
            full.tick(0, no_scripts).unwrap(),
            vec![VmEvent::PlayBgm(BgmRequest {
                name: "majyo015_02.ogg".into(),
                fade_in: 2,
                fade_out: 2,
                volume: 90,
            })]
        );

        let mut stop = Vm::new("bgm.sc", program(".playBGM *"));
        assert_eq!(
            stop.tick(0, no_scripts).unwrap(),
            vec![VmEvent::PlayBgm(BgmRequest {
                name: "*".into(),
                fade_in: 2,
                fade_out: 2,
                volume: 100,
            })]
        );
    }

    #[test]
    fn play_se_parser_preserves_channels_defaults_and_inline_mix() {
        let mut vm = Vm::new(
            "se.sc",
            program(
                ".playSE AT_chime.ogg[120,-150] f 0 *\n.playSE2 fune_no_ue_a.ogg[50] t\n.playSE3 umi_a\n.playSE3 *",
            ),
        );
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![VmEvent::PlaySe(SeRequest {
                channel: 1,
                name: "AT_chime.ogg".into(),
                looped: false,
                first: 0,
                second: 2,
                volume: 100,
                pan: -100,
            })]
        );
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![VmEvent::PlaySe(SeRequest {
                channel: 2,
                name: "fune_no_ue_a.ogg".into(),
                looped: true,
                first: 2,
                second: 2,
                volume: 50,
                pan: 0,
            })]
        );
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![VmEvent::PlaySe(SeRequest {
                channel: 3,
                name: "umi_a".into(),
                looped: false,
                first: 2,
                second: 2,
                volume: 100,
                pan: 0,
            })]
        );
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![VmEvent::PlaySe(SeRequest {
                channel: 3,
                name: "*".into(),
                looped: false,
                first: 2,
                second: 2,
                volume: 100,
                pan: 0,
            })]
        );
    }

    #[test]
    fn play_voice_emits_one_resource_request_without_waiting() {
        let mut vm = Vm::new(
            "voice.sc",
            program(".playVoice maj-A00_01-0003\n.set X = 1"),
        );
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![VmEvent::PlayVoice(VoiceRequest {
                name: "maj-A00_01-0003".into(),
            })]
        );
        assert_eq!(vm.state(), &VmState::Ready);
        assert_eq!(vm.local("X"), None);
        vm.tick(0, no_scripts).unwrap();
        assert_eq!(vm.local("X"), Some(1));
    }

    #[test]
    fn panel_parser_matches_every_corpus_shape_and_blocks_for_scene_animation() {
        let mut standard = Vm::new("panel.sc", program(".panel 1"));
        assert_eq!(
            standard.tick(0, no_scripts).unwrap(),
            vec![VmEvent::SetPanel(PanelRequest {
                mode: 1,
                duration: -1,
                image: String::new(),
            })]
        );
        assert_eq!(standard.state(), &VmState::WaitingPanel);
        assert!(standard.tick(1000, no_scripts).unwrap().is_empty());

        let mut custom = Vm::new("panel.sc", program(".panel 1 * msgPanel_girl.png"));
        assert_eq!(
            custom.tick(0, no_scripts).unwrap(),
            vec![VmEvent::SetPanel(PanelRequest {
                mode: 1,
                duration: -1,
                image: "msgPanel_girl.png".into(),
            })]
        );
    }

    #[test]
    fn stage_parser_covers_every_argument_shape_used_by_the_corpus() {
        let mut standard = Vm::new("stage.sc", program(".stage * BLACK.png 0 0"));
        assert_eq!(
            standard.tick(0, no_scripts).unwrap(),
            vec![VmEvent::SetStage(StageRequest {
                base_spec: "*".into(),
                base_x: 0,
                base_y: 0,
                image: "BLACK.png".into(),
                x: 0,
                y: 0,
                extras: vec![],
            })]
        );

        let request = parse_stage(&[
            "base:parts".into(),
            "10".into(),
            "20".into(),
            "foreground.png".into(),
            "30".into(),
            "40".into(),
            "normal.png".into(),
            "50,60".into(),
        ])
        .unwrap();
        assert_eq!(request.base_x, 10);
        assert_eq!(request.base_y, 20);
        assert_eq!(request.image, "foreground.png");
        assert_eq!(
            parse_stage(&[
                "*".into(),
                "[noblink:2]ev00_0070a02.png".into(),
                "0".into(),
                "0".into()
            ])
            .unwrap()
            .image,
            "ev00_0070a02.png"
        );
        assert_eq!((request.x, request.y), (30, 40));
        assert_eq!(
            request.extras,
            vec![StageExtra {
                name: "normal.png".into(),
                first: 50,
                second: 60,
            }]
        );
    }

    #[test]
    fn at_load_preserves_layer_position_and_accepts_script_overrides() {
        let mut vm = Vm::new(
            "at-load.sc",
            program(
                "@define sara 400 1269 -1760\n@load sara [noblink:2]first.png 1266 -1754 100\n@load sara second.png * -1758\n",
            ),
        );
        assert!(vm.tick(0, no_scripts).unwrap().is_empty());
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![
                VmEvent::ApplyChar(CharRequest::Load {
                    id: 400,
                    image: "first.png".into(),
                    secondary: None,
                    tertiary: None,
                    blink: false,
                }),
                VmEvent::ApplyChar(CharRequest::Position {
                    id: 400,
                    x: 1266,
                    y: -1754,
                }),
                VmEvent::ApplyChar(CharRequest::BlendRate { id: 400, rate: 100 }),
            ]
        );
        assert_eq!(
            vm.tick(0, no_scripts).unwrap(),
            vec![
                VmEvent::ApplyChar(CharRequest::Load {
                    id: 400,
                    image: "second.png".into(),
                    secondary: None,
                    tertiary: None,
                    blink: true,
                }),
                VmEvent::ApplyChar(CharRequest::Position {
                    id: 400,
                    x: 1266,
                    y: -1758,
                }),
            ]
        );
    }

    #[test]
    fn transition_config_matches_original_three_argument_parser() {
        let mut vm = Vm::new("transition.sc", program(".transition 22 * 30"));
        let events = vm.tick(0, no_scripts).unwrap();
        assert_eq!(
            events,
            vec![VmEvent::SetTransition(TransitionConfig {
                transition_type: 22,
                mask: "*".into(),
                duration: 30,
            })]
        );
        assert_eq!(vm.state(), &VmState::Ready);
    }

    #[test]
    fn timer_is_cooperative_and_uses_ten_millisecond_units() {
        let mut vm = Vm::new("wait.sc", program(".wait 13\n.set X = 1\n.end"));
        vm.tick(0, no_scripts).unwrap();
        assert_eq!(vm.state(), &VmState::WaitingTimer { remaining_ms: 130 });
        vm.tick(129, no_scripts).unwrap();
        assert_eq!(vm.local("X"), None);
        vm.tick(1, no_scripts).unwrap();
        assert_eq!(vm.local("X"), Some(1));
        vm.tick(0, no_scripts).unwrap();
    }

    #[test]
    fn pragma_restores_all_original_modes() {
        let mut vm = Vm::new(
            "pragma.sc",
            program(
                ".setGlobal G = 1\n.set L = 2\n.pragma skip_enable\n.pragma disable_control\n.pragma clear_global_variables\n.pragma clear_local_variables",
            ),
        );
        for _ in 0..6 {
            vm.tick(0, no_scripts).unwrap();
        }
        assert!(vm.skip_enabled);
        assert!(!vm.control_enabled);
        assert_eq!(vm.global("G"), None);
        assert_eq!(vm.local("L"), None);

        let mut monologue = Vm::new(
            "monologue.sc",
            program(".pragma monologue\nskipped\nmonologue_end visible"),
        );
        monologue.tick(0, no_scripts).unwrap();
        let events = monologue.tick(0, no_scripts).unwrap();
        assert!(matches!(
            &events[0],
            VmEvent::Message(MessageRequest { text, .. }) if text == "monologue_end visible"
        ));
    }

    #[test]
    fn message_voice_spec_strips_volume_and_pan_before_resource_load() {
        let mut vm = Vm::new(
            "voice-spec.sc",
            program(".message 280 yur-K01-0003_b[100,-60] speaker text"),
        );
        let events = vm.tick(0, no_scripts).unwrap();
        assert_eq!(
            events,
            vec![VmEvent::Message(MessageRequest {
                id: 280,
                voice: "yur-K01-0003_b".into(),
                speaker: "speaker".into(),
                text: "text".into(),
                read: false,
            })]
        );
    }

    #[test]
    fn message_always_waits_for_input_and_preserves_control_codes() {
        let mut vm = Vm::new(
            "message.sc",
            program(".pragma enable_control\n.message 100 voice speaker hello world\n.set X = 1"),
        );
        vm.tick(0, no_scripts).unwrap();
        let events = vm.tick(0, no_scripts).unwrap();
        assert_eq!(
            events,
            vec![VmEvent::Message(MessageRequest {
                id: 100,
                voice: "voice".into(),
                speaker: "speaker".into(),
                text: "hello world".into(),
                read: false,
            })]
        );
        assert_eq!(vm.state(), &VmState::WaitingInput);
        vm.advance_input();
        vm.tick(0, no_scripts).unwrap();
        assert_eq!(vm.local("X"), Some(1));

        let mut controls = Vm::new("controls.sc", program(".message 1   \\a\\vtext"));
        let events = controls.tick(0, no_scripts).unwrap();
        assert_eq!(
            events,
            vec![VmEvent::Message(MessageRequest {
                id: 1,
                voice: String::new(),
                speaker: String::new(),
                text: "\\a\\vtext".into(),
                read: false,
            })]
        );
        assert_eq!(controls.state(), &VmState::WaitingInput);
    }

    #[test]
    fn end_commits_the_message_read_bit_without_ending_the_script() {
        let mut vm = Vm::new(
            "end.sc",
            program(
                ".pragma enable_control\n.message 7 voice speaker first\n.end\n.set X = 1\n.message 7 voice speaker second",
            ),
        );
        vm.tick(0, no_scripts).unwrap();
        let first = vm.tick(0, no_scripts).unwrap();
        assert!(matches!(
            &first[0],
            VmEvent::Message(MessageRequest { read: false, .. })
        ));
        vm.advance_input();
        assert_eq!(vm.tick(0, no_scripts).unwrap(), vec![VmEvent::EndMessage]);
        vm.tick(0, no_scripts).unwrap();
        assert_eq!(vm.local("X"), Some(1));
        let second = vm.tick(0, no_scripts).unwrap();
        assert!(matches!(
            &second[0],
            VmEvent::Message(MessageRequest { read: true, .. })
        ));
    }

    #[test]
    fn chain_from_test_sc_is_deferred_to_the_next_engine_update() {
        let mut vm = Vm::new(
            "test.sc",
            program(".setGlobal FLAG = 2\n.chain A00_01.sc\n.set X = 9"),
        );
        vm.tick(0, no_scripts).unwrap();
        let first_events = vm.tick(0, no_scripts).unwrap();
        assert_eq!(
            first_events,
            vec![VmEvent::EndMessage, VmEvent::PersistGlobals]
        );
        assert_eq!(
            vm.state(),
            &VmState::PendingChain {
                path: "A00_01.sc".into()
            }
        );
        assert_eq!(vm.current_script(), Some("test.sc"));
        assert_eq!(vm.local("X"), None);

        let second_events = vm
            .tick(0, |name| {
                assert_eq!(name, "A00_01.sc");
                Ok(program(".set X = FLAG\n.wait 1"))
            })
            .unwrap();
        assert_eq!(
            second_events,
            vec![VmEvent::ScriptChanged("A00_01.sc".into())]
        );
        assert_eq!(vm.current_script(), Some("A00_01.sc"));
        assert_eq!(vm.global("FLAG"), Some(2));
        assert_eq!(vm.local("X"), Some(2));
        assert_eq!(vm.state(), &VmState::Ready);

        vm.tick(0, no_scripts).unwrap();
        assert_eq!(vm.state(), &VmState::WaitingTimer { remaining_ms: 10 });
    }
}
