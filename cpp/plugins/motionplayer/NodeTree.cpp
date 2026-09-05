//
// Build persistent node tree from PSB layer hierarchy.
// Aligned to libkrkr2.so sub_6B4A6C (0x6B4A6C).
//

#include "NodeTree.h"
#include "MotionPlayerExtension.h"
#include "MotionNode.h"
#include "RuntimeSupport.h"
#include "Player.h"
#include "ncbind.hpp"
#include "tjsArray.h"
#include "psbfile/PSBFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace motion::detail {

    namespace {

        // PSB helper: extract a number from a dictionary key.
        std::optional<double>
        nodeTreePsbNumber(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                          const char *key) {
            if (!dic) return std::nullopt;
            auto val = (*dic)[key];
            if (auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(val)) {
                switch (number->numberType) {
                    case PSB::PSBNumberType::Float:
                        return number->getValue<float>();
                    case PSB::PSBNumberType::Double:
                        return number->getValue<double>();
                    case PSB::PSBNumberType::Int:
                        return static_cast<double>(number->getValue<int>());
                    case PSB::PSBNumberType::Long:
                    default:
                        return static_cast<double>(number->getValue<tjs_int64>());
                }
            }
            if (auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(val)) {
                return boolean->value ? 1.0 : 0.0;
            }
            return std::nullopt;
        }

        // PSB helper: extract a string from a dictionary key.
        std::string
        nodeTreePsbString(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                          const char *key) {
            if (!dic) return {};
            if (auto text = std::dynamic_pointer_cast<PSB::PSBString>((*dic)[key])) {
                return text->value;
            }
            return {};
        }

        // PSB helper: extract a list from a dictionary key.
        std::shared_ptr<PSB::PSBList>
        nodeTreePsbList(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                        const char *key) {
            if (!dic) return nullptr;
            return std::dynamic_pointer_cast<PSB::PSBList>((*dic)[key]);
        }

        std::string nodeTreeLowercase(std::string value) {
            for (auto &ch : value) {
                ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        bool nodeTreeIsYuzuStartupLogo(const MotionSnapshot &snapshot) {
            const auto path = nodeTreeLowercase(snapshot.path);
            return path.find("yuzulogo") != std::string::npos;
        }

        bool nodeTreeIsYuzuTitlePresentation(const MotionSnapshot &snapshot) {
            const auto path = nodeTreeLowercase(snapshot.path);
            return path.find("title_bg") != std::string::npos ||
                path.find("titlebg") != std::string::npos;
        }

        bool nodeTreeIsYuzuLogoRootTextLayer(const std::string &name) {
            const auto lower = nodeTreeLowercase(name);
            return lower == "software" || lower.rfind("moji_", 0) == 0;
        }

        bool nodeTreeIsYuzuTitleCharacterSource(const std::string &source) {
            const auto lower = nodeTreeLowercase(source);
            return lower.find("src/title/ch") != std::string::npos ||
                lower.find("/title/ch") != std::string::npos ||
                lower.find("title2_ch") != std::string::npos;
        }

        bool nodeTreeIsYuzuTitleCompositeLayer(
            const std::string &name,
            const std::string &source = {}) {
            const auto lowerName = nodeTreeLowercase(name);
            const auto lowerSource = nodeTreeLowercase(source);
            return lowerName.find("title_char") != std::string::npos ||
                lowerSource.find("src/title/pos") != std::string::npos ||
                lowerSource.find("/title/pos") != std::string::npos ||
                lowerSource.find("title_char") != std::string::npos;
        }

        std::string nodeTreeFirstLayerSource(
            const std::shared_ptr<const PSB::PSBDictionary> &layer) {
            const auto frames = nodeTreePsbList(layer, "frameList");
            if(!frames) return {};
            for(int i = 0; i < static_cast<int>(frames->size()); ++i) {
                const auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frames)[i]);
                if(!frame) continue;
                const auto content =
                    std::dynamic_pointer_cast<PSB::PSBDictionary>(
                        (*frame)["content"]);
                const auto src = nodeTreePsbString(content, "src");
                if(!src.empty()) {
                    return src;
                }
            }
            return {};
        }

        bool nodeTreeLayerReferencesTitlePresentation(
            const std::shared_ptr<const PSB::PSBDictionary> &layer);

        bool nodeTreeFrameContentReferencesTitlePresentation(
            const std::shared_ptr<const PSB::PSBDictionary> &frame,
            const std::string &layerName) {
            const auto content =
                std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frame)["content"]);
            const auto src = nodeTreePsbString(content, "src");
            return nodeTreeIsYuzuTitleCompositeLayer(layerName, src) ||
                nodeTreeIsYuzuTitleCharacterSource(src);
        }

        bool nodeTreeLayerReferencesTitlePresentation(
            const std::shared_ptr<const PSB::PSBDictionary> &layer) {
            if(!layer) return false;
            const auto layerName = nodeTreePsbString(layer, "label");
            if(nodeTreeIsYuzuTitleCompositeLayer(layerName)) {
                return true;
            }
            const auto frames = nodeTreePsbList(layer, "frameList");
            if(frames) {
                for(int i = 0; i < static_cast<int>(frames->size()); ++i) {
                    const auto frame =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(
                            (*frames)[i]);
                    if(frame &&
                       nodeTreeFrameContentReferencesTitlePresentation(
                           frame, layerName)) {
                        return true;
                    }
                }
            }
            const auto children = nodeTreePsbList(layer, "children");
            if(children) {
                for(int i = 0; i < static_cast<int>(children->size()); ++i) {
                    const auto child =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(
                            (*children)[i]);
                    if(nodeTreeLayerReferencesTitlePresentation(child)) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool nodeTreeLayerSetReferencesYuzuTitlePresentation(
            const std::vector<std::string> &names,
            const std::unordered_map<
                std::string,
                std::shared_ptr<const PSB::PSBDictionary>> &layersByName) {
            for (const auto &name : names) {
                const auto it = layersByName.find(name);
                if (it != layersByName.end() &&
                    nodeTreeLayerReferencesTitlePresentation(it->second)) {
                    return true;
                }
            }
            return false;
        }

        bool nodeTreeIsYuzuTitlePersistentRootLayer(
            const std::string &name,
            const std::shared_ptr<const PSB::PSBDictionary> &layer) {
            const auto lowerName = nodeTreeLowercase(name);
            const auto source = nodeTreeFirstLayerSource(layer);
            const auto lowerSource = nodeTreeLowercase(source);
            return nodeTreeIsYuzuTitleCharacterSource(source) ||
                lowerName == "logo" ||
                lowerSource.find("src/title/logo") != std::string::npos ||
                lowerSource.find("/title/logo") != std::string::npos ||
                lowerName.rfind("bg2_", 0) == 0 ||
                lowerSource.find("src/title/bg2_") != std::string::npos ||
                lowerSource.find("/title/bg2_") != std::string::npos;
        }

        std::shared_ptr<PSB::PSBDictionary>
        nodeTreeCopyDictionaryWithoutKeys(
            const std::shared_ptr<const PSB::PSBDictionary> &source,
            const std::unordered_set<std::string> &skipKeys) {
            auto copy = std::make_shared<PSB::PSBDictionary>();
            if (!source) return copy;
            for (const auto &[key, value] : *source) {
                if (skipKeys.find(key) != skipKeys.end()) {
                    continue;
                }
                copy->emplace(key, value);
            }
            return copy;
        }

        std::shared_ptr<PSB::PSBDictionary>
        nodeTreeMakeNumberFrame(
            int time,
            int type,
            const std::shared_ptr<PSB::PSBDictionary> &content = nullptr) {
            auto frame = std::make_shared<PSB::PSBDictionary>();
            frame->emplace("time", std::make_shared<PSB::PSBNumber>(time));
            frame->emplace("type", std::make_shared<PSB::PSBNumber>(type));
            if (content) {
                frame->emplace("content", content);
            }
            return frame;
        }

        std::shared_ptr<PSB::PSBDictionary>
        nodeTreeTitleIntroContent(
            const std::shared_ptr<const PSB::PSBDictionary> &sourceContent,
            int opacity) {
            auto content = nodeTreeCopyDictionaryWithoutKeys(
                sourceContent, { "mask", "opa" });
            // Keep source/position fields from the root static layer, but add
            // opacity so these synthetic intro layers fade in before the full
            // title_charall composite takes over.
            content->emplace("mask", std::make_shared<PSB::PSBNumber>(0x403));
            content->emplace("opa", std::make_shared<PSB::PSBNumber>(opacity));
            return content;
        }

        std::shared_ptr<const PSB::PSBDictionary>
        nodeTreeMakeYuzuTitleIntroLayer(
            const std::shared_ptr<const PSB::PSBDictionary> &rootLayer,
            const std::string &label,
            int startFrame,
            int endFrame,
            int hideFrame) {
            if (!rootLayer) return nullptr;
            const auto frames = nodeTreePsbList(rootLayer, "frameList");
            if (!frames || frames->size() == 0) return nullptr;
            const auto firstFrame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*frames)[0]);
            if (!firstFrame) return nullptr;
            const auto sourceContent =
                std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*firstFrame)["content"]);
            if (!sourceContent) return nullptr;

            auto introLayer = nodeTreeCopyDictionaryWithoutKeys(
                rootLayer, { "label", "frameList", "children" });
            introLayer->emplace("label", std::make_shared<PSB::PSBString>(label));

            auto introFrames = std::make_shared<PSB::PSBList>(3);
            introFrames->push_back(nodeTreeMakeNumberFrame(
                startFrame, 3, nodeTreeTitleIntroContent(sourceContent, 0)));
            introFrames->push_back(nodeTreeMakeNumberFrame(
                endFrame, 2, nodeTreeTitleIntroContent(sourceContent, 255)));
            introFrames->push_back(nodeTreeMakeNumberFrame(hideFrame, 0));
            introLayer->emplace("frameList", introFrames);
            return introLayer;
        }

        void walkTree(const std::shared_ptr<const PSB::PSBDictionary> &psbNode,
                      int parentIdx,
                      std::vector<MotionNode> &nodes);

        struct TitleCharacterRootLayer {
            std::string label;
            std::string source;
            std::shared_ptr<const PSB::PSBDictionary> layer;
            size_t rootOrder = 0;
            int sequenceOrder = 0;
            double introStartFrame = 0.0;
            double introEndFrame = 0.0;
        };

        std::optional<int> nodeTreeExtractTitleCharacterNumber(
            const std::string &value) {
            const auto lower = nodeTreeLowercase(value);
            const auto marker = lower.find("ch");
            if(marker == std::string::npos) {
                return std::nullopt;
            }
            size_t pos = marker + 2;
            if(pos >= lower.size() ||
               !std::isdigit(static_cast<unsigned char>(lower[pos]))) {
                return std::nullopt;
            }
            int result = 0;
            while(pos < lower.size() &&
                  std::isdigit(static_cast<unsigned char>(lower[pos]))) {
                result = result * 10 + (lower[pos] - '0');
                ++pos;
            }
            return result;
        }

        std::vector<TitleCharacterRootLayer>
        nodeTreeCollectTitleCharacterRootLayers(
            const MotionSnapshot &snapshot) {
            std::vector<TitleCharacterRootLayer> result;
            for(size_t index = 0; index < snapshot.layerNames.size(); ++index) {
                const auto &name = snapshot.layerNames[index];
                const auto it = snapshot.layersByName.find(name);
                if(it == snapshot.layersByName.end()) {
                    continue;
                }
                const auto source = nodeTreeFirstLayerSource(it->second);
                if(!nodeTreeIsYuzuTitleCharacterSource(source)) {
                    continue;
                }
                TitleCharacterRootLayer candidate;
                candidate.label = name;
                candidate.source = source;
                candidate.layer = it->second;
                candidate.rootOrder = index;
                candidate.sequenceOrder =
                    nodeTreeExtractTitleCharacterNumber(source)
                        .value_or(nodeTreeExtractTitleCharacterNumber(name)
                                      .value_or(static_cast<int>(index)));
                result.push_back(std::move(candidate));
            }
            return result;
        }

        void nodeTreeCollectTitleCharacterSources(
            const std::shared_ptr<const PSB::PSBDictionary> &layer,
            std::unordered_set<std::string> &sources,
            bool includeCompositeLayers) {
            if(!layer) return;
            const auto layerName = nodeTreePsbString(layer, "label");
            if(!includeCompositeLayers &&
               nodeTreeIsYuzuTitleCompositeLayer(layerName,
                                                 nodeTreeFirstLayerSource(layer))) {
                return;
            }
            const auto frames = nodeTreePsbList(layer, "frameList");
            if(frames) {
                for(int i = 0; i < static_cast<int>(frames->size()); ++i) {
                    const auto frame =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(
                            (*frames)[i]);
                    if(!frame) continue;
                    const auto content =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(
                            (*frame)["content"]);
                    const auto src = nodeTreePsbString(content, "src");
                    if(nodeTreeIsYuzuTitleCharacterSource(src)) {
                        sources.insert(nodeTreeLowercase(src));
                    }
                }
            }
            const auto children = nodeTreePsbList(layer, "children");
            if(children) {
                for(int i = 0; i < static_cast<int>(children->size()); ++i) {
                    const auto child =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(
                            (*children)[i]);
                    nodeTreeCollectTitleCharacterSources(
                        child, sources, includeCompositeLayers);
                }
            }
        }

        std::unordered_set<std::string>
        nodeTreeCollectTitleCharacterSources(
            const std::unordered_map<
                std::string,
                std::shared_ptr<const PSB::PSBDictionary>> &layersByName,
            bool includeCompositeLayers) {
            std::unordered_set<std::string> sources;
            for(const auto &[_, layer] : layersByName) {
                nodeTreeCollectTitleCharacterSources(
                    layer, sources, includeCompositeLayers);
            }
            return sources;
        }

        struct TitleCompositeFadeWindow {
            double startFrame = 0.0;
            double endFrame = 0.0;
        };

        std::optional<TitleCompositeFadeWindow>
        nodeTreeFindTitleCompositeFadeWindow(
            const std::unordered_map<
                std::string,
                std::shared_ptr<const PSB::PSBDictionary>> &layersByName) {
            for(const auto &[layerName, layer] : layersByName) {
                if(!layer) continue;
                const auto firstSource = nodeTreeFirstLayerSource(layer);
                if(!nodeTreeIsYuzuTitleCompositeLayer(layerName, firstSource)) {
                    continue;
                }
                const auto frames = nodeTreePsbList(layer, "frameList");
                if(!frames) continue;

                std::optional<double> startFrame;
                for(int i = 0; i < static_cast<int>(frames->size()); ++i) {
                    const auto frame =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(
                            (*frames)[i]);
                    if(!frame) continue;
                    const auto type =
                        static_cast<int>(nodeTreePsbNumber(frame, "type")
                                             .value_or(0.0));
                    if(type == 0) {
                        continue;
                    }
                    const auto content =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(
                            (*frame)["content"]);
                    if(!content) {
                        continue;
                    }
                    const auto time =
                        nodeTreePsbNumber(frame, "time").value_or(0.0);
                    // Runtime frame parsing treats omitted opacity as the
                    // default fully opaque slot value.
                    const auto opacity =
                        nodeTreePsbNumber(content, "opa").value_or(255.0);
                    if(!startFrame && opacity <= 16.0) {
                        startFrame = time;
                        continue;
                    }
                    if(startFrame && time > *startFrame &&
                       opacity >= 250.0) {
                        return TitleCompositeFadeWindow{ *startFrame, time };
                    }
                }
            }
            return std::nullopt;
        }

        bool nodeTreeAppendYuzuTitleIntroLayers(
            const MotionSnapshot &snapshot,
            const std::unordered_map<
                std::string,
                std::shared_ptr<const PSB::PSBDictionary>> &clipLayersByName,
            std::vector<MotionNode> &nodes) {
            auto rootCharacters =
                nodeTreeCollectTitleCharacterRootLayers(snapshot);
            if(rootCharacters.empty()) {
                return false;
            }

            const auto clipCharacterSources =
                nodeTreeCollectTitleCharacterSources(clipLayersByName, false);
            bool clipAlreadyHasAllRootCharacters = !clipCharacterSources.empty();
            for(const auto &character : rootCharacters) {
                if(clipCharacterSources.find(nodeTreeLowercase(
                       character.source)) == clipCharacterSources.end()) {
                    clipAlreadyHasAllRootCharacters = false;
                    break;
                }
            }
            if(clipAlreadyHasAllRootCharacters) {
                return false;
            }

            const auto compositeFade =
                nodeTreeFindTitleCompositeFadeWindow(clipLayersByName);
            if(!compositeFade ||
               compositeFade->endFrame <= compositeFade->startFrame ||
               compositeFade->startFrame <= 0.0) {
                return false;
            }

            double segmentFrames =
                compositeFade->endFrame - compositeFade->startFrame;
            if(segmentFrames * rootCharacters.size() >
               compositeFade->startFrame) {
                segmentFrames =
                    compositeFade->startFrame /
                    static_cast<double>(rootCharacters.size());
            }

            std::vector<size_t> sequence;
            sequence.reserve(rootCharacters.size());
            for(size_t index = 0; index < rootCharacters.size(); ++index) {
                sequence.push_back(index);
            }
            std::stable_sort(
                sequence.begin(), sequence.end(),
                [&](size_t lhs, size_t rhs) {
                    const auto &left = rootCharacters[lhs];
                    const auto &right = rootCharacters[rhs];
                    if(left.sequenceOrder != right.sequenceOrder) {
                        return left.sequenceOrder < right.sequenceOrder;
                    }
                    return left.rootOrder < right.rootOrder;
                });

            for(size_t order = 0; order < sequence.size(); ++order) {
                auto &character = rootCharacters[sequence[order]];
                character.introStartFrame =
                    compositeFade->startFrame -
                    segmentFrames *
                        static_cast<double>(sequence.size() - order);
                character.introEndFrame =
                    character.introStartFrame + segmentFrames;
                if(character.introStartFrame < 0.0) {
                    character.introStartFrame = 0.0;
                }
                if(character.introEndFrame <= character.introStartFrame) {
                    character.introEndFrame = character.introStartFrame + 1.0;
                }
            }

            // Fade timings follow character source numbering, while draw order
            // follows the original root layer list, which carries the title
            // character occlusion relationship.
            std::stable_sort(
                rootCharacters.begin(), rootCharacters.end(),
                [](const auto &lhs, const auto &rhs) {
                    return lhs.rootOrder < rhs.rootOrder;
                });
            const int hideFrame = static_cast<int>(
                std::lround(compositeFade->endFrame));
            bool appended = false;
            for (const auto &character : rootCharacters) {
                if (!character.layer) {
                    continue;
                }
                const auto introLayer = nodeTreeMakeYuzuTitleIntroLayer(
                    character.layer, character.label + "_intro",
                    static_cast<int>(std::lround(character.introStartFrame)),
                    static_cast<int>(std::lround(character.introEndFrame)),
                    hideFrame);
                if (!introLayer) {
                    continue;
                }
                walkTree(introLayer, 0, nodes);
                appended = true;
            }
            return appended;
        }

        // Check if any frame in frameList has a non-empty "src" in its "content".
        bool checkHasSource(const std::shared_ptr<const PSB::PSBDictionary> &node) {
            auto frameList = nodeTreePsbList(node, "frameList");
            if (!frameList) return false;
            for (int i = 0; i < static_cast<int>(frameList->size()); ++i) {
                auto frame = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frameList)[i]);
                if (!frame) continue;
                auto content = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frame)["content"]);
                if (!content) continue;
                auto srcVal = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*content)["src"]);
                if (srcVal && !srcVal->value.empty()) return true;
            }
            return false;
        }

        // Recursively walk PSB layer tree, appending nodes to the flat vector.
        void walkTree(const std::shared_ptr<const PSB::PSBDictionary> &psbNode,
                      int parentIdx,
                      std::vector<MotionNode> &nodes) {
            if (!psbNode) return;

            MotionNode node;
            node.index = static_cast<int>(nodes.size());
            node.parentIndex = parentIdx;
            node.psbNode = psbNode;

            // "label" → layerName (node+0)
            node.layerName = nodeTreePsbString(psbNode, "label");

            // "type" → nodeType (node+28)
            // 0=obj, 1=shape, 3=motion, 4=particle, 5=camera, 6=emitter,
            // 7=shapeAABB, 9=camConstraint, 10=anchor, 12=stencilComposite
            if (auto v = nodeTreePsbNumber(psbNode, "type"))
                node.nodeType = static_cast<int>(*v);

            // "coordinate" → coordinateMode (node+24)
            if (auto v = nodeTreePsbNumber(psbNode, "coordinate"))
                node.coordinateMode = static_cast<int>(*v);

            // "parameterize" → motion-local parameter table index.
            if (auto v = nodeTreePsbNumber(psbNode, "parameterize"))
                node.parameterizeIndex = static_cast<int>(*v);

            // "inheritMask" → inheritFlags (node+40, default 0x1FC)
            if (auto v = nodeTreePsbNumber(psbNode, "inheritMask"))
                node.inheritFlags = static_cast<int>(*v);

            // "groundCorrection" → bool (node+47)
            if (auto v = nodeTreePsbNumber(psbNode, "groundCorrection"))
                node.groundCorrection = (*v != 0.0);

            // "transformOrder" → 4 ints (node+84..96, default [0,1,2,3])
            if (auto toList = nodeTreePsbList(psbNode, "transformOrder")) {
                for (int i = 0; i < 4 && i < static_cast<int>(toList->size()); ++i) {
                    if (auto v = std::dynamic_pointer_cast<PSB::PSBNumber>((*toList)[i])) {
                        switch (v->numberType) {
                            case PSB::PSBNumberType::Int:
                                node.transformOrder[i] = v->getValue<int>(); break;
                            default:
                                node.transformOrder[i] = static_cast<int>(v->getValue<tjs_int64>()); break;
                        }
                    }
                }
            }

            // "meshTransform" → meshType (node+2000, sub_6B3C78 at 0x6B4190)
            if (auto v = nodeTreePsbNumber(psbNode, "meshTransform"))
                node.meshType = static_cast<int>(*v);
            // "meshSyncChildMask" → meshFlags (node+2004, sub_6B3C78 at 0x6B41B8)
            if (auto v = nodeTreePsbNumber(psbNode, "meshSyncChildMask"))
                node.meshFlags = static_cast<int>(*v);
            // "meshDivision" → meshDivision (node+2008, sub_6B3C78 at 0x6B41D8)
            if (auto v = nodeTreePsbNumber(psbNode, "meshDivision"))
                node.meshDivision = static_cast<int>(*v);

            // E-mote's secondary-motion layers store their animated 4x4
            // patches in meshCombinator raw resources instead of frame.mesh.bp.
            // Each resource is meshCount consecutive arrays of 32 little-endian
            // floats. The entries are sampled by their controller variable and
            // additively combined during Player::updateLayers.
            const auto meshCombinator =
                std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*psbNode)["meshCombinator"]);
            const auto combinatorList = meshCombinator
                ? nodeTreePsbList(meshCombinator, "combinatorList")
                : nullptr;
            if(combinatorList) {
                node.meshCombinators.reserve(combinatorList->size());
                for(int combinatorIndex = 0;
                    combinatorIndex < static_cast<int>(combinatorList->size());
                    ++combinatorIndex) {
                    const auto item =
                        std::dynamic_pointer_cast<PSB::PSBDictionary>(
                            (*combinatorList)[combinatorIndex]);
                    const auto variable = item
                        ? std::dynamic_pointer_cast<PSB::PSBDictionary>(
                              (*item)["variable"])
                        : nullptr;
                    const auto resource = item
                        ? std::dynamic_pointer_cast<PSB::PSBResource>(
                              (*item)["rawMeshList"])
                        : nullptr;
                    if(!item || !variable || !resource) {
                        continue;
                    }

                    MotionNode::MeshCombinatorEntry entry;
                    entry.variable = nodeTreePsbString(variable, "key");
                    entry.rangeBegin =
                        nodeTreePsbNumber(variable, "rangeBegin").value_or(0.0);
                    entry.rangeEnd =
                        nodeTreePsbNumber(variable, "rangeEnd").value_or(0.0);
                    entry.meshCount = static_cast<int>(
                        nodeTreePsbNumber(variable, "meshCount").value_or(0.0));
                    entry.neutralIndex = static_cast<int>(
                        nodeTreePsbNumber(item, "neutralIndex").value_or(0.0));
                    entry.meshType = static_cast<int>(
                        nodeTreePsbNumber(item, "meshType").value_or(0.0));

                    constexpr std::size_t kPatchFloatCount = 32;
                    constexpr std::size_t kFloatBytes = sizeof(float);
                    const std::size_t availableFloats =
                        resource->data.size() / kFloatBytes;
                    if(entry.meshCount <= 0) {
                        entry.meshCount = static_cast<int>(
                            availableFloats / kPatchFloatCount);
                    }
                    const std::size_t requiredFloats =
                        static_cast<std::size_t>(entry.meshCount) *
                        kPatchFloatCount;
                    if(entry.variable.empty() || entry.meshCount <= 0 ||
                       availableFloats < requiredFloats) {
                        continue;
                    }

                    entry.rawMeshes.resize(requiredFloats);
                    std::memcpy(entry.rawMeshes.data(), resource->data.data(),
                                requiredFloats * kFloatBytes);
                    node.meshCombinators.push_back(std::move(entry));
                }
            }

            // "stencilType" → stencilType (node+52)
            if (auto v = nodeTreePsbNumber(psbNode, "stencilType")) {
                node.stencilTypeBase = static_cast<int>(*v);
                node.stencilType = node.stencilTypeBase;
            }

            // "shape" → shapeType (node+32, sub_6B3C78 case 1)
            if (auto v = nodeTreePsbNumber(psbNode, "shape"))
                node.shapeType = static_cast<int>(*v);

            // "anchor" → anchorType/cameraConstraintType (node+2376, sub_6B3C78 case 9)
            if (auto v = nodeTreePsbNumber(psbNode, "anchor"))
                node.anchorType = node.cameraConstraintType = static_cast<int>(*v);

            // Particle properties (sub_6B3C78 case 4, 0x6B4438..0x6B45E4)
            if (auto v = nodeTreePsbNumber(psbNode, "particle"))
                node.particleType = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleMaxNum"))
                node.particleMaxNum = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleAccelRatio"))
                node.particleAccelRatio = *v;
            if (auto v = nodeTreePsbNumber(psbNode, "particleInheritAngle"))
                node.particleInheritAngle = (*v != 0.0);
            if (auto v = nodeTreePsbNumber(psbNode, "particleInheritVelocity"))
                node.particleInheritVelocity = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleFlyDirection"))
                node.particleFlyDirection = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleApplyZoomToVelocity"))
                node.particleApplyZoomToVelocity = static_cast<int>(*v);
            if (auto v = nodeTreePsbNumber(psbNode, "particleDeleteOutsideScreen"))
                node.particleDeleteOutside = (*v != 0.0);
            if (auto v = nodeTreePsbNumber(psbNode, "particleTriVolume"))
                node.particleTriVolume = (*v != 0.0);
            // Binary: node+2192 is ONE field, used as both accel decay ratio
            // and camera damping. "particleCameraDamping" PSB key overwrites
            // "particleAccelRatio" if both present (same binary offset).
            if (auto v = nodeTreePsbNumber(psbNode, "particleCameraDamping"))
                node.particleAccelRatio = *v;

            // Check if any frame has a source image
            node.hasSource = checkHasSource(psbNode);

            // "emoteEdit" → emoteEditDict (node+1980, sub_6B3C78 at 0x6B3D48)
            if (auto ee = std::dynamic_pointer_cast<PSB::PSBDictionary>((*psbNode)["emoteEdit"])) {
                node.emoteEditDict = ee;
                if(auto value = nodeTreePsbNumber(ee, "priorDraw")) {
                    node.authoredPriorDraw = static_cast<int>(*value);
                }
            }

            // === TJS↔Native bridge: create child objects (sub_6B3C78 case 3/4) ===
            if (node.nodeType == 3) {
                // Aligned to sub_6B3C78 case 3 (0x6B43C0..0x6B46E0):
                // operator new(0x568) → Player constructor → sub_6F1794 (NCB CreateAdaptor)
                // → store as tTJSVariant at node+1912.
                using PlayerAdaptor = ncbInstanceAdaptor<Player>;
                auto *childNative = new Player(ResourceManager{});
                if (auto *dispatch = PlayerAdaptor::CreateAdaptor(childNative)) {
                    node.childPlayerVar = tTJSVariant(dispatch, dispatch);
                    dispatch->Release();
                } else {
                    node.nativeChildPlayer.reset(childNative);
                }
            } else if (node.nodeType == 4) {
                // Aligned to sub_6B3C78 case 4 (0x6B45D8..0x6B45E4):
                // sub_704CB8 (TJSCreateArrayObject) → store at node+2296.
                if (auto *array = TJSCreateArrayObject()) {
                    node.particleArrayVar = tTJSVariant(array, array);
                    array->Release();
                }
            }

            // Add this node to the flat vector
            const int thisIdx = node.index;
            nodes.push_back(std::move(node));

            // Recurse into "children"
            auto children = nodeTreePsbList(psbNode, "children");
            if (children) {
                for (int i = 0; i < static_cast<int>(children->size()); ++i) {
                    auto child = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                        (*children)[i]);
                    walkTree(child, thisIdx, nodes);
                }
            }
        }

    } // anonymous namespace

    std::vector<MotionNode> buildNodeTree(
        const MotionSnapshot &snapshot,
        const MotionClip *clip) {

        std::vector<MotionNode> nodes;
        // Aligned to Player_buildNodeTree (0x6B51F0): root index 0 is a
        // synthetic container node, and all PSB layers are attached under it.
        MotionNode root;
        root.index = 0;
        root.parentIndex = -1;
        nodes.push_back(std::move(root));

        // Determine which layer set to use: current clip content first, then
        // fall back to snapshot root-level layers. This matches libkrkr2.so
        // using player+528 as the active motion/clip content object before
        // reading its "layer" property in Player_buildNodeTree (0x6B51F0).
        const std::unordered_map<std::string,
            std::shared_ptr<const PSB::PSBDictionary>> *layersByName = nullptr;
        const std::vector<std::string> *layerNames = nullptr;
        const std::vector<std::shared_ptr<const PSB::PSBDictionary>>
            *orderedLayers = nullptr;

        if (clip) {
            layersByName = &clip->layersByName;
            layerNames = &clip->layerNames;
            orderedLayers = &clip->orderedLayers;
        }

        if (!layersByName) {
            layersByName = &snapshot.layersByName;
            layerNames = &snapshot.layerNames;
        }

        if ((!orderedLayers || orderedLayers->empty()) &&
            (!layerNames || layerNames->empty())) {
            return nodes;
        }

        // Aligned to Player_buildNodeTree_recursive(player, 0, layerArray):
        // every top-level PSB layer uses the synthetic root (index 0) as parent.
        if (orderedLayers && !orderedLayers->empty()) {
            for (const auto &layer : *orderedLayers) {
                walkTree(layer, 0, nodes);
            }
        } else {
            for (const auto &name : *layerNames) {
                auto it = layersByName->find(name);
                if (it == layersByName->end()) continue;
                walkTree(it->second, 0, nodes);
            }
        }

        if (layersByName != &snapshot.layersByName &&
            nodeTreeIsYuzuStartupLogo(snapshot)) {
            std::unordered_set<std::string> includedLabels;
            includedLabels.reserve(nodes.size());
            for (const auto &node : nodes) {
                if (!node.layerName.empty()) {
                    includedLabels.insert(node.layerName);
                }
            }

            // Yuzu's startup logo clip lists the animated shape layers, while
            // the static "software creation" and logo glyph masks stay in the
            // root layer list. Keep those root layers live for this presentation.
            for (const auto &name : snapshot.layerNames) {
                if (!nodeTreeIsYuzuLogoRootTextLayer(name) ||
                    includedLabels.find(name) != includedLabels.end()) {
                    continue;
                }
                auto it = snapshot.layersByName.find(name);
                if (it == snapshot.layersByName.end()) continue;
                const size_t previousSize = nodes.size();
                walkTree(it->second, 0, nodes);
                for (size_t i = previousSize; i < nodes.size(); ++i) {
                    if (!nodes[i].layerName.empty()) {
                        includedLabels.insert(nodes[i].layerName);
                    }
                }
            }
        }

        if (layersByName != &snapshot.layersByName &&
            nodeTreeIsYuzuTitlePresentation(snapshot) &&
            nodeTreeLayerSetReferencesYuzuTitlePresentation(*layerNames,
                                                            *layersByName)) {
            const bool appendedTitleIntro =
                nodeTreeAppendYuzuTitleIntroLayers(snapshot, *layersByName,
                                                   nodes);
            if(appendedTitleIntro) {
                auto hasIncludedRootLayer =
                    [&](const std::string &label,
                        const std::shared_ptr<const PSB::PSBDictionary> &layer) {
                    for (const auto &node : nodes) {
                        if (node.parentIndex == 0 && node.layerName == label &&
                            node.psbNode == layer) {
                            return true;
                        }
                    }
                    return false;
                };

                // Some Yuzu title clips animate only the hand-off/composite
                // subset. Final static characters, logo, and frame strips live
                // in the root layer list and must stay present as siblings
                // until the full title presentation settles.
                for (const auto &name : snapshot.layerNames) {
                    auto it = snapshot.layersByName.find(name);
                    if (it == snapshot.layersByName.end()) continue;
                    if (!nodeTreeIsYuzuTitlePersistentRootLayer(name,
                                                                it->second)) {
                        continue;
                    }
                    if (hasIncludedRootLayer(name, it->second)) continue;
                    walkTree(it->second, 0, nodes);
                }
            }
        }

        // Baseline motionplayer compatibility: resolve the first node for a
        // stencil label, matching the original public implementation.
        std::unordered_map<std::string, int> nodeIndexByLabel;
        nodeIndexByLabel.reserve(nodes.size());
        for(const auto &node : nodes) {
            if(!node.layerName.empty()) {
                nodeIndexByLabel.emplace(node.layerName, node.index);
            }
        }
        for(auto &node : nodes) {
            if(node.nodeType != 12 || (node.stencilType & 4) == 0 ||
               !node.psbNode) {
                continue;
            }
            const auto maskLayers = nodeTreePsbList(
                node.psbNode, "stencilCompositeMaskLayerList");
            if(!maskLayers) {
                continue;
            }
            for(const auto &item : *maskLayers) {
                const auto label =
                    std::dynamic_pointer_cast<PSB::PSBString>(item);
                if(!label || label->value.empty()) {
                    continue;
                }
                const auto found = nodeIndexByLabel.find(label->value);
                if(found == nodeIndexByLabel.end()) {
                    continue;
                }
                auto &target = nodes[found->second];
                if(target.nodeType == 0 || target.nodeType == 3) {
                    target.stencilCompositeMaskReferenced = true;
                }
            }
        }

        // Optional packages can refine vendor-specific label scoping without
        // replacing the public node walker or renderer.
        if(const auto *extension = motionPlayerExtension();
           extension && extension->configureNodeTree) {
            extension->configureNodeTree(nodes);
        }

        if (auto logger = spdlog::get("plugin")) {
            logger->debug("buildNodeTree: clipLabel='{}', rootLayers={}, {} nodes built",
                          clip ? clip->label : std::string{},
                          layerNames->size(), nodes.size());
        }

        return nodes;
    }

} // namespace motion::detail
