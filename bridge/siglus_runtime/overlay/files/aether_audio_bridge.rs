//! Kira PCM output bridge for AetherKiri.
//!
//! Replaces the cpal device backend with an in-process mixer backend: kira
//! renders into a lock-protected FIFO of interleaved stereo f32 samples and
//! the C++ provider (`bridge/siglus_runtime/src/siglus_audio_output.cpp`)
//! drains it through the FFI below and feeds it to an SDL2 audio device.
//!
//! This sidesteps ALSA/cpal device selection entirely (the reason embedded
//! startup logged "snd_pcm_open failed ... No such device") while keeping all
//! decoding/mixing/fade logic in kira unchanged.

use kira::manager::{AudioManager, AudioManagerSettings};

/// Renderer runs at this fixed rate; SDL opens its device at the same rate
/// (PipeWire/Pulse resample natively if the sink differs).
pub const HOST_SAMPLE_RATE: u32 = 48000;

// ---------------------------------------------------------------------------
// Platform-neutral aliases consumed by audio/kira_hub.rs
// ---------------------------------------------------------------------------

#[cfg(not(target_arch = "wasm32"))]
pub type HostBackend = PcmBackend;
#[cfg(target_arch = "wasm32")]
pub type HostBackend = kira::manager::backend::mock::MockBackend;

#[cfg(not(target_arch = "wasm32"))]
pub type HostAudioManagerSettings = AudioManagerSettings<PcmBackend>;
#[cfg(target_arch = "wasm32")]
pub type HostAudioManagerSettings = AudioManagerSettings<HostBackend>;

#[cfg(not(target_arch = "wasm32"))]
pub type HostAudioManager = AudioManager<PcmBackend>;
#[cfg(target_arch = "wasm32")]
pub type HostAudioManager = AudioManager<HostBackend>;

// ---------------------------------------------------------------------------
// PCM backend + FIFO (desktop only)
// ---------------------------------------------------------------------------

#[cfg(not(target_arch = "wasm32"))]
mod pcm {
    use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
    use std::sync::{Arc, Mutex, RwLock};
    use std::time::{Duration, Instant};

    use super::HOST_SAMPLE_RATE;
    use kira::Frame;
    use kira::manager::backend::{Backend, Renderer};

    const RING_CAPACITY_FRAMES: usize = HOST_SAMPLE_RATE as usize * 2; // 2 s
    const CHUNK_FRAMES: usize = 240; // 5 ms render slices

    /// Settings accepted by [`AudioManager`]. Default renders at 48 kHz.
    #[derive(Debug, Clone, Copy)]
    pub struct PcmBackendSettings {
        pub sample_rate: u32,
    }

    /// Setup can only fail if the caller forces a zero sample rate and even
    /// then kira keeps running silently; surfaced for `AudioManager::new`.
    #[derive(Debug)]
    pub struct PcmBackendError;

    impl std::fmt::Display for PcmBackendError {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str("kira pcm backend unavailable")
        }
    }

    impl std::error::Error for PcmBackendError {}

    impl Default for PcmBackendSettings {
        fn default() -> Self {
            Self {
                sample_rate: HOST_SAMPLE_RATE,
            }
        }
    }

    /// Interleaved stereo f32 FIFO shared with the C++ consumer over FFI.
    struct AudioSink {
        inner: Mutex<SinkState>,
        stop: AtomicBool,
    }

    struct SinkState {
        buf: Box<[f32]>,
        read_pos: usize,
        write_pos: usize,
        count: usize,
    }

    impl AudioSink {
        fn new() -> Self {
            Self {
                inner: Mutex::new(SinkState {
                    buf: vec![0.0; RING_CAPACITY_FRAMES * 2].into_boxed_slice(),
                    read_pos: 0,
                    write_pos: 0,
                    count: 0,
                }),
                stop: AtomicBool::new(false),
            }
        }

        /// Pushes interleaved samples, overwriting the oldest data when full.
        fn push(&self, chunk: &[f32]) {
            let mut state = match self.inner.lock() {
                Ok(s) => s,
                Err(poisoned) => poisoned.into_inner(),
            };
            let cap = state.buf.len();
            let mut dropped = chunk.len().saturating_sub(cap);
            for &sample in chunk {
                if state.count == cap {
                    state.read_pos = (state.read_pos + 1) % cap;
                    state.count -= 1;
                    dropped += 1;
                }
                let write_pos = state.write_pos;
                state.buf[write_pos] = sample;
                state.write_pos = (write_pos + 1) % cap;
                state.count += 1;
            }
            TOTAL_DROPPED_SAMPLES.fetch_add(dropped as u64, Ordering::Relaxed);
        }

        /// Copies up to `out.len()` interleaved samples; returns how many were
        /// actually written.
        fn drain_into(&self, out: &mut [f32]) -> usize {
            let mut state = match self.inner.lock() {
                Ok(s) => s,
                Err(poisoned) => poisoned.into_inner(),
            };
            let want = out.len().min(state.count);
            let cap = state.buf.len();
            for item in out.iter_mut().take(want) {
                *item = state.buf[state.read_pos];
                state.read_pos = (state.read_pos + 1) % cap;
            }
            state.count -= want;
            want
        }
    }

    static CURRENT_SINK: RwLock<Option<Arc<AudioSink>>> = RwLock::new(None);
    static TOTAL_WRITTEN_FRAMES: AtomicU64 = AtomicU64::new(0);
    static TOTAL_DROPPED_SAMPLES: AtomicU64 = AtomicU64::new(0);

    fn register_sink(sink: Arc<AudioSink>) {
        if let Ok(mut guard) = CURRENT_SINK.write() {
            *guard = Some(sink);
        }
    }

    fn current_sink() -> Option<Arc<AudioSink>> {
        CURRENT_SINK.read().ok()?.clone()
    }

    enum State {
        Uninitialized,
        Started { stop: Arc<AtomicBool> },
    }

    /// A [`Backend`] that renders the mix on a private thread into
    /// [`CURRENT_SINK`], paced by wall-clock time so game-side fades/tweens
    /// advance at real speed without touching any audio device.
    pub struct PcmBackend {
        sample_rate: u32,
        state: State,
    }

    impl Backend for PcmBackend {
        type Settings = PcmBackendSettings;

        type Error = PcmBackendError;

        fn setup(settings: Self::Settings) -> Result<(Self, u32), Self::Error> {
            let rate = if settings.sample_rate > 0 {
                settings.sample_rate
            } else {
                HOST_SAMPLE_RATE
            };
            Ok((
                Self {
                    sample_rate: rate,
                    state: State::Uninitialized,
                },
                rate,
            ))
        }

        fn start(&mut self, renderer: Renderer) -> Result<(), Self::Error> {
            let stop = Arc::new(AtomicBool::new(false));
            let sink = Arc::new(AudioSink::new());
            register_sink(Arc::clone(&sink));
            let renderer = Arc::new(Mutex::new(renderer));
            let stop_for_thread = Arc::clone(&stop);
            std::thread::Builder::new()
                .name("kira-pcm-render".to_string())
                .spawn(move || render_loop(renderer, sink, stop_for_thread))
                .map(|_| ())
                .map_err(|_| PcmBackendError)?;
            // Dropping the manager (game close/re-open) halts its render
            // thread through this flag; the sink dies with the thread.
            self.state = State::Started { stop };
            Ok(())
        }
    }

    impl Drop for PcmBackend {
        fn drop(&mut self) {
            if let State::Started { stop } = &self.state {
                stop.store(true, Ordering::SeqCst);
            }
        }
    }

    fn render_loop(
        renderer: Arc<Mutex<Renderer>>,
        sink: Arc<AudioSink>,
        stop: Arc<AtomicBool>,
    ) {
        let mut chunk: Vec<f32> = Vec::with_capacity(CHUNK_FRAMES * 2);
        let mut next_deadline = Instant::now();
        const SLICE: Duration = Duration::from_millis(5);
        loop {
            if stop.load(Ordering::SeqCst) || sink.stop.load(Ordering::SeqCst) {
                return;
            }
            let mut renderer = match renderer.try_lock() {
                Ok(r) => r,
                Err(_) => {
                    std::thread::sleep(Duration::from_millis(1));
                    continue;
                }
            };
            chunk.clear();
            renderer.on_start_processing();
            for _ in 0..CHUNK_FRAMES {
                let Frame { left, right } = renderer.process();
                chunk.push(left);
                chunk.push(right);
            }
            drop(renderer);
            sink.push(&chunk);
            TOTAL_WRITTEN_FRAMES.fetch_add(CHUNK_FRAMES as u64, Ordering::Relaxed);

            next_deadline += SLICE;
            let now = Instant::now();
            if next_deadline <= now {
                // Fell behind (scheduler stall); resynchronize instead of
                // rendering a catch-up burst that would corrupt pacing.
                next_deadline = now;
            } else {
                std::thread::sleep(next_deadline - now);
            }
        }
    }

    const SIGLUS_AK_OK: i32 = 0;
    const SIGLUS_AK_INVALID_ARGUMENT: i32 = -1;
    const SIGLUS_AK_INVALID_STATE: i32 = -2;

    // -- FFI consumed by siglus_audio_output.cpp ----------------------------

    /// # Safety
    /// Out pointers must be valid when non-null.
    #[no_mangle]
    pub unsafe extern "C" fn siglus_ak_audio_get_format(
        out_sample_rate: *mut u32,
        out_channels: *mut u32,
    ) -> i32 {
        if !out_sample_rate.is_null() {
            *out_sample_rate = HOST_SAMPLE_RATE;
        }
        if !out_channels.is_null() {
            *out_channels = 2;
        }
        SIGLUS_AK_OK
    }

    /// Drains up to `out_capacity_samples` interleaved stereo f32 samples.
    /// Returns the number of samples written, or a negative SIGLUS_AK_* code.
    ///
    /// # Safety
    /// `out_samples` must point to `out_capacity_samples` writable floats.
    #[no_mangle]
    pub unsafe extern "C" fn siglus_ak_read_audio_f32(
        out_samples: *mut f32,
        out_capacity_samples: usize,
    ) -> i64 {
        if out_samples.is_null() || out_capacity_samples == 0 {
            return SIGLUS_AK_INVALID_ARGUMENT as i64;
        }
        let Some(sink) = current_sink() else {
            return SIGLUS_AK_INVALID_STATE as i64;
        };
        let out = std::slice::from_raw_parts_mut(out_samples, out_capacity_samples);
        sink.drain_into(out) as i64
    }

    /// Reports cumulative mixer counters for diagnostics.
    ///
    /// # Safety
    /// Out pointers must be valid when non-null.
    #[no_mangle]
    pub unsafe extern "C" fn siglus_ak_audio_stats(
        out_written_frames: *mut u64,
        out_dropped_samples: *mut u64,
    ) -> i32 {
        if !out_written_frames.is_null() {
            *out_written_frames = TOTAL_WRITTEN_FRAMES.load(Ordering::Relaxed);
        }
        if !out_dropped_samples.is_null() {
            *out_dropped_samples = TOTAL_DROPPED_SAMPLES.load(Ordering::Relaxed);
        }
        SIGLUS_AK_OK
    }
}

#[cfg(not(target_arch = "wasm32"))]
pub use pcm::{
    siglus_ak_audio_get_format, siglus_ak_audio_stats, siglus_ak_read_audio_f32, PcmBackend,
};

// No threads exist on wasm32-unknown-unknown, so there is no PCM bridge
// implementation there; the type aliases above fall back to the mock backend,
// matching the previous "audio unavailable" behavior on web builds.
