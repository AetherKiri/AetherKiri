//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "tjs.h"

namespace motion {

    namespace detail {
        struct MotionSnapshot;
    }

    class ResourceManager {
    public:
        ResourceManager();

        explicit ResourceManager(iTJSDispatch2 *kag, tjs_int cacheSize);

        tTJSVariant load(ttstr path) const;
        void unload(ttstr path) const;
        void clearCache() const;
        tTJSVariant getLastLoadedModule() const;
        void rememberLoadedModule(
            ttstr path, const tTJSVariant &loaded,
            std::shared_ptr<detail::MotionSnapshot> snapshot = {}) const;
        tTJSVariant findLoaded(ttstr path) const;
        tTJSVariant findLoadedModule(ttstr path) const;
        tTJSVariant findSource(ttstr path) const;
        [[nodiscard]] std::size_t uniqueCachedModuleCount() const;
        struct CachedModuleEntry {
            std::string key;
            tTJSVariant module;
            std::uint64_t loadGeneration = 0;
        };
        [[nodiscard]] std::vector<CachedModuleEntry> uniqueCachedModules() const;
        [[nodiscard]] static tjs_int getEmotePSBDecryptSeed();
        static bool applyEmotePSBDecryptFunc(std::uint8_t *data,
                                             std::size_t size);
        // Drop process-level warm PSB/TJS objects during an explicit memory
        // compaction pass. Active players retain their own snapshots.
        static void trimStaticStateForMemoryPressure();
        [[nodiscard]] static tjs_int getDecryptSeed() {
            return getEmotePSBDecryptSeed();
        }
        static void resetStaticStateForHostSession();

        static tjs_error setEmotePSBDecryptSeed(tTJSVariant *r, tjs_int count,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptFunc(tTJSVariant *r, tjs_int n,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

    private:
        struct State {
            std::unordered_map<std::string, tTJSVariant> loadedModules;
            std::unordered_map<iTJSDispatch2 *, std::uint64_t>
                moduleLoadGenerations;
            std::unordered_map<
                iTJSDispatch2 *, std::shared_ptr<detail::MotionSnapshot>>
                cachedSnapshots;
            std::uint64_t nextLoadGeneration = 0;
            std::string lastLoadedPath;
            tTJSVariant lastLoadedModule;
        };

        std::shared_ptr<State> _state;
        inline static int _decryptSeed;
        inline static tTJSVariant _decryptFunc;
    };
} // namespace motion
