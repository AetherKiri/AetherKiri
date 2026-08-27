//! Language variant data-package resolution for localized Siglus games.
//!
//! Localized distributions (Chinese fan-patch families and similar) ship
//! translated data as sibling files that keep the original stem but replace
//! the extension — `Gameexe.dat` -> `Gameexe.chs`, `Scene.pck` ->
//! `Scene.chs`, `g00/<name>.g00` -> `<name>.g01`, `dat/<name>.dbs` ->
//! `<name>.dbc` — plus a per-language save directory (`savedata/` ->
//! `save_chs/`). The original Windows engine implements this by patching
//! file-open inside the process; on this runtime the same policy is a pure
//! data-table lookup consulted by the game VFS, so a language package loads
//! without any game-specific engine code.
//!
//! Policy:
//! - `AETHERKIRI_GAME_LANG` selects a language explicitly: `zh`, `chs`,
//!   `zh-hans`, `zh-cn` force the Chinese set; `ja`, `jp`, `jpn`, `en`,
//!   `off`, `none` force the original files. Any other non-empty value is
//!   ignored with a warning and falls back to auto-detection.
//! - Without the variable the resolver auto-detects conservatively: a
//!   mapping fires only when the requested file name exactly matches a
//!   mapping-source convention and the variant file exists. A missing
//!   variant means the original file is used and nothing else changes.
//! - Save-directory variants additionally require the variant directory to
//!   exist.
//!
//! Adding a language means adding a `LangVariantSet` table row; no call
//! sites change. A future system-level language selector only needs to set
//! `AETHERKIRI_GAME_LANG` (or replace [`selection`] with a runtime setting).

use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::sync::OnceLock;

/// Extension/directory replacement conventions for one language package.
pub struct LangVariantSet {
    /// BCP-47-ish tag used in reports ("zh").
    pub tag: &'static str,
    /// (requested extension, variant extension), lowercase ASCII.
    pub ext_map: &'static [(&'static str, &'static str)],
    /// (requested directory name, variant directory name).
    pub dir_map: &'static [(&'static str, &'static str)],
    /// Well-known variant file names probed for the one-shot report.
    pub sentinels: &'static [&'static str],
}

/// Simplified-Chinese fan-patch convention (chs extension family).
pub const ZH_HANS: LangVariantSet = LangVariantSet {
    tag: "zh",
    ext_map: &[("dat", "chs"), ("pck", "chs"), ("g00", "g01"), ("dbs", "dbc")],
    dir_map: &[("savedata", "save_chs")],
    sentinels: &["Gameexe.chs", "Scene.chs"],
};

enum Selection {
    /// No `AETHERKIRI_GAME_LANG`: mappings apply only when variant files
    /// actually exist (existence is checked by the caller).
    Auto,
    /// Explicit language set from the environment (may be `None` = force
    /// original files).
    Forced(Option<&'static LangVariantSet>),
}

static SELECTION: OnceLock<Selection> = OnceLock::new();

fn selection() -> &'static Selection {
    SELECTION.get_or_init(|| {
        let Ok(raw) = std::env::var("AETHERKIRI_GAME_LANG") else {
            return Selection::Auto;
        };
        match raw.trim().to_ascii_lowercase().as_str() {
            "" => Selection::Auto,
            "zh" | "chs" | "zh-hans" | "zh-cn" => Selection::Forced(Some(&ZH_HANS)),
            "ja" | "jp" | "jpn" | "en" | "off" | "none" => Selection::Forced(None),
            other => {
                eprintln!(
                    "[AETHERKIRI_LANG] unknown AETHERKIRI_GAME_LANG={other:?}; ignoring (auto-detect)"
                );
                Selection::Auto
            }
        }
    })
}

fn active_set() -> Option<&'static LangVariantSet> {
    match selection() {
        Selection::Forced(set) => *set,
        // Auto-detection cannot know a language up front; treat every table
        // as a candidate. The per-file existence check keeps this
        // conservative: mappings only fire on exact variant files.
        Selection::Auto => Some(&ZH_HANS),
    }
}

/// Map a requested game file to its language-variant candidate, if the
/// requested extension participates in the active mapping table. Pure path
/// math — the caller performs the existence check.
pub fn map_file_candidate(path: &Path) -> Option<PathBuf> {
    if matches!(selection(), Selection::Forced(None)) {
        note_forced_off();
        return None;
    }
    let set = active_set()?;
    let file_name = path.file_name()?;
    let name = file_name.to_str()?;
    let (stem, ext) = split_stem_ext(name)?;
    let (_, variant_ext) = set
        .ext_map
        .iter()
        .find(|(from, _)| ext.eq_ignore_ascii_case(from))?;
    let candidate = format!("{stem}.{variant_ext}");
    Some(path.with_file_name(candidate))
}

/// Report + log a successful variant substitution. The first call for a
/// language probes the containing directory for well-known variant files and
/// emits the one-shot `variant=<tag> files=[...]` report; individual
/// substitutions are logged with a cap so per-frame resource churn cannot
/// flood stderr.
pub fn note_variant_file_used(requested: &Path, variant: &Path) {
    const MAX_PER_FILE_LOGS: usize = 24;
    static REPORT: OnceLock<Mutex<ReportState>> = OnceLock::new();
    let mut state = REPORT
        .get_or_init(|| {
            Mutex::new(ReportState {
                announced: false,
                logged: 0,
                suppressed_note: false,
            })
        })
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner());

    let set = match active_set() {
        Some(set) => set,
        None => return,
    };
    let source = match selection() {
        Selection::Auto => "auto",
        Selection::Forced(_) => "AETHERKIRI_GAME_LANG",
    };
    if !state.announced {
        state.announced = true;
        let mut files = probe_sentinels(variant.parent(), set);
        if files.is_empty() {
            files.push(file_name_of(variant));
        }
        eprintln!(
            "[AETHERKIRI_LANG] variant={} source={} files=[{}]",
            set.tag,
            source,
            files.join(", ")
        );
    }
    if state.logged < MAX_PER_FILE_LOGS {
        state.logged += 1;
        eprintln!(
            "[AETHERKIRI_LANG] {} -> {}",
            file_name_of(requested),
            file_name_of(variant)
        );
    } else if !state.suppressed_note {
        state.suppressed_note = true;
        eprintln!(
            "[AETHERKIRI_LANG] further variant substitutions suppressed (limit {MAX_PER_FILE_LOGS})"
        );
    }
}

struct ReportState {
    announced: bool,
    logged: usize,
    suppressed_note: bool,
}

fn file_name_of(path: &Path) -> String {
    path.file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| path.to_string_lossy().into_owned())
}

/// List the well-known variant files found beside the first substituted
/// file.
fn probe_sentinels(dir: Option<&Path>, set: &LangVariantSet) -> Vec<String> {
    let mut found = Vec::new();
    if let Some(dir) = dir {
        for sentinel in set.sentinels {
            if variant_path_exists(&dir.join(sentinel)) {
                found.push((*sentinel).to_string());
            }
        }
    }
    found
}

fn note_forced_off() {
    static REPORTED: OnceLock<()> = OnceLock::new();
    if REPORTED.set(()).is_ok() {
        eprintln!(
            "[AETHERKIRI_LANG] variant=off source=AETHERKIRI_GAME_LANG (original files only)"
        );
    }
}

/// Map a well-known per-language save directory (`savedata` ->
/// `save_chs`). The variant directory must already exist, otherwise the
/// original directory name is kept.
pub fn variant_save_dir(project_dir: &Path, base_dir: &str) -> Option<PathBuf> {
    let set = active_set()?;
    let (_, variant_name) = set
        .dir_map
        .iter()
        .find(|(from, _)| from.eq_ignore_ascii_case(base_dir))?;
    let candidate = project_dir.join(variant_name);
    if variant_path_exists(&candidate) {
        note_save_dir_variant(base_dir, variant_name);
        Some(candidate)
    } else {
        None
    }
}

fn note_save_dir_variant(base_dir: &str, variant_name: &str) {
    static NOTED: OnceLock<()> = OnceLock::new();
    if NOTED.set(()).is_ok() {
        eprintln!("[AETHERKIRI_LANG] save_dir {base_dir} -> {variant_name}");
    }
}

fn split_stem_ext(name: &str) -> Option<(&str, &str)> {
    let (stem, ext) = name.rsplit_once('.')?;
    if stem.is_empty() || ext.is_empty() {
        return None;
    }
    Some((stem, ext))
}

#[cfg(all(target_arch = "wasm32", target_os = "unknown"))]
fn variant_path_exists(_path: &Path) -> bool {
    false
}

#[cfg(not(all(target_arch = "wasm32", target_os = "unknown")))]
fn variant_path_exists(path: &Path) -> bool {
    path.exists()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn candidate(name: &str) -> Option<String> {
        map_file_candidate(Path::new(name)).map(|p| p.to_string_lossy().into_owned())
    }

    #[test]
    fn maps_known_extensions() {
        assert_eq!(candidate("Gameexe.dat").as_deref(), Some("Gameexe.chs"));
        assert_eq!(candidate("Scene.pck").as_deref(), Some("Scene.chs"));
        assert_eq!(
            candidate("g00/bg_aaa01.g00").as_deref(),
            Some("g00/bg_aaa01.g01")
        );
        assert_eq!(
            candidate("dat/sfight_map.dbs").as_deref(),
            Some("dat/sfight_map.dbc")
        );
    }

    #[test]
    fn leaves_unmapped_names_alone() {
        assert_eq!(candidate("Gameexe.chs"), None);
        assert_eq!(candidate("bgm/track01.ogg"), None);
        assert_eq!(candidate("dat/spfont.otf"), None);
        assert_eq!(candidate("Scene.pck.bak").as_deref(), None);
        assert_eq!(candidate(".dat"), None);
    }

    #[test]
    fn extension_case_is_normalized() {
        assert_eq!(candidate("GAMEEXE.DAT").as_deref(), Some("GAMEEXE.chs"));
    }
}
