#pragma once

// Shared, defensive metadata helpers for the non-motion PSB families.
//
// The original FreeMote handlers expose the same resource table through
// slightly different object layouts (tachie/image, MMO, SCN and sound
// archives).  Keeping the traversal here avoids four subtly different
// implementations and, more importantly, keeps malformed optional metadata
// from taking down the PSB media provider.  The parser remains the owner of
// the object graph; this header only creates lightweight ImageMetadata views.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Consts.h"
#include "../PSBFile.h"
#include "../PSBValue.h"
#include "../resources/ImageMetadata.h"

namespace PSB::Metadata {

inline std::shared_ptr<PSBDictionary>
AsDictionary(const std::shared_ptr<IPSBValue> &value) {
    return std::dynamic_pointer_cast<PSBDictionary>(value);
}

inline std::shared_ptr<PSBList> AsList(const std::shared_ptr<IPSBValue> &value) {
    return std::dynamic_pointer_cast<PSBList>(value);
}

inline std::shared_ptr<PSBResource>
AsResource(const std::shared_ptr<IPSBValue> &value) {
    return std::dynamic_pointer_cast<PSBResource>(value);
}

inline std::string StringValue(const std::shared_ptr<IPSBValue> &value) {
    if(const auto string = std::dynamic_pointer_cast<PSBString>(value)) {
        return string->value;
    }
    return {};
}

inline int NumberValue(const std::shared_ptr<IPSBValue> &value,
                       const int fallback = 0) {
    const auto number = std::dynamic_pointer_cast<PSBNumber>(value);
    if(!number) {
        if(const auto string = std::dynamic_pointer_cast<PSBString>(value)) {
            try {
                return std::stoi(string->value);
            } catch(...) {
                return fallback;
            }
        }
        return fallback;
    }

    switch(number->numberType) {
        case PSBNumberType::Float:
            return static_cast<int>(number->getValue<float>());
        case PSBNumberType::Double:
            return static_cast<int>(number->getValue<double>());
        case PSBNumberType::Int:
            return number->getValue<int>();
        case PSBNumberType::Long:
        default:
            return static_cast<int>(number->getLongValue());
    }
}

inline std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

inline PSBCompressType ParseCompression(const std::string &value) {
    const auto lower = LowerAscii(value);
    if(lower == "rl" || lower == "rle") {
        return PSBCompressType::RL;
    }
    if(lower == "bmp") {
        return PSBCompressType::Bmp;
    }
    if(lower == "tlg") {
        return PSBCompressType::Tlg;
    }
    if(lower == "none" || lower == "raw") {
        return PSBCompressType::None;
    }
    return PSBCompressType::ByName;
}

inline std::string ResourceIdentity(const std::shared_ptr<PSBResource> &resource) {
    if(!resource) {
        return {};
    }
    if(resource->index.has_value()) {
        return std::string(resource->isExtra ? "extra:" : "resource:") +
            std::to_string(resource->index.value());
    }
    return "ptr:" + std::to_string(
        reinterpret_cast<std::uintptr_t>(resource.get()));
}

inline std::shared_ptr<PSBResource>
FindDirectResource(const std::shared_ptr<PSBDictionary> &dictionary,
                   const char *key = "pixel") {
    if(!dictionary || !key) {
        return nullptr;
    }
    const auto it = dictionary->find(key);
    return it == dictionary->end() ? nullptr : AsResource(it->second);
}

inline std::shared_ptr<PSBResource>
FindFirstResource(const std::shared_ptr<IPSBValue> &value, int depth = 0) {
    if(!value || depth > 64) {
        return nullptr;
    }
    if(const auto resource = AsResource(value)) {
        return resource;
    }
    if(const auto dictionary = AsDictionary(value)) {
        for(const auto &[key, child] : *dictionary) {
            (void)key;
            if(const auto resource = FindFirstResource(child, depth + 1)) {
                return resource;
            }
        }
        return nullptr;
    }
    if(const auto list = AsList(value)) {
        for(const auto &child : *list) {
            if(const auto resource = FindFirstResource(child, depth + 1)) {
                return resource;
            }
        }
    }
    return nullptr;
}

inline std::shared_ptr<PSBResource>
FindFirstResourceByIndex(const PSBFile &file, const std::uint32_t index) {
    for(const auto &resource : file.resources) {
        if(resource && resource->index == index) {
            return resource;
        }
    }
    for(const auto &resource : file.extraResources) {
        if(resource && resource->index == index) {
            return resource;
        }
    }
    return nullptr;
}

inline void ApplyImageFields(
    ImageMetadata &metadata, const std::shared_ptr<PSBDictionary> &dictionary,
    const std::shared_ptr<PSBDictionary> &parent = nullptr) {
    if(!dictionary) {
        return;
    }

    auto get = [&](const char *key) { return (*dictionary)[key]; };
    const auto parentValue = [&](const char *key) {
        return parent ? (*parent)[key] : std::shared_ptr<IPSBValue>{};
    };

    const int width = NumberValue(get("width"),
                                  NumberValue(get("truncated_width"),
                                              NumberValue(parentValue("width"),
                                                          metadata.getWidth())));
    const int height = NumberValue(get("height"),
                                   NumberValue(get("truncated_height"),
                                               NumberValue(parentValue("height"),
                                                           metadata.getHeight())));
    if(width > 0) {
        metadata.setWidth(width);
    }
    if(height > 0) {
        metadata.setHeight(height);
    }

    metadata.setTop(NumberValue(get("top"), NumberValue(parentValue("top"),
                                                         metadata.getTop())));
    metadata.setLeft(NumberValue(get("left"), NumberValue(parentValue("left"),
                                                           metadata.getLeft())));
    metadata.setOpacity(NumberValue(
        get("opacity"), NumberValue(parentValue("opacity"), metadata.getOpacity())));
    metadata.setVisible(NumberValue(
                           get("visible"),
                           NumberValue(parentValue("visible"),
                                       metadata.getVisible() ? 1 : 0)) != 0);

    for(const char *key : {"type", "texture_type", "pixel_type"}) {
        const auto value = StringValue(get(key));
        if(!value.empty()) {
            metadata.setType(value);
            break;
        }
    }
    if(metadata.getType().empty() && parent) {
        for(const char *key : {"type", "texture_type", "pixel_type"}) {
            const auto value = StringValue(parentValue(key));
            if(!value.empty()) {
                metadata.setType(value);
                break;
            }
        }
    }

    for(const char *key : {"palType", "pal_type", "palette_type",
                           "clut_type"}) {
        const auto value = StringValue(get(key));
        if(!value.empty()) {
            metadata.setPalType(value);
            break;
        }
    }
    if(metadata.getPalType().empty() && parent) {
        for(const char *key : {"palType", "pal_type", "palette_type",
                               "clut_type"}) {
            const auto value = StringValue(parentValue(key));
            if(!value.empty()) {
                metadata.setPalType(value);
                break;
            }
        }
    }

    for(const char *key : {"pal", "palette"}) {
        if(const auto palette = AsResource(get(key))) {
            metadata.setPalette(palette);
            break;
        }
    }
    if(metadata.getPalette().data.empty() && parent) {
        for(const char *key : {"pal", "palette"}) {
            if(const auto palette = AsResource(parentValue(key))) {
                metadata.setPalette(palette);
                break;
            }
        }
    }

    for(const char *key : {"compress", "compression"}) {
        const auto value = StringValue(get(key));
        if(!value.empty()) {
            metadata.setCompress(ParseCompression(value));
            break;
        }
    }
    if(const auto label = StringValue(get("name")); !label.empty()) {
        metadata.setLabel(label);
    }
    if(const auto layerType = get("layer_type")) {
        metadata.setLayerType(NumberValue(layerType, metadata.getLayerType()));
    }
}

struct Collector {
    std::vector<std::unique_ptr<IResourceMetadata>> resources;
    std::unordered_set<std::string> seen;
    bool deduplicate = true;
    PSBSpec spec = PSBSpec::Other;
    PSBType type = PSBType::PSB;
    std::uint32_t syntheticName = 0;

    void add(const std::string &name, const std::string &part,
             const std::shared_ptr<PSBResource> &resource,
             const std::shared_ptr<PSBDictionary> &dictionary = nullptr,
             const std::shared_ptr<PSBDictionary> &parent = nullptr,
             const int defaultWidth = 0, const int defaultHeight = 0) {
        if(!resource) {
            return;
        }
        const auto identity = ResourceIdentity(resource);
        if(deduplicate && !identity.empty() && !seen.insert(identity).second) {
            return;
        }

        auto metadata = std::make_unique<ImageMetadata>();
        std::string resolvedName = name;
        if(resolvedName.empty()) {
            if(resource->index.has_value()) {
                resolvedName = std::to_string(resource->index.value());
            } else {
                resolvedName = "resource" + std::to_string(syntheticName++);
            }
        }
        metadata->setName(std::move(resolvedName));
        metadata->setPart(part);
        metadata->setResource(resource);
        metadata->setSpec(spec);
        metadata->setPSBType(type);
        metadata->setWidth(defaultWidth);
        metadata->setHeight(defaultHeight);
        ApplyImageFields(*metadata, dictionary, parent);
        if(metadata->getCompress() == PSBCompressType::ByName) {
            const auto lower = LowerAscii(metadata->getName());
            if(lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".tlg") == 0) {
                metadata->setCompress(PSBCompressType::Tlg);
            }
        }
        resources.push_back(std::move(metadata));
    }
};

inline void CollectTachieValue(Collector &collector,
                               const std::shared_ptr<IPSBValue> &value,
                               std::string label,
                               std::string path = "",
                               const std::shared_ptr<PSBDictionary> &parent = nullptr) {
    if(!value) {
        return;
    }
    if(const auto list = AsList(value)) {
        for(size_t index = 0; index < list->size(); ++index) {
            const auto childPath = path.empty() ? std::to_string(index)
                                                : path + "/" + std::to_string(index);
            CollectTachieValue(collector, (*list)[static_cast<int>(index)], label,
                               childPath, parent);
        }
        return;
    }
    const auto dictionary = AsDictionary(value);
    if(!dictionary) {
        return;
    }

    if(const auto childLabel = StringValue((*dictionary)["label"]);
       !childLabel.empty()) {
        if(!label.empty()) {
            label += "-";
        }
        label += childLabel;
    }

    if(const auto resource = FindDirectResource(dictionary, "pixel")) {
        std::string name;
        if(resource->index.has_value()) {
            name = std::to_string(resource->index.value());
        } else {
            name = path;
        }
        collector.add(name, label, resource, dictionary, parent, 1, 1);
    }

    for(const auto &[key, child] : *dictionary) {
        if(key == "pixel") {
            continue;
        }
        const auto childPath = path.empty() ? key : path + "/" + key;
        CollectTachieValue(collector, child, label, childPath, dictionary);
    }
}

inline void CollectMmoValue(Collector &collector,
                            const std::shared_ptr<IPSBValue> &value,
                            const std::string &part,
                            const std::string &inheritedLabel,
                            const std::string &path = "",
                            const std::shared_ptr<PSBDictionary> &parent = nullptr) {
    if(!value) {
        return;
    }
    if(const auto list = AsList(value)) {
        for(size_t index = 0; index < list->size(); ++index) {
            const auto childPath = path.empty() ? std::to_string(index)
                                                : path + "/" + std::to_string(index);
            CollectMmoValue(collector, (*list)[static_cast<int>(index)], part,
                            inheritedLabel, childPath, parent);
        }
        return;
    }

    const auto dictionary = AsDictionary(value);
    if(!dictionary) {
        return;
    }
    std::string label = inheritedLabel;
    if(const auto childLabel = StringValue((*dictionary)["label"]);
       !childLabel.empty()) {
        label = childLabel;
    }

    if(const auto resource = FindDirectResource(dictionary, "pixel")) {
        std::string name = label;
        if(name.empty()) {
            name = path;
        }
        if(name.empty() && resource->index.has_value()) {
            name = std::to_string(resource->index.value());
        }
        collector.add(name, part, resource, dictionary, parent, 1, 1);
    }

    for(const auto &[key, child] : *dictionary) {
        if(key == "pixel") {
            continue;
        }
        const auto childPath = path.empty() ? key : path + "/" + key;
        CollectMmoValue(collector, child, part, label, childPath, dictionary);
    }
}

} // namespace PSB::Metadata
