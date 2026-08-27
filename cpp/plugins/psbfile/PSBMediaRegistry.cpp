#include "PSBMediaRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>

#include "PSBMedia.h"
#include "PSBValue.h"
#include "resources/ImageMetadata.h"

namespace PSB {

    namespace {
        std::string lowerAscii(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](const unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return value;
        }

        std::string basename(std::string value) {
            const auto slash = value.find_last_of("/\\");
            if(slash != std::string::npos) {
                value.erase(0, slash + 1);
            }
            return value;
        }

        std::string stripExtension(std::string value) {
            const auto dot = value.find_last_of('.');
            if(dot != std::string::npos) {
                value.erase(dot);
            }
            return value;
        }

        std::string normalizePath(std::string value) {
            std::replace(value.begin(), value.end(), '\\', '/');
            return lowerAscii(std::move(value));
        }

        std::string mapSourcePath(const std::string &src) {
            // Motion PSBs refer to source/<group>/<name> while the resource
            // table stores source/<group>/icon/<name>/pixel.
            if(src.size() > 4 && src.compare(0, 4, "src/") == 0) {
                const auto rest = src.substr(4);
                const auto slash = rest.find('/');
                if(slash != std::string::npos) {
                    return "source/" + rest.substr(0, slash) + "/icon/" +
                        rest.substr(slash + 1);
                }
            }
            return src;
        }

        std::shared_ptr<const PSBDictionary>
        asDictionary(const std::shared_ptr<IPSBValue> &value) {
            return std::dynamic_pointer_cast<const PSBDictionary>(value);
        }

        std::shared_ptr<const PSBList>
        asList(const std::shared_ptr<IPSBValue> &value) {
            return std::dynamic_pointer_cast<const PSBList>(value);
        }

        std::optional<std::string>
        asString(const std::shared_ptr<IPSBValue> &value) {
            const auto text = std::dynamic_pointer_cast<const PSBString>(value);
            if(!text || text->value.empty()) {
                return std::nullopt;
            }
            return text->value;
        }

        std::optional<int>
        asPositiveInteger(const std::shared_ptr<IPSBValue> &value) {
            const auto number = std::dynamic_pointer_cast<PSBNumber>(value);
            if(!number) {
                return std::nullopt;
            }
            double numeric = 0.0;
            switch(number->numberType) {
                case PSBNumberType::Float:
                    numeric = number->getValue<float>();
                    break;
                case PSBNumberType::Double:
                    numeric = number->getValue<double>();
                    break;
                case PSBNumberType::Int:
                    numeric = number->getValue<int>();
                    break;
                case PSBNumberType::Long:
                default:
                    numeric = static_cast<double>(number->getLongValue());
                    break;
            }
            if(!std::isfinite(numeric) || numeric <= 0.0 ||
               numeric > std::numeric_limits<int>::max()) {
                return std::nullopt;
            }
            return static_cast<int>(std::lround(numeric));
        }

        std::pair<int, int> motionCanvasSize(const PSBFile &file) {
            const auto root = asDictionary(file.getRootValue());
            const auto screen = root
                ? asDictionary((*root)["screenSize"])
                : nullptr;
            if(!screen) {
                return {0, 0};
            }
            const auto width = asPositiveInteger((*screen)["width"]);
            const auto height = asPositiveInteger((*screen)["height"]);
            return {width.value_or(0), height.value_or(0)};
        }

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

        std::string motionStem(const std::string &container) {
            auto name = stripExtension(basename(normalizePath(container)));
            if(name.rfind("dx_", 0) == 0) {
                name.erase(0, 3);
            }
            return name;
        }

        std::optional<std::string> selectMotionName(
            const std::shared_ptr<const PSBDictionary> &motionDictionary,
            const std::string &preferred) {
            if(!motionDictionary) {
                return std::nullopt;
            }

            if(auto exact = asDictionary((*motionDictionary)[preferred])) {
                if((*exact)["layer"] != nullptr) {
                    return preferred;
                }
            }

            for(const auto *fallback : {"normal", "show", "bt"}) {
                if(auto candidate = asDictionary((*motionDictionary)[fallback])) {
                    if((*candidate)["layer"] != nullptr) {
                        return std::string(fallback);
                    }
                }
            }

            for(const auto &[name, value] : *motionDictionary) {
                if(asDictionary(value) &&
                   (*asDictionary(value))["layer"] != nullptr) {
                    return name;
                }
            }
            return std::nullopt;
        }

        void collectMotionSources(
            const std::shared_ptr<const PSBDictionary> &motionDictionary,
            const std::string &motionName,
            const std::shared_ptr<const PSBDictionary> &objectTree,
            std::vector<std::string> &sources,
            std::unordered_set<std::string> &visited) {
            if(!motionDictionary || !objectTree) {
                return;
            }
            const auto motion = asDictionary((*motionDictionary)[motionName]);
            if(!motion) {
                return;
            }

            const auto visitKey = std::to_string(
                reinterpret_cast<std::uintptr_t>(motion.get())) + ":" +
                motionName;
            if(!visited.insert(visitKey).second) {
                return;
            }

            const auto layers = asList((*motion)["layer"]);
            if(!layers) {
                return;
            }
            for(size_t index = 0; index < layers->size(); ++index) {
                const auto layer = asDictionary((*layers)[static_cast<int>(index)]);
                if(!layer) {
                    continue;
                }
                const auto frames = asList((*layer)["frameList"]);
                if(!frames || frames->size() == 0) {
                    continue;
                }
                const auto frame = asDictionary((*frames)[0]);
                const auto content = frame
                    ? asDictionary((*frame)["content"])
                    : nullptr;
                const auto source = content
                    ? asString((*content)["src"])
                    : std::nullopt;
                if(!source || source->empty()) {
                    continue;
                }

                if(source->rfind("src/", 0) == 0) {
                    const auto mapped = mapSourcePath(*source);
                    if(std::find(sources.begin(), sources.end(), mapped) ==
                       sources.end()) {
                        sources.push_back(mapped);
                    }
                    continue;
                }

                if(source->rfind("motion/", 0) != 0) {
                    continue;
                }
                const auto reference = source->substr(7);
                const auto slash = reference.find('/');
                if(slash == std::string::npos) {
                    continue;
                }
                const auto objectName = reference.substr(0, slash);
                const auto childMotionName = reference.substr(slash + 1);
                const auto object = asDictionary((*objectTree)[objectName]);
                const auto childMotions = object
                    ? asDictionary((*object)["motion"])
                    : nullptr;
                collectMotionSources(childMotions, childMotionName,
                                     objectTree, sources, visited);
            }
        }

        std::vector<std::string> collectAuthoredMotionSources(
            const PSBFile &file, const std::string &container) {
            std::vector<std::string> result;
            const auto root = file.getObjects();
            if(!root) {
                return result;
            }
            const auto objectTree = asDictionary((*root)["object"]);
            const auto preferred = motionStem(container);
            std::unordered_set<std::string> visited;

            if(objectTree) {
                // Prefer the object whose name matches the motion archive.
                // This excludes background/preview variants that often share
                // the same four embedded images but are not consumed by the
                // sliced layer.
                auto preferredObject =
                    asDictionary((*objectTree)[preferred]);
                if(preferredObject) {
                    const auto motions =
                        asDictionary((*preferredObject)["motion"]);
                    if(const auto selected = selectMotionName(motions, preferred)) {
                        collectMotionSources(motions, *selected, objectTree,
                                             result, visited);
                    }
                }

                if(result.empty()) {
                    for(const auto &[name, value] : *objectTree) {
                        (void)name;
                        const auto object = asDictionary(value);
                        const auto motions = object
                            ? asDictionary((*object)["motion"])
                            : nullptr;
                        const auto selected = selectMotionName(motions, preferred);
                        if(!selected) {
                            continue;
                        }
                        collectMotionSources(motions, *selected, objectTree,
                                             result, visited);
                        if(!result.empty()) {
                            break;
                        }
                    }
                }
            }

            // A few PSB producers place the motion dictionary at the root
            // rather than below object/<name>. Keep that layout generic too.
            if(result.empty()) {
                const auto rootMotions = asDictionary((*root)["motion"]);
                if(const auto selected = selectMotionName(rootMotions, preferred)) {
                    collectMotionSources(rootMotions, *selected, objectTree,
                                         result, visited);
                }
            }
            return result;
        }

        void registerMotionSliceSetForContainer(PSBMedia *psbMedia,
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
            const auto sources =
                collectAuthoredMotionSources(file, containerKey);
            if(sources.empty()) {
                return;
            }

            auto resources = handler->collectResources(file, false);
            std::vector<std::string> imageKeys;
            std::unordered_set<std::string> usedIndexes;
            std::uint32_t syntheticIndex = 0;
            for(const auto &source : sources) {
                ImageMetadata *matched = nullptr;
                for(auto &metadata : resources) {
                    auto *image = dynamic_cast<ImageMetadata *>(metadata.get());
                    if(image == nullptr || image->getResource() == nullptr) {
                        continue;
                    }
                    const auto imageName = normalizePath(image->getName());
                    const auto sourceName = normalizePath(source);
                    if(imageName == sourceName ||
                       imageName.rfind(sourceName + "/", 0) == 0) {
                        matched = image;
                        break;
                    }
                }
                if(matched == nullptr) {
                    continue;
                }

                auto index = matched->getIndex();
                if(index == UINT32_MAX) {
                    while(usedIndexes.find(std::to_string(syntheticIndex)) !=
                          usedIndexes.end()) {
                        ++syntheticIndex;
                    }
                    index = syntheticIndex++;
                }
                const auto indexText = std::to_string(index);
                if(!usedIndexes.insert(indexText).second) {
                    continue;
                }

                const auto alias = containerKey + "/" + indexText + ".tlg";
                // ImageMetadata aliases are normally registered above.  The
                // add call also covers PSB producers that omit a resource
                // index and therefore had no indexed alias yet.
                psbMedia->add(alias, matched->getResource(), matched);
                imageKeys.push_back(alias);
            }

            if(!imageKeys.empty()) {
                const auto [authoredWidth, authoredHeight] =
                    motionCanvasSize(file);
                psbMedia->addMotionSliceSet(containerKey,
                                             std::move(imageKeys),
                                             authoredWidth,
                                             authoredHeight);
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
        registerMotionSliceSetForContainer(GetGlobalPSBMedia(), container, file);
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
            registerMotionSliceSetForContainer(psbMedia, container, file);
        }
    }

    void registerMotionSliceResources(const ttstr &container,
                                      const PSBFile &file) {
        initPSBMedia();
        registerMotionSliceSetForContainer(GetGlobalPSBMedia(), container,
                                           file);
    }

    void registerMotionSliceResources(const std::vector<ttstr> &containers,
                                      const PSBFile &file) {
        initPSBMedia();
        auto *psbMedia = GetGlobalPSBMedia();
        if(psbMedia == nullptr) {
            return;
        }
        for(const auto &container : containers) {
            registerMotionSliceSetForContainer(psbMedia, container, file);
        }
    }
} // namespace PSB
