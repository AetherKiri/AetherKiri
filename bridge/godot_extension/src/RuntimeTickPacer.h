#ifndef AETHERKIRI_GODOT_EXTENSION_RUNTIME_TICK_PACER_H_
#define AETHERKIRI_GODOT_EXTENSION_RUNTIME_TICK_PACER_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace aetherkiri::godot_host {

class ArtemisLogicalFramePacer {
public:
    struct Step {
        bool should_tick = false;
        double delta_seconds = 0.0;
    };

    Step Advance(double host_delta_seconds) {
        if (!std::isfinite(host_delta_seconds) || host_delta_seconds <= 0.0) {
            return {};
        }

        phase_seconds_ += host_delta_seconds;
        pending_seconds_ += host_delta_seconds;
        constexpr double kBoundaryEpsilonSeconds = 1e-9;
        const double completed_frames = std::floor(
            (phase_seconds_ + kBoundaryEpsilonSeconds) / kLogicalFrameSeconds);
        if (completed_frames < 1.0) return {};

        // A slow host still gets one runtime update per presented frame. Dropping
        // additional catch-up callbacks avoids a burst after a load or resume,
        // while the full elapsed time remains available to time-based effects.
        phase_seconds_ -= completed_frames * kLogicalFrameSeconds;
        phase_seconds_ = std::max(0.0, phase_seconds_);
        const double elapsed_seconds = pending_seconds_;
        pending_seconds_ = 0.0;
        return {true, elapsed_seconds};
    }

    void Reset() {
        phase_seconds_ = 0.0;
        pending_seconds_ = 0.0;
    }

    static constexpr double logical_frame_seconds() {
        return kLogicalFrameSeconds;
    }

private:
    static constexpr double kLogicalFrameSeconds = 1.0 / 60.0;
    double phase_seconds_ = 0.0;
    double pending_seconds_ = 0.0;
};

class RuntimeTickMillisecondQuantizer {
public:
    uint32_t Quantize(double delta_seconds) {
        if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0) return 0;
        const double maximum =
            static_cast<double>(std::numeric_limits<uint32_t>::max());
        const double exact_milliseconds =
            delta_seconds * 1000.0 + remainder_milliseconds_;
        if (exact_milliseconds >= maximum) {
            remainder_milliseconds_ = 0.0;
            return std::numeric_limits<uint32_t>::max();
        }
        const double rounded_milliseconds = std::round(exact_milliseconds);
        const auto result = static_cast<uint32_t>(
            std::clamp(rounded_milliseconds, 0.0, maximum));
        remainder_milliseconds_ = exact_milliseconds - rounded_milliseconds;
        return result;
    }

    void Reset() { remainder_milliseconds_ = 0.0; }

private:
    double remainder_milliseconds_ = 0.0;
};

}  // namespace aetherkiri::godot_host

#endif  // AETHERKIRI_GODOT_EXTENSION_RUNTIME_TICK_PACER_H_
