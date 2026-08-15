#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../../bridge/godot_extension/src/RuntimeTickPacer.h"

#include <cstdint>

namespace {

using aetherkiri::godot_host::ArtemisLogicalFramePacer;
using aetherkiri::godot_host::RuntimeTickMillisecondQuantizer;

struct PacerResult {
    int ticks = 0;
    double delivered_seconds = 0.0;
};

PacerResult RunPacer(double refresh_rate, int host_frames) {
    ArtemisLogicalFramePacer pacer;
    PacerResult result;
    for (int frame = 0; frame < host_frames; ++frame) {
        const auto step = pacer.Advance(1.0 / refresh_rate);
        if (!step.should_tick) continue;
        ++result.ticks;
        result.delivered_seconds += step.delta_seconds;
    }
    return result;
}

}  // namespace

TEST_CASE("Artemis logical frames stay at 60 Hz on high refresh hosts") {
    const auto at_60_hz = RunPacer(60.0, 60);
    CHECK(at_60_hz.ticks == 60);
    CHECK(at_60_hz.delivered_seconds == Catch::Approx(1.0));

    const auto at_80_hz = RunPacer(80.0, 80);
    CHECK(at_80_hz.ticks == 60);
    CHECK(at_80_hz.delivered_seconds == Catch::Approx(1.0));

    const auto at_120_hz = RunPacer(120.0, 120);
    CHECK(at_120_hz.ticks == 60);
    CHECK(at_120_hz.delivered_seconds == Catch::Approx(1.0));

    const auto at_144_hz = RunPacer(144.0, 144);
    CHECK(at_144_hz.ticks == 60);
    CHECK(at_144_hz.delivered_seconds == Catch::Approx(1.0));
}

TEST_CASE("Artemis pacer preserves elapsed time without catch-up bursts") {
    ArtemisLogicalFramePacer pacer;
    const auto first = pacer.Advance(1.0 / 120.0);
    CHECK_FALSE(first.should_tick);

    const auto second = pacer.Advance(1.0 / 120.0);
    REQUIRE(second.should_tick);
    CHECK(second.delta_seconds == Catch::Approx(1.0 / 60.0));

    const auto slow = pacer.Advance(1.0 / 30.0);
    REQUIRE(slow.should_tick);
    CHECK(slow.delta_seconds == Catch::Approx(1.0 / 30.0));
}

TEST_CASE("runtime tick quantizer retains fractional milliseconds") {
    RuntimeTickMillisecondQuantizer quantizer;
    uint32_t total_milliseconds = 0;
    for (int frame = 0; frame < 60; ++frame) {
        total_milliseconds += quantizer.Quantize(1.0 / 60.0);
    }
    CHECK(total_milliseconds == 1000u);

    quantizer.Reset();
    total_milliseconds = 0;
    for (int frame = 0; frame < 120; ++frame) {
        total_milliseconds += quantizer.Quantize(1.0 / 120.0);
    }
    CHECK(total_milliseconds == 1000u);
}
