#include <catch2/catch_test_macros.hpp>

#include "FontStream.h"
#include "UtilStreams.h"

#include <algorithm>
#include <array>
#include <limits>

namespace {
class TruncatedMemoryStream final : public tTVPMemoryStream {
public:
    using tTVPMemoryStream::tTVPMemoryStream;

    tjs_uint Read(void *buffer, tjs_uint readSize) override {
        if(AlreadyRead)
            return 0;
        AlreadyRead = true;
        return tTVPMemoryStream::Read(buffer, std::min<tjs_uint>(readSize, 2));
    }

private:
    bool AlreadyRead = false;
};
} // namespace

TEST_CASE("font streams share bounded immutable bytes") {
    TVPClearFontStreamCache();
    TVPSetFontStreamCacheLimits(1024, 2);

    const std::array<tjs_uint8, 6> payload{{0, 1, 2, 3, 4, 5}};
    auto makeSource = [&]() {
        auto *stream = new tTVPMemoryStream();
        stream->WriteBuffer(payload.data(), payload.size());
        stream->SetPosition(0);
        return stream;
    };

    tTJSBinaryStream *first = TVPCreateCachedFontStream(
        makeSource(), TJS_W("font://shared-test"));
    REQUIRE(first != nullptr);
    std::array<tjs_uint8, payload.size()> firstRead{};
    first->ReadBuffer(firstRead.data(), firstRead.size());
    CHECK(firstRead == payload);

    tTJSBinaryStream *second = TVPCreateCachedFontStream(
        makeSource(), TJS_W("font://shared-test"));
    REQUIRE(second != nullptr);
    std::array<tjs_uint8, payload.size()> secondRead{};
    second->ReadBuffer(secondRead.data(), secondRead.size());
    CHECK(secondRead == payload);

    delete first;
    delete second;

    tTJSBinaryStream *extreme = TVPCreateCachedFontStream(
        makeSource(), TJS_W("font://seek-boundary"));
    REQUIRE(extreme != nullptr);
    extreme->Seek(2, TJS_BS_SEEK_SET);
    CHECK(extreme->Seek(std::numeric_limits<tjs_int64>::max(),
                        TJS_BS_SEEK_CUR) == 2);
    CHECK(extreme->Seek(std::numeric_limits<tjs_int64>::min(),
                        TJS_BS_SEEK_CUR) == 2);
    delete extreme;

    auto *emptyKeySource = makeSource();
    emptyKeySource->SetPosition(3);
    CHECK(TVPCreateCachedFontStream(emptyKeySource, TJS_W("")) ==
          emptyKeySource);
    delete emptyKeySource;

    auto *truncated = new TruncatedMemoryStream();
    truncated->WriteBuffer(payload.data(), payload.size());
    truncated->SetPosition(3);
    CHECK(TVPCreateCachedFontStream(truncated, TJS_W("font://truncated")) ==
          truncated);
    CHECK(truncated->GetPosition() == 3);
    delete truncated;

    TVPClearFontStreamCache();
}
