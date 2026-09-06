#include "siglus_audio_output.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "siglus_ffi.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define SIGLUS_AUDIO_DIAG(message) \
    __android_log_print(ANDROID_LOG_INFO, "AetherSiglusAudio", "%s", message)
#else
#define SIGLUS_AUDIO_DIAG(message) ((void)0)
#endif

namespace aetherkiri::siglus::audio {
    namespace {
        // ~21 ms of stereo audio per OpenAL buffer; four buffers keep roughly
        // 85 ms queued so scheduling jitter never reaches the listener.
        constexpr ALsizei kBufferFrames = 1024;
        constexpr size_t kBufferCount = 4;
        // The pump wakes ~every 8 ms; report stats about twice per second.
        constexpr uint64_t kHeartbeatPumps = 250;

        struct OutputState {
            std::atomic<uint64_t> consumed_samples { 0 };
            std::atomic<uint64_t> underruns { 0 };
            std::atomic<uint64_t> pumps { 0 };
            // True while the previous refill still received mixer data; lets
            // us count silence gaps as underruns only during active playback
            // instead of flagging quiet menu screens.
            std::atomic<bool> active { false };
        };

        OutputState g_state;
        std::mutex g_mutex;

        // Lifecycle objects owned between StartOutput and StopOutput. The
        // pump thread keeps the context current for its lifetime because an
        // AL context may be current on only one thread at a time.
        ALCdevice *g_device = nullptr;
        ALCcontext *g_context = nullptr;
        ALCcontext *g_previous_context = nullptr;
        ALuint g_source = 0;
        ALuint g_buffers[kBufferCount] = {};
        std::thread g_pump;
        std::atomic<bool> g_stop { false };
        uint32_t g_rate = 0;

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
            SIGLUS_AUDIO_DIAG("[siglus-audio] stats");
        }

        // The kira mixer renders interleaved stereo f32; OpenAL Soft accepts
        // that directly through AL_EXT_FLOAT32, but the plain AL 1.1 stereo16
        // format keeps the drain on the guaranteed core path for every
        // OpenAL build this app ships.
        void ConvertS16(const float *in, size_t sample_count, int16_t *out) {
            for(size_t i = 0; i < sample_count; ++i) {
                float clamped = in[i];
                if(clamped > 1.0f) {
                    clamped = 1.0f;
                } else if(clamped < -1.0f) {
                    clamped = -1.0f;
                }
                out[i] = static_cast<int16_t>(clamped * 32767.0f);
            }
        }

        // Refills one buffer from the Rust mixer FIFO, padding with silence
        // when the mixer has not produced data yet. Always leaves the buffer
        // queued so the processed-buffer bookkeeping stays uniform.
        void RefillBuffer(ALuint buffer) {
            float staging[kBufferFrames * 2];
            const int64_t got =
                siglus_ak_read_audio_f32(staging, kBufferFrames * 2);
            size_t samples =
                got > 0 ? static_cast<size_t>(got) : 0u;
            if(samples > kBufferFrames * 2) {
                samples = kBufferFrames * 2;
            }
            int16_t pcm[kBufferFrames * 2] = {};
            ConvertS16(staging, samples, pcm);
            alBufferData(buffer, AL_FORMAT_STEREO16, pcm,
                         static_cast<ALsizei>(sizeof(pcm)),
                         static_cast<ALsizei>(g_rate));
            alSourceQueueBuffers(g_source, 1, &buffer);

            const bool was_active = g_state.active.load(std::memory_order_relaxed);
            const bool is_active = samples > 0;
            if(was_active && !is_active) {
                g_state.underruns.fetch_add(1, std::memory_order_relaxed);
            }
            g_state.active.store(is_active, std::memory_order_relaxed);
            g_state.consumed_samples.fetch_add(samples,
                                               std::memory_order_relaxed);
        }

        void PumpLoop() {
            // An AL context is current on at most one thread; this pump owns
            // it for the lifetime of the output.
            alcMakeContextCurrent(g_context);
            alSourcePlay(g_source);

            while(!g_stop.load(std::memory_order_relaxed)) {
                ALint processed = 0;
                alGetSourcei(g_source, AL_BUFFERS_PROCESSED, &processed);
                while(processed-- > 0) {
                    ALuint buffer = 0;
                    alSourceUnqueueBuffers(g_source, 1, &buffer);
                    if(buffer != 0) {
                        RefillBuffer(buffer);
                    }
                }
                // A fully drained queue flips the source to STOPPED; kick it
                // again so playback resumes as soon as the mixer produces.
                ALint state = 0;
                alGetSourcei(g_source, AL_SOURCE_STATE, &state);
                if(state != AL_PLAYING) {
                    alSourcePlay(g_source);
                }
                const uint64_t pumps =
                    g_state.pumps.fetch_add(1, std::memory_order_relaxed) + 1;
                if(pumps % kHeartbeatPumps == 0) {
                    PrintStats("stats");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }

            alcMakeContextCurrent(nullptr);
        }

        // Precondition: g_mutex held.
        void StopLocked() {
            if(g_device == nullptr) {
                return;
            }
            g_stop.store(true, std::memory_order_relaxed);
            if(g_pump.joinable()) {
                g_pump.join();
            }
            if(g_context != nullptr) {
                alcMakeContextCurrent(g_context);
                if(g_source != 0) {
                    alSourceStop(g_source);
                    alDeleteSources(1, &g_source);
                    g_source = 0;
                }
                alDeleteBuffers(static_cast<ALsizei>(kBufferCount), g_buffers);
                for(ALuint &buffer : g_buffers) {
                    buffer = 0;
                }
                alcMakeContextCurrent(g_previous_context);
                alcDestroyContext(g_context);
                g_context = nullptr;
            }
            if(g_device != nullptr) {
                alcCloseDevice(g_device);
                g_device = nullptr;
            }
            PrintStats("stopped");
        }
    }  // namespace

    bool StartOutput(std::string &error) {
        std::lock_guard<std::mutex> lock(g_mutex);
        StopLocked();

        uint32_t rate = 0;
        uint32_t channels = 0;
        if(siglus_ak_audio_get_format(&rate, &channels) != SIGLUS_AK_OK ||
           rate == 0 || channels == 0 || channels != 2) {
            error = "rust mixer format unavailable";
            return false;
        }

        g_previous_context = alcGetCurrentContext();
        g_device = alcOpenDevice(nullptr);
        if(g_device == nullptr) {
            error = "alcOpenDevice failed";
            return false;
        }
        g_context = alcCreateContext(g_device, nullptr);
        if(g_context == nullptr) {
            error = "alcCreateContext failed";
            alcCloseDevice(g_device);
            g_device = nullptr;
            return false;
        }
        alcMakeContextCurrent(g_context);
        alDistanceModel(AL_NONE);
        alGenBuffers(static_cast<ALsizei>(kBufferCount), g_buffers);
        alGenSources(1, &g_source);
        if(alGetError() != AL_NO_ERROR) {
            error = "OpenAL object creation failed";
            if(g_source != 0) {
                alDeleteSources(1, &g_source);
                g_source = 0;
            }
            alDeleteBuffers(static_cast<ALsizei>(kBufferCount), g_buffers);
            for(ALuint &buffer : g_buffers) {
                buffer = 0;
            }
            alcMakeContextCurrent(g_previous_context);
            alcDestroyContext(g_context);
            g_context = nullptr;
            alcCloseDevice(g_device);
            g_device = nullptr;
            return false;
        }
        g_rate = rate;

        // Preload the full ring so playback starts with the deepest possible
        // buffer, then hand the context to the pump thread.
        for(const ALuint buffer : g_buffers) {
            RefillBuffer(buffer);
        }
        g_stop.store(false, std::memory_order_relaxed);
        g_pump = std::thread(PumpLoop);
        PrintStats("started");
        return true;
    }

    void StopOutput() {
        std::lock_guard<std::mutex> lock(g_mutex);
        StopLocked();
    }

}  // namespace aetherkiri::siglus::audio
