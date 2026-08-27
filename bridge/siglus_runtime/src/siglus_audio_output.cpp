#include "siglus_audio_output.h"

#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "siglus_ffi.h"

namespace aetherkiri::siglus::audio {
    namespace {
        // ~21 ms of stereo f32 at 48 kHz per callback; short enough to hide
        // jitter, long enough to stay cheap.
        constexpr uint32_t kSdlBufferFrames = 1024;
        // Print drain statistics roughly every 2 seconds of callbacks.
        constexpr uint64_t kHeartbeatCallbacks = 2u * 48000u / kSdlBufferFrames;

        struct OutputState {
            SDL_AudioDeviceID device = 0;
            std::atomic<uint64_t> consumed_samples { 0 };
            std::atomic<uint64_t> underruns { 0 };
            std::atomic<uint64_t> callbacks { 0 };
            // True while the previous callback still received mixer data;
            // lets us count silence gaps as underruns only during active
            // playback instead of flagging quiet menu screens.
            std::atomic<bool> active { false };
        };

        OutputState g_state;
        std::mutex g_mutex;

        void PrintStats(const char *tag) {
            uint64_t written = 0;
            uint64_t dropped = 0;
            (void)siglus_ak_audio_stats(&written, &dropped);
            std::fprintf(stderr,
                         "[siglus-audio] %s written_frames=%llu dropped_samples=%llu "
                         "consumed_samples=%llu underruns=%llu\n",
                         tag,
                         static_cast<unsigned long long>(written),
                         static_cast<unsigned long long>(dropped),
                         static_cast<unsigned long long>(
                             g_state.consumed_samples.load(std::memory_order_relaxed)),
                         static_cast<unsigned long long>(
                             g_state.underruns.load(std::memory_order_relaxed)));
        }

        void SDLCALL DrainCallback(void *, Uint8 *stream, int len) {
            auto *out = reinterpret_cast<float *>(stream);
            const size_t samples = static_cast<size_t>(len) / sizeof(float);
            const int64_t got =
                siglus_ak_read_audio_f32(out, samples);
            size_t drained = got > 0 ? static_cast<size_t>(got) : 0u;
            if(drained < samples) {
                std::memset(out + drained, 0,
                            (samples - drained) * sizeof(float));
            }
            const bool was_active =
                g_state.active.load(std::memory_order_relaxed);
            const bool is_active = drained > 0;
            if(was_active && !is_active) {
                g_state.underruns.fetch_add(1, std::memory_order_relaxed);
            }
            g_state.active.store(is_active, std::memory_order_relaxed);
            g_state.consumed_samples.fetch_add(drained,
                                               std::memory_order_relaxed);
            const uint64_t calls =
                g_state.callbacks.fetch_add(1, std::memory_order_relaxed) + 1;
            if(calls % kHeartbeatCallbacks == 0) {
                PrintStats("stats");
            }
        }

        // Precondition: g_mutex held.
        void StopLocked() {
            if(g_state.device == 0) {
                return;
            }
            SDL_CloseAudioDevice(g_state.device);
            g_state.device = 0;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            PrintStats("stopped");
        }
    }  // namespace

    bool StartOutput(std::string &error) {
        std::lock_guard<std::mutex> lock(g_mutex);
        StopLocked();

        uint32_t rate = 0;
        uint32_t channels = 0;
        if(siglus_ak_audio_get_format(&rate, &channels) != SIGLUS_AK_OK ||
           rate == 0 || channels == 0) {
            error = "rust mixer format unavailable";
            return false;
        }
        if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            error = std::string("SDL_InitSubSystem(AUDIO) failed: ")
                        += SDL_GetError();
            return false;
        }
        SDL_AudioSpec desired {};
        desired.freq = static_cast<int>(rate);
        desired.format = AUDIO_F32SYS;
        desired.channels = static_cast<Uint8>(channels);
        desired.samples = kSdlBufferFrames;
        desired.callback = DrainCallback;
        desired.userdata = &g_state;
        SDL_AudioSpec obtained {};
        g_state.device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
        if(g_state.device == 0) {
            error = std::string("SDL_OpenAudioDevice failed: ")
                        += SDL_GetError();
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        // Consume any stale samples buffered while no device was open.
        SDL_ClearQueuedAudio(g_state.device);
        SDL_PauseAudioDevice(g_state.device, 0);
        std::fprintf(stderr,
                     "[siglus-audio] sdl output opened: driver=%s freq=%d "
                     "channels=%u samples=%u\n",
                     SDL_GetCurrentAudioDriver(), obtained.freq,
                     obtained.channels, obtained.samples);
        return true;
    }

    void StopOutput() {
        std::lock_guard<std::mutex> lock(g_mutex);
        StopLocked();
    }

}  // namespace aetherkiri::siglus::audio
