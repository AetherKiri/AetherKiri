//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "tjs.h"

namespace motion {

    class ResourceManager {
    public:
        ResourceManager();

        explicit ResourceManager(iTJSDispatch2 *kag, tjs_int cacheSize);

        tTJSVariant load(ttstr path) const;
        tTJSVariant loadSource(ttstr path) const;
        void unload(ttstr path) const;
        void clearCache() const;
        tTJSVariant getLastLoadedModule() const;
        tTJSVariant findLoaded(ttstr path) const;
        tTJSVariant findSource(ttstr path) const;
        tjs_int requireLayerId();
        tjs_int requireLayerIdForName(ttstr name);
        void releaseLayerId(tjs_int id);
        [[nodiscard]] static tjs_int getEmotePSBDecryptSeed();

        static tjs_error setEmotePSBDecryptSeed(tTJSVariant *r, tjs_int count,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptFunc(tTJSVariant *r, tjs_int n,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

    private:
        struct State {
            std::unordered_map<std::string, tTJSVariant> loadedModules;
            std::string lastLoadedPath;
            tTJSVariant lastLoadedModule;
            std::unordered_map<std::string, tjs_int> layerIdsByName;
            std::unordered_map<tjs_int, std::string> layerNamesById;
            std::unordered_set<tjs_int> usedLayerIds;
            tjs_int nextLayerId = 1;
        };

        std::shared_ptr<State> _state;
        inline static int _decryptSeed;
    };
} // namespace motion
