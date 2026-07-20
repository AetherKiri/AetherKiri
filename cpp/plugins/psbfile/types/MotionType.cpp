//
// Created by LiDon on 2025/9/15.
//

#include <algorithm>
#include <cctype>

#include "../PSBFile.h"
#include "MotionType.h"

namespace PSB {

#define LOGGER spdlog::get("plugin")

    namespace {
        int GetNumberValue(const std::shared_ptr<IPSBValue> &value,
                           int fallback = 0) {
            if(!value) {
                return fallback;
            }
            if(const auto num = std::dynamic_pointer_cast<PSBNumber>(value)) {
                if(num->numberType == PSBNumberType::Float) {
                    return static_cast<int>(num->getFloatValue());
                }
                if(num->numberType == PSBNumberType::Double) {
                    return static_cast<int>(
                        BitConverter::fromByteArray<double>(num->data));
                }
                return static_cast<int>(num->getLongValue());
            }
            if(const auto str = std::dynamic_pointer_cast<PSBString>(value)) {
                try {
                    return std::stoi(str->value);
                } catch(...) {
                }
            }
            return fallback;
        }

        std::string GetStringValue(const std::shared_ptr<IPSBValue> &value) {
            if(const auto str = std::dynamic_pointer_cast<PSBString>(value)) {
                return str->value;
            }
            return {};
        }

        std::shared_ptr<PSBDictionary> GetDictionaryValue(
            const std::shared_ptr<IPSBValue> &value) {
            return std::dynamic_pointer_cast<PSBDictionary>(value);
        }

        std::shared_ptr<PSBResource> GetDirectResource(
            const std::shared_ptr<PSBDictionary> &dict,
            const std::string &key) {
            if(!dict) {
                return nullptr;
            }
            const auto it = dict->find(key);
            if(it == dict->end()) {
                return nullptr;
            }
            return std::dynamic_pointer_cast<PSBResource>(it->second);
        }

        std::shared_ptr<PSBResource> FindResourceRecursive(
            const std::shared_ptr<IPSBValue> &value,
            const std::string &key,
            int depth = 0) {
            if(!value || depth > 3) {
                return nullptr;
            }
            if(const auto dict = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                if(auto direct = GetDirectResource(dict, key)) {
                    return direct;
                }
                for(const auto &[childKey, childValue] : *dict) {
                    (void)childKey;
                    if(auto nested = FindResourceRecursive(childValue, key, depth + 1)) {
                        return nested;
                    }
                }
            } else if(const auto list =
                          std::dynamic_pointer_cast<PSBList>(value)) {
                for(const auto &childValue : *list) {
                    if(auto nested = FindResourceRecursive(childValue, key, depth + 1)) {
                        return nested;
                    }
                }
            }
            return nullptr;
        }

        std::string FindStringRecursive(const std::shared_ptr<IPSBValue> &value,
                                        const std::vector<std::string> &keys,
                                        int depth = 0) {
            if(!value || depth > 3) {
                return {};
            }
            if(const auto dict = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                for(const auto &key : keys) {
                    if(const auto it = dict->find(key); it != dict->end()) {
                        if(const auto str = GetStringValue(it->second); !str.empty()) {
                            return str;
                        }
                    }
                }
                for(const auto &[childKey, childValue] : *dict) {
                    (void)childKey;
                    if(const auto nested =
                           FindStringRecursive(childValue, keys, depth + 1);
                       !nested.empty()) {
                        return nested;
                    }
                }
            } else if(const auto list =
                          std::dynamic_pointer_cast<PSBList>(value)) {
                for(const auto &childValue : *list) {
                    if(const auto nested =
                           FindStringRecursive(childValue, keys, depth + 1);
                       !nested.empty()) {
                        return nested;
                    }
                }
            }
            return {};
        }

        int FindNumberRecursive(const std::shared_ptr<IPSBValue> &value,
                                const std::vector<std::string> &keys,
                                int fallback,
                                int depth = 0) {
            if(!value || depth > 3) {
                return fallback;
            }
            if(const auto dict = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                for(const auto &key : keys) {
                    if(const auto it = dict->find(key); it != dict->end()) {
                        const int found = GetNumberValue(it->second, fallback);
                        if(found != fallback) {
                            return found;
                        }
                    }
                }
                for(const auto &[childKey, childValue] : *dict) {
                    (void)childKey;
                    const int nested =
                        FindNumberRecursive(childValue, keys, fallback, depth + 1);
                    if(nested != fallback) {
                        return nested;
                    }
                }
            } else if(const auto list =
                          std::dynamic_pointer_cast<PSBList>(value)) {
                for(const auto &childValue : *list) {
                    const int nested =
                        FindNumberRecursive(childValue, keys, fallback, depth + 1);
                    if(nested != fallback) {
                        return nested;
                    }
                }
            }
            return fallback;
        }

        PSBCompressType ParseCompressType(const std::string &value) {
            if(value.empty()) {
                return PSBCompressType::ByName;
            }
            std::string tmp = value;
            std::transform(tmp.begin(), tmp.end(), tmp.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            if(tmp == "rl" || tmp == "rle") {
                return PSBCompressType::RL;
            }
            if(tmp == "bmp") {
                return PSBCompressType::Bmp;
            }
            if(tmp == "tlg") {
                return PSBCompressType::Tlg;
            }
            if(tmp == "none" || tmp == "raw") {
                return PSBCompressType::None;
            }
            return PSBCompressType::ByName;
        }

        bool LooksLikeMotionAlias(const std::string &value) {
            if(value.empty() || value.size() > 256) {
                return false;
            }
            return value.rfind("motion/", 0) == 0 || value.rfind("src/", 0) == 0;
        }

        std::string DetectImageExtension(const std::shared_ptr<PSBResource> &resource) {
            if(!resource || resource->data.empty()) {
                return {};
            }

            const auto &data = resource->data;
            if(data.size() >= 8 && data[0] == 0x89 && data[1] == 0x50 &&
               data[2] == 0x4e && data[3] == 0x47) {
                return ".png";
            }
            if(data.size() >= 2 && data[0] == 'B' && data[1] == 'M') {
                return ".bmp";
            }
            if(data.size() >= 3 && data[0] == 0xff && data[1] == 0xd8 &&
               data[2] == 0xff) {
                return ".jpg";
            }
            if(data.size() >= 3 && data[0] == 'T' && data[1] == 'L' &&
               data[2] == 'G') {
                return ".tlg";
            }
            return ".png";
        }

        void ApplyImageMetadata(ImageMetadata &meta,
                                const std::shared_ptr<PSBDictionary> &dict,
                                const std::shared_ptr<PSBDictionary> &parent = nullptr) {
            if(!dict) {
                return;
            }

            meta.setWidth(GetNumberValue((*dict)["width"], meta.getWidth()));
            meta.setHeight(GetNumberValue((*dict)["height"], meta.getHeight()));
            meta.setTop(GetNumberValue((*dict)["top"], meta.getTop()));
            meta.setLeft(GetNumberValue((*dict)["left"], meta.getLeft()));
            meta.setOpacity(GetNumberValue((*dict)["opacity"], meta.getOpacity()));
            meta.setVisible(GetNumberValue((*dict)["visible"],
                                           meta.getVisible() ? 1 : 0) != 0);
            meta.setLayerType(
                GetNumberValue((*dict)["layer_type"], meta.getLayerType()));

            if(const auto type = GetStringValue((*dict)["type"]); !type.empty()) {
                meta.setType(type);
            }
            if(meta.getType().empty()) {
                if(const auto texType =
                       GetStringValue((*dict)["texture_type"]); !texType.empty()) {
                    meta.setType(texType);
                }
            }
            if(meta.getType().empty()) {
                if(const auto pixelType =
                       GetStringValue((*dict)["pixel_type"]); !pixelType.empty()) {
                    meta.setType(pixelType);
                }
            }
            if(meta.getType().empty() && parent) {
                if(const auto parentType =
                       GetStringValue((*parent)["type"]); !parentType.empty()) {
                    meta.setType(parentType);
                }
            }
            if(meta.getType().empty() && parent) {
                if(const auto parentTexType =
                       GetStringValue((*parent)["texture_type"]); !parentTexType.empty()) {
                    meta.setType(parentTexType);
                }
            }
            if(meta.getType().empty()) {
                if(const auto nestedType =
                       FindStringRecursive(dict, { "type", "texture_type",
                                                   "pixel_type" });
                   !nestedType.empty()) {
                    meta.setType(nestedType);
                }
            }
            if(meta.getType().empty() && parent) {
                if(const auto nestedParentType =
                       FindStringRecursive(parent, { "type", "texture_type",
                                                     "pixel_type" });
                   !nestedParentType.empty()) {
                    meta.setType(nestedParentType);
                }
            }
            if(const auto palType =
                   FindStringRecursive(dict, { "pal_type", "palette_type",
                                               "clut_type" });
               !palType.empty()) {
                meta.setPalType(palType);
            } else if(parent) {
                if(const auto parentPalType =
                       FindStringRecursive(parent, { "pal_type", "palette_type",
                                                     "clut_type" });
                   !parentPalType.empty()) {
                    meta.setPalType(parentPalType);
                }
            }
            if(const auto palette =
                   FindResourceRecursive(dict, "pal")) {
                meta.setPalette(palette);
            } else if(const auto palette =
                          FindResourceRecursive(dict, "palette")) {
                meta.setPalette(palette);
            } else if(parent) {
                if(const auto parentPalette =
                       FindResourceRecursive(parent, "pal")) {
                    meta.setPalette(parentPalette);
                } else if(const auto parentPalette =
                              FindResourceRecursive(parent, "palette")) {
                    meta.setPalette(parentPalette);
                }
            }

            if(const auto compress =
                   FindStringRecursive(dict, { "compress", "compression" });
               !compress.empty()) {
                meta.setCompress(ParseCompressType(compress));
            } else if(parent) {
                if(const auto parentCompress =
                       FindStringRecursive(parent, { "compress", "compression" });
                   !parentCompress.empty()) {
                    meta.setCompress(ParseCompressType(parentCompress));
                }
            }
            if(const auto label = GetStringValue((*dict)["name"]); !label.empty()) {
                meta.setLabel(label);
            }

            if(const auto clip =
                   std::dynamic_pointer_cast<PSBDictionary>((*dict)["clip"])) {
                meta.setWidth(GetNumberValue((*clip)["width"],
                                             GetNumberValue((*clip)["w"],
                                                            meta.getWidth())));
                meta.setHeight(GetNumberValue((*clip)["height"],
                                              GetNumberValue((*clip)["h"],
                                                             meta.getHeight())));
                meta.setLeft(GetNumberValue((*clip)["left"],
                                            GetNumberValue((*clip)["x"],
                                                           meta.getLeft())));
                meta.setTop(GetNumberValue((*clip)["top"],
                                           GetNumberValue((*clip)["y"],
                                                          meta.getTop())));
            }

            if(parent) {
                if(meta.getWidth() <= 0) {
                    meta.setWidth(GetNumberValue((*parent)["width"], meta.getWidth()));
                }
                if(meta.getHeight() <= 0) {
                    meta.setHeight(GetNumberValue((*parent)["height"], meta.getHeight()));
                }
                if(meta.getLeft() == 0) {
                    meta.setLeft(GetNumberValue((*parent)["left"], meta.getLeft()));
                }
                if(meta.getTop() == 0) {
                    meta.setTop(GetNumberValue((*parent)["top"], meta.getTop()));
                }
            }

            if(meta.getWidth() <= 0) {
                meta.setWidth(FindNumberRecursive(dict, { "width", "w" },
                                                 meta.getWidth()));
            }
            if(meta.getHeight() <= 0) {
                meta.setHeight(FindNumberRecursive(dict, { "height", "h" },
                                                  meta.getHeight()));
            }
        }

        void AddMotionResource(
            std::vector<std::unique_ptr<IResourceMetadata>> &resourceList,
            const std::string &path,
            const std::shared_ptr<PSBResource> &resource,
            PSBSpec spec,
            const std::shared_ptr<PSBDictionary> &dict = nullptr,
            const std::shared_ptr<PSBDictionary> &parent = nullptr) {
            if(path.empty() || !resource) {
                return;
            }

            auto addOne = [&](const std::string &name) {
                auto meta = std::make_unique<ImageMetadata>();
                meta->setName(name);
                meta->setResource(std::make_shared<PSBResource>(*resource));
                meta->setCompress(PSBCompressType::ByName);
                meta->setPSBType(PSBType::Motion);
                meta->setSpec(spec);
                ApplyImageMetadata(*meta, dict, parent);
                resourceList.push_back(std::move(meta));
            };

            addOne(path);
            const std::string ext = DetectImageExtension(resource);
            if(!ext.empty() &&
               !(path.size() >= ext.size() &&
                 path.compare(path.size() - ext.size(), ext.size(), ext) == 0)) {
                addOne(path + ext);
            }
        }

        void CollectMotionResources(
            std::vector<std::unique_ptr<IResourceMetadata>> &resourceList,
            const std::shared_ptr<IPSBValue> &value,
            const std::string &path,
            PSBSpec spec,
            const std::shared_ptr<PSBDictionary> &parent = nullptr) {
            if(!value) {
                return;
            }

            if(const auto resource = std::dynamic_pointer_cast<PSBResource>(value)) {
                AddMotionResource(resourceList, path, resource, spec);
                return;
            }

            if(const auto dict = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                if(const auto directPixel = GetDirectResource(dict, "pixel")) {
                    const std::string pixelPath =
                        path.empty() ? "pixel" : path + "/pixel";
                    AddMotionResource(resourceList, pixelPath, directPixel, spec,
                                      dict, parent);

                    for(const auto &[key, child] : *dict) {
                        (void)key;
                        const auto aliasString =
                            std::dynamic_pointer_cast<PSBString>(child);
                        if(!aliasString || !LooksLikeMotionAlias(aliasString->value)) {
                            continue;
                        }
                        AddMotionResource(resourceList, aliasString->value,
                                          directPixel, spec, dict, parent);
                        if(aliasString->value.size() < 6 ||
                           aliasString->value.compare(aliasString->value.size() - 6,
                                                      6, "/pixel") != 0) {
                            AddMotionResource(resourceList,
                                              aliasString->value + "/pixel",
                                              directPixel, spec, dict, parent);
                        }
                    }
                }
                for(const auto &[key, child] : *dict) {
                    if(key == "pixel") {
                        continue;
                    }
                    const std::string nextPath = path.empty() ? key : path + "/" + key;
                    CollectMotionResources(resourceList, child, nextPath, spec, dict);
                }
                return;
            }

            if(const auto list = std::dynamic_pointer_cast<PSBList>(value)) {
                for(size_t i = 0; i < list->size(); ++i) {
                    const auto child = (*list)[static_cast<int>(i)];
                    const std::string nextPath = path.empty()
                        ? std::to_string(i)
                        : path + "/" + std::to_string(i);
                    CollectMotionResources(resourceList, child, nextPath, spec, parent);
                }
            }
        }
    }

    bool MotionType::isThisType(const PSBFile &psb) {
        const auto objects = psb.getObjects();
        if(objects == nullptr) {
            return false;
        }

        const auto fdId = objects->find("id");
        if(fdId == objects->end()) {
            return false;
        }
        const auto idValue = std::dynamic_pointer_cast<PSBString>(fdId->second);
        return idValue != nullptr && idValue->value == "motion";
    }

    std::vector<std::unique_ptr<IResourceMetadata>>
    MotionType::collectResources(const PSBFile &psb, bool deDuplication) {
        (void)deDuplication;
        std::vector<std::unique_ptr<IResourceMetadata>> resourceList;
        const auto root = psb.getRootValue();
        CollectMotionResources(resourceList, root, "", psb.getPlatform());
        return resourceList;
    }
} // namespace PSB
