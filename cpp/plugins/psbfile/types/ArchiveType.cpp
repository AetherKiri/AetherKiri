//
// Created by LiDon on 2025/9/15.
//

#include "../PSBFile.h"
#include "ArchiveType.h"

namespace PSB {

    bool ArchiveType::isThisType(const PSBFile &psb) {
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
        const std::string &id = idValue->value;

        return id == "archive" ||
            (id == "scenario" && objects->find("file_info") != objects->end());
    }

    std::vector<std::unique_ptr<IResourceMetadata>>
    ArchiveType::collectResources(const PSBFile &psb, bool deDuplication) {
        return {};
    }
} // namespace PSB
