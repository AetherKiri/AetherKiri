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
        (void)psb;
        (void)deDuplication;
        // Archive-info PSBs describe an external MDF/PSB file table.  They do
        // not own media chunks; returning no metadata is the format contract
        // (and matches the upstream FreeMote handler).  The owning archive
        // loader exposes the referenced files through its normal Storage
        // provider instead.
        return {};
    }
} // namespace PSB
