#ifndef AETHERKIRI_SIGLUS_AUDIO_OUTPUT_H_
#define AETHERKIRI_SIGLUS_AUDIO_OUTPUT_H_

#include <string>

namespace aetherkiri::siglus::audio {

    // Opens an OpenAL streaming output draining the Rust mixer FIFO through
    // the siglus_ak_read_audio_f32 FFI (see siglus_ffi.h). Idempotent: an
    // already running output is restarted. Returns false with details on
    // failure.
    bool StartOutput(std::string &error);

    // Closes the stream and logs cumulative drained/underrun statistics.
    // Safe to call when not started.
    void StopOutput();

}  // namespace aetherkiri::siglus::audio

#endif  // AETHERKIRI_SIGLUS_AUDIO_OUTPUT_H_
