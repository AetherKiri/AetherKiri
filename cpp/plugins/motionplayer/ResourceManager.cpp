//
// Created by LiDon on 2025/9/15.
//

#include "ResourceManager.h"
#include "tjsDictionary.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <vector>

#include <spdlog/spdlog.h>

#include "RuntimeSupport.h"
#include "StorageIntf.h"

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
        if(value.size() < suffix.size()) {
            return false;
        }
        if(value.compare(value.size() - suffix.size(), suffix.size(), suffix) !=
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

    bool labelMatchesSplitEmote(const std::string &label,
                                const std::vector<std::string> &candidates) {
        const auto lowered = lowercase(label);
        for(const auto &candidate : candidates) {
            if(candidate.empty()) {
                continue;
            }
            if(lowered == candidate ||
               (lowered.size() > candidate.size() &&
                lowered.compare(lowered.size() - candidate.size(),
                                candidate.size(), candidate) == 0)) {
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
        if(std::any_of(snapshot.mainTimelineLabels.begin(),
                       snapshot.mainTimelineLabels.end(), matches) ||
           std::any_of(snapshot.diffTimelineLabels.begin(),
                       snapshot.diffTimelineLabels.end(), matches)) {
            return true;
        }
        for(const auto &[label, clip] : snapshot.clipsByLabel) {
            (void)clip;
            if(matches(label)) {
                return true;
            }
        }
        return false;
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

    tTJSVariant &recentMotionModule() {
        static tTJSVariant module;
        return module;
    }

    void rememberRecentMotionModule(const tTJSVariant &loaded) {
        if(loaded.Type() == tvtObject &&
           motion::detail::lookupModuleSnapshot(loaded)) {
            recentMotionModule() = loaded;
        }
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
    _decryptSeed = parsedSeed;
    LOGGER->info("setEmotePSBDecryptSeed: {}", _decryptSeed);
    return TJS_S_OK;
}

tjs_error motion::ResourceManager::setEmotePSBDecryptFunc(tTJSVariant *r,
                                                          tjs_int n,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *obj) {
    if(n == 0) {
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

    const auto alias = _state ? fallbackSplitEmoteModule(_state->lastLoadedModule,
                                                         path)
                              : tTJSVariant{};
    if(alias.Type() != tvtVoid) {
        rememberLoadedModule(path, alias);
        return alias;
    }

    const auto recentAlias = fallbackSplitEmoteModule(recentMotionModule(), path);
    if(recentAlias.Type() != tvtVoid) {
        rememberLoadedModule(path, recentAlias);
        return recentAlias;
    }

    const auto loaded = detail::loadPSBVariant(path, _decryptSeed);
    if(loaded.Type() != tvtVoid && _state) {
        rememberLoadedModule(path, loaded);
    }
    return loaded;
}

void motion::ResourceManager::rememberLoadedModule(
    ttstr path, const tTJSVariant &loaded) const {
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
    rememberRecentMotionModule(loaded);
}

void motion::ResourceManager::unload(ttstr path) const {
    LOGGER->debug("ResourceManager::unload({})", path.AsStdString());
    if(!_state) {
        return;
    }

    const auto key = path.AsStdString();
    _state->loadedModules.erase(key);
    if(_state->lastLoadedPath == key) {
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
        result.push_back({ key, module });
    }
    return result;
}
