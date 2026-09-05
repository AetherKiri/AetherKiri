//
// Created by LiDon on 2025/9/11.
//
#pragma once

#include <list>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

#include "PSBValue.h"
#include "StorageIntf.h"
#include "resources/ImageMetadata.h"

namespace PSB {
    namespace detail {
        bool IsSupportedImageHeader(const std::vector<uint8_t> &data);
    }

    struct PSBMediaCacheStats {
        size_t entryCount = 0;
        size_t entryLimit = 0;
        size_t bytesInUse = 0;
        size_t byteLimit = 0;
        uint64_t hitCount = 0;
        uint64_t missCount = 0;
    };

    class PSBMedia;
    bool GetPSBMediaCacheStats(PSBMediaCacheStats &outStats);
    void SetPSBMediaCacheBudget(size_t maxEntries, size_t maxBytes);
    PSBMedia *GetGlobalPSBMedia();

    class PSBMedia : public iTVPStorageMedia {
    public:
        PSBMedia();

        ~PSBMedia() override = default;

        void AddRef() override { _ref++; }

        void Release() override {
            if(_ref == 1)
                delete this;
            else
                _ref--;
        }

        void GetName(ttstr &name) override { name = TJS_W("psb"); }

        void NormalizeDomainName(ttstr &name) override;

        void NormalizePathName(ttstr &name) override;

        bool CheckExistentStorage(const ttstr &name) override;

        tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override;

        void GetListAt(const ttstr &name, iTVPStorageLister *lister) override;

        void GetLocallyAccessibleName(ttstr &name) override;

        void add(const std::string &name,
                 const std::shared_ptr<PSBResource> &resource,
                 const ImageMetadata *imageMeta = nullptr);
        void removeByPrefix(const std::string &prefix);
        void clear();
        void setCacheBudget(size_t maxEntries, size_t maxBytes);
        PSBMediaCacheStats getCacheStats() const;

    public:
        struct CachedImageInfo {
            std::string debugKey;
            std::string label;
            int width = 0;
            int height = 0;
            int left = 0;
            int top = 0;
            int opacity = 255;
            bool visible = true;
            int layerType = 0;
            std::string type;
            std::string paletteType;
            PSBSpec spec = PSBSpec::Other;
            PSBCompressType compress = PSBCompressType::ByName;
            std::vector<uint8_t> palette;
        };
        struct CacheEntry {
            std::shared_ptr<PSBResource> resource;
            std::shared_ptr<std::vector<uint8_t>> convertedImage;
            CachedImageInfo imageInfo;
            bool hasImageInfo = false;
            size_t sizeBytes = 0;
            std::list<std::string>::iterator lruIt;
        };

        struct ImageInfoEntry {
            std::string key;
            CachedImageInfo info;
        };
        bool ensureArchiveLoaded(const std::string &archiveKey,
                                 bool reloadIfLoaded = false);
        std::vector<ImageInfoEntry> getImagesByPrefix(const std::string &prefix) const;
        bool getImageInfo(const std::string &key, CachedImageInfo &outInfo) const;

        // Motion PSBs can contain the source images consumed by a sliced
        // layer without shipping standalone .tlg files.  Keep the authored
        // image order as a storage-level capability; the private PackinOne
        // compatibility layer may resolve a bare slice request to one of
        // these indexed aliases without knowing a title's layer names.
        void addMotionSliceSet(std::string archiveKey,
                               std::vector<std::string> imageKeys,
                               int authoredWidth = 0,
                               int authoredHeight = 0);
        bool resolveMotionSliceStorage(const std::string &request,
                                       std::string &resolved);
        bool getMotionSliceCanvasSize(const std::string &storage,
                                      int &width,
                                      int &height) const;

        struct LayerPosition {
            std::string sceneName;
            std::string layerName;
            std::string srcPath;
            float left = 0;
            float top = 0;
            int width = 0;
            int height = 0;
            int opacity = 255;
            bool visible = true;
        };
        void addLayerPositions(const std::string &archiveKey,
                               std::vector<LayerPosition> positions);
        std::vector<LayerPosition> getLayerPositions(const std::string &prefix) const;

        struct ButtonBoundInfo {
            std::string sceneName;
            std::string buttonName;
            std::string imageKey;
            float left = 0;
            float top = 0;
            int width = 0;
            int height = 0;
        };
        void addButtonBounds(const std::string &archiveKey,
                             std::vector<ButtonBoundInfo> bounds);
        std::vector<ButtonBoundInfo> getButtonBounds(const std::string &prefix) const;

    private:
        using ResourceMap = std::unordered_map<std::string, CacheEntry>;

        std::string canonicalizeKey(const std::string &key) const;
        std::vector<std::string> discoverMotionArchives(
            const std::string &request);
        ResourceMap::iterator findBySuffixLocked(const std::string &key);
        bool tryLazyLoadArchive(const std::string &key,
                                bool reloadIfLoaded = false);
        void touchLocked(CacheEntry &entry);
        void adaptBudgetByMemoryPressureLocked();
        void evictIfNeededLocked();

        int _ref = 0;
        mutable std::mutex _mutex;
        ResourceMap _resources;
        std::list<std::string> _lru;
        size_t _bytesInUse = 0;
        size_t _configuredMaxEntryCount = 2048;
        size_t _configuredMaxByteSize = 192ULL * 1024ULL * 1024ULL;
        size_t _maxEntryCount = 2048;
        size_t _maxByteSize = 192ULL * 1024ULL * 1024ULL;
        uint64_t _hitCount = 0;
        uint64_t _missCount = 0;
        std::unordered_set<std::string> _loadedArchives;
        std::unordered_set<std::string> _failedArchives;
        std::unordered_set<std::string> _knownResourceKeys;
        std::unordered_set<std::string> _missingResourceKeys;
        std::unordered_map<std::string, std::vector<LayerPosition>> _layerPositions;
        std::unordered_map<std::string, std::vector<ButtonBoundInfo>> _buttonBoundsMap;
        mutable std::mutex _motionDiscoveryMutex;
        std::unordered_set<std::string> _motionScannedRoots;
        std::vector<std::string> _motionKnownArchives;

        struct MotionSliceSet {
            std::string archiveKey;
            std::vector<std::string> imageKeys;
            std::unordered_map<std::string, std::string> assignments;
            int authoredWidth = 0;
            int authoredHeight = 0;
            size_t nextImage = 0;
            uint64_t generation = 0;
        };
        std::vector<MotionSliceSet> _motionSliceSets;
        std::unordered_map<std::string, std::string> _motionSliceAliases;
        uint64_t _motionSliceGeneration = 0;
    };
} // namespace PSB
