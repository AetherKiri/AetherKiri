//
// Created by LiDon on 2025/9/15.
//

#include "ScnType.h"
#include "ResourceMetadataHelpers.h"

namespace PSB {

    bool ScnType::isThisType(const PSBFile &psb) {
        const auto objects = psb.getObjects();
        if(psb.getObjects() == nullptr) {
            return false;
        }

        if(objects->find("scenes") != objects->end() &&
           objects->find("name") != objects->end()) {
            return true;
        }

        if(objects->find("list") != objects->end() &&
           objects->find("map") != objects->end() && !psb.resources.empty()) {
            return true;
        }

        return false;
    }

    std::vector<std::unique_ptr<IResourceMetadata>>
    ScnType::collectResources(const PSBFile &psb, bool deDuplication) {
        Metadata::Collector collector;
        collector.deduplicate = deDuplication;
        collector.spec = psb.getPlatform();
        collector.type = PSBType::Scn;

        // SCN resources are intentionally exported from the root dictionary.
        // Nested line/event dictionaries are script data, not stable storage
        // names; the generic PSB media registration already exposes those
        // paths when a title needs them.
        const auto root = psb.getObjects();
        if(!root) {
            return {};
        }
        for(const auto &[name, value] : *root) {
            if(const auto resource = Metadata::AsResource(value)) {
                collector.add(name, {}, resource);
            }
        }
        return std::move(collector.resources);
    }
} // namespace PSB
