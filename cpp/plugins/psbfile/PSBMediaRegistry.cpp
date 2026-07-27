#include "PSBMediaRegistry.h"

#include "PSBMedia.h"
#include "PSBValue.h"
#include "resources/ImageMetadata.h"

namespace PSB {

    namespace {
        void registerValueResources(PSBMedia *psbMedia,
                                    const ttstr &normalizedContainer,
                                    const std::shared_ptr<IPSBValue> &value,
                                    std::vector<std::string> &path) {
            if(psbMedia == nullptr || value == nullptr) {
                return;
            }

            if(const auto resource = std::dynamic_pointer_cast<PSBResource>(value)) {
                ttstr resourceKey;
                for(size_t index = 0; index < path.size(); ++index) {
                    if(index != 0) {
                        resourceKey += TJS_W("/");
                    }
                    resourceKey += ttstr{ path[index] };
                }
                if(resourceKey.IsEmpty()) {
                    return;
                }
                psbMedia->NormalizePathName(resourceKey);
                psbMedia->add((normalizedContainer + TJS_W("/") + resourceKey)
                                  .AsStdString(),
                              resource);
                return;
            }

            if(const auto dic = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                for(const auto &[key, child] : *dic) {
                    path.push_back(key);
                    registerValueResources(psbMedia, normalizedContainer, child, path);
                    path.pop_back();
                }
                return;
            }

            if(const auto list = std::dynamic_pointer_cast<PSBList>(value)) {
                for(size_t index = 0; index < list->size(); ++index) {
                    path.push_back(std::to_string(index));
                    registerValueResources(psbMedia, normalizedContainer,
                                           (*list)[static_cast<int>(index)],
                                           path);
                    path.pop_back();
                }
            }
        }

        void registerRootResourcesForContainer(
            PSBMedia *psbMedia, const ttstr &container,
            const std::shared_ptr<const PSBDictionary> &root) {
            if(psbMedia == nullptr || root == nullptr || container.IsEmpty()) {
                return;
            }

            ttstr normalizedContainer = container;
            psbMedia->NormalizeDomainName(normalizedContainer);

            std::vector<std::string> path;
            registerValueResources(
                psbMedia, normalizedContainer,
                std::const_pointer_cast<PSBDictionary>(root), path);
        }

        void registerImageAliasesForContainer(PSBMedia *psbMedia,
                                              const ttstr &container,
                                              const PSBFile &file) {
            if(psbMedia == nullptr || container.IsEmpty() ||
               file.getType() != PSBType::Motion) {
                return;
            }

            auto *handler = file.getTypeHandler();
            if(handler == nullptr) {
                return;
            }

            ttstr normalizedContainer = container;
            psbMedia->NormalizeDomainName(normalizedContainer);
            const auto containerKey = normalizedContainer.AsStdString();
            auto resources = handler->collectResources(file, false);
            for(auto &metadata : resources) {
                auto *image = dynamic_cast<ImageMetadata *>(metadata.get());
                if(image == nullptr) {
                    continue;
                }
                const auto resource = image->getResource();
                const auto name = image->getName();
                if(resource == nullptr || name.empty()) {
                    continue;
                }

                psbMedia->add(containerKey + "/" + name, resource, image);
                const auto index = image->getIndex();
                if(index != UINT32_MAX) {
                    const auto indexedName = std::to_string(index) + ".tlg";
                    if(indexedName != name) {
                        psbMedia->add(containerKey + "/" + indexedName,
                                      resource, image);
                    }
                }
            }
        }
    } // namespace

    void registerRootResources(const ttstr &container,
                               const std::shared_ptr<const PSBDictionary> &root) {
        initPSBMedia();
        registerRootResourcesForContainer(GetGlobalPSBMedia(), container, root);
    }

    void registerRootResources(const std::vector<ttstr> &containers,
                               const std::shared_ptr<const PSBDictionary> &root) {
        initPSBMedia();
        auto *psbMedia = GetGlobalPSBMedia();
        if(psbMedia == nullptr) {
            return;
        }
        for(const auto &container : containers) {
            registerRootResourcesForContainer(psbMedia, container, root);
        }
    }

    void registerRootResources(const ttstr &container, const PSBFile &file) {
        registerRootResources(container, file.getObjects());
        registerImageAliasesForContainer(GetGlobalPSBMedia(), container, file);
    }

    void registerRootResources(const std::vector<ttstr> &containers,
                               const PSBFile &file) {
        initPSBMedia();
        auto *psbMedia = GetGlobalPSBMedia();
        if(psbMedia == nullptr) {
            return;
        }
        for(const auto &container : containers) {
            registerRootResourcesForContainer(psbMedia, container,
                                              file.getObjects());
            registerImageAliasesForContainer(psbMedia, container, file);
        }
    }
} // namespace PSB
