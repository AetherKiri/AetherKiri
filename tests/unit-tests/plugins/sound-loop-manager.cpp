#include <catch2/catch_test_macros.hpp>

#include "sound/WaveIntf.h"
#include "sound/WaveLoopManager.h"

namespace {

class FormatProbeDecoder final : public tTVPWaveDecoder {
public:
    explicit FormatProbeDecoder(const tTVPWaveFormat &format) : format_(format) {}

    void GetFormat(tTVPWaveFormat &format) override { format = format_; }

    bool Render(void *, tjs_uint, tjs_uint &rendered) override {
        rendered = 0;
        return false;
    }

    bool SetPosition(tjs_uint64 position) override {
        position_ = position;
        return true;
    }

    bool DesiredFormat(const tTVPWaveFormat &format) override {
        format_ = format;
        return true;
    }

private:
    tTVPWaveFormat format_{};
    tjs_uint64 position_ = 0;
};

} // namespace

TEST_CASE("krkrz WaveLoopManager bridge preserves Aether format negotiation") {
    tTVPWaveFormat initial{};
    initial.SamplesPerSec = 44100;
    initial.Channels = 2;
    initial.BitsPerSample = 16;
    initial.BytesPerSample = 2;
    initial.TotalSamples = 441000;
    initial.Seekable = true;

    FormatProbeDecoder decoder(initial);
    tTVPWaveLoopManager manager;
    manager.SetDecoder(&decoder);
    CHECK(manager.GetFormat().SamplesPerSec == 44100);
    CHECK(manager.GetFormat().Channels == 2);

    tTVPWaveFormat requested = initial;
    requested.SamplesPerSec = 48000;
    requested.Channels = 1;
    requested.BytesPerSample = 4;
    requested.IsFloat = true;
    CHECK(manager.DesiredFormat(requested));
    CHECK(manager.GetFormat().SamplesPerSec == 48000);
    CHECK(manager.GetFormat().Channels == 1);
    CHECK(manager.GetFormat().IsFloat);
}
