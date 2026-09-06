use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use blowfish::Blowfish;
use cipher::{BlockCipherDecrypt, KeyInit};
use encoding_rs::SHIFT_JIS;
use miniz_oxide::inflate::decompress_to_vec_zlib;

use crate::profile::{ArchiveProfile, GameProfile, TypePasswordProfile, detect_archive_profile};

const VERSION: u32 = 2;

#[derive(Debug, Clone)]
pub struct Entry {
    pub name: String,
    pub offset: u64,
    pub unpacked_size: u32,
    pub size: u32,
    pub aligned_size: u32,
    pub packed: bool,
    rc4_key: Option<Vec<u8>>,
}

struct ParsedIndex {
    entries: HashMap<String, Entry>,
    movie_key: Option<Vec<u8>>,
}

#[derive(Debug)]
pub struct PazArchive {
    path: PathBuf,
    stem: String,
    xor_key: u8,
    data_key: Option<Vec<u8>>,
    movie_key: Option<Vec<u8>>,
    parts: Vec<PathBuf>,
    entries: HashMap<String, Entry>,
}

impl PazArchive {
    pub fn open(path: impl AsRef<Path>) -> Result<Self, String> {
        let path = path.as_ref();
        let profile = path
            .parent()
            .and_then(detect_archive_profile)
            .ok_or("no archive profile matches the PAZ archive directory")?;
        Self::open_with_archive_profile(path, profile)
    }

    pub fn open_with_profile(
        path: impl AsRef<Path>,
        profile: &'static GameProfile,
    ) -> Result<Self, String> {
        Self::open_with_archive_profile(path, profile.archive)
    }

    pub fn open_with_archive_profile(
        path: impl AsRef<Path>,
        profile: &'static ArchiveProfile,
    ) -> Result<Self, String> {
        let path = path.as_ref().to_path_buf();
        let stem = path
            .file_stem()
            .and_then(|v| v.to_str())
            .ok_or("archive has no UTF-8 stem")?
            .to_ascii_lowercase();
        let keys = profile.archive_key(&stem).ok_or_else(|| {
            format!(
                "profile {} does not support PAZ archive: {stem}",
                profile.id
            )
        })?;
        let data_key = keys.data.map(hex).transpose()?;
        let mut file = File::open(&path).map_err(|e| format!("open {}: {e}", path.display()))?;
        file.seek(SeekFrom::Start(0x20))
            .map_err(|e| format!("seek index header: {e}"))?;
        let mut size_bytes = [0u8; 4];
        file.read_exact(&mut size_bytes)
            .map_err(|e| format!("read index size: {e}"))?;
        let encoded_size = u32::from_le_bytes(size_bytes);
        let xor_key = (encoded_size >> 24) as u8;
        if xor_key != 0 {
            for value in &mut size_bytes {
                *value ^= xor_key;
            }
        }
        let index_size = u32::from_le_bytes(size_bytes) as usize;
        if index_size == 0 || !index_size.is_multiple_of(8) {
            return Err(format!("invalid index size {index_size}"));
        }
        let mut index = vec![0u8; index_size];
        file.read_exact(&mut index)
            .map_err(|e| format!("read index: {e}"))?;
        if xor_key != 0 {
            for value in &mut index {
                *value ^= xor_key;
            }
        }
        blowfish_decrypt_le(&mut index, &hex(keys.index)?)?;
        let parsed = parse_index(&index, &stem, profile.type_passwords)?;
        let mut parts = Vec::new();
        for suffix in b'A'..=b'Z' {
            let part = PathBuf::from(format!("{}{}", path.display(), suffix as char));
            if !part.is_file() {
                break;
            }
            parts.push(part);
        }
        Ok(Self {
            path,
            stem,
            xor_key,
            data_key,
            movie_key: parsed.movie_key,
            parts,
            entries: parsed.entries,
        })
    }

    pub fn entries(&self) -> impl Iterator<Item = &Entry> {
        self.entries.values()
    }
    pub fn contains(&self, name: &str) -> bool {
        self.entries.contains_key(&normalize(name))
    }

    pub fn read(&self, name: &str) -> Result<Vec<u8>, String> {
        let entry = self
            .entries
            .get(&normalize(name))
            .ok_or_else(|| format!("{} not found in {}", name, self.stem))?;
        let mut data = read_virtual(
            &self.path,
            &self.parts,
            entry.offset,
            entry.aligned_size as usize,
        )?;
        if self.xor_key != 0 {
            for value in &mut data {
                *value ^= self.xor_key;
            }
        }
        if let Some(movie_key) = &self.movie_key {
            let entry_key = entry
                .rc4_key
                .as_ref()
                .ok_or_else(|| format!("movie entry has no key: {}", entry.name))?;
            movie_decrypt(&mut data, movie_key, entry_key);
        } else {
            let data_key = self
                .data_key
                .as_ref()
                .ok_or_else(|| format!("archive has no data key: {}", entry.name))?;
            blowfish_decrypt_le(&mut data, data_key)?;
            if let Some(key) = &entry.rc4_key {
                rc4_apply(&mut data, key, VERSION);
            }
        }
        data.truncate(entry.size as usize);
        if entry.packed {
            data = decompress_to_vec_zlib(&data)
                .map_err(|e| format!("inflate {}: {e:?}", entry.name))?;
        }
        let expected_size = entry.unpacked_size as usize;
        if data.len() > expected_size && data[expected_size..].iter().all(|&value| value == 0) {
            data.truncate(expected_size);
        }
        if data.len() != expected_size {
            return Err(format!(
                "{} decoded to {}, expected {}",
                entry.name,
                data.len(),
                entry.unpacked_size
            ));
        }
        Ok(data)
    }
}

#[derive(Debug, Default)]
pub struct Vfs {
    archives: Vec<PazArchive>,
}

impl Vfs {
    pub fn mount_game(root: impl AsRef<Path>) -> Result<Self, String> {
        let root = root.as_ref();
        let profile = detect_archive_profile(root)
            .ok_or("no Minori archive profile matches this installation")?;
        Self::mount_game_with_archive_profile(root, profile)
    }

    pub fn mount_game_with_profile(
        root: impl AsRef<Path>,
        profile: &'static GameProfile,
    ) -> Result<Self, String> {
        Self::mount_game_with_archive_profile(root, profile.archive)
    }

    pub fn mount_game_with_archive_profile(
        root: impl AsRef<Path>,
        profile: &'static ArchiveProfile,
    ) -> Result<Self, String> {
        let mut archives = Vec::new();
        for keys in profile.archive_keys {
            let path = root.as_ref().join(format!("{}.paz", keys.archive));
            if path.is_file() {
                archives.push(PazArchive::open_with_archive_profile(path, profile)?);
            }
        }
        if archives.is_empty() {
            return Err("no supported Minori PAZ archives found".into());
        }
        Ok(Self { archives })
    }

    pub fn read(&self, name: &str) -> Result<Vec<u8>, String> {
        for archive in self.archives.iter().rev() {
            if archive.contains(name) {
                return archive.read(name);
            }
        }
        Err(format!("resource not found: {name}"))
    }

    pub fn read_optional(&self, name: &str) -> Result<Option<Vec<u8>>, String> {
        for archive in self.archives.iter().rev() {
            if archive.contains(name) {
                return archive.read(name).map(Some);
            }
        }
        Ok(None)
    }

    pub fn entries(&self) -> impl Iterator<Item = (&str, &Entry)> {
        self.archives
            .iter()
            .flat_map(|a| a.entries().map(move |e| (a.stem.as_str(), e)))
    }
}

fn parse_index(
    data: &[u8],
    stem: &str,
    type_passwords: TypePasswordProfile,
) -> Result<ParsedIndex, String> {
    let mut cursor = Cursor { data, pos: 0 };
    let count = cursor.i32()?;
    if !(1..=1_000_000).contains(&count) {
        return Err(format!("invalid {stem} entry count {count}"));
    }
    let movie_key = if stem == "mov" {
        Some(cursor.take(0x100)?.to_vec())
    } else {
        None
    };
    let mut entries = HashMap::with_capacity(count as usize);
    for _ in 0..count {
        let raw_name = cursor.c_string()?;
        let (name, _, had_errors) = SHIFT_JIS.decode(&raw_name);
        if had_errors {
            return Err("invalid Shift-JIS PAZ entry name".into());
        }
        let name = name.into_owned();
        let offset = cursor.u64()?;
        let unpacked_size = cursor.u32()?;
        let size = cursor.u32()?;
        let aligned_size = cursor.u32()?;
        let packed = cursor.i32()? != 0;
        let password = type_password(
            &name,
            matches!(stem, "bgm" | "se" | "voice"),
            packed,
            type_passwords,
        );
        let rc4_key = password.map(|password| {
            let source = format!(
                "{} {:08X} {}",
                name.to_ascii_lowercase(),
                unpacked_size,
                password
            );
            let (bytes, _, _) = SHIFT_JIS.encode(&source);
            bytes.into_owned()
        });
        let entry = Entry {
            name: name.clone(),
            offset,
            unpacked_size,
            size,
            aligned_size,
            packed,
            rc4_key,
        };
        entries.insert(normalize(&name), entry);
    }
    Ok(ParsedIndex { entries, movie_key })
}

fn type_password(
    name: &str,
    audio: bool,
    packed: bool,
    passwords: TypePasswordProfile,
) -> Option<&'static str> {
    if packed {
        return None;
    }
    let lower = name.to_ascii_lowercase();
    if lower.ends_with(".png") {
        Some(passwords.png)
    } else if lower.ends_with(".ogg") || audio {
        Some(passwords.ogg)
    } else if lower.ends_with(".sc") {
        Some(passwords.sc)
    } else if lower.ends_with(".avi") || lower.ends_with(".mpg") || lower.ends_with(".mpeg") {
        Some(passwords.avi)
    } else {
        None
    }
}

fn normalize(name: &str) -> String {
    name.replace('\\', "/").to_ascii_lowercase()
}

pub(crate) fn blowfish_decrypt_le(data: &mut [u8], key: &[u8]) -> Result<(), String> {
    if !data.len().is_multiple_of(8) {
        return Err("Blowfish input is not block aligned".into());
    }
    let cipher =
        Blowfish::<byteorder::LE>::new_from_slice(key).map_err(|_| "invalid Blowfish key")?;
    for chunk in data.chunks_exact_mut(8) {
        let block: &mut [u8; 8] = chunk.try_into().unwrap();
        cipher.decrypt_block(block.into());
    }
    Ok(())
}

fn rc4_apply(data: &mut [u8], key: &[u8], version: u32) {
    let mut state = [0u8; 256];
    for (i, value) in state.iter_mut().enumerate() {
        *value = i as u8;
    }
    let mut s = 0usize;
    for i in 0..256 {
        s = (s + key[i % key.len()] as usize + state[i] as usize) & 0xff;
        state.swap(i, s);
    }
    let mut x = 0usize;
    let mut y = 0usize;
    let mut next = || {
        x = (x + 1) & 0xff;
        let a = state[x];
        y = (y + a as usize) & 0xff;
        let b = state[y];
        state[x] = b;
        state[y] = a;
        state[(a as usize + b as usize) & 0xff]
    };
    if version >= 2 {
        for _ in 0..((crc32fast::hash(key) >> 12) & 0xff) {
            let _ = next();
        }
    }
    for value in data {
        *value ^= next();
    }
}

fn movie_decrypt(data: &mut [u8], movie_key: &[u8], entry_key: &[u8]) {
    if data.is_empty() {
        return;
    }
    let key: Vec<u8> = movie_key
        .iter()
        .enumerate()
        .map(|(index, value)| value ^ entry_key[index % entry_key.len()])
        .collect();
    let mut block = vec![0; data.len().min(0x10000)];
    rc4_apply(&mut block, &key, 0);
    for (index, value) in data.iter_mut().enumerate() {
        *value ^= block[index % block.len()];
    }
}

fn hex(value: &str) -> Result<Vec<u8>, String> {
    if !value.len().is_multiple_of(2) {
        return Err("odd hex key length".into());
    }
    (0..value.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&value[i..i + 2], 16).map_err(|e| e.to_string()))
        .collect()
}

fn read_virtual(
    primary: &Path,
    parts: &[PathBuf],
    offset: u64,
    size: usize,
) -> Result<Vec<u8>, String> {
    let mut files = Vec::with_capacity(parts.len() + 1);
    files.push(primary.to_path_buf());
    files.extend_from_slice(parts);
    let mut remaining_offset = offset;
    let mut output = Vec::with_capacity(size);
    let mut remaining = size;
    for path in files {
        if remaining == 0 {
            break;
        }
        let length = std::fs::metadata(&path)
            .map_err(|e| format!("stat {}: {e}", path.display()))?
            .len();
        if remaining_offset >= length {
            remaining_offset -= length;
            continue;
        }
        let available = (length - remaining_offset).min(remaining as u64) as usize;
        let mut file = File::open(&path).map_err(|e| format!("open {}: {e}", path.display()))?;
        file.seek(SeekFrom::Start(remaining_offset))
            .map_err(|e| format!("seek {}: {e}", path.display()))?;
        let start = output.len();
        output.resize(start + available, 0);
        file.read_exact(&mut output[start..])
            .map_err(|e| format!("read {}: {e}", path.display()))?;
        remaining -= available;
        remaining_offset = 0;
    }
    if output.len() != size {
        return Err(format!(
            "virtual archive read at {offset} requested {size} bytes, got {}",
            output.len()
        ));
    }
    Ok(output)
}

struct Cursor<'a> {
    data: &'a [u8],
    pos: usize,
}
impl Cursor<'_> {
    fn take(&mut self, size: usize) -> Result<&[u8], String> {
        let end = self.pos.checked_add(size).ok_or("index overflow")?;
        let value = self.data.get(self.pos..end).ok_or("truncated PAZ index")?;
        self.pos = end;
        Ok(value)
    }
    fn u32(&mut self) -> Result<u32, String> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }
    fn i32(&mut self) -> Result<i32, String> {
        Ok(i32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }
    fn u64(&mut self) -> Result<u64, String> {
        Ok(u64::from_le_bytes(self.take(8)?.try_into().unwrap()))
    }
    fn c_string(&mut self) -> Result<Vec<u8>, String> {
        let tail = self.data.get(self.pos..).ok_or("truncated string")?;
        let length = tail
            .iter()
            .position(|v| *v == 0)
            .ok_or("unterminated entry name")?;
        let value = tail[..length].to_vec();
        self.pos += length + 1;
        Ok(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn virtual_read_spans_archive_parts() {
        let directory = tempfile::tempdir().unwrap();
        let primary = directory.path().join("bg.paz");
        let part_a = directory.path().join("bg.pazA");
        let part_b = directory.path().join("bg.pazB");
        std::fs::write(&primary, b"0123").unwrap();
        std::fs::write(&part_a, b"4567").unwrap();
        std::fs::write(&part_b, b"89AB").unwrap();
        let data = read_virtual(&primary, &[part_a, part_b], 2, 8).unwrap();
        assert_eq!(data, b"23456789");
    }

    #[test]
    fn virtual_read_rejects_truncated_archive_parts() {
        let directory = tempfile::tempdir().unwrap();
        let primary = directory.path().join("bg.paz");
        std::fs::write(&primary, b"0123").unwrap();
        let error = read_virtual(&primary, &[], 2, 3).unwrap_err();
        assert!(error.contains("requested 3 bytes, got 2"));
    }

    #[test]
    fn movie_cipher_repeats_the_first_64k_rc4_block() {
        let movie_key: Vec<u8> = (0..=255).collect();
        let entry_key = b"sppl_01_op.avi 066C5834 hYPH3Fxw";
        let mut data: Vec<u8> = (0..70_000).map(|value| value as u8).collect();
        let original = data.clone();
        movie_decrypt(&mut data, &movie_key, entry_key);
        assert_ne!(data, original);
        movie_decrypt(&mut data, &movie_key, entry_key);
        assert_eq!(data, original);
    }
}
