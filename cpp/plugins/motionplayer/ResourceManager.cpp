//
// Created by LiDon on 2025/9/15.
//

#include "ResourceManager.h"
#include "tjsDictionary.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <spdlog/spdlog.h>

#include "RuntimeSupport.h"
#include "StorageIntf.h"
#include "xp3filter.h"

#define LOGGER spdlog::get("plugin")

namespace {
    std::string lowercase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        return value;
    }

    bool tryParseDecryptSeed(const tTJSVariant &value, tjs_int &outSeed) {
        switch(value.Type()) {
            case tvtInteger:
                outSeed = static_cast<tjs_int>(value.AsInteger());
                return true;

            case tvtReal:
                outSeed = static_cast<tjs_int>(value.AsReal());
                return true;

            case tvtString: {
                const auto seedText = ttstr(value).AsStdString();
                if(seedText.empty()) {
                    outSeed = 0;
                    return true;
                }
                char *end = nullptr;
                const auto parsed =
                    std::strtoll(seedText.c_str(), &end, 0);
                if(end == seedText.c_str()) {
                    return false;
                }
                outSeed = static_cast<tjs_int>(parsed);
                return true;
            }

            case tvtOctet: {
                auto *octet = value.AsOctetNoAddRef();
                if(!octet) {
                    return false;
                }
                const auto *data =
                    static_cast<const std::uint8_t *>(octet->GetData());
                const auto length =
                    static_cast<size_t>(octet->GetLength());
                if(data == nullptr || length == 0) {
                    outSeed = 0;
                    return true;
                }

                std::uint64_t accum = 0;
                const auto limit = std::min(length, sizeof(accum));
                for(size_t index = 0; index < limit; ++index) {
                    accum |= static_cast<std::uint64_t>(data[index])
                        << (index * 8);
                }
                outSeed = static_cast<tjs_int>(accum);
                return true;
            }

            default:
                return false;
        }
    }

    bool motionResourceDebugEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_MOTION_DEBUG");
            return value && *value && std::strcmp(value, "0") != 0;
        }();
        return enabled;
    }

    bool stripSuffixInPlace(std::string &value, const std::string &suffix) {
        if(value.size() < suffix.size() ||
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) !=
               0) {
            return false;
        }
        value.resize(value.size() - suffix.size());
        return true;
    }

    bool splitEmoteRequestLabels(const ttstr &path,
                                 std::vector<std::string> &labels) {
        std::string storage =
            motion::detail::narrow(TVPExtractStorageName(path));
        if(storage.empty()) {
            storage = motion::detail::narrow(path);
            const auto slash = storage.find_last_of("/\\");
            if(slash != std::string::npos) {
                storage = storage.substr(slash + 1);
            }
        }
        storage = lowercase(storage);
        if(storage.rfind("dx_", 0) == 0) {
            storage = storage.substr(3);
        }
        if(!stripSuffixInPlace(storage, ".mtn") &&
           !stripSuffixInPlace(storage, ".psb")) {
            stripSuffixInPlace(storage, ".mt");
        }
        if(storage.size() <= 3 ||
           storage.compare(storage.size() - 3, 3, "emo") != 0) {
            return false;
        }
        labels.push_back(storage);
        const auto base = storage.substr(0, storage.size() - 3);
        if(!base.empty()) {
            labels.push_back(base);
        }
        return true;
    }

    bool labelMatchesSplitEmote(
        const std::string &label,
        const std::vector<std::string> &candidates) {
        const auto lowered = lowercase(label);
        for(const auto &candidate : candidates) {
            if(!candidate.empty() &&
               (lowered == candidate ||
                (lowered.size() > candidate.size() &&
                 lowered.compare(lowered.size() - candidate.size(),
                                 candidate.size(), candidate) == 0))) {
                return true;
            }
        }
        return false;
    }

    bool snapshotHasSplitEmoteLabel(
        const motion::detail::MotionSnapshot &snapshot,
        const std::vector<std::string> &labels) {
        const auto matches = [&labels](const std::string &label) {
            return labelMatchesSplitEmote(label, labels);
        };
        return std::any_of(snapshot.mainTimelineLabels.begin(),
                           snapshot.mainTimelineLabels.end(), matches) ||
            std::any_of(snapshot.diffTimelineLabels.begin(),
                        snapshot.diffTimelineLabels.end(), matches) ||
            std::any_of(snapshot.clipsByLabel.begin(),
                        snapshot.clipsByLabel.end(),
                        [&matches](const auto &entry) {
                            return matches(entry.first);
                        });
    }

    tTJSVariant fallbackSplitEmoteModule(const tTJSVariant &lastLoaded,
                                         const ttstr &path) {
        std::vector<std::string> labels;
        if(!splitEmoteRequestLabels(path, labels)) {
            return {};
        }
        const auto snapshot = motion::detail::lookupModuleSnapshot(lastLoaded);
        if(!snapshot || !snapshotHasSplitEmoteLabel(*snapshot, labels)) {
            return {};
        }
        if(motionResourceDebugEnabled()) {
            LOGGER->info(
                "ResourceManager::load split emote alias: request={} source={}",
                path.AsStdString(), snapshot->path);
        }
        return lastLoaded;
    }

    struct WarmMotionModule {
        tTJSVariant module;
        std::shared_ptr<motion::detail::MotionSnapshot> snapshot;
        std::string placedPath;
        tjs_int decryptSeed = 0;
        std::uint64_t lastUse = 0;
    };

    struct WarmMotionModuleCache {
        std::vector<WarmMotionModule> entries;
        std::uint64_t useCounter = 0;
    };

    std::string placedMotionPath(const ttstr &path) {
        if(path.IsEmpty()) {
            return {};
        }
        const auto placed = TVPGetPlacedPath(path);
        return lowercase(
            placed.IsEmpty() ? path.AsStdString() : placed.AsStdString());
    }

    WarmMotionModuleCache &warmMotionModuleCache() {
        // TJS dispatch objects must be released while the host session is
        // alive, not from a process-global destructor after TJS teardown.
        // resetStaticStateForHostSession() performs the explicit clear.
        static auto *cache = new WarmMotionModuleCache();
        return *cache;
    }

    void clearWarmMotionModuleCache() {
        auto &cache = warmMotionModuleCache();
        cache.entries.clear();
        cache.useCounter = 0;
    }

    void rememberWarmMotionModule(
        const tTJSVariant &loaded,
        std::shared_ptr<motion::detail::MotionSnapshot> snapshot = {}) {
        if(loaded.Type() != tvtObject) {
            return;
        }
        if(!snapshot) {
            snapshot = motion::detail::lookupModuleSnapshot(loaded);
        }
        if(!snapshot || snapshot->path.empty()) {
            return;
        }
        const auto placedPath = placedMotionPath(
            motion::detail::widen(snapshot->path));
        if(placedPath.empty()) {
            return;
        }

        auto &cache = warmMotionModuleCache();
        const auto decryptSeed =
            motion::ResourceManager::getEmotePSBDecryptSeed();
        const auto existing = std::find_if(
            cache.entries.begin(), cache.entries.end(),
            [&placedPath, decryptSeed](const auto &entry) {
                return entry.decryptSeed == decryptSeed &&
                    entry.placedPath == placedPath;
            });
        if(existing != cache.entries.end()) {
            existing->module = loaded;
            existing->snapshot = std::move(snapshot);
            existing->lastUse = ++cache.useCounter;
            return;
        }

        cache.entries.push_back({ loaded, std::move(snapshot), placedPath,
                                  decryptSeed, ++cache.useCounter });
        // Kiri scenes commonly rotate through six character resources. Keep
        // more than one parsed module warm so a later expression/source clone
        // does not synchronously reopen and parse the PSB after its short-lived
        // ResourceManager has been released.
        constexpr std::size_t kMaximumWarmMotionModules = 12;
        if(cache.entries.size() > kMaximumWarmMotionModules) {
            const auto oldest = std::min_element(
                cache.entries.begin(), cache.entries.end(),
                [](const auto &left, const auto &right) {
                    return left.lastUse < right.lastUse;
                });
            if(oldest != cache.entries.end()) {
                if(motionResourceDebugEnabled()) {
                    LOGGER->info(
                        "Motion resource warm cache evict: path={}",
                        oldest->placedPath);
                }
                cache.entries.erase(oldest);
            }
        }
    }

    WarmMotionModule reuseExactWarmMotionModule(
        const ttstr &path, const tjs_int decryptSeed) {
        const auto placedPath = placedMotionPath(path);
        if(placedPath.empty()) {
            return {};
        }
        auto &cache = warmMotionModuleCache();
        const auto found = std::find_if(
            cache.entries.begin(), cache.entries.end(),
            [&placedPath, decryptSeed](const auto &entry) {
                return entry.decryptSeed == decryptSeed &&
                    entry.placedPath == placedPath && entry.snapshot &&
                    entry.module.Type() == tvtObject;
            });
        if(found == cache.entries.end()) {
            return {};
        }
        found->lastUse = ++cache.useCounter;
        if(motionResourceDebugEnabled()) {
            LOGGER->info("Motion resource warm cache hit: path={}",
                         placedPath);
        }
        return *found;
    }

    tTJSVariant mostRecentWarmMotionModule() {
        auto &cache = warmMotionModuleCache();
        const auto recent = std::max_element(
            cache.entries.begin(), cache.entries.end(),
            [](const auto &left, const auto &right) {
                return left.lastUse < right.lastUse;
            });
        return recent != cache.entries.end() ? recent->module : tTJSVariant{};
    }

    std::uint16_t readU16LE(const std::uint8_t *data) {
        return static_cast<std::uint16_t>(data[0]) |
            (static_cast<std::uint16_t>(data[1]) << 8);
    }

    std::uint32_t readU32LE(const std::uint8_t *data) {
        return static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
    }

    void writeU32LE(std::uint8_t *data, const std::uint32_t value) {
        data[0] = static_cast<std::uint8_t>(value);
        data[1] = static_cast<std::uint8_t>(value >> 8);
        data[2] = static_cast<std::uint8_t>(value >> 16);
        data[3] = static_cast<std::uint8_t>(value >> 24);
    }

    bool isPsbHeader(const std::uint8_t *data, const std::size_t size) {
        return size >= 56 && data[0] == 'P' && data[1] == 'S' &&
            data[2] == 'B' && data[3] == '\0';
    }

    bool hasSanePsbV4BaseHeader(const std::uint8_t *data,
                                const std::size_t size) {
        if(!isPsbHeader(data, size) || readU16LE(data + 4) < 4) {
            return false;
        }
        const auto headerLength = readU32LE(data + 8);
        const auto namesOffset = readU32LE(data + 12);
        const auto entryOffset = readU32LE(data + 36);
        return headerLength >= 56 && headerLength <= size &&
            namesOffset >= headerLength && namesOffset < size &&
            entryOffset >= headerLength && entryOffset < size;
    }

    bool hasSanePsbV4ExtraOffsets(const std::uint8_t *data,
                                  const std::size_t size) {
        const auto offsetsOffset = readU32LE(data + 44);
        const auto lengthsOffset = readU32LE(data + 48);
        const auto dataOffset = readU32LE(data + 52);
        return offsetsOffset >= 56 && offsetsOffset < size &&
            lengthsOffset >= offsetsOffset && lengthsOffset < size &&
            dataOffset >= lengthsOffset && dataOffset <= size;
    }

    bool hasSanePsbV4ExtraHeader(const std::uint8_t *data,
                                 const std::size_t size) {
        return hasSanePsbV4BaseHeader(data, size) &&
            hasSanePsbV4ExtraOffsets(data, size);
    }

    bool invokeEmoteDecryptClosure(const tTJSVariant &decryptFunc,
                                   std::uint8_t *data,
                                   const std::size_t size) {
        if(!data || size > std::numeric_limits<unsigned int>::max()) {
            LOGGER->error(
                "E-mote PSB decrypt callback received invalid buffer size: {}",
                size);
            return false;
        }

        auto *accessor =
            new CBinaryAccessor(data, static_cast<unsigned int>(size));
        tTJSVariant buffer(accessor, accessor);
        accessor->Release();
        tTJSVariant length(static_cast<tjs_int64>(size));
        tTJSVariant *params[] = { &buffer, &length };
        const auto closure = decryptFunc.AsObjectClosureNoAddRef();
        const tjs_error status =
            closure.FuncCall(0, nullptr, nullptr, nullptr, 2, params, nullptr);
        if(TJS_FAILED(status)) {
            LOGGER->error("E-mote PSB decrypt callback failed: {}", status);
            return false;
        }
        return true;
    }

    bool recoverLegacyPsbV4ExtraHeader(
        const tTJSVariant &decryptFunc, std::uint8_t *data,
        const std::size_t size) {
        if(!hasSanePsbV4BaseHeader(data, size) ||
           hasSanePsbV4ExtraHeader(data, size) ||
           (readU16LE(data + 6) & 1) == 0) {
            return true;
        }

        // Older title callbacks decrypt the 36-byte v3 header and leave the
        // three v4 extra-resource offsets encrypted.  Use the callback itself
        // as a cipher oracle: first capture its header keystream, then ask its
        // payload branch to continue at byte 44 with the same cipher state.
        // This keeps title-specific keys and algorithms inside the callback.
        std::array<std::uint8_t, 56> keystream{};
        keystream[0] = 'P';
        keystream[1] = 'S';
        keystream[2] = 'B';
        keystream[4] = 4;
        keystream[6] = 1;
        if(!invokeEmoteDecryptClosure(
               decryptFunc, keystream.data(), keystream.size())) {
            return false;
        }

        std::array<std::uint8_t, 56> continuation{};
        continuation[0] = 'P';
        continuation[1] = 'S';
        continuation[2] = 'B';
        continuation[4] = 4;
        continuation[6] = 3;
        std::array<std::uint8_t, 36> desiredHeader{};
        writeU32LE(desiredHeader.data(), 44);
        writeU32LE(desiredHeader.data() + 16, 56);
        for(std::size_t index = 0; index < desiredHeader.size(); ++index) {
            continuation[8 + index] =
                desiredHeader[index] ^ keystream[8 + index];
        }
        std::copy_n(data + 44, 12, continuation.data() + 44);
        if(!invokeEmoteDecryptClosure(
               decryptFunc, continuation.data(), continuation.size()) ||
           !hasSanePsbV4ExtraOffsets(continuation.data(), size)) {
            LOGGER->warn(
                "E-mote PSB v4 extra-resource header remains encrypted");
            return false;
        }
        std::copy_n(continuation.data() + 44, 12, data + 44);
        return true;
    }

}

motion::ResourceManager::ResourceManager() : _state(std::make_shared<State>()) {}

motion::ResourceManager::ResourceManager(iTJSDispatch2 *kag,
                                         tjs_int cacheSize) :
    _state(std::make_shared<State>()) {
    LOGGER->info("kag: {}, cacheSize: {}", static_cast<void *>(kag), cacheSize);

    // Pre-define ShortCutInitialPadKeyMap on the KAG window if not already set.
    // The encrypted keybinder.tjs accesses .ShortCutInitialPadKeyMap on the
    // window object. If undefined, it crashes with "Invalid object context".
    if(kag) {
        const tjs_char *padKeys[] = {
            TJS_W("ShortCutInitialPadKeyMap"),
            TJS_W("ShortCutInitialGamePadKeyMap"),
            TJS_W("_proceedingKeyList"),
            nullptr
        };
        for(int i = 0; padKeys[i]; ++i) {
            tTJSVariant existing;
            if(TJS_FAILED(kag->PropGet(0, padKeys[i], nullptr, &existing, kag)) ||
               existing.Type() == tvtVoid) {
                iTJSDispatch2 *dict = TJSCreateDictionaryObject();
                if(dict) {
                    tTJSVariant v(dict, dict);
                    kag->PropSet(TJS_MEMBERENSURE, padKeys[i], nullptr,
                                 &v, kag);
                    dict->Release();
                }
            }
        }
    }
}

tjs_int motion::ResourceManager::getEmotePSBDecryptSeed() {
    return _decryptSeed;
}

void motion::ResourceManager::resetStaticStateForHostSession() {
    clearWarmMotionModuleCache();
    _decryptFunc.Clear();
    _decryptSeed = 0;
}

void motion::ResourceManager::trimStaticStateForMemoryPressure() {
    // Called only from the strongest (MAX) compaction level. A normal
    // MINIMIZE notification must not discard parsed Motion modules: scene
    // transitions routinely create short-lived ResourceManager wrappers, and
    // reparsing the same PSB on the application thread produces visible
    // frame spikes. The explicit MAX path remains available to release every
    // process-level warm module when the host really needs all caches gone.
    clearWarmMotionModuleCache();
}

bool motion::ResourceManager::applyEmotePSBDecryptFunc(
    std::uint8_t *data, const std::size_t size) {
    if(_decryptFunc.Type() != tvtObject ||
       !_decryptFunc.AsObjectNoAddRef()) {
        return true;
    }
    return invokeEmoteDecryptClosure(_decryptFunc, data, size) &&
        recoverLegacyPsbV4ExtraHeader(_decryptFunc, data, size);
}

tjs_error motion::ResourceManager::setEmotePSBDecryptSeed(tTJSVariant *,
                                                          tjs_int count,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *) {
    if(count != 1) {
        return TJS_E_BADPARAMCOUNT;
    }
    tjs_int parsedSeed = 0;
    if(!tryParseDecryptSeed(*p[0], parsedSeed)) {
        return TJS_E_INVALIDPARAM;
    }
    if(_decryptSeed != parsedSeed) {
        clearWarmMotionModuleCache();
    }
    _decryptSeed = parsedSeed;
    LOGGER->info("setEmotePSBDecryptSeed: {}", _decryptSeed);
    return TJS_S_OK;
}

tjs_error motion::ResourceManager::setEmotePSBDecryptFunc(tTJSVariant *r,
                                                          tjs_int n,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *obj) {
    if(n == 0) {
        clearWarmMotionModuleCache();
        _decryptFunc.Clear();
        LOGGER->info("setEmotePSBDecryptFunc: cleared");
        return TJS_S_OK;
    }
    if(n != 1) {
        return TJS_E_BADPARAMCOUNT;
    }
    if((*p)->Type() != tvtObject && (*p)->Type() != tvtVoid) {
        return TJS_E_INVALIDPARAM;
    }

    clearWarmMotionModuleCache();
    _decryptFunc = *p[0];
    if(_decryptFunc.Type() == tvtObject && _decryptFunc.AsObjectNoAddRef()) {
        LOGGER->info("setEmotePSBDecryptFunc: callback registered");
    } else {
        LOGGER->info("setEmotePSBDecryptFunc: cleared");
    }
    return TJS_S_OK;
}

tTJSVariant motion::ResourceManager::load(ttstr path) const {
    const auto rawPath = path.AsStdString();
    const auto loweredPath = lowercase(rawPath);
    if((loweredPath.find(".mtn") != std::string::npos ||
        loweredPath.find(".mt") != std::string::npos) &&
       motionResourceDebugEnabled()) {
        LOGGER->info("Motion resource manager load: {}", rawPath);
    }

    // ResourceManager is shared by the script-facing wrapper and every
    // EmotePlayer created from it.  A load of an already registered module
    // must therefore reuse that module instead of parsing and recursively
    // scanning the complete PSB again on the render thread.
    if(const auto cached = findLoaded(path); cached.Type() != tvtVoid) {
        _state->lastLoadedPath = rawPath;
        _state->lastLoadedModule = cached;
        rememberWarmMotionModule(cached);
        return cached;
    }

    // KAG layer clones and scene transitions construct independent resource
    // managers for the same motions. Reuse a recently parsed immutable
    // snapshot when both the resolved storage path and decrypt seed match;
    // otherwise each expression/source replacement reparses the large PSB on
    // the application thread.
    if(const auto warm = reuseExactWarmMotionModule(path, _decryptSeed);
       warm.module.Type() == tvtObject) {
        rememberLoadedModule(path, warm.module, warm.snapshot);
        return warm.module;
    }

    const auto alias = _state
        ? fallbackSplitEmoteModule(_state->lastLoadedModule, path)
        : tTJSVariant{};
    if(alias.Type() != tvtVoid) {
        rememberLoadedModule(path, alias);
        rememberWarmMotionModule(alias);
        return alias;
    }

    const auto recentAlias = fallbackSplitEmoteModule(
        mostRecentWarmMotionModule(), path);
    if(recentAlias.Type() != tvtVoid) {
        rememberLoadedModule(path, recentAlias);
        rememberWarmMotionModule(recentAlias);
        return recentAlias;
    }

    std::shared_ptr<detail::MotionSnapshot> loadedSnapshot;
    const auto loaded = detail::loadPSBVariant(
        path, _decryptSeed, &loadedSnapshot);
    if(loaded.Type() != tvtVoid && _state) {
        rememberLoadedModule(path, loaded, loadedSnapshot);
        rememberWarmMotionModule(loaded, std::move(loadedSnapshot));
    }
    return loaded;
}

void motion::ResourceManager::rememberLoadedModule(
    ttstr path, const tTJSVariant &loaded,
    std::shared_ptr<detail::MotionSnapshot> snapshot) const {
    if(loaded.Type() == tvtVoid || !_state) {
        return;
    }

    const auto key = path.AsStdString();
    if(!key.empty()) {
        _state->loadedModules[key] = loaded;
        _state->lastLoadedPath = key;
    }

    ttstr trimmed = path;
    if(path.StartsWith(TJS_W("lzfs://./"))) {
        trimmed = path.SubString(9, path.GetLen() - 9);
        const auto trimmedKey = trimmed.AsStdString();
        if(!trimmedKey.empty()) {
            _state->loadedModules[trimmedKey] = loaded;
        }
    }

    const ttstr storage = TVPExtractStorageName(trimmed);
    if(!storage.IsEmpty()) {
        const auto storageKey = storage.AsStdString();
        _state->loadedModules[storageKey] = loaded;
        if(storageKey.rfind("dx_", 0) == 0 && storageKey.size() > 3) {
            _state->loadedModules[storageKey.substr(3)] = loaded;
        }
    }

    const ttstr placed = TVPGetPlacedPath(trimmed);
    if(!placed.IsEmpty()) {
        _state->loadedModules[placed.AsStdString()] = loaded;
    }
    _state->lastLoadedModule = loaded;
    if(loaded.Type() == tvtObject) {
        if(auto *object = loaded.AsObjectNoAddRef()) {
            if(!snapshot) {
                snapshot = detail::lookupModuleSnapshot(loaded);
            }
            if(snapshot) {
                _state->cachedSnapshots[object] = std::move(snapshot);
            }
            _state->moduleLoadGenerations[object] =
                ++_state->nextLoadGeneration;
        }
    }

    for(auto it = _state->cachedSnapshots.begin();
        it != _state->cachedSnapshots.end();) {
        const auto object = it->first;
        const bool referenced = std::any_of(
            _state->loadedModules.begin(), _state->loadedModules.end(),
            [object](const auto &entry) {
                return entry.second.Type() == tvtObject &&
                    entry.second.AsObjectNoAddRef() == object;
            });
        if(!referenced) {
            _state->moduleLoadGenerations.erase(object);
            it = _state->cachedSnapshots.erase(it);
        } else {
            ++it;
        }
    }
}

void motion::ResourceManager::unload(ttstr path) const {
    LOGGER->debug("ResourceManager::unload({})", path.AsStdString());
    if(!_state) {
        return;
    }

    const auto key = path.AsStdString();
    const auto loaded = findLoadedModule(path);
    iTJSDispatch2 *object = loaded.Type() == tvtObject
        ? loaded.AsObjectNoAddRef()
        : nullptr;
    if(object != nullptr) {
        for(auto it = _state->loadedModules.begin();
            it != _state->loadedModules.end();) {
            if(it->second.Type() == tvtObject &&
               it->second.AsObjectNoAddRef() == object) {
                it = _state->loadedModules.erase(it);
            } else {
                ++it;
            }
        }
        _state->moduleLoadGenerations.erase(object);
        _state->cachedSnapshots.erase(object);
    } else {
        _state->loadedModules.erase(key);
    }
    if(_state->lastLoadedPath == key ||
       (_state->lastLoadedModule.Type() == tvtObject &&
        _state->lastLoadedModule.AsObjectNoAddRef() == object)) {
        _state->lastLoadedPath.clear();
        _state->lastLoadedModule.Clear();
    }
}

void motion::ResourceManager::clearCache() const {
    LOGGER->debug("ResourceManager::clearCache()");
    if(!_state) {
        return;
    }

    _state->loadedModules.clear();
    _state->moduleLoadGenerations.clear();
    _state->cachedSnapshots.clear();
    _state->nextLoadGeneration = 0;
    _state->lastLoadedPath.clear();
    _state->lastLoadedModule.Clear();
}

tTJSVariant motion::ResourceManager::getLastLoadedModule() const {
    return _state ? _state->lastLoadedModule : tTJSVariant{};
}

tTJSVariant motion::ResourceManager::findLoaded(ttstr path) const {
    if(!_state) {
        return {};
    }

    const auto it = _state->loadedModules.find(path.AsStdString());
    return it != _state->loadedModules.end() ? it->second : tTJSVariant{};
}

tTJSVariant motion::ResourceManager::findLoadedModule(ttstr path) const {
    if(!_state || path.IsEmpty()) {
        return {};
    }

    const auto tryKey = [this](const std::string &key) -> tTJSVariant {
        if(key.empty()) {
            return {};
        }
        const auto it = _state->loadedModules.find(key);
        return it != _state->loadedModules.end() ? it->second : tTJSVariant{};
    };

    if(auto loaded = tryKey(path.AsStdString()); loaded.Type() == tvtObject) {
        return loaded;
    }

    ttstr trimmed = path;
    if(path.StartsWith(TJS_W("lzfs://./"))) {
        trimmed = path.SubString(9, path.GetLen() - 9);
    }
    if(auto loaded = tryKey(trimmed.AsStdString()); loaded.Type() == tvtObject) {
        return loaded;
    }

    const ttstr placed = TVPGetPlacedPath(trimmed);
    if(!placed.IsEmpty()) {
        if(auto loaded = tryKey(placed.AsStdString()); loaded.Type() == tvtObject) {
            return loaded;
        }
    }

    for(const auto &candidate : detail::buildMotionLookupCandidates(path)) {
        if(auto loaded = tryKey(candidate.AsStdString()); loaded.Type() == tvtObject) {
            return loaded;
        }
        const ttstr candidatePlaced = TVPGetPlacedPath(candidate);
        if(!candidatePlaced.IsEmpty()) {
            if(auto loaded = tryKey(candidatePlaced.AsStdString());
               loaded.Type() == tvtObject) {
                return loaded;
            }
        }
    }

    if(motionResourceDebugEnabled()) {
        LOGGER->info("ResourceManager::findLoadedModule({}): cache miss",
                     path.AsStdString());
    }
    return {};
}

tTJSVariant motion::ResourceManager::findSource(ttstr path) const {
    return findLoadedModule(path);
}

std::size_t motion::ResourceManager::uniqueCachedModuleCount() const {
    return uniqueCachedModules().size();
}

std::vector<motion::ResourceManager::CachedModuleEntry>
motion::ResourceManager::uniqueCachedModules() const {
    std::vector<CachedModuleEntry> result;
    if(!_state) {
        return result;
    }

    std::unordered_set<iTJSDispatch2 *> seen;
    for(const auto &[key, module] : _state->loadedModules) {
        if(module.Type() != tvtObject) {
            continue;
        }
        iTJSDispatch2 *obj = module.AsObjectNoAddRef();
        if(!obj || !seen.insert(obj).second) {
            continue;
        }
        const auto generation = _state->moduleLoadGenerations.find(obj);
        result.push_back({
            key,
            module,
            generation != _state->moduleLoadGenerations.end()
                ? generation->second
                : 0,
        });
    }
    return result;
}
