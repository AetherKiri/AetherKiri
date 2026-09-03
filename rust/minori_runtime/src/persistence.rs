use std::collections::HashMap;
use std::fs;
use std::path::Path;

use blowfish::BlowfishLE;
use cipher::{BlockCipherDecrypt, BlockCipherEncrypt, KeyInit};
use encoding_rs::SHIFT_JIS;
use miniz_oxide::deflate::compress_to_vec_zlib;
use miniz_oxide::inflate::decompress_to_vec_zlib;

use crate::scene::ManualSceneState;
use crate::vm::{BgmRequest, MessageRequest};

const KEY: &[u8] = b"minori";
const MANUAL_SAVE_MAGIC: &[u8; 8] = b"MINORISV";

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct ManualSaveData {
    pub name: String,
    pub script: String,
    pub pc: Option<usize>,
    pub message: Option<MessageRequest>,
    pub bgm: Option<BgmRequest>,
    pub scene: Option<ManualSceneState>,
    pub globals: HashMap<String, i32>,
    pub locals: HashMap<String, i32>,
    pub control_enabled: bool,
    pub skip_enabled: bool,
    pub seen_messages: Vec<i32>,
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
}

pub(crate) fn encode_system_dat(globals: &HashMap<String, i32>) -> Result<Vec<u8>, String> {
    let mut entries = Vec::with_capacity(globals.len());
    for (name, value) in globals {
        let (encoded, _, had_errors) = SHIFT_JIS.encode(name);
        if had_errors {
            return Err(format!("global variable name is not Shift-JIS: {name}"));
        }
        entries.push((encoded.into_owned(), *value));
    }
    entries.sort_by(|left, right| left.0.cmp(&right.0));

    let mut plain = Vec::new();
    for (name, value) in entries {
        plain.extend_from_slice(&name);
        plain.push(b'\t');
        plain.extend_from_slice(value.to_string().as_bytes());
        plain.push(b'\n');
    }
    plain.push(0);

    let compressed = compress_to_vec_zlib(&plain, 6);
    let mut envelope = Vec::with_capacity(4 + compressed.len() + 7);
    envelope.extend_from_slice(
        &u32::try_from(plain.len())
            .map_err(|_| "system.dat plaintext is too large")?
            .to_le_bytes(),
    );
    envelope.extend_from_slice(&compressed);
    envelope.resize(envelope.len().next_multiple_of(8), 0);
    blowfish_encrypt(&mut envelope)?;
    Ok(envelope)
}

pub(crate) fn decode_system_dat(data: &[u8]) -> Result<HashMap<String, i32>, String> {
    if data.is_empty() || !data.len().is_multiple_of(8) {
        return Err("system.dat is not Blowfish block aligned".into());
    }
    let mut envelope = data.to_vec();
    blowfish_decrypt(&mut envelope)?;
    let expected_len = u32::from_le_bytes(envelope[..4].try_into().unwrap()) as usize;
    if expected_len == 0 {
        return Err("system.dat has a zero plaintext length".into());
    }
    let plain = decompress_to_vec_zlib(&envelope[4..])
        .map_err(|error| format!("system.dat zlib stream: {error:?}"))?;
    if plain.len() != expected_len {
        return Err(format!(
            "system.dat plaintext length is {}, expected {expected_len}",
            plain.len()
        ));
    }
    let Some(terminator) = plain.iter().position(|&value| value == 0) else {
        return Err("system.dat plaintext has no terminator".into());
    };
    if terminator + 1 != plain.len() {
        return Err("system.dat plaintext has bytes after its terminator".into());
    }

    let mut globals = HashMap::new();
    for line in plain[..terminator].split(|&value| value == b'\n') {
        if line.is_empty() {
            continue;
        }
        let Some(tab) = line.iter().position(|&value| value == b'\t') else {
            return Err("system.dat variable line has no tab".into());
        };
        let (name, _, name_errors) = SHIFT_JIS.decode(&line[..tab]);
        if name_errors || name.is_empty() {
            return Err("system.dat contains an invalid variable name".into());
        }
        let value = std::str::from_utf8(&line[tab + 1..])
            .map_err(|_| "system.dat contains a non-ASCII integer")?
            .parse::<i32>()
            .map_err(|_| "system.dat contains an invalid integer")?;
        globals.insert(name.into_owned(), value);
    }
    Ok(globals)
}

pub(crate) fn load_system_dat(path: &Path) -> Result<HashMap<String, i32>, String> {
    match fs::read(path) {
        Ok(data) => decode_system_dat(&data),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(HashMap::new()),
        Err(error) => Err(format!("read {}: {error}", path.display())),
    }
}

pub(crate) fn save_system_dat(path: &Path, globals: &HashMap<String, i32>) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("create {}: {error}", parent.display()))?;
    }
    let data = encode_system_dat(globals)?;
    fs::write(path, data).map_err(|error| format!("write {}: {error}", path.display()))
}

pub(crate) fn load_manual_slot(path: &Path) -> Result<Option<ManualSaveData>, String> {
    match fs::read(path) {
        Ok(data) => decode_manual_save(&data).map(Some),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(format!("read {}: {error}", path.display())),
    }
}

pub(crate) fn save_manual_slot(path: &Path, save: &ManualSaveData) -> Result<(), String> {
    if save.rgba.len() != save.width as usize * save.height as usize * 4 {
        return Err("manual save frame size is invalid".into());
    }
    let mut payload = Vec::new();
    push_string(&mut payload, &save.name)?;
    push_string(&mut payload, &save.script)?;
    payload.extend_from_slice(
        &u64::try_from(save.pc.ok_or("manual save has no program counter")?)
            .map_err(|_| "manual save program counter is too large")?
            .to_le_bytes(),
    );
    push_variables(&mut payload, &save.globals)?;
    push_variables(&mut payload, &save.locals)?;
    payload.push(u8::from(save.control_enabled));
    payload.push(u8::from(save.skip_enabled));
    payload.extend_from_slice(
        &u32::try_from(save.seen_messages.len())
            .map_err(|_| "too many seen messages")?
            .to_le_bytes(),
    );
    for id in &save.seen_messages {
        payload.extend_from_slice(&id.to_le_bytes());
    }
    match &save.message {
        Some(message) => {
            payload.push(1);
            payload.extend_from_slice(&message.id.to_le_bytes());
            payload.push(u8::from(message.read));
            push_string(&mut payload, &message.voice)?;
            push_string(&mut payload, &message.speaker)?;
            push_string(&mut payload, &message.text)?;
        }
        None => payload.push(0),
    }
    match &save.bgm {
        Some(bgm) => {
            payload.push(1);
            push_string(&mut payload, &bgm.name)?;
            payload.extend_from_slice(&bgm.fade_in.to_le_bytes());
            payload.extend_from_slice(&bgm.fade_out.to_le_bytes());
            payload.extend_from_slice(&bgm.volume.to_le_bytes());
        }
        None => payload.push(0),
    }
    match &save.scene {
        Some(scene) => {
            payload.push(1);
            let encoded = bincode::serialize(scene)
                .map_err(|error| format!("encode manual scene: {error}"))?;
            payload.extend_from_slice(
                &u32::try_from(encoded.len())
                    .map_err(|_| "manual scene is too large")?
                    .to_le_bytes(),
            );
            payload.extend_from_slice(&encoded);
        }
        None => payload.push(0),
    }
    payload.extend_from_slice(&save.width.to_le_bytes());
    payload.extend_from_slice(&save.height.to_le_bytes());
    payload.extend_from_slice(&save.rgba);

    let compressed = compress_to_vec_zlib(&payload, 6);
    let mut output = Vec::with_capacity(16 + compressed.len());
    output.extend_from_slice(MANUAL_SAVE_MAGIC);
    output.extend_from_slice(&4_u32.to_le_bytes());
    output.extend_from_slice(
        &u32::try_from(payload.len())
            .map_err(|_| "manual save is too large")?
            .to_le_bytes(),
    );
    output.extend_from_slice(&compressed);
    decode_manual_save(&output)?;
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("create {}: {error}", parent.display()))?;
    }
    fs::write(path, output).map_err(|error| format!("write {}: {error}", path.display()))
}

pub(crate) fn decode_manual_save(data: &[u8]) -> Result<ManualSaveData, String> {
    if data.len() < 16 || &data[..8] != MANUAL_SAVE_MAGIC {
        return Err("manual save header is invalid".into());
    }
    let version = u32::from_le_bytes(data[8..12].try_into().unwrap());
    if !matches!(version, 1..=4) {
        return Err("manual save version is unsupported".into());
    }
    let expected = u32::from_le_bytes(data[12..16].try_into().unwrap()) as usize;
    let payload = decompress_to_vec_zlib(&data[16..])
        .map_err(|error| format!("manual save zlib stream: {error:?}"))?;
    if payload.len() != expected {
        return Err("manual save payload length is invalid".into());
    }
    let mut offset = 0;
    let name = take_string(&payload, &mut offset)?;
    let script = take_string(&payload, &mut offset)?;
    let (pc, message, globals, locals, control_enabled, skip_enabled, seen_messages) =
        if version == 1 {
            let message_id = take_i32(&payload, &mut offset)?;
            let globals = take_variables(&payload, &mut offset)?;
            (
                None,
                (message_id != 0).then_some(MessageRequest {
                    id: message_id,
                    voice: String::new(),
                    speaker: String::new(),
                    text: String::new(),
                    read: false,
                }),
                globals,
                HashMap::new(),
                true,
                false,
                Vec::new(),
            )
        } else {
            let pc = usize::try_from(take_u64(&payload, &mut offset)?)
                .map_err(|_| "manual save program counter is too large")?;
            let globals = take_variables(&payload, &mut offset)?;
            let locals = take_variables(&payload, &mut offset)?;
            let control_enabled = take_byte(&payload, &mut offset)? != 0;
            let skip_enabled = take_byte(&payload, &mut offset)? != 0;
            let seen_count = take_u32(&payload, &mut offset)? as usize;
            let mut seen_messages = Vec::with_capacity(seen_count);
            for _ in 0..seen_count {
                seen_messages.push(take_i32(&payload, &mut offset)?);
            }
            let message = if take_byte(&payload, &mut offset)? != 0 {
                Some(MessageRequest {
                    id: take_i32(&payload, &mut offset)?,
                    read: take_byte(&payload, &mut offset)? != 0,
                    voice: take_string(&payload, &mut offset)?,
                    speaker: take_string(&payload, &mut offset)?,
                    text: take_string(&payload, &mut offset)?,
                })
            } else {
                None
            };
            (
                Some(pc),
                message,
                globals,
                locals,
                control_enabled,
                skip_enabled,
                seen_messages,
            )
        };
    let bgm = if version >= 3 && take_byte(&payload, &mut offset)? != 0 {
        Some(BgmRequest {
            name: take_string(&payload, &mut offset)?,
            fade_in: take_i32(&payload, &mut offset)?,
            fade_out: take_i32(&payload, &mut offset)?,
            volume: take_i32(&payload, &mut offset)?,
        })
    } else {
        None
    };
    let scene = if version >= 4 && take_byte(&payload, &mut offset)? != 0 {
        let len = take_u32(&payload, &mut offset)? as usize;
        if offset + len > payload.len() {
            return Err("manual scene is truncated".into());
        }
        let scene = bincode::deserialize(&payload[offset..offset + len])
            .map_err(|error| format!("decode manual scene: {error}"))?;
        offset += len;
        Some(scene)
    } else {
        None
    };
    let width = take_u32(&payload, &mut offset)?;
    let height = take_u32(&payload, &mut offset)?;
    let frame_len = width as usize * height as usize * 4;
    if offset + frame_len != payload.len() {
        return Err("manual save frame length is invalid".into());
    }
    Ok(ManualSaveData {
        name,
        script,
        pc,
        message,
        bgm,
        scene,
        globals,
        locals,
        control_enabled,
        skip_enabled,
        seen_messages,
        width,
        height,
        rgba: payload[offset..].to_vec(),
    })
}

fn push_variables(output: &mut Vec<u8>, variables: &HashMap<String, i32>) -> Result<(), String> {
    let mut entries: Vec<_> = variables.iter().collect();
    entries.sort_by_key(|(name, _)| name.as_str());
    output.extend_from_slice(
        &u32::try_from(entries.len())
            .map_err(|_| "too many manual save variables")?
            .to_le_bytes(),
    );
    for (name, value) in entries {
        push_string(output, name)?;
        output.extend_from_slice(&value.to_le_bytes());
    }
    Ok(())
}

fn take_variables(data: &[u8], offset: &mut usize) -> Result<HashMap<String, i32>, String> {
    let count = take_u32(data, offset)? as usize;
    let mut variables = HashMap::with_capacity(count);
    for _ in 0..count {
        variables.insert(take_string(data, offset)?, take_i32(data, offset)?);
    }
    Ok(variables)
}

fn take_byte(data: &[u8], offset: &mut usize) -> Result<u8, String> {
    let value = *data
        .get(*offset)
        .ok_or("manual save payload is truncated")?;
    *offset += 1;
    Ok(value)
}

fn take_u64(data: &[u8], offset: &mut usize) -> Result<u64, String> {
    if *offset + 8 > data.len() {
        return Err("manual save payload is truncated".into());
    }
    let value = u64::from_le_bytes(data[*offset..*offset + 8].try_into().unwrap());
    *offset += 8;
    Ok(value)
}

fn push_string(output: &mut Vec<u8>, value: &str) -> Result<(), String> {
    output.extend_from_slice(
        &u32::try_from(value.len())
            .map_err(|_| "manual save string is too long")?
            .to_le_bytes(),
    );
    output.extend_from_slice(value.as_bytes());
    Ok(())
}

fn take_u32(data: &[u8], offset: &mut usize) -> Result<u32, String> {
    if *offset + 4 > data.len() {
        return Err("manual save payload is truncated".into());
    }
    let value = u32::from_le_bytes(data[*offset..*offset + 4].try_into().unwrap());
    *offset += 4;
    Ok(value)
}

fn take_i32(data: &[u8], offset: &mut usize) -> Result<i32, String> {
    Ok(i32::from_le_bytes(take_u32(data, offset)?.to_le_bytes()))
}

fn take_string(data: &[u8], offset: &mut usize) -> Result<String, String> {
    let len = take_u32(data, offset)? as usize;
    if *offset + len > data.len() {
        return Err("manual save string is truncated".into());
    }
    let value = std::str::from_utf8(&data[*offset..*offset + len])
        .map_err(|_| "manual save string is not UTF-8")?
        .to_string();
    *offset += len;
    Ok(value)
}

fn blowfish_encrypt(data: &mut [u8]) -> Result<(), String> {
    let cipher = BlowfishLE::new_from_slice(KEY).map_err(|_| "invalid system.dat Blowfish key")?;
    for chunk in data.chunks_exact_mut(8) {
        let block: &mut [u8; 8] = chunk.try_into().unwrap();
        cipher.encrypt_block(block.into());
    }
    Ok(())
}

fn blowfish_decrypt(data: &mut [u8]) -> Result<(), String> {
    let cipher = BlowfishLE::new_from_slice(KEY).map_err(|_| "invalid system.dat Blowfish key")?;
    for chunk in data.chunks_exact_mut(8) {
        let block: &mut [u8; 8] = chunk.try_into().unwrap();
        cipher.decrypt_block(block.into());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn manual_save_round_trips_metadata_globals_and_frame() {
        let save = ManualSaveData {
            name: "春の森".into(),
            script: "A00_01.sc".into(),
            pc: Some(37),
            message: Some(MessageRequest {
                id: 140,
                voice: "voice-1".into(),
                speaker: "アリス".into(),
                text: "春".into(),
                read: true,
            }),
            bgm: Some(BgmRequest {
                name: "theme.ogg".into(),
                fade_in: 10,
                fade_out: 20,
                volume: 80,
            }),
            scene: Some(ManualSceneState {
                background_rgba: vec![9; 8],
                panel: None,
                characters: Vec::new(),
                rand_state: 7,
            }),
            globals: HashMap::from([("FLAG".into(), 3)]),
            locals: HashMap::from([("LOCAL".into(), -2)]),
            control_enabled: true,
            skip_enabled: false,
            seen_messages: vec![100, 140],
            width: 2,
            height: 1,
            rgba: vec![1, 2, 3, 255, 4, 5, 6, 255],
        };
        let path = std::env::temp_dir().join(format!(
            "minori-save-test-{}-{}.sav",
            std::process::id(),
            std::thread::current().name().unwrap_or("thread")
        ));
        save_manual_slot(&path, &save).unwrap();
        let encoded = fs::read(&path).unwrap();
        let _ = fs::remove_file(path);
        assert_eq!(decode_manual_save(&encoded).unwrap(), save);
        assert!(decode_manual_save(&encoded[..15]).is_err());
    }

    #[test]
    fn system_dat_round_trips_sorted_shift_jis_globals() {
        let globals = HashMap::from([("Z_FLAG".into(), -2), ("A_FLAG".into(), 17)]);
        let encoded = encode_system_dat(&globals).unwrap();
        assert!(encoded.len().is_multiple_of(8));
        assert_ne!(&encoded[..4], &(17_u32.to_le_bytes()));
        assert_eq!(decode_system_dat(&encoded).unwrap(), globals);
    }

    #[test]
    fn system_dat_rejects_unaligned_or_corrupt_envelopes() {
        assert!(decode_system_dat(&[0; 7]).is_err());
        assert!(decode_system_dat(&[0; 8]).is_err());
    }
}
