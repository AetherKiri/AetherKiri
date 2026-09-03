use std::path::Path;

#[derive(Debug, Clone, Copy)]
pub struct ArchiveKeyProfile {
    pub archive: &'static str,
    pub index: &'static str,
    pub data: Option<&'static str>,
}

#[derive(Debug, Clone, Copy)]
pub struct TypePasswordProfile {
    pub png: &'static str,
    pub ogg: &'static str,
    pub sc: &'static str,
    pub avi: &'static str,
}

#[derive(Debug)]
pub struct ArchiveProfile {
    pub id: &'static str,
    pub executable_markers: &'static [&'static str],
    pub archive_keys: &'static [ArchiveKeyProfile],
    pub type_passwords: TypePasswordProfile,
}

impl ArchiveProfile {
    pub fn matches_root(&self, root: &Path) -> bool {
        self.executable_markers
            .iter()
            .any(|name| root.join(name).is_file())
    }

    pub fn archive_key(&self, archive: &str) -> Option<ArchiveKeyProfile> {
        self.archive_keys
            .iter()
            .copied()
            .find(|keys| keys.archive == archive)
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct LogicalRect {
    pub left: f64,
    pub top: f64,
    pub right: f64,
    pub bottom: f64,
}

impl LogicalRect {
    pub fn contains(self, x: f64, y: f64) -> bool {
        (self.left..self.right).contains(&x) && (self.top..self.bottom).contains(&y)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GameMenuItemKind {
    Skip,
    QuickSave,
    System,
    Load,
    Save,
}

#[derive(Debug, Clone, Copy)]
pub struct GameMenuItemProfile {
    pub kind: GameMenuItemKind,
    pub resource: &'static str,
    pub x: i32,
    pub y: i32,
    pub width: f64,
    pub height: f64,
}

#[derive(Debug)]
pub struct GameMenuProfile {
    pub background: &'static str,
    pub right_margin: i32,
    pub bottom_margin: i32,
    pub trigger_size: f64,
    pub items: &'static [GameMenuItemProfile],
}

#[derive(Debug)]
pub struct SaveLoadProfile {
    pub base: &'static str,
    pub save_heading: &'static str,
    pub load_heading: &'static str,
    pub page_resource: &'static str,
    pub page_x: i32,
    pub page_y: i32,
    pub left_column: LogicalRect,
    pub right_column: LogicalRect,
    pub first_row_top: f64,
    pub row_height: f64,
    pub row_step: f64,
    pub close: LogicalRect,
    pub thumbnail_width: u32,
    pub thumbnail_height: u32,
    pub thumbnail_left: [i32; 2],
    pub thumbnail_top: i32,
}

#[derive(Debug, Clone, Copy)]
pub struct MessageWaitProfile {
    pub resource: &'static str,
    pub frames: u8,
    pub interval_ms: u32,
    pub x: f64,
    pub y: f64,
}

#[derive(Debug)]
pub struct GameProfile {
    pub id: &'static str,
    pub archive: &'static ArchiveProfile,
    pub entry_script: &'static str,
    pub title_background: &'static str,
    pub title_new_game: LogicalRect,
    pub title_load_game: LogicalRect,
    pub message_panel: &'static str,
    pub message_wait: MessageWaitProfile,
    pub game_menu: GameMenuProfile,
    pub save_load: SaveLoadProfile,
}

impl GameProfile {
    pub fn matches_root(&self, root: &Path) -> bool {
        self.archive.matches_root(root)
    }
}

const SUPIPARA_ARCHIVE_KEYS: &[ArchiveKeyProfile] = &[
    ArchiveKeyProfile {
        archive: "sys",
        index: "DC2EA1EA00FEA73CCB10E86DC56FCEB026A9C31CE2315BFB5C8F6FD6973B8E83",
        data: Some("E6AF033FB4191EBA5A06709EB7FD482917E08DE670C7E5B5B82224C2C8F3A76F"),
    },
    ArchiveKeyProfile {
        archive: "scr",
        index: "3F6BD315E116186C16C520D9634D69A966E93223D1032D333B72D9CA01569DC2",
        data: Some("67409C9A2A59ABC9FDD27755C832ED2107A8F5B15FC4FCF90B707B5FD04B92F8"),
    },
    ArchiveKeyProfile {
        archive: "bg",
        index: "3EEEE8E90D94AAB6AA162783107F617905AC8F8A9AEFC38C05B6A7A1D64A2127",
        data: Some("442844CAF0466C67613310F16990D9258BB8FE05E41DFA1234D3AED5FAA5F615"),
    },
    ArchiveKeyProfile {
        archive: "st",
        index: "972F3265474FDA1D7F2896135690716C15788941DAE332CC0BA151C88712BE82",
        data: Some("397F9CAEE01FF67AF70BAC717BAA14D0CE0DA7BCAFC5CF3A926BFA464186AA8D"),
    },
    ArchiveKeyProfile {
        archive: "bgm",
        index: "E0E3A1A338FAB67CE04B09D79219B17611CC9A6BC306D101B8A62EB2802FD069",
        data: Some("E6697893B71850FC28B28B43A4B3DE789825784D4F6A787A344150E46C317F73"),
    },
    ArchiveKeyProfile {
        archive: "se",
        index: "DE4699D80BF8EC5890B8752CA68B6C94474AECD71EF5DC989A67C61414D8460A",
        data: Some("5AA4666FAE80E8AFE70CC0AA81F522E1C861CE7E0B0AB2B06EC68155F0E20834"),
    },
    ArchiveKeyProfile {
        archive: "voice",
        index: "72EFDF542ECD672CA1DFF05D1F753FA33C3F270A99E1919CC99DFDF086FAB3E8",
        data: Some("963A964FF9784A657D8621929C1502F8339926F1E33996A7935FDFA653FFA1A4"),
    },
    ArchiveKeyProfile {
        archive: "mov",
        index: "102CCE3EB7B6694BD1ADBEC6D675245D643A32A0DF18A922A952468B249225BA",
        data: None,
    },
];

pub static SUPIPARA_ARCHIVE_PROFILE: ArchiveProfile = ArchiveProfile {
    id: "supipara-story-01",
    executable_markers: &["sppl1.exe", "sppl1_cn.exe"],
    archive_keys: SUPIPARA_ARCHIVE_KEYS,
    type_passwords: TypePasswordProfile {
        png: "36TXHE5N",
        ogg: "oRQCAU1o",
        sc: "gTcL74ch",
        avi: "hYPH3Fxw",
    },
};

const TRINOLINE_ARCHIVE_KEYS: &[ArchiveKeyProfile] = &[
    ArchiveKeyProfile {
        archive: "bg",
        index: "955FC5ACAA293514A2B75A8A9D06543CE75B22BBFA57BD049762FC07D4232F7F",
        data: Some("5B81450BC03E5E1510A0CC66C41AC8047C5A4C05B05C419C8A13088259033CE2"),
    },
    ArchiveKeyProfile {
        archive: "bgm",
        index: "B90688022DAA47DA531AD6A181CCB10EDB05604E8A6C8DFF010E2DD9E9355158",
        data: Some("43778DDEB785AD88BA47EC887D80ADDE00FED2531A868F151CD10AB5611209CA"),
    },
    ArchiveKeyProfile {
        archive: "mov",
        index: "9C49892514D8BE366A529ECEE68801BAF9B7B3720EF23F81C0EECAF0D05CD96E",
        data: None,
    },
    ArchiveKeyProfile {
        archive: "scr",
        index: "B3B3E21EEE2CA5CA220358256DB6033CD4257D06B89637654AEA1C6283B8436E",
        data: Some("FB8E18A6FDE6CFFFB366027EE508F10017477713112E34D732377721F8505653"),
    },
    ArchiveKeyProfile {
        archive: "se",
        index: "0479A5EC44A8BF0CA5D3702D4C9DF27F86B85D19FE12778748BEAD2FAC5D3155",
        data: Some("5E37BB71679A7A654F468FF9D59F46E604186F09FC7D42F4B8231BAA854FBBE2"),
    },
    ArchiveKeyProfile {
        archive: "st",
        index: "EC7EABF7F19EE3E0466DC3BFC04D945A3BE3B7F950878E7C69146F34EF9DAAAA",
        data: Some("B955B50065E91822A6A9708366519E4AA5F6C86D671EB286DFB778F7BFD4EEB4"),
    },
    ArchiveKeyProfile {
        archive: "sys",
        index: "F6DBE06AFD38B69695438F3D10F0BF7578C3462697BA1A4ECCDDE1B88CC57BB9",
        data: Some("5B934AEDFC8687438DA7015C9933E525A1EF96FFF49E8784B0DB48586F49F9AC"),
    },
    ArchiveKeyProfile {
        archive: "voice",
        index: "2CA40C776100657BCBD15085F737718AA69C475F00FCD9D0C206E52D07FC1C34",
        data: Some("BC60BE1B82807E1E886FAB45E9ECDBD3FD301ECDA7D7D3E639EA54DAE53325B4"),
    },
];

pub static TRINOLINE_ARCHIVE_PROFILE: ArchiveProfile = ArchiveProfile {
    id: "trinoline",
    executable_markers: &["trinoline.exe", "trinoline_chs.exe"],
    archive_keys: TRINOLINE_ARCHIVE_KEYS,
    type_passwords: TypePasswordProfile {
        png: "B64oH18C",
        ogg: "PiSKji5i",
        sc: "z3Mw9x2r",
        avi: "8n4DgsEb",
    },
};

const SUPIPARA_GAME_MENU_ITEMS: &[GameMenuItemProfile] = &[
    GameMenuItemProfile {
        kind: GameMenuItemKind::Skip,
        resource: "gameMenuSkip.png",
        x: 23,
        y: 41,
        width: 52.0,
        height: 39.0,
    },
    GameMenuItemProfile {
        kind: GameMenuItemKind::QuickSave,
        resource: "gameMenuQSave.png",
        x: 100,
        y: 22,
        width: 50.0,
        height: 51.0,
    },
    GameMenuItemProfile {
        kind: GameMenuItemKind::System,
        resource: "gameMenuSystem.png",
        x: 112,
        y: 97,
        width: 36.0,
        height: 27.0,
    },
    GameMenuItemProfile {
        kind: GameMenuItemKind::Load,
        resource: "gameMenuLoad.png",
        x: 72,
        y: 127,
        width: 33.0,
        height: 34.0,
    },
    GameMenuItemProfile {
        kind: GameMenuItemKind::Save,
        resource: "gameMenuSave.png",
        x: 27,
        y: 101,
        width: 37.0,
        height: 37.0,
    },
];

pub static TRINOLINE_PROFILE: GameProfile = GameProfile {
    id: "trinoline",
    archive: &TRINOLINE_ARCHIVE_PROFILE,
    entry_script: "test.sc",
    title_background: "topmenu\\TopMenu.png",
    title_new_game: LogicalRect {
        left: 260.0,
        top: 220.0,
        right: 400.0,
        bottom: 280.0,
    },
    title_load_game: LogicalRect {
        left: 420.0,
        top: 220.0,
        right: 510.0,
        bottom: 280.0,
    },
    message_panel: "msgPanel.png",
    message_wait: MessageWaitProfile {
        resource: "pauseAnimeNextPage.png",
        frames: 9,
        interval_ms: 100,
        x: 1138.0,
        y: 674.0,
    },
    game_menu: GameMenuProfile {
        background: "gameMenuBack.png",
        right_margin: 6,
        bottom_margin: 15,
        trigger_size: 96.0,
        items: SUPIPARA_GAME_MENU_ITEMS,
    },
    save_load: SaveLoadProfile {
        base: "saveload\\saveDefaultBackground.png",
        save_heading: "saveload\\saveText.png",
        load_heading: "saveload\\loadText.png",
        page_resource: "saveload\\page01.png",
        page_x: 0,
        page_y: 0,
        left_column: LogicalRect {
            left: 0.0,
            top: 0.0,
            right: 640.0,
            bottom: 720.0,
        },
        right_column: LogicalRect {
            left: 640.0,
            top: 0.0,
            right: 1280.0,
            bottom: 720.0,
        },
        first_row_top: 81.0,
        row_height: 96.0,
        row_step: 108.0,
        close: LogicalRect {
            left: 1080.0,
            top: 0.0,
            right: 1280.0,
            bottom: 80.0,
        },
        thumbnail_width: 160,
        thumbnail_height: 90,
        thumbnail_left: [68, 459],
        thumbnail_top: 84,
    },
};

pub static SUPIPARA_PROFILE: GameProfile = GameProfile {
    id: "supipara-story-01",
    archive: &SUPIPARA_ARCHIVE_PROFILE,
    entry_script: "test.sc",
    title_background: "topMenu1.png",
    title_new_game: LogicalRect {
        left: 1056.0,
        top: 32.0,
        right: 1280.0,
        bottom: 64.0,
    },
    title_load_game: LogicalRect {
        left: 1056.0,
        top: 76.0,
        right: 1280.0,
        bottom: 112.0,
    },
    message_panel: "msgPanel.png",
    message_wait: MessageWaitProfile {
        resource: "pauseAnimeNextPage.png",
        frames: 9,
        interval_ms: 100,
        x: 1138.0,
        y: 674.0,
    },
    game_menu: GameMenuProfile {
        background: "gameMenuBack.png",
        right_margin: 6,
        bottom_margin: 15,
        trigger_size: 96.0,
        items: SUPIPARA_GAME_MENU_ITEMS,
    },
    save_load: SaveLoadProfile {
        base: "saveloadBase.png",
        save_heading: "saveloadSave.png",
        load_heading: "saveloadLoad.png",
        page_resource: "saveload_Page1.png",
        page_x: 1072,
        page_y: 0,
        left_column: LogicalRect {
            left: 65.0,
            top: 0.0,
            right: 408.0,
            bottom: 720.0,
        },
        right_column: LogicalRect {
            left: 456.0,
            top: 0.0,
            right: 800.0,
            bottom: 720.0,
        },
        first_row_top: 81.0,
        row_height: 96.0,
        row_step: 108.0,
        close: LogicalRect {
            left: 1080.0,
            top: 0.0,
            right: 1280.0,
            bottom: 80.0,
        },
        thumbnail_width: 160,
        thumbnail_height: 90,
        thumbnail_left: [68, 459],
        thumbnail_top: 84,
    },
};

pub fn detect_archive_profile(root: &Path) -> Option<&'static ArchiveProfile> {
    [&SUPIPARA_ARCHIVE_PROFILE, &TRINOLINE_ARCHIVE_PROFILE]
        .into_iter()
        .find(|profile| profile.matches_root(root))
}

pub fn detect_game_profile(root: &Path) -> Option<&'static GameProfile> {
    [&SUPIPARA_PROFILE, &TRINOLINE_PROFILE]
        .into_iter()
        .find(|profile| profile.matches_root(root))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn supipara_profile_keeps_work_specific_resources_out_of_the_core() {
        assert_eq!(SUPIPARA_PROFILE.entry_script, "test.sc");
        assert!(SUPIPARA_PROFILE.title_new_game.contains(1100.0, 48.0));
        assert!(SUPIPARA_PROFILE.title_load_game.contains(1100.0, 96.0));
        assert_eq!(SUPIPARA_PROFILE.game_menu.items.len(), 5);
        assert_eq!(SUPIPARA_PROFILE.archive.archive_keys.len(), 8);
        assert!(SUPIPARA_PROFILE.archive.archive_key("voice").is_some());
    }

    #[test]
    fn trinoline_archive_profile_uses_garbro_v2_scheme() {
        assert_eq!(TRINOLINE_ARCHIVE_PROFILE.archive_keys.len(), 8);
        assert_eq!(TRINOLINE_ARCHIVE_PROFILE.type_passwords.sc, "z3Mw9x2r");
        assert!(TRINOLINE_ARCHIVE_PROFILE.archive_key("scr").is_some());
    }
}
