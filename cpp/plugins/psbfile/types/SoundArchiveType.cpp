//
// Created by LiDon on 2025/9/15.
//

#include "SoundArchiveType.h"
#include "ResourceMetadataHelpers.h"

namespace PSB {

    bool SoundArchiveType::isThisType(const PSBFile &psb) {
        const auto objects = psb.getObjects();
        if(objects == nullptr) {
            return false;
        }

        const auto fdId = objects->find("id");
        if(fdId == objects->end()) {
            return false;
        }
        const auto str = std::dynamic_pointer_cast<PSBString>(fdId->second);
        return str != nullptr && str->value == "sound_archive";
    }

    std::vector<std::unique_ptr<IResourceMetadata>>
    SoundArchiveType::collectResources(const PSBFile &psb, bool deDuplication) {
        Metadata::Collector collector;
        collector.deduplicate = deDuplication;
        collector.spec = psb.getPlatform();
        collector.type = PSBType::SoundArchive;

        const auto root = psb.getObjects();
        if(!root) {
            return {};
        }
        const auto voice = Metadata::AsDictionary((*root)[G_VoiceResourceKey]);
        if(!voice) {
            return {};
        }

        // A sound_archive stores one or more channel payloads below each
        // named voice.  The generic object walk exposes the implementation
        // paths, but the public plugin contract also expects the authored
        // voice name to be openable.  Register the first payload under that
        // name and keep all channel paths available through the generic walk.
        for(const auto &[name, value] : *voice) {
            auto resource = Metadata::FindFirstResource(value);
            if(!resource) {
                // Some producers keep only a numeric resource reference in a
                // compact channel dictionary.  Recover it from the parsed
                // chunk table when possible.
                if(const auto dictionary = Metadata::AsDictionary(value)) {
                    for(const char *key : {"data", "resource", "pixel"}) {
                        const auto candidate = (*dictionary)[key];
                        if(const auto ref = std::dynamic_pointer_cast<PSBResource>(candidate)) {
                            if(ref->index.has_value()) {
                                resource = Metadata::FindFirstResourceByIndex(
                                    psb, ref->index.value());
                            }
                        }
                        if(resource) {
                            break;
                        }
                    }
                }
            }
            if(!resource) {
                continue;
            }
            collector.add(name, G_VoiceResourceKey, resource, nullptr, nullptr);
        }

        return std::move(collector.resources);
    }
} // namespace PSB
