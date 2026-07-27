//
// Created to verify motionplayer/emoteplayer behavior aligned to libkrkr2.so.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#if defined(AETHERKIRI_EXPECT_INTERNAL_EMOTE)
#include <lz4frame.h>
#endif

#include "motionplayer/EmotePlayer.h"
#include "motionplayer/MotionPlayerExtension.h"
#include "motionplayer/MotionNode.h"
#include "motionplayer/NodeTree.h"
#include "motionplayer/Player.h"
#include "motionplayer/PlayerInternal.h"
#include "motionplayer/ResourceManager.h"
#include "motionplayer/RuntimeSupport.h"
#include "ncbind.hpp"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "SysInitImpl.h"
#include "psbfile/PSBValue.h"
#include "psbfile/PSBFile.h"
#include "test_config.h"
#include "tjsObject.h"

extern tTJS *TVPScriptEngine;
extern "C" void TVPRegisterMotionPlayerPluginAnchor();

#if defined(AETHERKIRI_EXPECT_INTERNAL_EMOTE)
namespace {
    const bool kPrivateMotionRuntimeRegistered = [] {
        TVPRegisterMotionPlayerPluginAnchor();
        return true;
    }();
}
#endif

#if defined(AETHERKIRI_EXPECT_INTERNAL_EMOTE)
TEST_CASE("PSBFile unwraps LZ4-frame motion resources") {
    std::ifstream input(
        TEST_FILES_PATH "/emote/e-mote3.0バニラパジャマa.psb",
        std::ios::binary | std::ios::ate);
    REQUIRE(input);
    const auto inputSize = static_cast<size_t>(input.tellg());
    REQUIRE(inputSize != 0);
    input.seekg(0);

    std::vector<std::uint8_t> source(inputSize);
    input.read(reinterpret_cast<char *>(source.data()),
               static_cast<std::streamsize>(source.size()));
    REQUIRE(static_cast<size_t>(input.gcount()) == source.size());

    std::vector<std::uint8_t> compressed(
        LZ4F_compressFrameBound(source.size(), nullptr));
    const size_t compressedSize = LZ4F_compressFrame(
        compressed.data(), compressed.size(), source.data(), source.size(),
        nullptr);
    REQUIRE_FALSE(LZ4F_isError(compressedSize));
    compressed.resize(compressedSize);

    PSB::PSBFile file;
    file.setSeed(742877301);
    bool callbackSawDecompressedPsb = false;
    file.setPreParseCallback(
        [&callbackSawDecompressedPsb](const std::uint8_t *data,
                                     const size_t size) {
            callbackSawDecompressedPsb =
                size >= 4 && data[0] == 'P' && data[1] == 'S' &&
                data[2] == 'B' && data[3] == '\0';
            return callbackSawDecompressedPsb;
        });
    REQUIRE(file.loadPSBData(compressed.data(), compressed.size(),
                             ttstr(TJS_W("lz4-motion.psb"))));
    REQUIRE(callbackSawDecompressedPsb);
    REQUIRE(file.getType() == PSB::PSBType::Motion);
}
#endif

TEST_CASE("motionplayer optional E-mote extension matches build mode") {
    TVPRegisterMotionPlayerPluginAnchor();
        const auto *extension = motion::motionPlayerExtension();
#if defined(AETHERKIRI_EXPECT_INTERNAL_EMOTE)
    REQUIRE(extension != nullptr);
    REQUIRE(extension->abiVersion ==
            motion::kMotionPlayerExtensionAbiVersion);
    REQUIRE(extension->detectExtendedEmoteMode != nullptr);
        REQUIRE(extension->collectControlMetadata != nullptr);
        REQUIRE(extension->configureNodeTree != nullptr);
        REQUIRE(extension->ensureControlState != nullptr);
        REQUIRE(extension->hasActivePhysics != nullptr);
        REQUIRE(extension->serializeControlState != nullptr);
        REQUIRE(extension->unserializeControlState != nullptr);
    REQUIRE(extension->stepAutoBlink != nullptr);
    REQUIRE(extension->stepPhysics != nullptr);
    REQUIRE(extension->renderPolicy != nullptr);
#else
    REQUIRE(extension == nullptr);
#endif
}

TEST_CASE("motionplayer honors a split module's authored composition entry point") {
    motion::detail::MotionSnapshot snapshot;
    auto root = std::make_shared<PSB::PSBDictionary>();
    auto metadata = std::make_shared<PSB::PSBDictionary>();
    auto base = std::make_shared<PSB::PSBDictionary>();
    base->emplace(
        "chara", std::make_shared<PSB::PSBString>("all_parts"));
    base->emplace(
        "motion", std::make_shared<PSB::PSBString>("タイムライン構造"));
    metadata->emplace("base", base);
    root->emplace("metadata", metadata);
    snapshot.root = root;
    snapshot.clipsByOwnerAndLabel["all_parts"].emplace(
        "タイムライン構造", motion::detail::MotionClip{});

    const auto authored =
        motion::detail::resolveMotionCompositionEntryPoint(
            snapshot, "all_parts", "全体構造");
    CHECK(authored.owner == "all_parts");
    CHECK(authored.label == "タイムライン構造");

    snapshot.clipsByOwnerAndLabel["all_parts"].emplace(
        "全体構造", motion::detail::MotionClip{});
    const auto explicitReference =
        motion::detail::resolveMotionCompositionEntryPoint(
            snapshot, "all_parts", "全体構造");
    CHECK(explicitReference.owner == "all_parts");
    CHECK(explicitReference.label == "全体構造");

    snapshot.clipsByOwnerAndLabel["all_parts"].erase("全体構造");
    snapshot.clipsByOwnerAndLabel["all_parts"].erase("タイムライン構造");
    const auto fallback =
        motion::detail::resolveMotionCompositionEntryPoint(
            snapshot, "all_parts", "全体構造");
    CHECK(fallback.owner == "all_parts");
    CHECK(fallback.label == "全体構造");
}

#if defined(AETHERKIRI_EXPECT_INTERNAL_EMOTE)
TEST_CASE("motionplayer resolves hierarchical split motions without motion icons") {
    using motion::internal::shouldSearchCachedMotionComposition;

    REQUIRE(shouldSearchCachedMotionComposition(
        "all_parts/全体構造", ""));
    REQUIRE(shouldSearchCachedMotionComposition(
        "all_parts/背景構造", "全体構造"));
    REQUIRE_FALSE(shouldSearchCachedMotionComposition(
        "standalone_motion", ""));
}
#endif

TEST_CASE("motion presentation excludes structural binder layers") {
    REQUIRE_FALSE(
        motion::internal::presentationLayerTypeCanReceivePixels(ltBinder));
    REQUIRE(
        motion::internal::presentationLayerTypeCanReceivePixels(ltOpaque));
    REQUIRE(
        motion::internal::presentationLayerTypeCanReceivePixels(ltAlpha));
}

TEST_CASE("startup logo presentation preserves its authored origin") {
    using motion::internal::startupLogoUsesCenteredOrigin;

    constexpr int canvasWidth = 1920;
    constexpr int canvasHeight = 1080;

    CHECK(startupLogoUsesCenteredOrigin({ -960.0f, -540.0f, 960.0f, 540.0f },
                                        canvasWidth, canvasHeight));
    CHECK(startupLogoUsesCenteredOrigin({ -300.0f, -120.0f, 300.0f, 120.0f },
                                        canvasWidth, canvasHeight));
    CHECK(startupLogoUsesCenteredOrigin({ -768.0f, -432.0f, 768.0f, 432.0f },
                                        canvasWidth, canvasHeight));

    CHECK_FALSE(startupLogoUsesCenteredOrigin({ 0.0f, 0.0f, 1920.0f, 1080.0f },
                                              canvasWidth, canvasHeight));
    CHECK_FALSE(startupLogoUsesCenteredOrigin(
        { -1.0f, -1.0f, 1919.0f, 1079.0f }, canvasWidth, canvasHeight));
    CHECK_FALSE(startupLogoUsesCenteredOrigin(
        { -8.0f, -4.0f, 192.0f, 96.0f }, canvasWidth, canvasHeight));
    CHECK_FALSE(startupLogoUsesCenteredOrigin(
        { -1.0f, -1.0f, 3.0f, 3.0f }, canvasWidth, canvasHeight));
    CHECK_FALSE(startupLogoUsesCenteredOrigin({ 0.0f, 0.0f, 960.0f, 540.0f },
                                              canvasWidth, canvasHeight));
}

TEST_CASE("motionplayer identifies full-canvas split composition planes") {
    const std::array<int, 4> canvasRect{0, 0, 1280, 720};
    const std::array<int, 4> partialRect{20, 0, 1280, 720};

    REQUIRE(motion::internal::isFullCanvasCompositeRenderRoot(
        true, false, false, 255, canvasRect, 1280, 720));
    REQUIRE_FALSE(motion::internal::isFullCanvasCompositeRenderRoot(
        true, true, false, 255, canvasRect, 1280, 720));
    REQUIRE_FALSE(motion::internal::isFullCanvasCompositeRenderRoot(
        true, false, false, 255, partialRect, 1280, 720));

    REQUIRE(motion::internal::isFullCanvasDirectRenderPlane(
        true, false, false, false, 0, 255, canvasRect, 1280, 720));
    REQUIRE_FALSE(motion::internal::isFullCanvasDirectRenderPlane(
        true, false, false, false, 16, 255, canvasRect, 1280, 720));
    REQUIRE_FALSE(motion::internal::isFullCanvasDirectRenderPlane(
        true, false, false, false, 0, 255, partialRect, 1280, 720));
}

#if defined(AETHERKIRI_EXPECT_INTERNAL_EMOTE)
TEST_CASE("motionplayer treats mode-6 difference leaves as pass-through") {
    REQUIRE(motion::internal::isDifferenceAlphaPassThroughLeaf(
        true, false, 22));
    REQUIRE(motion::internal::isDifferenceAlphaPassThroughLeaf(
        true, false, 6));

    REQUIRE_FALSE(motion::internal::isDifferenceAlphaPassThroughLeaf(
        true, false, 16));
    REQUIRE_FALSE(motion::internal::isDifferenceAlphaPassThroughLeaf(
        false, false, 22));
    REQUIRE_FALSE(motion::internal::isDifferenceAlphaPassThroughLeaf(
        true, true, 22));
}

TEST_CASE("motionplayer keeps parentless flags-6 groups alpha-only") {
    REQUIRE(motion::internal::isIndependentDifferenceAlphaMaskGroup(
        true, false, 6, false));
    REQUIRE(motion::internal::isIndependentDifferenceAlphaMaskGroup(
        true, false, 22, false));

    REQUIRE_FALSE(motion::internal::isIndependentDifferenceAlphaMaskGroup(
        true, false, 6, true));
    REQUIRE_FALSE(motion::internal::isIndependentDifferenceAlphaMaskGroup(
        true, true, 6, false));
    REQUIRE_FALSE(motion::internal::isIndependentDifferenceAlphaMaskGroup(
        false, false, 6, false));
    REQUIRE_FALSE(motion::internal::isIndependentDifferenceAlphaMaskGroup(
        true, false, 5, false));
}

TEST_CASE("motionplayer applies parentless difference masks only to colour leaves") {
    REQUIRE(motion::internal::canReceiveIndependentDifferenceAlphaMask(
        true, false, 16));
    REQUIRE(motion::internal::canReceiveIndependentDifferenceAlphaMask(
        true, false, 5));

    REQUIRE_FALSE(motion::internal::canReceiveIndependentDifferenceAlphaMask(
        true, false, 22));
    REQUIRE_FALSE(motion::internal::canReceiveIndependentDifferenceAlphaMask(
        true, true, 16));
    REQUIRE_FALSE(motion::internal::canReceiveIndependentDifferenceAlphaMask(
        false, false, 16));
}

TEST_CASE("motionplayer pairs authored difference-alpha leaves by layer name") {
    REQUIRE(motion::internal::isAuthoredDifferenceAlphaPair(
        "fade_t", "fade_t"));
    REQUIRE_FALSE(motion::internal::isAuthoredDifferenceAlphaPair(
        "fade_t", "fade_r"));
    REQUIRE_FALSE(motion::internal::isAuthoredDifferenceAlphaPair(
        "", ""));

    REQUIRE(motion::internal::isNestedDifferenceAlphaPair(
        4, std::vector<std::size_t>{8, 7, 4, 1}));
    REQUIRE_FALSE(motion::internal::isNestedDifferenceAlphaPair(
        4, std::vector<std::size_t>{8, 7, 1}));
    REQUIRE(motion::internal::isGenericDifferenceAlphaLabel(
        "追加パーツ"));
    REQUIRE(motion::internal::isGenericDifferenceAlphaLabel(
        "■追加パーツ"));
    REQUIRE(motion::internal::isGenericDifferenceAlphaLabel(""));
    REQUIRE_FALSE(motion::internal::isGenericDifferenceAlphaLabel(
        "■追加パーツ揺れ_fade_s"));
    REQUIRE(motion::internal::isUnambiguousNestedDifferenceAlphaPair(1));
    REQUIRE_FALSE(
        motion::internal::isUnambiguousNestedDifferenceAlphaPair(0));
    REQUIRE_FALSE(
        motion::internal::isUnambiguousNestedDifferenceAlphaPair(2));

    REQUIRE(motion::internal::isSyntheticMotionBlankSource(
        "blank/64:32:12:8"));
    REQUIRE_FALSE(motion::internal::isSyntheticMotionBlankSource(
        "src/tex/0074"));
}

TEST_CASE("motionplayer selects combined masks only for carrier bases") {
    using motion::internal::shouldUseCombinedDifferenceAlphaMask;

    REQUIRE(shouldUseCombinedDifferenceAlphaMask(false, 2));
    REQUIRE(shouldUseCombinedDifferenceAlphaMask(false, 3));
    REQUIRE_FALSE(shouldUseCombinedDifferenceAlphaMask(true, 2));
    REQUIRE_FALSE(shouldUseCombinedDifferenceAlphaMask(false, 1));
    REQUIRE_FALSE(shouldUseCombinedDifferenceAlphaMask(false, 0));
    REQUIRE_FALSE(shouldUseCombinedDifferenceAlphaMask(true, 0));
}

TEST_CASE("motionplayer flags-6 masks subtract alpha like libgame") {
    using motion::internal::applyMotionAlphaMaskValueLike_0x6AC4E4;
    using motion::internal::independentDifferenceAlphaMaskOperation;

    // Same-name liquid/mask pairs use flags=6's reverse-alpha operation.
    REQUIRE(independentDifferenceAlphaMaskOperation(true, 6) == 2);
    // The broad base mosaic has no same-name mask and consumes the combined
    // carrier surface as a normal crop, retaining its right-hand portion.
    REQUIRE(independentDifferenceAlphaMaskOperation(false, 6) == 1);

    // flags=6 has operation 2 in its low bits: opaque mask pixels remove the
    // colour layer, while transparent mask pixels retain it.
    REQUIRE(applyMotionAlphaMaskValueLike_0x6AC4E4(
                200, 255, true, 2) == 0);
    REQUIRE(applyMotionAlphaMaskValueLike_0x6AC4E4(
                200, 0, true, 2) == 200);
    REQUIRE(applyMotionAlphaMaskValueLike_0x6AC4E4(
                200, 64, true, 2) == 0);
    REQUIRE(applyMotionAlphaMaskValueLike_0x6AC4E4(
                200, 128, false, 2) == 99);

    // Operation 1 remains the ordinary crop branch.
    REQUIRE(applyMotionAlphaMaskValueLike_0x6AC4E4(
                200, 255, true, 1) == 200);
    REQUIRE(applyMotionAlphaMaskValueLike_0x6AC4E4(
                200, 0, true, 1) == 0);
    REQUIRE(applyMotionAlphaMaskValueLike_0x6AC4E4(
                200, 128, false, 1) == 100);
}

TEST_CASE("motionplayer recovers alpha-empty difference masks from RGB") {
    using motion::internal::differenceAlphaFromRgb;
    using motion::internal::shouldRecoverDifferenceAlphaFromRgb;

    REQUIRE(shouldRecoverDifferenceAlphaFromRgb(0, 8));
    REQUIRE(shouldRecoverDifferenceAlphaFromRgb(0, 4096));
    REQUIRE_FALSE(shouldRecoverDifferenceAlphaFromRgb(1, 4096));
    REQUIRE_FALSE(shouldRecoverDifferenceAlphaFromRgb(0, 7));

    REQUIRE(differenceAlphaFromRgb(255, 255, 255) == 255);
    REQUIRE(differenceAlphaFromRgb(64, 64, 64) == 64);
    REQUIRE(differenceAlphaFromRgb(32, 96, 48) == 96);
    REQUIRE(differenceAlphaFromRgb(0, 0, 0) == 0);
}
#endif

TEST_CASE("motionplayer keeps Z as ordering depth by default") {
    motion::Player player;
    REQUIRE(player.getZFactor() == Catch::Approx(0.0));
}

namespace {

    constexpr tjs_int kEmoteSeed = 742877301;

    void ensurePluginRuntime() {
        static bool initialized = false;
        if(initialized)
            return;
        const ttstr testRoot(TEST_FILES_PATH "/");
        TVPNativeProjectDir = testRoot;
        TVPProjectDir = TVPNormalizeStorageName(testRoot);
        if(TVPProjectDir.GetLastChar() != TJS_W('/'))
            TVPProjectDir += TJS_W("/");
        if(TVPGetScriptEngine() == nullptr)
            TVPScriptEngine = new tTJS();
        ncbAutoRegister::AllRegist();
        ncbAutoRegister::LoadModule(TJS_W("psbfile.dll"));
        initialized = true;
    }

    ttstr motionFixturePath() {
        return ttstr(TEST_FILES_PATH "/emote/e-mote3.0バニラパジャマa.psb");
    }

    ttstr pimgFixturePath() {
        return ttstr(TEST_FILES_PATH "/emote/ezsave.pimg");
    }

    void setEmoteSeed() {
        ensurePluginRuntime();
        tTJSVariant seed{kEmoteSeed};
        tTJSVariant *params[] = { &seed };
        REQUIRE(motion::ResourceManager::setEmotePSBDecryptSeed(
                    nullptr, 1, params, nullptr) == TJS_S_OK);
    }

    class TemporaryEmoteDecryptCallback {
    public:
        explicit TemporaryEmoteDecryptCallback(tTJSVariant callback) {
            tTJSVariant *params[] = { &callback };
            REQUIRE(motion::ResourceManager::setEmotePSBDecryptFunc(
                        nullptr, 1, params, nullptr) == TJS_S_OK);
        }

        ~TemporaryEmoteDecryptCallback() {
            motion::ResourceManager::setEmotePSBDecryptFunc(
                nullptr, 0, nullptr, nullptr);
        }
    };

    void writeTestU16LE(std::uint8_t *data, const std::uint16_t value) {
        data[0] = static_cast<std::uint8_t>(value);
        data[1] = static_cast<std::uint8_t>(value >> 8);
    }

    void writeTestU32LE(std::uint8_t *data, const std::uint32_t value) {
        data[0] = static_cast<std::uint8_t>(value);
        data[1] = static_cast<std::uint8_t>(value >> 8);
        data[2] = static_cast<std::uint8_t>(value >> 16);
        data[3] = static_cast<std::uint8_t>(value >> 24);
    }

    void cryptLegacyEmoteHeader(std::uint8_t *data, const std::size_t count) {
        std::uint32_t a = 123456789;
        std::uint32_t b = 362436069;
        std::uint32_t c = 521288629;
        std::uint32_t d = 0x13579BDFu;
        std::uint32_t value = 0;
        for(std::size_t index = 0; index < count; ++index) {
            if(value == 0) {
                const std::uint32_t temp = (a << 11) ^ a;
                a = b;
                b = c;
                c = d;
                d ^= temp ^ ((temp ^ (d >> 11)) >> 8);
                value = d;
            }
            data[index] ^= static_cast<std::uint8_t>(value);
            value >>= 8;
        }
    }

    class TemporaryAutoPath {
    public:
        TemporaryAutoPath() {
            const auto suffix = std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count();
            path_ = std::filesystem::temp_directory_path() /
                ("aetherkiri-d3d-emote-" + std::to_string(suffix));
            std::filesystem::create_directories(path_);
            storagePath_ = ttstr((path_.string() + "/").c_str());
            TVPAddAutoPath(storagePath_);
            TVPClearStorageCaches();
        }

        ~TemporaryAutoPath() {
            TVPRemoveAutoPath(storagePath_);
            TVPClearStorageCaches();
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        const std::filesystem::path &path() const { return path_; }

    private:
        std::filesystem::path path_;
        ttstr storagePath_;
    };

    tTJSVariant getProp(const tTJSVariant &object, const tjs_char *name) {
        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);

        tTJSVariant result;
        REQUIRE(TJS_SUCCEEDED(dispatch->PropGet(0, name, nullptr, &result,
                                               dispatch)));
        return result;
    }

    tTJSVariant getIndex(const tTJSVariant &object, tjs_int index) {
        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);

        tTJSVariant result;
        REQUIRE(TJS_SUCCEEDED(
            dispatch->PropGetByNum(TJS_IGNOREPROP, index, &result, dispatch)));
        return result;
    }

    tjs_int variantCount(const tTJSVariant &object) {
        return static_cast<tjs_int>(getProp(object, TJS_W("count")).AsInteger());
    }

    std::vector<std::pair<ttstr, tTJSVariant>>
    dictionaryEntries(const tTJSVariant &object) {
        struct Enumerator : tTJSDispatch {
            std::vector<std::pair<ttstr, tTJSVariant>> entries;

            tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, iTJSDispatch2 *) override {
                if(numparams >= 3) {
                    entries.emplace_back(ttstr(*param[0]), *param[2]);
                }
                if(result) {
                    *result = static_cast<tjs_int>(1);
                }
                return TJS_S_OK;
            }
        } enumerator;

        REQUIRE(object.Type() == tvtObject);
        auto *dispatch = object.AsObjectNoAddRef();
        REQUIRE(dispatch != nullptr);
        tTJSVariantClosure closure(&enumerator, nullptr);
        if(TJS_FAILED(
               dispatch->EnumMembers(TJS_IGNOREPROP, &closure, dispatch))) {
            return {};
        }
        return enumerator.entries;
    }

    void dumpDictionary(const tTJSVariant &object, const std::string &prefix,
                        int depth = 0) {
        if(depth > 2 || object.Type() != tvtObject) {
            return;
        }

        for(const auto &[key, value] : dictionaryEntries(object)) {
            std::cerr << prefix << key.AsStdString()
                      << " type=" << static_cast<int>(value.Type());
            if(value.Type() == tvtString) {
                std::cerr << " value=" << ttstr(value).AsStdString();
            } else if(value.Type() == tvtInteger) {
                std::cerr << " value=" << value.AsInteger();
            } else if(value.Type() == tvtReal) {
                std::cerr << " value=" << value.AsReal();
            }
            std::cerr << "\n";

            if(value.Type() != tvtObject) {
                continue;
            }

            if(const auto count = variantCount(value); count > 0) {
                const auto limit = std::min<tjs_int>(count, 3);
                std::cerr << prefix << "  [count]=" << count << "\n";
                for(tjs_int index = 0; index < limit; ++index) {
                    const auto item = getIndex(value, index);
                    std::cerr << prefix << "  [" << index
                              << "] type=" << static_cast<int>(item.Type());
                    if(item.Type() == tvtString) {
                        std::cerr << " value=" << ttstr(item).AsStdString();
                    } else if(item.Type() == tvtInteger) {
                        std::cerr << " value=" << item.AsInteger();
                    } else if(item.Type() == tvtReal) {
                        std::cerr << " value=" << item.AsReal();
                    }
                    std::cerr << "\n";
                    if(item.Type() == tvtObject) {
                        dumpDictionary(item, prefix + "    ", depth + 1);
                    }
                }
            } else {
                dumpDictionary(value, prefix + "  ", depth + 1);
            }
        }
    }

    void dumpPsbValue(const std::shared_ptr<PSB::IPSBValue> &value,
                      const std::string &prefix, int depth = 0) {
        if(!value || depth > 3) {
            return;
        }

        if(auto text = std::dynamic_pointer_cast<PSB::PSBString>(value)) {
            std::cerr << prefix << "string=" << text->value << "\n";
            return;
        }
        if(auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(value)) {
            std::cerr << prefix << "number=" << number->toString() << "\n";
            return;
        }
        if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
            std::cerr << prefix << "bool=" << (boolean->value ? "true" : "false")
                      << "\n";
            return;
        }
        if(auto resource = std::dynamic_pointer_cast<PSB::PSBResource>(value)) {
            std::cerr << prefix << "resource index="
                      << resource->index.value_or(UINT32_MAX)
                      << " size=" << resource->data.size() << "\n";
            return;
        }
        if(auto list = std::dynamic_pointer_cast<PSB::PSBList>(value)) {
            std::cerr << prefix << "list size=" << list->size() << "\n";
            const auto limit = std::min<size_t>(list->size(), 3);
            for(size_t index = 0; index < limit; ++index) {
                std::cerr << prefix << "  [" << index << "]\n";
                dumpPsbValue((*list)[static_cast<int>(index)], prefix + "    ",
                             depth + 1);
            }
            return;
        }
        if(auto dic = std::dynamic_pointer_cast<PSB::PSBDictionary>(value)) {
            std::cerr << prefix << "dict size="
                      << std::distance(dic->begin(), dic->end()) << "\n";
            int count = 0;
            for(const auto &[key, child] : *dic) {
                std::cerr << prefix << "  " << key << "\n";
                dumpPsbValue(child, prefix + "    ", depth + 1);
                if(++count >= 12) {
                    break;
                }
            }
            return;
        }

        std::cerr << prefix << "type=" << static_cast<int>(value->getType())
                  << " text=" << value->toString() << "\n";
    }

    bool containsString(const tTJSVariant &object, const ttstr &expected) {
        const auto count = variantCount(object);
        for(tjs_int index = 0; index < count; ++index) {
            if(ttstr(getIndex(object, index)) == expected) {
                return true;
            }
        }
        return false;
    }

} // namespace

TEST_CASE("storage resolves logical E-mote PSBs to DirectX exports") {
    ensurePluginRuntime();
    TemporaryAutoPath autoPath;

    std::ofstream(autoPath.path() / "dx_gallery_body.psb", std::ios::binary)
        .put('\0');
    std::ofstream(autoPath.path() / "dxlow_gallery_low.psb", std::ios::binary)
        .put('\0');
    std::ofstream(autoPath.path() / "gallery_original.psb", std::ios::binary)
        .put('\0');
    TVPClearStorageCaches();

    const auto body = TVPGetPlacedPath(TJS_W("gallery_body.psb"));
    REQUIRE_FALSE(body.IsEmpty());
    CHECK(TVPExtractStorageName(body) == TJS_W("dx_gallery_body.psb"));
    CHECK(TVPIsExistentStorage(TJS_W("gallery_body.psb")));

    const auto low = TVPGetPlacedPath(TJS_W("gallery_low.psb"));
    REQUIRE_FALSE(low.IsEmpty());
    CHECK(TVPExtractStorageName(low) == TJS_W("dxlow_gallery_low.psb"));

    const auto original = TVPGetPlacedPath(TJS_W("gallery_original.psb"));
    REQUIRE_FALSE(original.IsEmpty());
    CHECK(TVPExtractStorageName(original) == TJS_W("gallery_original.psb"));
}

TEST_CASE("motionplayer completes legacy decryption for PSB v4 headers") {
    ensurePluginRuntime();

    tTJSVariant callback;
    TVPExecuteExpression(TJS_W(
        "(function(buf, len) {"
        " var A=123456789, B=362436069, C=521288629, D=324508639;"
        " var version=buf[4]+(buf[5]<<8);"
        " var flags=version>2 ? buf[6]+(buf[7]<<8) : 2;"
        " var V=0, T, off, count;"
        " if(flags&1) {"
        "  off=8; count=36;"
        "  for(var i=0;i<count;++i) {"
        "   if(!V) {"
        "    T=(A<<11)^A; T&=0xFFFFFFFF;"
        "    A=B; B=C; C=D;"
        "    D^=T^((T^(D>>11))>>8); V=D;"
        "   }"
        "   buf[off+i]^=V; V>>=8;"
        "  }"
        " }"
        " if(flags&2) {"
        "  off=buf[8]|(buf[9]<<8)|(buf[10]<<16)|(buf[11]<<24);"
        "  var end=buf[24]|(buf[25]<<8)|(buf[26]<<16)|(buf[27]<<24);"
        "  count=end-off;"
        "  for(var j=0;j<count;++j) {"
        "   if(!V) {"
        "    T=(A<<11)^A; T&=0xFFFFFFFF;"
        "    A=B; B=C; C=D;"
        "    D^=T^((T^(D>>11))>>8); V=D;"
        "   }"
        "   buf[off+j]^=V; V>>=8;"
        "  }"
        " }"
        "})"), &callback);
    REQUIRE(callback.Type() == tvtObject);
    TemporaryEmoteDecryptCallback decryptCallback(callback);

    std::array<std::uint8_t, 128> plaintext{};
    plaintext[0] = 'P';
    plaintext[1] = 'S';
    plaintext[2] = 'B';
    writeTestU16LE(plaintext.data() + 4, 4);
    writeTestU16LE(plaintext.data() + 6, 1);
    writeTestU32LE(plaintext.data() + 8, 56);
    writeTestU32LE(plaintext.data() + 12, 56);
    writeTestU32LE(plaintext.data() + 16, 80);
    writeTestU32LE(plaintext.data() + 20, 84);
    writeTestU32LE(plaintext.data() + 24, 96);
    writeTestU32LE(plaintext.data() + 28, 100);
    writeTestU32LE(plaintext.data() + 32, 104);
    writeTestU32LE(plaintext.data() + 36, 60);
    writeTestU32LE(plaintext.data() + 40, 0x12345678);
    writeTestU32LE(plaintext.data() + 44, 88);
    writeTestU32LE(plaintext.data() + 48, 92);
    writeTestU32LE(plaintext.data() + 52, 96);

    auto encrypted = plaintext;
    cryptLegacyEmoteHeader(encrypted.data() + 8, 48);
    REQUIRE(motion::ResourceManager::applyEmotePSBDecryptFunc(
        encrypted.data(), encrypted.size()));
    CHECK(std::equal(
        plaintext.begin(), plaintext.begin() + 56, encrypted.begin()));
}

TEST_CASE("motionplayer maps parameter values across the authored clip span") {
    motion::detail::MotionClip clip;
    clip.totalFrames = 29.0;
    clip.selfSyncTime = 28.0;

    motion::detail::MotionParameterInfo parameter;
    parameter.id = "charview";
    parameter.discretization = true;
    parameter.rangeBegin = 0.0;
    parameter.rangeEnd = 14.0;
    parameter.division = 14.0;

    REQUIRE(motion::detail::parameterizedClipTime(clip, parameter, 1.0) ==
            Catch::Approx(2.0));
    REQUIRE(motion::detail::parameterizedClipTime(clip, parameter, 2.0) ==
            Catch::Approx(4.0));
    REQUIRE(motion::detail::parameterizedClipTime(clip, parameter, 3.0) ==
            Catch::Approx(6.0));
    REQUIRE(motion::detail::parameterizedClipTime(clip, parameter, 14.0) ==
            Catch::Approx(28.0));

    // `division` is also the quantization step count when discretization is
    // enabled, including non-integer parameter ranges.
    parameter.rangeBegin = -1.0;
    parameter.rangeEnd = 1.1;
    parameter.division = 4.0;
    REQUIRE(motion::detail::parameterizedClipTime(clip, parameter, 0.31) ==
            Catch::Approx(14.0));
    REQUIRE(motion::detail::parameterizedClipTime(clip, parameter, 0.32) ==
            Catch::Approx(21.0));

    SECTION("selector values are clamped at both authored endpoints") {
        parameter.rangeBegin = 0.0;
        parameter.rangeEnd = 14.0;
        parameter.division = 14.0;
        REQUIRE(motion::detail::parameterizedClipTime(
                    clip, parameter, -100.0) == Catch::Approx(0.0));
        REQUIRE(motion::detail::parameterizedClipTime(
                    clip, parameter, 100.0) == Catch::Approx(28.0));
    }

    SECTION("descending parameter ranges retain their authored direction") {
        parameter.rangeBegin = 14.0;
        parameter.rangeEnd = 0.0;
        parameter.division = 14.0;
        REQUIRE(motion::detail::parameterizedClipTime(
                    clip, parameter, 14.0) == Catch::Approx(0.0));
        REQUIRE(motion::detail::parameterizedClipTime(
                    clip, parameter, 7.0) == Catch::Approx(14.0));
        REQUIRE(motion::detail::parameterizedClipTime(
                    clip, parameter, 0.0) == Catch::Approx(28.0));
    }

    SECTION("non-discrete parameters interpolate continuously") {
        parameter.discretization = false;
        parameter.rangeBegin = 0.0;
        parameter.rangeEnd = 10.0;
        REQUIRE(motion::detail::parameterizedClipTime(
                    clip, parameter, 2.5) == Catch::Approx(7.0));
    }

    SECTION("timeline and degenerate-range fallbacks are bounded") {
        clip.selfSyncTime = 0.0;
        clip.totalFrames = 7.0;
        parameter.discretization = false;
        parameter.rangeBegin = 0.0;
        parameter.rangeEnd = 6.0;
        REQUIRE(motion::detail::parameterizedClipTime(
                    clip, parameter, 3.0) == Catch::Approx(3.0));

        parameter.rangeEnd = parameter.rangeBegin;
        REQUIRE(motion::detail::parameterizedClipTime(
                    clip, parameter, 3.0) == Catch::Approx(0.0));
    }
}

TEST_CASE("motionplayer resource chain and query surface") {
    setEmoteSeed();

    motion::Player player;
    const auto motionPath = motionFixturePath();
    const auto pimgPath = pimgFixturePath();

    REQUIRE_FALSE(player.isExistMotion(ttstr(TEST_FILES_PATH "/emote/missing.psb")));
    REQUIRE_FALSE(player.isExistMotion(pimgPath));
    REQUIRE(player.findMotion(pimgPath).Type() == tvtVoid);

    const auto motion = player.findMotion(motionPath);
    REQUIRE(motion.Type() == tvtObject);
    REQUIRE(player.isExistMotion(motionPath));

    const auto motions = player.motionList();
    REQUIRE(variantCount(motions) == 1);

    const auto layerNames = player.getLayerNames();
    REQUIRE(variantCount(layerNames) > 0);

    const auto firstLayer = ttstr(getIndex(layerNames, 0));
    REQUIRE_FALSE(firstLayer.IsEmpty());
    // The fixture's top-level entries are ordinary grouping layers, not child
    // motion nodes. getLayerMotion therefore reports void while the generic
    // layer getter remains available for hit testing and metadata queries.
    REQUIRE(player.getLayerMotion(firstLayer).Type() == tvtVoid);
    REQUIRE(player.getLayerGetter(firstLayer).Type() == tvtObject);
    REQUIRE(variantCount(player.getLayerGetterList()) == variantCount(layerNames));

    const auto firstLayerId = player.requireLayerId(firstLayer);
    REQUIRE(firstLayerId > 0);
    player.releaseLayerId(firstLayerId);
    REQUIRE(player.requireLayerId(firstLayer) > 0);

    const auto mainTimelineLabels = player.getMainTimelineLabelList();
    const auto diffTimelineLabels = player.getDiffTimelineLabelList();
    REQUIRE(mainTimelineLabels.Type() == tvtObject);
    REQUIRE(diffTimelineLabels.Type() == tvtObject);

    if(variantCount(mainTimelineLabels) > 0) {
        const auto label = ttstr(getIndex(mainTimelineLabels, 0));
        REQUIRE_FALSE(label.IsEmpty());
        REQUIRE_FALSE(player.getTimelinePlaying(label));
        REQUIRE(player.getVariableFrameList(label).Type() == tvtObject);
    }

    const auto variableKeys = player.getVariableKeys();
    REQUIRE(variableKeys.Type() == tvtObject);
    if(variantCount(variableKeys) > 0) {
        const auto variableLabel = ttstr(getIndex(variableKeys, 0));
        REQUIRE(player.getVariableFrameList(variableLabel).Type() == tvtObject);
    }
}

TEST_CASE("motionplayer draw cache and playback state") {
    setEmoteSeed();

    motion::Player player;
    const auto motionPath = motionFixturePath();
    const auto pimgPath = pimgFixturePath();

    REQUIRE(player.findMotion(motionPath).Type() == tvtObject);
    REQUIRE(player.findSource(pimgPath).Type() == tvtObject);

    player.setFlip(true);
    player.setOpacity(0.5);
    player.setVisible(true);
    player.setSlant(1.25);
    player.setZoom(1.5);
    player.setClearColor(0x102030);
    player.setCanvasCaptureEnabled(true);
    player.registerBg(ttstr(TJS_W("bg")));
    player.registerCaption(ttstr(TJS_W("caption")));

    player.draw();
    const auto canvas = player.captureCanvas();
    REQUIRE(canvas.Type() == tvtObject);
    REQUIRE(getProp(canvas, TJS_W("width")).AsInteger() > 0);
    REQUIRE(getProp(canvas, TJS_W("height")).AsInteger() > 0);
    REQUIRE(getProp(canvas, TJS_W("sourceCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("backgroundCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("captionCount")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("flip")).AsInteger() == 1);
    REQUIRE(getProp(canvas, TJS_W("opacity")).AsReal() == 0.5);

    player.frameProgress(16.0);
    REQUIRE(player.getFrameLastTime() == 16.0);
    REQUIRE(player.getTickCount() == Catch::Approx(16.0 * 1000.0 / 60.0));
    REQUIRE(player.getFrameTickCount() == 16.0);

    player.clearCache();
    player.draw();
    REQUIRE(player.captureCanvas().Type() == tvtObject);

    REQUIRE(player.findSource(pimgPath).Type() == tvtObject);
    player.unload(pimgPath);
    player.draw();
    REQUIRE(player.captureCanvas().Type() == tvtObject);

    player.unloadAll();
    REQUIRE(variantCount(player.motionList()) == 0);
}

TEST_CASE("emoteplayer timeline state and todo stubs") {
    setEmoteSeed();

    motion::ResourceManager rm;
    const auto module = rm.load(motionFixturePath());
    REQUIRE(module.Type() == tvtObject);

    motion::EmotePlayer player(rm);
    player.setModule(module);
    REQUIRE(player.getModule().Type() == tvtObject);

    player.setCoord(100.0, 200.0);
    player.setScale(1.0);
    REQUIRE(player.contains(100.0, 200.0));
    REQUIRE_FALSE(player.contains(99.0, 199.0));

    player.hide();
    REQUIRE_FALSE(player.contains(100.0, 200.0));
    player.show();
    REQUIRE(player.contains(100.0, 200.0));

    player.setVariable(TJS_W("manual"), 3.5);
    REQUIRE(player.getVariable(TJS_W("manual")) == 3.5);

    // After delegation to Player, countVariables returns real count from PSB.
    // The loaded PSB may or may not have variables.
    const auto varCount = player.countVariables();
    REQUIRE(varCount >= 0);
    if(varCount > 0) {
        REQUIRE_FALSE(ttstr(player.getVariableLabelAt(0)).IsEmpty());
    }
    REQUIRE(player.getOuterForce().Type() == tvtVoid);

    const auto mainCount = player.countMainTimelines();
    const auto diffCount = player.countDiffTimelines();
    REQUIRE((mainCount + diffCount) > 0);

    const auto label =
        mainCount > 0 ? player.getMainTimelineLabelAt(0)
                      : player.getDiffTimelineLabelAt(0);
    REQUIRE_FALSE(label.IsEmpty());
    REQUIRE(player.getTimelineTotalFrameCount(label) >= 0);

    player.playTimeline(label, motion::TimelinePlayFlagParallel);
    REQUIRE(player.isTimelinePlaying(label));
    REQUIRE_FALSE(player.getAnimating());
    REQUIRE(player.countPlayingTimelines() >= 1);
    REQUIRE(player.getPlayingTimelineLabelAt(0) == label);

    player.progress(10.0);
    REQUIRE(player.getProgress() == Catch::Approx(0.6));

    player.fadeOutTimeline(label, 1.0, 0);
    REQUIRE(player.getTimelineBlendRatio(label) <= 1.0);
    player.stopTimeline(label);
    REQUIRE_FALSE(player.isTimelinePlaying(label));

    player.fadeInTimeline(label, 1.0, motion::TimelinePlayFlagSequential);
    REQUIRE(player.isTimelinePlaying(label));
    REQUIRE(player.getTimelineBlendRatio(label) >= 0.0);
    REQUIRE(player.getTimelineBlendRatio(label) <= 1.0);

    player.skip();
    if(!player.isLoopTimeline(label)) {
        REQUIRE_FALSE(player.isTimelinePlaying(label));
    }

    player.playTimeline(label, motion::TimelinePlayFlagParallel);
    player.stopTimeline(TJS_W(""));
    REQUIRE_FALSE(player.getPlayer().getAllplaying());

    player.assignState();
    player.setOuterForce(1.0, 2.0);
}

#if defined(AETHERKIRI_EXPECT_INTERNAL_EMOTE)
TEST_CASE("emoteplayer clone preserves render state") {
    setEmoteSeed();
    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("emoteplayer.dll")));

    motion::ResourceManager rm;
    const auto module = rm.load(motionFixturePath());
    REQUIRE(module.Type() == tvtObject);

    motion::EmotePlayer player(rm);
    player.setModule(module);
    const auto mainCount = player.countMainTimelines();
    const auto diffCount = player.countDiffTimelines();
    REQUIRE((mainCount + diffCount) > 0);

    const auto label =
        mainCount > 0 ? player.getMainTimelineLabelAt(0)
                      : player.getDiffTimelineLabelAt(0);
    player.playTimeline(label, motion::TimelinePlayFlagParallel);
    player.setVariable(TJS_W("clone_dynamic"), 3.5);
    player.setCoord(100.0, 200.0);
    player.progress(2000.0 / 60.0);
    REQUIRE(player.isTimelinePlaying(label));

    const auto clonedVariant = player.clone();
    REQUIRE(clonedVariant.Type() == tvtObject);
    auto *cloned =
        ncbInstanceAdaptor<motion::EmotePlayer>::GetNativeInstance(
            clonedVariant.AsObjectNoAddRef(), true);
    REQUIRE(cloned != nullptr);

    REQUIRE(cloned->isTimelinePlaying(label));
    REQUIRE(cloned->countPlayingTimelines() ==
            player.countPlayingTimelines());
    REQUIRE(cloned->getVariable(TJS_W("clone_dynamic")) == 3.5);
    REQUIRE(cloned->contains(100.0, 200.0));
    REQUIRE(cloned->getProgress() == Catch::Approx(2.0));

    cloned->progress(1000.0 / 60.0);
    REQUIRE(cloned->getProgress() == Catch::Approx(3.0));
}
#endif

TEST_CASE("motionplayer can play internal logo motion clips") {
    setEmoteSeed();

    const auto baseDir = std::filesystem::path(".debugtmp") / "titleprobe_hd" /
        "data1080";
    if(!std::filesystem::exists(baseDir / "yuzulogo.mtn") ||
       !std::filesystem::exists(baseDir / "m2logo.mtn")) {
        return;
    }

    motion::Player player;
    const auto yuzuPath =
        ttstr(std::filesystem::absolute(baseDir / "yuzulogo.mtn").string());
    const auto m2Path =
        ttstr(std::filesystem::absolute(baseDir / "m2logo.mtn").string());

    const auto verifyOne = [&](const ttstr &path, const ttstr &label,
                               const tjs_int expectedLayers,
                               const tjs_int expectedFrames) {
        INFO("path=" << path.AsStdString() << " label=" << label.AsStdString());
        REQUIRE(player.findMotion(path).Type() == tvtObject);
        const auto snapshot = motion::detail::lookupModuleSnapshot(
            player.findMotion(path));
        REQUIRE(snapshot != nullptr);

        const auto mainLabels = player.getMainTimelineLabelList();
        const auto diffLabels = player.getDiffTimelineLabelList();
        REQUIRE(containsString(mainLabels, label));
        REQUIRE(variantCount(diffLabels) == 0);
        REQUIRE(player.getTimelineTotalFrameCount(label) == expectedFrames);

        player.playTimeline(label, motion::PlayFlagForce);
        REQUIRE(player.getTimelinePlaying(label));
        const auto layerNames = player.getLayerNames();
        const auto getterList = player.getLayerGetterList();
        const auto commands = player.getCommandList();
        std::cerr << "logo test path=" << path.AsStdString()
                  << " label=" << label.AsStdString()
                  << " layers=" << variantCount(layerNames)
                  << " commands=" << variantCount(commands) << "\n";
        for(tjs_int index = 0; index < variantCount(commands); ++index) {
            const auto command = ttstr(getIndex(commands, index));
            int sourceType = -1;
            try {
                sourceType = static_cast<int>(player.findSource(command).Type());
            } catch(...) {
                std::cerr << "  command[" << index << "]=" << command.AsStdString()
                          << " sourceError=<non-std-exception>\n";
                continue;
            }
            std::cerr << "  command[" << index << "]=" << command.AsStdString()
                      << " sourceType=" << sourceType << "\n";
        }
        for(tjs_int index = 0; index < variantCount(layerNames) && index < 2; ++index) {
            const auto layerName = ttstr(getIndex(layerNames, index));
            std::cerr << "  layer[" << index << "]=" << layerName.AsStdString()
                      << "\n";
            const auto clip =
                snapshot->clipsByLabel.find(label.AsStdString());
            REQUIRE(clip != snapshot->clipsByLabel.end());
            const auto layer =
                clip->second.layersByName.find(layerName.AsStdString());
            REQUIRE(layer != clip->second.layersByName.end());
            if(const auto frameList = (*layer->second)["frameList"]) {
                std::cerr << "    native frameList\n";
                dumpPsbValue(frameList, "      ");
            }
            if(const auto children = (*layer->second)["children"]) {
                std::cerr << "    native children\n";
                dumpPsbValue(children, "      ");
            }
        }
        REQUIRE(variantCount(layerNames) == expectedLayers);
        REQUIRE(variantCount(getterList) == expectedLayers);
        REQUIRE(player.getLayerMotion(ttstr(getIndex(player.getLayerNames(), 0)))
                    .Type() == tvtObject);
        REQUIRE(player.getProgressCompat() == Catch::Approx(0.0));

        player.frameProgress(static_cast<double>(expectedFrames - 1));
        REQUIRE(player.getTimelinePlaying(label));
        REQUIRE(player.getProgressCompat() < 1.0);

        player.frameProgress(1.0);
        REQUIRE_FALSE(player.getTimelinePlaying(label));
        REQUIRE(player.getProgressCompat() == Catch::Approx(1.0));
    };

    verifyOne(yuzuPath, TJS_W("yuzulogo"), 4, 241);
    verifyOne(m2Path, TJS_W("back_white"), 2, 91);
}

TEST_CASE("motionplayer non-loop motion clips finish at sync boundary") {
    setEmoteSeed();

    auto snapshot = std::make_shared<motion::detail::MotionSnapshot>();
    snapshot->path = "unit/yuzu-like-logo.mtn";
    snapshot->mainTimelineLabels.push_back("logo");
    snapshot->loopTimelines["logo"] = false;
    snapshot->timelineLoopTimes["logo"] = -1.0;
    snapshot->timelineTotalFrames["logo"] = 120.0;

    auto &clip = snapshot->clipsByLabel["logo"];
    clip.label = "logo";
    clip.loop = false;
    clip.loopTime = -1.0;
    clip.totalFrames = 120.0;
    clip.selfSyncTime = 30.0;

    motion::Player player;
    player.loadFromSnapshot(snapshot);
    REQUIRE(player.playMotionLike_0x6B2284(TJS_W("logo"),
                                           motion::PlayFlagForce));
    REQUIRE(player.getMotion() == TJS_W("logo"));
    REQUIRE(player.getTimelinePlaying(TJS_W("logo")));

    player.frameProgress(29.0);
    REQUIRE(player.getTimelinePlaying(TJS_W("logo")));
    REQUIRE(player.getAllplaying());

    player.frameProgress(1.0);
    REQUIRE_FALSE(player.getTimelinePlaying(TJS_W("logo")));
    REQUIRE_FALSE(player.getAllplaying());
    REQUIRE(player.getProgressCompat() == Catch::Approx(1.0));

    motion::Player autoPlayer;
    autoPlayer.loadFromSnapshot(snapshot);
    autoPlayer.playTimeline(TJS_W("logo"), motion::PlayFlagForce);
    REQUIRE(autoPlayer.getTimelinePlaying(TJS_W("logo")));

    for(int i = 0; i < 31; ++i) {
        autoPlayer.autoProgressFromContinuousTick(
            static_cast<tjs_uint64>(1000 + i * 17));
    }
    REQUIRE_FALSE(autoPlayer.getTimelinePlaying(TJS_W("logo")));
    REQUIRE_FALSE(autoPlayer.getAllplaying());
    REQUIRE(autoPlayer.getProgressCompat() == Catch::Approx(1.0));
}

TEST_CASE("motionplayer crops E-mote icons from a shared PSB atlas") {
    auto root = std::make_shared<PSB::PSBDictionary>();
    auto source = std::make_shared<PSB::PSBDictionary>();
    auto tex = std::make_shared<PSB::PSBDictionary>();
    auto icons = std::make_shared<PSB::PSBDictionary>();
    auto icon = std::make_shared<PSB::PSBDictionary>();
    auto texture = std::make_shared<PSB::PSBDictionary>();
    auto pixels = std::make_shared<PSB::PSBResource>();

    icon->emplace("left", std::make_shared<PSB::PSBNumber>(1));
    icon->emplace("top", std::make_shared<PSB::PSBNumber>(1));
    icon->emplace("width", std::make_shared<PSB::PSBNumber>(2));
    icon->emplace("height", std::make_shared<PSB::PSBNumber>(2));
    icon->emplace("originX", std::make_shared<PSB::PSBNumber>(7));
    icon->emplace("originY", std::make_shared<PSB::PSBNumber>(9));
    icons->emplace("part", icon);
    texture->emplace("width", std::make_shared<PSB::PSBNumber>(4));
    texture->emplace("height", std::make_shared<PSB::PSBNumber>(4));
    texture->emplace("pixel", pixels);
    tex->emplace("icon", icons);
    tex->emplace("texture", texture);
    source->emplace("tex", tex);
    root->emplace("source", source);

    pixels->data.resize(4u * 4u * 4u);
    for(size_t index = 0; index < 16u; ++index) {
        pixels->data[index * 4u + 0u] =
            static_cast<std::uint8_t>(index);
        pixels->data[index * 4u + 1u] = 0x40;
        pixels->data[index * 4u + 2u] = 0x80;
        pixels->data[index * 4u + 3u] = 0xff;
    }

    motion::detail::MotionSnapshot snapshot;
    snapshot.path = "unit/shared-atlas.psb";
    snapshot.root = root;
    snapshot.resourcesByPath.emplace(
        "source/tex/texture/pixel", pixels);

    int width = 0;
    int height = 0;
    double originX = 0.0;
    double originY = 0.0;
    bool decodedIsBgra = true;
    std::string resourcePath;
    std::string compressName;
    std::vector<std::uint8_t> decoded;
    const auto *resource =
        motion::internal::findPSBResourceBySourceName(
            snapshot, "src/tex/part", width, height, decoded,
            originX, originY, &decodedIsBgra, true,
            &resourcePath, &compressName);

    REQUIRE(resource == pixels.get());
    REQUIRE(width == 2);
    REQUIRE(height == 2);
    REQUIRE(originX == 7.0);
    REQUIRE(originY == 9.0);
    REQUIRE_FALSE(decodedIsBgra);
    REQUIRE(resourcePath == "source/tex/icon/part/atlas@1,1,2,2");
    REQUIRE(decoded.size() == 16u);
    REQUIRE(decoded[0] == 5u);
    REQUIRE(decoded[4] == 6u);
    REQUIRE(decoded[8] == 9u);
    REQUIRE(decoded[12] == 10u);
}

TEST_CASE("motionplayer combines E-mote source groups with icon names") {
    auto imageContent = std::make_shared<PSB::PSBDictionary>();
    imageContent->emplace(
        "src", std::make_shared<PSB::PSBString>("tex"));
    imageContent->emplace(
        "icon", std::make_shared<PSB::PSBString>("0018"));
    motion::internal::FrameContentState imageState;
    motion::internal::mergeFrameContent(imageContent, imageState, 0);
    REQUIRE(imageState.src == "src/tex/0018");
    REQUIRE(imageState.motionIcon.empty());

    auto motionContent = std::make_shared<PSB::PSBDictionary>();
    motionContent->emplace(
        "src", std::make_shared<PSB::PSBString>("頭部差分A"));
    motionContent->emplace(
        "icon", std::make_shared<PSB::PSBString>("正面"));
    motion::internal::FrameContentState motionState;
    motion::internal::mergeFrameContent(motionContent, motionState, 3);
    REQUIRE(motionState.src == "頭部差分A");
    REQUIRE(motionState.motionIcon == "正面");

    auto blankContent = std::make_shared<PSB::PSBDictionary>();
    blankContent->emplace(
        "src", std::make_shared<PSB::PSBString>("blank"));
    blankContent->emplace(
        "icon",
        std::make_shared<PSB::PSBString>("64:32:12:8"));
    motion::internal::FrameContentState blankState;
    motion::internal::mergeFrameContent(blankContent, blankState, 0);
    REQUIRE(blankState.src == "blank/64:32:12:8");
}

#if defined(AETHERKIRI_EXPECT_INTERNAL_EMOTE)
TEST_CASE("motionplayer classifies legacy visible stencil branches") {
    TVPRegisterMotionPlayerPluginAnchor();
    const auto makeLayer =
        [](const std::string &label, int type,
           const std::vector<std::shared_ptr<PSB::PSBDictionary>> &children) {
            auto layer = std::make_shared<PSB::PSBDictionary>();
            layer->emplace(
                "label", std::make_shared<PSB::PSBString>(label));
            layer->emplace(
                "type", std::make_shared<PSB::PSBNumber>(type));
            if(!children.empty()) {
                auto list = std::make_shared<PSB::PSBList>(
                    children.size());
                for(const auto &child : children) {
                    list->push_back(child);
                }
                layer->emplace("children", list);
            }
            return layer;
        };

    const auto baseImage = makeLayer("base-image", 0, {});
    {
        auto content = std::make_shared<PSB::PSBDictionary>();
        content->emplace(
            "src", std::make_shared<PSB::PSBString>("src/tex/0001"));
        auto frame = std::make_shared<PSB::PSBDictionary>();
        frame->emplace(
            "content", content);
        auto frameList = std::make_shared<PSB::PSBList>(1);
        frameList->push_back(frame);
        baseImage->emplace("frameList", frameList);
    }
    const auto baseBranch = makeLayer("base-branch", 2, {baseImage});
    const auto contentImage = makeLayer("content-image", 0, {});
    const auto contentBranch =
        makeLayer("content-branch", 2, {contentImage});
    const auto stencil =
        makeLayer("stencil", 12, {baseBranch, contentBranch});
    stencil->emplace(
        "stencilType", std::make_shared<PSB::PSBNumber>(5));

    motion::detail::MotionSnapshot snapshot;
    snapshot.path = "unit/legacy-visible-stencil.psb";
    motion::detail::MotionClip clip;
    clip.label = "main";
    clip.orderedLayers.push_back(stencil);

    const auto nodes = motion::detail::buildNodeTree(snapshot, &clip);
    REQUIRE(nodes.size() == 6);
    REQUIRE(nodes[1].layerName == "stencil");
    REQUIRE(nodes[1].implicitVisibleStencilGroup);
    REQUIRE(nodes[2].implicitVisibleStencilBase);
    REQUIRE(nodes[3].implicitVisibleStencilBase);
    REQUIRE(nodes[2].implicitVisibleStencilGroupNodeIndex == 1);
    REQUIRE(nodes[3].implicitVisibleStencilGroupNodeIndex == 1);
    REQUIRE_FALSE(nodes[4].implicitVisibleStencilBase);
    REQUIRE_FALSE(nodes[5].implicitVisibleStencilBase);
}
#endif

TEST_CASE("motionplayer ignores empty legacy stencil base branches") {
    const auto makeLayer =
        [](const std::string &label, int type,
           const std::vector<std::shared_ptr<PSB::PSBDictionary>> &children,
           bool withSource = false) {
            auto layer = std::make_shared<PSB::PSBDictionary>();
            layer->emplace(
                "label", std::make_shared<PSB::PSBString>(label));
            layer->emplace(
                "type", std::make_shared<PSB::PSBNumber>(type));
            if(withSource) {
                auto content = std::make_shared<PSB::PSBDictionary>();
                content->emplace(
                    "src",
                    std::make_shared<PSB::PSBString>("src/tex/0001"));
                auto frame = std::make_shared<PSB::PSBDictionary>();
                frame->emplace(
                    "content", content);
                auto frameList = std::make_shared<PSB::PSBList>(1);
                frameList->push_back(frame);
                layer->emplace("frameList", frameList);
            }
            if(!children.empty()) {
                auto list = std::make_shared<PSB::PSBList>(
                    children.size());
                for(const auto &child : children) {
                    list->push_back(child);
                }
                layer->emplace("children", list);
            }
            return layer;
        };

    const auto emptyLayout = makeLayer("empty-layout", 2, {});
    const auto contentImage = makeLayer("content-image", 0, {}, true);
    const auto contentBranch =
        makeLayer("content-branch", 2, {contentImage});
    const auto stencil =
        makeLayer("stencil", 12, {emptyLayout, contentBranch});
    stencil->emplace(
        "stencilType", std::make_shared<PSB::PSBNumber>(5));

    motion::detail::MotionSnapshot snapshot;
    snapshot.path = "unit/empty-legacy-stencil.psb";
    motion::detail::MotionClip clip;
    clip.label = "main";
    clip.orderedLayers.push_back(stencil);

    const auto nodes = motion::detail::buildNodeTree(snapshot, &clip);
    REQUIRE(nodes.size() == 5);
    REQUIRE_FALSE(nodes[1].implicitVisibleStencilGroup);
    REQUIRE_FALSE(nodes[2].implicitVisibleStencilBase);
    REQUIRE(nodes[2].implicitVisibleStencilGroupNodeIndex == -1);
}
