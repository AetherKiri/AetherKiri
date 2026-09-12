// SPDX-License-Identifier: MPL-2.0
// Included in save_state. Keep the original V1 binary layout readable and put
// resumable animation/text state in a separately versioned RFVS payload.
use crate::subsystem::resources::{
    motion_manager::HostMotionSnapshot, text_manager::HostTextPlayback,
};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub(crate) struct HostPlaybackSnapshot {
    motions: HostMotionSnapshot,
    text: Vec<HostTextPlayback>,
}

impl HostPlaybackSnapshot {
    fn capture(game: &GameData) -> Self {
        Self {
            motions: game.motion_manager.capture_host_motions(),
            text: game.motion_manager.text_manager.capture_host_playback(),
        }
    }

    fn apply(&self, game: &mut GameData) {
        game.motion_manager.apply_host_motions(&self.motions);
        game.motion_manager
            .text_manager
            .apply_host_playback(&self.text);
    }
}

#[derive(Serialize, Deserialize)]
struct HostSaveEnvelopeV2 {
    version: u16,
    base: SaveStateSnapshotV1,
    playback: HostPlaybackSnapshot,
}

#[cfg(not(feature = "no_std"))]
fn encode_host_snapshot(snapshot: &SaveStateSnapshotV1) -> Result<Vec<u8>> {
    match &snapshot.host_playback {
        Some(playback) => Ok(bincode_opts().serialize(&HostSaveEnvelopeV2 {
            version: 2,
            base: snapshot.clone(),
            playback: playback.clone(),
        })?),
        // Also used to round-trip a legacy snapshot without inventing lost state.
        None => Ok(bincode_opts().serialize(snapshot)?),
    }
}

#[cfg(not(feature = "no_std"))]
fn decode_host_snapshot(payload: &[u8]) -> Result<SaveStateSnapshotV1> {
    let version = payload.get(..2).map(|v| u16::from_le_bytes([v[0], v[1]]));
    match version {
        Some(1) => Ok(bincode_opts().deserialize(payload)?),
        Some(2) => {
            let envelope: HostSaveEnvelopeV2 = bincode_opts().deserialize(payload)?;
            let mut snapshot = envelope.base;
            snapshot.host_playback = Some(envelope.playback);
            Ok(snapshot)
        }
        _ => bail!("Unsupported or truncated rfvp save-state version: {version:?}"),
    }
}

#[cfg(all(test, not(feature = "no_std")))]
mod host_snapshot_tests {
    use super::*;
    use crate::subsystem::resources::motion_manager::MotionManager;

    #[test]
    fn versioned_payload_preserves_legacy_wire_format() {
        let motion = MotionManager::new();
        let mut snapshot = SaveStateSnapshotV1 {
            host_playback: None,
            version: 1,
            motion: motion.capture_snapshot_v1(),
            audio: AudioSnapshotV1 {
                bgm: BgmPlayerSnapshotV1 {
                    version: 1,
                    slots: vec![],
                },
                se: SePlayerSnapshotV1 {
                    version: 1,
                    slots: vec![],
                },
            },
            globals_non_volatile: vec![],
            vm: ThreadManager::new().capture_snapshot_v1(),
        };
        let legacy = bincode_opts().serialize(&snapshot).unwrap();
        assert_eq!(&legacy[..2], &[1, 0]);
        assert_eq!(encode_host_snapshot(&snapshot).unwrap(), legacy);
        let decoded = decode_host_snapshot(&legacy).unwrap();
        assert!(decoded.host_playback.is_none());
        assert_eq!(encode_host_snapshot(&decoded).unwrap(), legacy);

        snapshot.host_playback = Some(HostPlaybackSnapshot {
            motions: motion.capture_host_motions(),
            text: motion.text_manager.capture_host_playback(),
        });
        let current = encode_host_snapshot(&snapshot).unwrap();
        assert_eq!(&current[..2], &[2, 0]);
        let decoded = decode_host_snapshot(&current).unwrap();
        assert!(decoded.host_playback.is_some());
        assert_eq!(encode_host_snapshot(&decoded).unwrap(), current);
        assert_eq!(bincode_opts().serialize(&decoded).unwrap(), legacy);
        assert!(decode_host_snapshot(&current[..current.len() - 1]).is_err());
        assert!(decode_host_snapshot(&[3, 0]).is_err());
        assert!(decode_host_snapshot(&[2]).is_err());
    }

    #[test]
    fn fixed_motion_buffer_rejects_wrong_lengths() {
        #[derive(Serialize, Deserialize)]
        struct Buffer(#[serde(with = "host_array")] [u8; 64]);
        let bytes = bincode_opts().serialize(&Buffer([7; 64])).unwrap();
        assert_eq!(
            bincode_opts().deserialize::<Buffer>(&bytes).unwrap().0,
            [7; 64]
        );
        let short = bincode_opts().serialize(&vec![7u8; 63]).unwrap();
        assert!(bincode_opts().deserialize::<Buffer>(&short).is_err());
    }
}

// Serde's built-in array implementations stop at 32 entries. Snow's fixed
// buffers use a length-checked sequence; no pointer or machine address is saved.
pub(crate) mod host_array {
    use serde::{Deserialize, Deserializer, Serialize, Serializer};
    pub fn serialize<T: Serialize, S: Serializer, const N: usize>(
        items: &[T; N],
        serializer: S,
    ) -> Result<S::Ok, S::Error> {
        items.as_slice().serialize(serializer)
    }
    pub fn deserialize<'de, T: Deserialize<'de>, D: Deserializer<'de>, const N: usize>(
        deserializer: D,
    ) -> Result<[T; N], D::Error> {
        let items = Vec::<T>::deserialize(deserializer)?;
        items
            .try_into()
            .map_err(|_| serde::de::Error::custom("Invalid fixed motion buffer length"))
    }
}
