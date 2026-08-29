//
// Created by LiDon on 2025/9/15.
//

#include "ImageType.h"
#include "ResourceMetadataHelpers.h"

namespace PSB {
    bool ImageType::isThisType(const PSBFile &psb) {
        const auto objects = psb.getObjects();
        if(objects == nullptr) {
            return false;
        }

        const auto fdId = objects->find("id");
        if(fdId == objects->end())
            return false;
        const auto idValue = std::dynamic_pointer_cast<PSBString>(fdId->second);
        if(!idValue)
            return false;

        return idValue->value == "image";
    }

    std::vector<std::unique_ptr<IResourceMetadata>>
    ImageType::collectResources(const PSBFile &psb, bool deDuplication) {
        Metadata::Collector collector;
        collector.deduplicate = deDuplication;
        collector.spec = psb.getPlatform();
        collector.type = PSBType::Tachie;

        const auto root = psb.getObjects();
        if(!root) {
            return {};
        }

        // FreeMote's ImageType walks imageList and uses the authored label
        // chain as the part name.  Keep that convention so callers can use
        // the indexed aliases (`<resource>.tlg`) while still retaining
        // palette and crop metadata from each leaf dictionary.
        auto imageList = (*root)[G_ImageSourceKey];
        if(imageList) {
            Metadata::CollectTachieValue(
                collector, imageList, {}, G_ImageSourceKey);
        }

        // A few exporters omit imageList and put pixel resources directly in
        // the root.  The generic fallback keeps those files readable without
        // changing the normal ImageType naming convention.
        if(collector.resources.empty()) {
            for(const auto &[key, value] : *root) {
                if(const auto resource = Metadata::AsResource(value)) {
                    collector.add(key, {}, resource, nullptr, nullptr, 1, 1);
                }
            }
        }
        return std::move(collector.resources);
    }
} // namespace PSB
