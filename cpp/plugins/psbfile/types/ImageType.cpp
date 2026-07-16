//
// Created by LiDon on 2025/9/15.
//

#include "../PSBFile.h"
#include "ImageType.h"

namespace PSB {
#define LOGGER spdlog::get("plugin")

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
        std::vector<std::unique_ptr<IResourceMetadata>> resourceList;
        // TODO:
        LOGGER->critical("TODO: ImageType::collectResources(...)");
        return resourceList;
    }
} // namespace PSB
