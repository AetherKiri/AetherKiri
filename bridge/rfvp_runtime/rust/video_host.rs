// SPDX-License-Identifier: MPL-2.0
// Included in the build-tree videoplayer module to keep its private backend
// types behind the host adapter. Decoder queues are bounded upstream.
impl VideoPlayerManager {
    pub fn pause_host(&mut self, paused: bool, elapsed: std::time::Duration) {
        let Some(playback) = self.playback.as_mut() else {
            return;
        };
        let (clock, sound) = match playback {
            #[cfg(feature = "mp4")]
            Playback::Mp4(p) => (&mut p.started_at, &mut p.audio_handle),
            Playback::Wmv(p) => (&mut p.started_at, &mut p.audio_handle),
            Playback::Mpeg(p) => (&mut p.started_at, &mut p.audio_handle),
        };
        if !paused {
            if let Some(started) = clock.as_mut() {
                *started += elapsed;
            }
        }
        if let Some(sound) = sound.as_mut() {
            if paused {
                sound.pause(Tween::default());
            } else {
                sound.resume(Tween::default());
            }
        }
    }
}
