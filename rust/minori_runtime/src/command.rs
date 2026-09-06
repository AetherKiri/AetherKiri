#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CommandKind {
    Chain,
    Char,
    Effect,
    End,
    EndScroll,
    Goto,
    HScroll,
    If,
    Label,
    Message,
    Movie,
    Panel,
    PlayBgm,
    PlaySe,
    PlaySe2,
    PlaySe3,
    PlayVoice,
    Pragma,
    Scroll,
    ScrollXf,
    Select,
    Set,
    SetGlobal,
    ShakeScreen,
    Stage,
    Transition,
    VScroll,
    Wait,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct OriginalHandler {
    pub parse: u32,
    pub execute: u32,
}

impl CommandKind {
    pub fn from_name(name: &str) -> Option<Self> {
        Some(match name {
            "chain" => Self::Chain,
            "char" => Self::Char,
            "effect" => Self::Effect,
            "end" => Self::End,
            "endScroll" => Self::EndScroll,
            "goto" => Self::Goto,
            "hscroll" => Self::HScroll,
            "if" => Self::If,
            "label" => Self::Label,
            "message" => Self::Message,
            "movie" => Self::Movie,
            "panel" => Self::Panel,
            "playBGM" => Self::PlayBgm,
            "playSE" => Self::PlaySe,
            "playSE2" => Self::PlaySe2,
            "playSE3" => Self::PlaySe3,
            "playVoice" => Self::PlayVoice,
            "pragma" => Self::Pragma,
            "scroll" => Self::Scroll,
            "scrollXF" => Self::ScrollXf,
            "select" => Self::Select,
            "set" => Self::Set,
            "setGlobal" => Self::SetGlobal,
            "shakeScreen" => Self::ShakeScreen,
            "stage" => Self::Stage,
            "transition" => Self::Transition,
            "vscroll" => Self::VScroll,
            "wait" => Self::Wait,
            _ => return None,
        })
    }

    pub const fn name(self) -> &'static str {
        match self {
            Self::Chain => "chain",
            Self::Char => "char",
            Self::Effect => "effect",
            Self::End => "end",
            Self::EndScroll => "endScroll",
            Self::Goto => "goto",
            Self::HScroll => "hscroll",
            Self::If => "if",
            Self::Label => "label",
            Self::Message => "message",
            Self::Movie => "movie",
            Self::Panel => "panel",
            Self::PlayBgm => "playBGM",
            Self::PlaySe => "playSE",
            Self::PlaySe2 => "playSE2",
            Self::PlaySe3 => "playSE3",
            Self::PlayVoice => "playVoice",
            Self::Pragma => "pragma",
            Self::Scroll => "scroll",
            Self::ScrollXf => "scrollXF",
            Self::Select => "select",
            Self::Set => "set",
            Self::SetGlobal => "setGlobal",
            Self::ShakeScreen => "shakeScreen",
            Self::Stage => "stage",
            Self::Transition => "transition",
            Self::VScroll => "vscroll",
            Self::Wait => "wait",
        }
    }

    pub const fn original_handler(self) -> OriginalHandler {
        match self {
            Self::Chain => OriginalHandler {
                parse: 0x4480B0,
                execute: 0x446AD0,
            },
            Self::Char => OriginalHandler {
                parse: 0x446D60,
                execute: 0x447480,
            },
            Self::Effect => OriginalHandler {
                parse: 0x4482B0,
                execute: 0x448470,
            },
            Self::End => OriginalHandler {
                parse: 0x439410,
                execute: 0x448560,
            },
            Self::EndScroll => OriginalHandler {
                parse: 0x4485C0,
                execute: 0x448620,
            },
            Self::Goto => OriginalHandler {
                parse: 0x4480B0,
                execute: 0x448AD0,
            },
            Self::HScroll => OriginalHandler {
                parse: 0x44C050,
                execute: 0x44C1A0,
            },
            Self::If => OriginalHandler {
                parse: 0x448F90,
                execute: 0x449010,
            },
            Self::Label => OriginalHandler {
                parse: 0x4480B0,
                execute: 0x46D5C0,
            },
            Self::Message => OriginalHandler {
                parse: 0x449770,
                execute: 0x449880,
            },
            Self::Movie => OriginalHandler {
                parse: 0x449A70,
                execute: 0x449C60,
            },
            Self::Panel => OriginalHandler {
                parse: 0x44A450,
                execute: 0x44A560,
            },
            Self::PlayBgm => OriginalHandler {
                parse: 0x44A6F0,
                execute: 0x44A890,
            },
            Self::PlaySe => OriginalHandler {
                parse: 0x44AB30,
                execute: 0x44A9E0,
            },
            Self::PlaySe2 => OriginalHandler {
                parse: 0x44AB30,
                execute: 0x44ACA0,
            },
            Self::PlaySe3 => OriginalHandler {
                parse: 0x45B5E0,
                execute: 0x45B900,
            },
            Self::PlayVoice => OriginalHandler {
                parse: 0x44AE00,
                execute: 0x44AE40,
            },
            Self::Pragma => OriginalHandler {
                parse: 0x44AFD0,
                execute: 0x44B020,
            },
            Self::Scroll => OriginalHandler {
                parse: 0x44C400,
                execute: 0x44C250,
            },
            Self::ScrollXf => OriginalHandler {
                parse: 0x45D430,
                execute: 0x45D800,
            },
            Self::Select => OriginalHandler {
                parse: 0x45DAD0,
                execute: 0x45DBF0,
            },
            Self::Set => OriginalHandler {
                parse: 0x44CB80,
                execute: 0x44CE00,
            },
            Self::SetGlobal => OriginalHandler {
                parse: 0x44CB80,
                execute: 0x44D1F0,
            },
            Self::ShakeScreen => OriginalHandler {
                parse: 0x44D510,
                execute: 0x44D5E0,
            },
            Self::Stage => OriginalHandler {
                parse: 0x44E020,
                execute: 0x44E3B0,
            },
            Self::Transition => OriginalHandler {
                parse: 0x44E930,
                execute: 0x44EA10,
            },
            Self::VScroll => OriginalHandler {
                parse: 0x44C050,
                execute: 0x44C0F0,
            },
            Self::Wait => OriginalHandler {
                parse: 0x44EAE0,
                execute: 0x44EB50,
            },
        }
    }
}

pub const GAME_COMMANDS: [CommandKind; 28] = [
    CommandKind::Chain,
    CommandKind::Char,
    CommandKind::Effect,
    CommandKind::End,
    CommandKind::EndScroll,
    CommandKind::Goto,
    CommandKind::HScroll,
    CommandKind::If,
    CommandKind::Label,
    CommandKind::Message,
    CommandKind::Movie,
    CommandKind::Panel,
    CommandKind::PlayBgm,
    CommandKind::PlaySe,
    CommandKind::PlaySe2,
    CommandKind::PlaySe3,
    CommandKind::PlayVoice,
    CommandKind::Pragma,
    CommandKind::Scroll,
    CommandKind::ScrollXf,
    CommandKind::Select,
    CommandKind::Set,
    CommandKind::SetGlobal,
    CommandKind::ShakeScreen,
    CommandKind::Stage,
    CommandKind::Transition,
    CommandKind::VScroll,
    CommandKind::Wait,
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registry_is_complete_and_round_trips() {
        assert_eq!(GAME_COMMANDS.len(), 28);
        for command in GAME_COMMANDS {
            assert_eq!(CommandKind::from_name(command.name()), Some(command));
            assert_ne!(command.original_handler().parse, 0);
            assert_ne!(command.original_handler().execute, 0);
        }
    }
}
