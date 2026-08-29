//
// Created by LiDon on 2025/9/15.
//

#include "MmoType.h"
#include "ResourceMetadataHelpers.h"

namespace PSB {
    bool MmoType::isThisType(const PSBFile &psb) {
        const auto objects = psb.getObjects();
        if(psb.getObjects() == nullptr) {
            return false;
        }
        auto fdOC = objects->find("objectChildren");
        auto fdSC = objects->find("sourceChildren");
        return fdOC != objects->end() && fdSC != objects->end();
    }

    std::vector<std::unique_ptr<IResourceMetadata>>
    MmoType::collectResources(const PSBFile &psb, bool deDuplication) {
        Metadata::Collector collector;
        collector.deduplicate = deDuplication;
        collector.spec = psb.getPlatform();
        collector.type = PSBType::Mmo;

        const auto root = psb.getObjects();
        if(!root) {
            return {};
        }

        // MMO projects keep image leaves below both sourceChildren and
        // bgChildren.  Traverse each branch with its part name so duplicate
        // resources can be collapsed by index while preserving the authored
        // label and dimensions.
        for(const auto *key : {G_MmoBgSourceKey.c_str(),
                               G_MmoSourceKey.c_str()}) {
            const auto branch = (*root)[key];
            if(branch) {
                Metadata::CollectMmoValue(collector, branch, key, {}, key);
            }
        }

        return std::move(collector.resources);
    }
} // namespace PSB
