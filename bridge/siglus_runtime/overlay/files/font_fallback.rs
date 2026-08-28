//! Glyph-level font fallback for the text renderer.
//!
//! Generic engine capability, not tied to any one title or language: when the
//! active face has no glyph for a character (its `cmap` maps it to glyph 0),
//! the renderer consults an ordered chain of fallback faces and rasterizes the
//! character from the first face that covers it. Characters the primary face
//! renders take exactly the same path as before — chain traversal only ever
//! happens on a per-character miss. Fallback faces are additionally rescaled
//! so one numeric font size means the same em box on every face (see
//! [`normalized_px`]); the primary face is passed through unchanged.
//!
//! Hot-path safety: both the per-character face resolution and the rasterized
//! glyph bitmaps live in bounded LRU caches behind one mutex. Font files are
//! parsed lazily, at most once per face, and only when a fallback glyph is
//! first needed. Per-frame work for already-seen characters is a map lookup
//! plus a bitmap clone, never font parsing or cmap walks.
//!
//! Default chain (first match wins):
//! 1. `AETHERKIRI_FONT_FALLBACKS` paths (user override, `:`/`;` separated;
//!    the special value `off` disables the whole chain).
//! 2. Game-local faces next to the project (`dat/`, `font/`, `fonts/`),
//!    excluding the currently loaded primary face.
//! 3. Well-known platform CJK faces (Noto Sans CJK, WenQuanYi, Droid).
//! 4. Host-runtime faces shipped next to the embedding executable.
//!
//! Diagnostics: `AETHERKIRI_FONT_FALLBACK_LOG=1` enables one stderr line per
//! newly resolved character plus a periodic chain summary; without it only a
//! single chain-construction notice is emitted per generation.

use crate::text_render::{rasterize_ab_glyph_uncached, RasterGlyph};
use ab_glyph::{Font, FontArc, FontVec, PxScale, ScaleFont};
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};

/// Bounded number of `char -> face` resolutions kept per generation.
const RESOLVE_CACHE_CAP: usize = 4096;
/// Bounded number of cached rasterized glyphs (a dialogue glyph is ~1 KB).
const RASTER_CACHE_CAP: usize = 2048;
/// Emit the summary line after this many newly resolved characters.
const STATS_LOG_INTERVAL: u64 = 512;
/// Quarter-pixel raster cache resolution: distinct sizes below 0.25 px share
/// one cache entry, which keeps layout jitter out of the cache key.
const PX_QUANTUM: f32 = 4.0;

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
struct RasterKey {
    /// 0 = primary face, 1.. = index into the fallback chain + 1.
    face: u32,
    ch: char,
    px: u16,
}

struct FallbackFace {
    source: String,
    path: PathBuf,
    /// Load attempt result: `None` while not attempted, `Some(None)` when the
    /// file could not be parsed (skip it forever), `Some(Some(font))` on hit.
    font: Option<Option<FontArc>>,
}

#[derive(Default)]
struct FallbackState {
    /// Incremented whenever the primary face or project root changes; bumps
    /// invalidate every cache so a SCRIPT font switch can never observe stale
    /// coverage decisions or rasters.
    generation: u64,
    project_dir: Option<PathBuf>,
    primary_source: Option<PathBuf>,
    chain: Vec<FallbackFace>,
    chain_built: bool,
    chain_disabled: bool,
    resolve: HashMap<char, (u32, u64)>,
    resolve_stamp: u64,
    raster: HashMap<RasterKey, (RasterGlyph, u64)>,
    raster_stamp: u64,
    stats_primary: u64,
    stats_face_hits: Vec<u64>,
    stats_miss: u64,
    resolved_chars: u64,
    logged_chars: HashSet<char>,
    logged_chain_notice: bool,
}

fn fallback_log_enabled() -> bool {
    static ENABLED: OnceLock<bool> = OnceLock::new();
    *ENABLED.get_or_init(|| {
        matches!(
            std::env::var("AETHERKIRI_FONT_FALLBACK_LOG")
                .unwrap_or_default()
                .trim(),
            "1" | "true" | "on" | "yes"
        )
    })
}

fn state() -> std::sync::MutexGuard<'static, FallbackState> {
    static STATE: OnceLock<Mutex<FallbackState>> = OnceLock::new();
    STATE
        .get_or_init(|| Mutex::new(FallbackState::default()))
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

/// Record the current primary face context from [`crate::text_render::FontCache`].
///
/// Called on every font (re)selection; cheap when nothing changed. A real
/// change bumps the generation so coverage decisions and cached rasters from
/// the previous face are never reused.
pub fn note_primary_font(project_dir: &Path, primary_source: Option<&Path>) {
    let mut st = state();
    let project_dir = Some(project_dir.to_path_buf()).filter(|p| !p.as_os_str().is_empty());
    let primary_source = primary_source.map(Path::to_path_buf);
    if st.project_dir == project_dir && st.primary_source == primary_source {
        return;
    }
    st.project_dir = project_dir;
    st.primary_source = primary_source;
    st.generation = st.generation.wrapping_add(1);
    st.chain.clear();
    st.chain_built = false;
    st.resolve.clear();
    st.raster.clear();
    st.stats_primary = 0;
    st.stats_face_hits.clear();
    st.stats_miss = 0;
    st.resolved_chars = 0;
    st.logged_chars.clear();
}

/// Rasterize `ch` at `font_px` with glyph-level fallback.
///
/// The primary face is used unchanged whenever it covers the character;
/// otherwise the first fallback face that maps `ch` to a real glyph wins. The
/// raster is served from an LRU cache keyed by (face, char, size).
pub fn rasterize_glyph_cached(primary: &FontArc, ch: char, font_px: f32) -> RasterGlyph {
    let mut st = state();
    let face = resolve_face_locked(&mut st, primary, ch);
    let font = face_font_locked(&mut st, primary, face);
    let scale_px = normalized_px(primary, &font, font_px);
    let px = quantize_px(scale_px);
    let key = RasterKey { face, ch, px };
    if st.raster.contains_key(&key) {
        st.raster_stamp += 1;
        let stamp = st.raster_stamp;
        let hit = st.raster.get_mut(&key).unwrap();
        let glyph = hit.0.clone();
        hit.1 = stamp;
        return glyph;
    }
    let glyph = rasterize_ab_glyph_uncached(&font, ch, scale_px);
    st.raster_stamp += 1;
    let stamp = st.raster_stamp;
    insert_lru(&mut st.raster, key, (glyph.clone(), stamp), RASTER_CACHE_CAP);
    glyph
}

/// Advance width of the face that would actually render `ch` at `font_px`.
///
/// Layout code uses this so a character resolved through the fallback chain
/// also metrics from that chain face instead of the primary's `.notdef`.
pub fn glyph_advance(primary: &FontArc, ch: char, font_px: f32) -> f32 {
    let mut st = state();
    let face = resolve_face_locked(&mut st, primary, ch);
    let font = face_font_locked(&mut st, primary, face);
    let scaled = font.as_scaled(PxScale::from(normalized_px(primary, &font, font_px)));
    scaled.h_advance(scaled.glyph_id(ch))
}

/// Per-face pixel size that keeps `font_px` visually consistent across faces.
///
/// `ab_glyph` derives its pixel scale from each face's hhea
/// ascender−descender span (`ScaleFont` divides by `height_unscaled`), not
/// from the em box, so one numeric size renders a different em height per
/// face: the embedded default face has height == units_per_em (ratio 1.0)
/// while spfont/Noto CJK carry height ≈ 1.45 × upm (ratio ≈ 0.69), which made
/// every fallback glyph draw ≈ 30 % smaller than the primary's. Rescale each
/// face so its em box matches the primary's; the primary itself passes
/// through untouched, so existing primary rendering is bit-identical.
fn normalized_px(primary: &FontArc, face: &FontArc, font_px: f32) -> f32 {
    if std::ptr::eq(primary, face) {
        return font_px.max(1.0);
    }
    let ratio = (em_px_ratio(primary) / em_px_ratio(face)).clamp(0.25, 4.0);
    (font_px * ratio).max(1.0)
}

/// Em-box pixels produced by one unit of `PxScale` for `font`:
/// units_per_em / (hhea ascent − descent). Defaults to 1.0 (no adjustment)
/// for faces with unusable metrics; the clamp only guards pathological data.
fn em_px_ratio(font: &FontArc) -> f32 {
    let upm = font.units_per_em().unwrap_or(0.0);
    let height = font.height_unscaled();
    if upm <= 0.0 || height.abs() < f32::EPSILON {
        return 1.0;
    }
    (upm / height).clamp(0.05, 20.0)
}

fn quantize_px(font_px: f32) -> u16 {
    ((font_px.max(1.0) * PX_QUANTUM).round() as u32).min(u16::MAX as u32) as u16
}

/// Resolve which face renders `ch`: 0 = primary (covers it, or nothing in the
/// chain did), otherwise chain index + 1. Cached per character per generation.
fn resolve_face_locked(st: &mut FallbackState, primary: &FontArc, ch: char) -> u32 {
    if st.resolve.contains_key(&ch) {
        st.resolve_stamp += 1;
        let stamp = st.resolve_stamp;
        let hit = st.resolve.get_mut(&ch).unwrap();
        let face = hit.0;
        hit.1 = stamp;
        return face;
    }

    let primary_covers = primary.glyph_id(ch).0 != 0;
    let mut face = 0u32;
    if !primary_covers && !st.chain_disabled {
        ensure_chain_locked(st);
        for idx in 0..st.chain.len() {
            let Some(font) = load_face_locked(st, idx) else {
                continue;
            };
            if font.glyph_id(ch).0 != 0 {
                face = idx as u32 + 1;
                break;
            }
        }
    }

    log_resolution_locked(st, primary_covers, face, ch);

    st.resolve_stamp += 1;
    let stamp = st.resolve_stamp;
    insert_lru(&mut st.resolve, ch, (face, stamp), RESOLVE_CACHE_CAP);
    face
}

fn ensure_chain_locked(st: &mut FallbackState) {
    if st.chain_built {
        return;
    }
    st.chain_built = true;
    let sources = fallback_sources(st);
    st.chain_disabled = sources.is_empty() && env_fallbacks_off();
    st.chain = sources
        .into_iter()
        .map(|(path, source)| FallbackFace {
            source,
            path,
            font: None,
        })
        .collect();
    if !st.logged_chain_notice {
        st.logged_chain_notice = true;
        let sources: Vec<&str> = st.chain.iter().map(|f| f.source.as_str()).collect();
        eprintln!(
            "[aetherkiri-font] glyph fallback chain gen={} faces={} [{}]",
            st.generation,
            st.chain.len(),
            sources.join(", ")
        );
    }
}

fn env_fallbacks_off() -> bool {
    std::env::var("AETHERKIRI_FONT_FALLBACKS")
        .map(|v| v.trim().eq_ignore_ascii_case("off"))
        .unwrap_or(false)
}

/// Enumerate candidate fallback faces in priority order, deduplicated and
/// filtered to files that exist. Game-local candidates come from the resolved
/// (case-insensitive) game VFS; platform candidates are plain absolute paths.
fn fallback_sources(st: &FallbackState) -> Vec<(PathBuf, String)> {
    let mut out: Vec<(PathBuf, String)> = Vec::new();
    let push = |path: PathBuf, source: String, out: &mut Vec<(PathBuf, String)>| {
        if path.as_os_str().is_empty() {
            return;
        }
        if st.primary_source.as_ref().is_some_and(|p| p == &path) {
            return;
        }
        if out.iter().any(|(p, _)| p == &path) {
            return;
        }
        out.push((path, source));
    };

    // 1. Explicit override: AETHERKIRI_FONT_FALLBACKS=path:path (or "off").
    let mut env_off = false;
    if let Ok(spec) = std::env::var("AETHERKIRI_FONT_FALLBACKS") {
        let spec = spec.trim();
        if spec.eq_ignore_ascii_case("off") {
            env_off = true;
        } else {
            for part in spec.split([':', ';']) {
                let part = part.trim();
                if part.is_empty() {
                    continue;
                }
                let path = PathBuf::from(part);
                if !crate::resource::game_file_exists(&path) {
                    continue;
                }
                push(path, format!("env:{part}"), &mut out);
            }
        }
    }
    if env_off {
        return out;
    }

    // 2. Game-local faces (other titles' fonts, original pack faces, ...).
    let mut game_locals: Vec<PathBuf> = Vec::new();
    if let Some(project) = st.project_dir.as_ref() {
        for sub in ["dat", "font", "fonts"] {
            let dir = project.join(sub);
            let Some(resolved) = crate::resource::resolve_game_path(&dir).ok().flatten() else {
                continue;
            };
            let Ok(entries) = std::fs::read_dir(&resolved) else {
                continue;
            };
            for entry in entries.flatten() {
                let path = entry.path();
                if !path.is_file() || !is_supported_font_path(&path) {
                    continue;
                }
                game_locals.push(path);
            }
        }
    }
    game_locals.sort_by_key(|path| {
        path.file_name()
            .and_then(|n| n.to_str())
            .map(|n| n.to_ascii_lowercase())
            .unwrap_or_default()
    });
    for path in game_locals {
        push(path, String::new(), &mut out);
    }

    // 3. Well-known platform CJK faces.
    for path in platform_fallback_candidates() {
        if crate::resource::game_file_exists(&path) {
            push(path.clone(), path.display().to_string(), &mut out);
        }
    }

    // 4. Host-runtime faces shipped next to the embedding executable.
    if let Ok(exe) = std::env::current_exe() {
        if let Some(exe_dir) = exe.parent() {
            for rel in [
                "aetherkiri-runtime-cjk.otf",
                "assets/fonts/aetherkiri-runtime-cjk.otf",
                "assets/font/aetherkiri-runtime-cjk.otf",
                "fonts/aetherkiri-runtime-cjk.otf",
            ] {
                let path = exe_dir.join(rel);
                if path.is_file() {
                    push(path.clone(), path.display().to_string(), &mut out);
                }
            }
        }
    }

    // Label the unlabelled game-local faces now that ordering is final.
    for (path, source) in out.iter_mut() {
        if source.is_empty() {
            let name = path
                .file_name()
                .and_then(|n| n.to_str())
                .unwrap_or("game font");
            *source = format!("game:{name}");
        }
    }
    out
}

fn is_supported_font_path(path: &Path) -> bool {
    matches!(
        path.extension().and_then(|s| s.to_str()).map(|s| s.to_ascii_lowercase()).as_deref(),
        Some("ttf" | "otf" | "ttc")
    )
}

#[cfg(any(target_os = "linux", target_os = "freebsd", target_os = "android"))]
fn platform_fallback_candidates() -> Vec<PathBuf> {
    [
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
    ]
    .iter()
    .map(PathBuf::from)
    .collect()
}

#[cfg(target_os = "windows")]
fn platform_fallback_candidates() -> Vec<PathBuf> {
    let windir = std::env::var_os("WINDIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(r"C:\Windows"));
    let fonts = windir.join("Fonts");
    ["msyh.ttc", "simsun.ttc", "simhei.ttf", "msgothic.ttc"]
        .iter()
        .map(|name| fonts.join(name))
        .collect()
}

#[cfg(target_os = "macos")]
fn platform_fallback_candidates() -> Vec<PathBuf> {
    [
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
        "/System/Library/Fonts/Supplemental/Osaka.ttf",
    ]
    .iter()
    .map(PathBuf::from)
    .collect()
}

#[cfg(not(any(
    target_os = "linux",
    target_os = "freebsd",
    target_os = "android",
    target_os = "windows",
    target_os = "macos"
)))]
fn platform_fallback_candidates() -> Vec<PathBuf> {
    Vec::new()
}

/// Lazily parse the face at `idx`; `None` once the attempt failed for good.
fn load_face_locked(st: &mut FallbackState, idx: usize) -> Option<FontArc> {
    let face = st.chain.get_mut(idx)?;
    if let Some(font) = face.font.as_ref() {
        return font.clone();
    }
    let font = crate::resource::read_file_bytes(&face.path).ok().and_then(|bytes| {
        FontVec::try_from_vec_and_index(bytes, 0)
            .ok()
            .map(FontArc::from)
    });
    face.font = Some(font.clone());
    font
}

fn face_font_locked(st: &mut FallbackState, primary: &FontArc, face: u32) -> FontArc {
    if face == 0 {
        return primary.clone();
    }
    load_face_locked(st, face as usize - 1).unwrap_or_else(|| primary.clone())
}

fn log_resolution_locked(st: &mut FallbackState, primary_covers: bool, face: u32, ch: char) {
    if face == 0 {
        if primary_covers {
            st.stats_primary += 1;
        } else {
            st.stats_miss += 1;
        }
    } else {
        let idx = face as usize - 1;
        if st.stats_face_hits.len() <= idx {
            st.stats_face_hits.resize(idx + 1, 0);
        }
        st.stats_face_hits[idx] += 1;
    }
    st.resolved_chars += 1;

    let log_enabled = fallback_log_enabled();
    if !log_enabled || st.logged_chars.len() >= RESOLVE_CACHE_CAP {
        return;
    }
    if !st.logged_chars.insert(ch) {
        return;
    }
    let source = if face == 0 {
        "primary".to_string()
    } else {
        st.chain
            .get(face as usize - 1)
            .map(|f| format!("fallback{} {}", face, f.source))
            .unwrap_or_else(|| format!("fallback{face}"))
    };
    eprintln!(
        "[aetherkiri-font] U+{:04X} {:?} -> {}",
        ch as u32, ch, source
    );
    if st.resolved_chars % STATS_LOG_INTERVAL as u64 == 0 {
        let hits: Vec<String> = st
            .stats_face_hits
            .iter()
            .enumerate()
            .map(|(idx, hits)| format!("fallback{}={}", idx + 1, hits))
            .collect();
        eprintln!(
            "[aetherkiri-font] stats gen={} primary={} {} miss={} chars={}",
            st.generation,
            st.stats_primary,
            hits.join(" "),
            st.stats_miss,
            st.resolved_chars
        );
    }
}

/// Insert into a stamp-ordered LRU map, evicting the least recently used
/// entry once `cap` is exceeded. Eviction scans at most `cap` entries and only
/// runs on the insert that crosses the cap, so amortized cost is negligible.
fn insert_lru<K: Copy + Eq + std::hash::Hash, V>(
    map: &mut HashMap<K, (V, u64)>,
    key: K,
    value: (V, u64),
    cap: usize,
) {
    if map.len() >= cap && !map.contains_key(&key) {
        if let Some(oldest) = map
            .iter()
            .min_by_key(|(_, (_, stamp))| *stamp)
            .map(|(k, _)| *k)
        {
            map.remove(&oldest);
        }
    }
    map.insert(key, value);
}
