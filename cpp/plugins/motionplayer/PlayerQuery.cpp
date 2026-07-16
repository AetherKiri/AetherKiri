// PlayerQuery.cpp — Viewport, timeline/variable queries, selector, misc, compat
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "HitTestInternal.h"
#include "SourceCache.h"

#include <cstdlib>
#include <cstring>

using namespace motion::internal;

namespace {
    template<typename Shape>
    tTJSVariant makeNativeShapeVariant(Shape *native) {
        if(auto *dispatch = ncbInstanceAdaptor<Shape>::CreateAdaptor(native)) {
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }
        delete native;
        return {};
    }

    bool motionDebugEnabled() {
        const char *enabled = std::getenv("AETHERKIRI_MOTION_DEBUG");
        return enabled && *enabled && std::strcmp(enabled, "0") != 0;
    }

    bool motionHitDebugEnabled() {
        const char *enabled = std::getenv("AETHERKIRI_MOTION_HIT_DEBUG");
        return enabled && *enabled && std::strcmp(enabled, "0") != 0;
    }

    std::string describeLayerForQueryDebug(tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return "<null>";
        }
        const auto type = layer->GetType();
        const bool drawableType = type != ltBinder;
        tjs_int imageWidth = -1;
        tjs_int imageHeight = -1;
        bool hasImage = false;
        if(drawableType) {
            try {
                imageWidth = layer->GetImageWidth();
                imageHeight = layer->GetImageHeight();
                hasImage = layer->GetHasImage();
            } catch(...) {
                imageWidth = -1;
                imageHeight = -1;
                hasImage = false;
            }
        }
        std::ostringstream out;
        out << "ptr=" << static_cast<const void *>(layer)
            << ",name=" << motion::detail::narrow(layer->GetName())
            << ",primary=" << (layer->IsPrimary() ? 1 : 0)
            << ",visible=" << (layer->GetVisible() ? 1 : 0)
            << ",parentVisible=" << (layer->GetParentVisible() ? 1 : 0)
            << ",opacity=" << layer->GetOpacity()
            << ",order=" << layer->GetOrderIndex()
            << ",overall=" << layer->GetOverallOrderIndex()
            << ",rect=[" << layer->GetLeft() << "," << layer->GetTop()
            << "," << layer->GetWidth() << "x" << layer->GetHeight() << "]"
            << ",image=";
        if(drawableType && imageWidth >= 0 && imageHeight >= 0) {
            out << imageWidth << "x" << imageHeight;
        } else {
            out << "?x?";
        }
        out << ",hasImage=" << (hasImage ? 1 : 0)
            << ",type=" << static_cast<int>(type)
            << ",children=" << layer->GetCount();
        return out.str();
    }

    std::string describeLayerAncestryForQueryDebug(tTJSNI_BaseLayer *layer) {
        std::ostringstream out;
        int depth = 0;
        while(layer && depth < 12) {
            if(depth != 0) {
                out << " <- ";
            }
            out << "[" << depth << ":"
                << describeLayerForQueryDebug(layer) << "]";
            layer = layer->GetParent();
            ++depth;
        }
        if(layer) {
            out << " <- ...";
        }
        return out.str();
    }

    bool layerBelongsToCgViewForQuery(tTJSNI_BaseLayer *layer) {
        for(auto *current = layer; current; current = current->GetParent()) {
            const auto name = motion::internal::psbDebugLowercase(
                motion::detail::narrow(current->GetName()));
            if(name.find("cg view layer") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    iTJSDispatch2 *selectVariantDispatchTarget(tTJSVariant *value) {
        if(!value || value->Type() != tvtObject) {
            return nullptr;
        }
        auto closure = value->AsObjectClosureNoAddRef();
        if(closure.ObjThis) {
            return closure.ObjThis;
        }
        if(closure.Object) {
            return closure.Object;
        }
        return value->AsObjectNoAddRef();
    }

    std::string describeDispatchLayerProbe(iTJSDispatch2 *dispatch) {
        if(!dispatch) {
            return "<null>";
        }
        tTJSNI_BaseLayer *layer = nullptr;
        const tjs_error er = dispatch->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
            reinterpret_cast<iTJSNativeInstance **>(&layer));
        std::ostringstream out;
        out << "dispatch=" << static_cast<const void *>(dispatch)
            << ",nis=" << er
            << ",layer=[" << describeLayerForQueryDebug(layer) << "]";
        return out.str();
    }

    bool startsWith(const std::string &value, const std::string &prefix) {
        return value.size() >= prefix.size() &&
            value.compare(0, prefix.size(), prefix) == 0;
    }

    bool containsString(const std::vector<std::string> &values,
                        const std::string &needle) {
        return std::find(values.begin(), values.end(), needle) != values.end();
    }

    std::string joinStrings(const std::vector<std::string> &values,
                            const char *separator = ",") {
        std::ostringstream out;
        for(size_t index = 0; index < values.size(); ++index) {
            if(index != 0) {
                out << separator;
            }
            out << values[index];
        }
        return out.str();
    }

    std::string labelTail(std::string value) {
        const auto slash = value.find_last_of("/\\");
        if(slash != std::string::npos) {
            value = value.substr(slash + 1);
        }
        return value;
    }

    void appendYuzuShortMotionMatches(
        std::vector<std::string> &matches,
        const std::vector<std::string> &labels,
        const std::string &base,
        const std::string &suffix) {
        if(base.empty() || suffix.empty()) {
            return;
        }
        const auto motionPrefix = std::string("motion/") + base + "/" + suffix;
        for(const auto &label : labels) {
            const auto lowered = motion::internal::psbDebugLowercase(label);
            const auto tail = labelTail(lowered);
            if(startsWith(lowered, motionPrefix) || startsWith(tail, suffix)) {
                if(!containsString(matches, label)) {
                    matches.push_back(label);
                }
            }
        }
    }

    std::vector<std::string> resolveYuzuShortMotionLabels(
        const motion::detail::MotionSnapshot &snapshot,
        const std::string &requestKey,
        const std::string &charaKey) {
        std::vector<std::string> labels;
        const auto request = motion::internal::psbDebugLowercase(requestKey);
        if(request.empty()) {
            return labels;
        }

        std::vector<std::string> bases;
        const auto pathBase = motion::internal::psbDebugLowercase(
            motion::internal::basenameWithoutExtension(snapshot.path));
        if(!pathBase.empty()) {
            bases.push_back(pathBase);
        }
        const auto charaBase = motion::internal::psbDebugLowercase(
            motion::internal::basenameWithoutExtension(charaKey));
        if(!charaBase.empty() && !containsString(bases, charaBase)) {
            bases.push_back(charaBase);
        }

        std::vector<std::string> allLabels = snapshot.mainTimelineLabels;
        for(const auto &label : snapshot.diffTimelineLabels) {
            if(!containsString(allLabels, label)) {
                allLabels.push_back(label);
            }
        }
        for(const auto &[label, _] : snapshot.clipsByLabel) {
            if(!containsString(allLabels, label)) {
                allLabels.push_back(label);
            }
        }

        for(const auto &base : bases) {
            if(!startsWith(request, base) || request.size() <= base.size()) {
                continue;
            }
            const auto suffix = request.substr(base.size());
            appendYuzuShortMotionMatches(labels, allLabels, base, suffix);
            if(!labels.empty()) {
                break;
            }
        }
        return labels;
    }

    float variableEaseWeightLike_0x671228(double ease) {
        if(ease > 0.0) {
            return static_cast<float>(ease + 1.0);
        }
        if(ease < 0.0) {
            return static_cast<float>(1.0 / (1.0 - ease));
        }
        return 1.0f;
    }

    bool hitTestMotionNodeShape(const motion::detail::MotionNode &node,
                                double x, double y) {
        motion::detail::HitData hit{};
        hit.type = node.shapeGeomType;
        for(size_t i = 0; i < std::size(node.shapeVertices) &&
                          i < hit.values.size();
            ++i) {
            hit.values[i] = node.shapeVertices[i];
        }
        return motion::detail::hitTestHitData(hit, x, y);
    }
}

namespace motion {

    // --- Viewport/display ---
    void Player::setFlip(bool v) { _runtime->flip = v; }

    bool Player::shouldMirrorEvalLabelLike_0x67C6B0(const std::string &label) {
        if(!_mirrorEvalEnabled || label.empty() || !_runtime->activeMotion) {
            return false;
        }

        if(_mirrorPositiveCache.find(label) != _mirrorPositiveCache.end()) {
            return true;
        }
        if(_mirrorNegativeCache.find(label) != _mirrorNegativeCache.end()) {
            return false;
        }

        const auto &matchList = _runtime->activeMotion->mirrorVariableMatchList;
        const bool matched =
            std::find(matchList.begin(), matchList.end(), label) !=
            matchList.end();
        if(matched) {
            _mirrorPositiveCache.insert(label);
        } else {
            _mirrorNegativeCache.insert(label);
        }
        return matched;
    }

    double &Player::ensureEvalResultSlotLike_0x686944(const std::string &label) {
        if(const auto it = _evalResultListIndex.find(label);
           it != _evalResultListIndex.end()) {
            return it->second->value;
        }

        _evalResultList.push_back(EvalResultEntry{label, 0.0});
        auto it = _evalResultList.end();
        --it;
        _evalResultListIndex[label] = it;
        return it->value;
    }

    void Player::removeEvalResultSlotLike_Reset(const std::string &label) {
        if(const auto it = _evalResultListIndex.find(label);
           it != _evalResultListIndex.end()) {
            _evalResultList.erase(it->second);
            _evalResultListIndex.erase(it);
        }
    }

    void Player::writeEvalResultValueLike_0x6C4668(const std::string &label,
                                                   double value) {
        if(label.empty()) {
            return;
        }
        ensureEvalResultSlotLike_0x686944(label) = value;
        _variableValues[label] = value;
        _evalResultValues[label] = value;
    }

    void Player::setOpacity(double v) { _runtime->opacity = v; }

    void Player::setVisible(bool v) { _runtime->visible = v; }

    void Player::setSlant(double v) { _runtime->slant = v; }

    void Player::setZoom(double v) { _runtime->zoom = v; }

    tTJSVariant Player::getLayerNames() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(activeLayerNames()));
    }

    void Player::releaseSyncWait() {
        _syncWaiting = false;
        _syncActive = false;
    }

    void Player::calcViewParam() {
        _runtime->lastViewParam = detail::makeDictionary({
            { "flip", _runtime->flip },
            { "opacity", _runtime->opacity },
            { "visible", _runtime->visible },
            { "slant", _runtime->slant },
            { "zoom", _runtime->zoom },
            { "zFactor", _zFactor },
            { "colorWeight", getColorWeight() },
        });
    }

    tTJSVariant Player::getLayerMotion(ttstr name) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime) {
            return {};
        }

        // Motion buttons are queried between draws. Keep their child players
        // synchronized with the parent node before returning the object used
        // by script-side contains(x, y).
        if(!_runtime->nodes.empty()) {
            updateLayers();
        }

        const auto key = detail::narrow(name);
        const auto it = _runtime->nodeLabelMap.find(key);
        if(it == _runtime->nodeLabelMap.end() || it->second < 0 ||
           it->second >= static_cast<int>(_runtime->nodes.size())) {
            return {};
        }

        const auto &node = _runtime->nodes[static_cast<size_t>(it->second)];
        if(node.nodeType == 3 && node.childPlayerVar.Type() == tvtObject) {
            if(auto *child = node.getChildPlayer()) {
                child->applyMotionParentRootStateForRender();
                child->ensureNodeTreeBuilt();
                child->updateLayers();
                if(LOGGER && motionHitDebugEnabled()) {
                    LOGGER->info(
                        "motion hit child query: parentMotion={} label={} parentPos=({:.2f},{:.2f}) childMotion={} childLabel={}",
                        _runtime->activeMotion
                            ? _runtime->activeMotion->path
                            : std::string("<none>"),
                        key, node.accumulated.posX, node.accumulated.posY,
                        child->_runtime && child->_runtime->activeMotion
                            ? child->_runtime->activeMotion->path
                            : std::string("<none>"),
                        detail::narrow(child->_motionKey));
                }
            }
            return node.childPlayerVar;
        }

        return {};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime) {
            return {};
        }

        if(!_runtime->nodes.empty()) {
            updateLayers();
        }

        const auto key = detail::narrow(name);
        const auto it = _runtime->nodeLabelMap.find(key);
        if(it == _runtime->nodeLabelMap.end() || it->second < 0 ||
           it->second >= static_cast<int>(_runtime->nodes.size())) {
            return {};
        }

        const auto &node = _runtime->nodes[static_cast<size_t>(it->second)];
        tTJSVariant motion;
        if(node.nodeType == 3 && node.childPlayerVar.Type() == tvtObject) {
            motion = node.childPlayerVar;
        }

        tTJSVariant shape;
        switch(node.shapeGeomType) {
            case ShapeTypeCircle: {
                auto *circle = new Circle();
                circle->x = node.shapeVertices[0];
                circle->y = node.shapeVertices[1];
                circle->r = node.shapeVertices[2];
                shape = makeNativeShapeVariant(circle);
                break;
            }
            case ShapeTypeRect: {
                auto *rect = new Rect();
                rect->l = node.shapeVertices[3];
                rect->t = node.shapeVertices[4];
                rect->w = node.shapeVertices[5] - node.shapeVertices[3];
                rect->h = node.shapeVertices[6] - node.shapeVertices[4];
                shape = makeNativeShapeVariant(rect);
                break;
            }
            case ShapeTypeQuad: {
                auto *quad = new Quad();
                for(size_t index = 0; index < 8; ++index) {
                    quad->verts[index] = node.shapeVertices[index + 7];
                }
                shape = makeNativeShapeVariant(quad);
                break;
            }
            default:
                break;
        }

        if(LOGGER && motionHitDebugEnabled()) {
            const auto &root = _runtime->nodes.front();
            LOGGER->info(
                "motion layer getter: motion={} chara={} motionKey={} layer={} type={} geom={} rootPos=({:.2f},{:.2f}) nodePos=({:.2f},{:.2f}) nodeScale=({:.4f},{:.4f}) rect=({:.2f},{:.2f},{:.2f},{:.2f})",
                _runtime->activeMotion
                    ? _runtime->activeMotion->path
                    : std::string("<none>"),
                detail::narrow(_chara), detail::narrow(_motionKey), key,
                node.nodeType, node.shapeGeomType,
                root.accumulated.posX, root.accumulated.posY,
                node.accumulated.posX, node.accumulated.posY,
                node.accumulated.scaleX, node.accumulated.scaleY,
                node.shapeVertices[3], node.shapeVertices[4],
                node.shapeVertices[5] - node.shapeVertices[3],
                node.shapeVertices[6] - node.shapeVertices[4]);
        }

        const auto layerId = requireLayerId(name);
        return detail::makeDictionary({
            { "name", name },
            { "label", name },
            { "id", layerId },
            { "type", node.nodeType },
            { "visible", node.accumulated.visible },
            { "branchVisible", node.accumulated.active },
            { "layerVisible", node.drawFlag },
            { "x", node.accumulated.posX },
            { "y", node.accumulated.posY },
            { "left", node.accumulated.posX },
            { "top", node.accumulated.posY },
            { "flipX", node.accumulated.flipX },
            { "flipY", node.accumulated.flipY },
            { "zoomX", node.accumulated.scaleX },
            { "zoomY", node.accumulated.scaleY },
            { "angleDeg", node.accumulated.angle },
            { "angleRad", node.accumulated.angle * 3.14159265358979323846 /
                              180.0 },
            { "slantX", node.accumulated.slantX },
            { "slantY", node.accumulated.slantY },
            { "opacity", node.accumulated.opacity },
            { "shape", shape },
            { "motion", motion },
        });
    }

    tTJSVariant Player::getLayerGetterList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        std::vector<tTJSVariant> items;
        for(const auto &layerName : activeLayerNames()) {
            const auto getter = getLayerGetter(detail::widen(layerName));
            if(getter.Type() != tvtVoid) {
                items.push_back(getter);
            }
        }
        return detail::makeArray(items);
    }

    void Player::skipToSync() {
        for(auto &[_, state] : _runtime->timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames;
            }
            if(!state.loop) {
                state.playing = false;
            }
        }
        if(const auto it = std::remove_if(_runtime->playingTimelineLabels.begin(),
                                          _runtime->playingTimelineLabels.end(),
                                          [this](const std::string &label) {
                                              const auto found =
                                                  _runtime->timelines.find(label);
                                              return found ==
                                                      _runtime->timelines.end() ||
                                                  !found->second.playing;
                                          });
           it != _runtime->playingTimelineLabels.end()) {
            _runtime->playingTimelineLabels.erase(
                it, _runtime->playingTimelineLabels.end());
        }
        _syncWaiting = false;
        _syncActive = false;
        _allplaying = !_runtime->playingTimelineLabels.empty();
        if(!_allplaying) {
            disableAutoProgress();
        }
    }

    void Player::setStereovisionCameraPosition(double x, double y, double z) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tTJSVariant vx = x;
        tTJSVariant vy = y;
        tTJSVariant vz = z;
        static tjs_uint addHint = 0;
        tTJSVariant *argsX[] = { &vx };
        tTJSVariant *argsY[] = { &vy };
        tTJSVariant *argsZ[] = { &vz };
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsX, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsY, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsZ, array);
        _cameraPosition = tTJSVariant(array, array);
        array->Release();
    }

    // --- Timeline/variable queries ---
    void Player::setVariableResolvedWeightLike_0x671228(
        const std::string &key, double value, double transition,
        double easeWeight) {
        const auto *activeMotion = _runtime->activeMotion.get();
        const auto bindingIt = activeMotion
            ? activeMotion->controllerBindings.find(key)
            : decltype(activeMotion->controllerBindings.find(key)){};
        const bool hasBinding =
            activeMotion && bindingIt != activeMotion->controllerBindings.end();

        if(hasBinding) {
            const auto queueControllerStateLikeBinary =
                [&](const std::string &targetKey,
                    VariableAnimatorState &state,
                    double currentValueInput,
                    double requestedValue,
                    double requestedTransition,
                    double requestedEaseWeight) {
                    const auto currentValue =
                        static_cast<float>(currentValueInput);
                    const auto targetValue =
                        static_cast<float>(requestedValue);
                    if(requestedTransition <= 0.0) {
                        state.queue.clear();
                        state.active = false;
                        state.currentValue = targetValue;
                        state.startValue = targetValue;
                        state.targetValue = targetValue;
                        state.progress = 1.0f;
                        state.duration = 0.0f;
                        state.weight =
                            static_cast<float>(requestedEaseWeight);
                        _variableValues[targetKey] = requestedValue;
                        ensureEvalResultSlotLike_0x686944(targetKey) =
                            requestedValue;
                        _evalResultValues[targetKey] = requestedValue;
                        return;
                    }

                    if(!_emoteAnimatorFlag) {
                        state.queue.clear();
                        state.active = false;
                        state.currentValue = currentValue;
                        state.startValue = currentValue;
                        state.targetValue = currentValue;
                        state.progress = 1.0f;
                        state.duration = 0.0f;
                    }

                    state.queue.push_back(VariableKeyframe{
                        targetValue,
                        static_cast<float>(requestedTransition),
                        static_cast<float>(requestedEaseWeight),
                    });
                    _variableValues[targetKey] = state.currentValue;
                    ensureEvalResultSlotLike_0x686944(targetKey) =
                        state.currentValue;
                    _evalResultValues[targetKey] = state.currentValue;
                };

            const auto queueControllerLikeBinary =
                [&](VariableAnimatorState &state,
                    double requestedValue,
                    double requestedTransition,
                    double requestedEaseWeight) {
                    queueControllerStateLikeBinary(
                        key, state,
                        _variableValues.count(key) ? _variableValues[key]
                                                   : getVariable(detail::widen(key)),
                        requestedValue, requestedTransition,
                        requestedEaseWeight);
                };

            switch(bindingIt->second.type) {
                case 0:
                case 1:
                case 2:
                    // Aligned to 0x671228 cases 0/1/2:
                    // these labels are routed to physics control groups, not to
                    // the generic eval-result map / animator sink.
                    _emoteDirty = true;
                    return;
                case 3:
                    // Aligned to 0x671228 default route for loopControl-built
                    // entries: no generic eval-result write happens here.
                    _emoteDirty = true;
                    return;
                case 4:
                case 5:
                case 7:
                case 8: {
                    if(bindingIt->second.type == 8 && activeMotion) {
                        const auto selectorIt =
                            activeMotion->selectorControls.find(key);
                        if(selectorIt != activeMotion->selectorControls.end()) {
                            const int selectedIndex =
                                static_cast<int>(value);
                            eraseControllerAnimatorStateLike_0x671228(key);
                            _variableValues[key] =
                                static_cast<double>(selectedIndex);
                            ensureEvalResultSlotLike_0x686944(key) =
                                static_cast<double>(selectedIndex);
                            _evalResultValues[key] =
                                static_cast<double>(selectedIndex);

                            const double resolvedEaseWeight = easeWeight;
                            int optionIndex = 0;
                            for(const auto &option : selectorIt->second.options) {
                                if(option.label.empty()) {
                                    ++optionIndex;
                                    continue;
                                }
                                const double targetValue =
                                    optionIndex == selectedIndex
                                        ? option.onValue
                                        : option.offValue;
                                const auto currentIt =
                                    _evalResultValues.find(option.label);
                                const double currentValue =
                                    currentIt != _evalResultValues.end()
                                        ? currentIt->second
                                        : (_variableValues.count(option.label)
                                               ? _variableValues[option.label]
                                               : getVariable(
                                                     detail::widen(option.label)));
                                const double range =
                                    std::abs(option.onValue - option.offValue);
                                const double scaledTransition =
                                    transition > 0.0 && range > 0.0000001
                                        ? std::abs(targetValue - currentValue) /
                                              range * transition
                                        : 0.0;
                                auto &optionState =
                                    _type8ControllerAnimators[option.label];
                                queueControllerStateLikeBinary(
                                    option.label, optionState, currentValue,
                                    targetValue, scaledTransition,
                                    resolvedEaseWeight);
                                ++optionIndex;
                            }
                            _emoteDirty = true;
                            return;
                        }
                    }
                    auto *bucket =
                        controllerAnimatorBucketLike_0x671228(
                            bindingIt->second.type);
                    if(!bucket) {
                        _emoteDirty = true;
                        return;
                    }
                    auto &state = (*bucket)[key];
                    ensureEvalResultSlotLike_0x686944(key);
                    queueControllerLikeBinary(state, value, transition,
                                              easeWeight);
                    _emoteDirty = true;
                    return;
                }
                case 6: {
                    if(bindingIt->second.role == "label") {
                        eraseControllerAnimatorStateLike_0x671228(key);
                        const double directValue =
                            static_cast<double>(static_cast<int>(value));
                        _variableValues[key] = directValue;
                        ensureEvalResultSlotLike_0x686944(key) = directValue;
                        _evalResultValues[key] = directValue;
                        _emoteDirty = true;
                        return;
                    }
                    auto &state = _type6ControllerAnimators[key];
                    ensureEvalResultSlotLike_0x686944(key);
                    queueControllerLikeBinary(state, value, transition,
                                              easeWeight);
                    _emoteDirty = true;
                    return;
                }
                default:
                    _emoteDirty = true;
                    return;
            }
        }

        // Aligned to Player_setVariable (0x671228): labels without a controller
        // binding bypass animator queues and write the eval map immediately.
        _variableAnimators.erase(key);
        _variableValues[key] = value;
        ensureEvalResultSlotLike_0x686944(key) = value;
        _evalResultValues[key] = value;
        _emoteDirty = true;
    }

    void Player::setVariable(ttstr label, double value, double transition,
                             double ease) {
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return;
        }

        setVariableResolvedWeightLike_0x671228(
            key, value, transition, variableEaseWeightLike_0x671228(ease));
    }

    double Player::getVariable(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return 0.0;
        }

        if(const auto it = _variableValues.find(key); it != _variableValues.end()) {
            return it->second;
        }

        if(!_runtime->activeMotion) {
            return 0.0;
        }

        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it != _runtime->activeMotion->variableFrames.end() &&
           !it->second.empty()) {
            return it->second.front().value;
        }

        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return it->second.first;
        }

        return 0.0;
    }

    tjs_int Player::countVariables() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->variableLabels.size())
            : 0;
    }

    ttstr Player::getVariableLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >= _runtime->activeMotion->variableLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->variableLabels[idx]);
    }

    tjs_int Player::countVariableFrameAt(tjs_int idx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0;
        }
        const auto frames = getVariableFrameList(label);
        return getObjectCount(frames);
    }

    ttstr Player::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return {};
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return {};
        }
        return detail::widen(it->second[frameIdx].label);
    }

    double Player::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0.0;
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return 0.0;
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return 0.0;
        }
        return it->second[frameIdx].value;
    }

    bool Player::getTimelinePlaying(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.playing;
        }
        return false;
    }

    tTJSVariant Player::getVariableRange(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return detail::makeArray(
                { tTJSVariant(it->second.first), tTJSVariant(it->second.second) });
        }
        return {};
    }

    tTJSVariant Player::getVariableFrameList(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it == _runtime->activeMotion->variableFrames.end()) {
            return detail::makeArray({});
        } else {
            std::vector<tTJSVariant> frames;
            for(const auto &frame : it->second) {
                frames.push_back(detail::makeDictionary({
                    { "label", detail::widen(frame.label) },
                    { "frame", frame.value },
                    { "value", frame.value },
                }));
            }
            return detail::makeArray(frames);
        }
    }

    bool Player::hitTestLayer(ttstr name, double x, double y) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime || !_runtime->activeMotion) {
            return false;
        }

        if(!_runtime->nodes.empty()) {
            updateLayers();
            calcBounds();
        }

        const auto key = detail::narrow(name);
        if(key.empty()) {
            return false;
        }

        auto findNodeRecursive =
            [&](auto &&self, Player *player) -> const detail::MotionNode * {
            if(!player || !player->_runtime) {
                return nullptr;
            }

            if(const auto it = player->_runtime->nodeLabelMap.find(key);
               it != player->_runtime->nodeLabelMap.end()) {
                const auto index = it->second;
                if(index >= 0 &&
                   index < static_cast<int>(player->_runtime->nodes.size())) {
                    return &player->_runtime->nodes[static_cast<size_t>(index)];
                }
            }

            for(auto &node : player->_runtime->nodes) {
                if(node.nodeType == 3) {
                    if(auto *child = node.getChildPlayer()) {
                        if(const auto *found = self(self, child)) {
                            return found;
                        }
                    }
                } else if(node.nodeType == 4) {
                    const int particleCount = node.getParticleCount();
                    for(int i = 0; i < particleCount; ++i) {
                        if(auto *child = node.getParticleChild(i)) {
                            if(const auto *found = self(self, child)) {
                                return found;
                            }
                        }
                    }
                }
            }

            return nullptr;
        };

        if(const auto *node = findNodeRecursive(findNodeRecursive, this)) {
            return hitTestMotionNodeShape(*node, x, y);
        }
        return false;
    }

    bool Player::contains(double x, double y) {
        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        if(!_runtime || !_runtime->activeMotion) {
            return false;
        }

        if(_motionParentPlayer && _motionParentPlayer->_runtime) {
            _motionParentPlayer->ensureNodeTreeBuilt();
            if(!_motionParentPlayer->_runtime->nodes.empty()) {
                _motionParentPlayer->updateLayers();
            }
            applyMotionParentRootStateForRender();
        }

        if(!_runtime->nodes.empty()) {
            updateLayers();
            calcBounds();
        }

        bool hasExplicitShape = false;
        bool shapeHit = false;
        for(const auto &node : _runtime->nodes) {
            if(node.nodeType != 1 || !node.accumulated.active ||
               node.activeSlot().done || node.shapeGeomType == ShapeTypePoint) {
                continue;
            }
            hasExplicitShape = true;
            if(hitTestMotionNodeShape(node, x, y)) {
                shapeHit = true;
                break;
            }
        }

        const bool boundsHit =
            _boundsMinX <= x && x < _boundsMaxX &&
            _boundsMinY <= y && y < _boundsMaxY;
        const bool hit = hasExplicitShape ? shapeHit : boundsHit;
        if(LOGGER && motionHitDebugEnabled()) {
            LOGGER->info(
                "motion contains: motion={} label={} point=({:.2f},{:.2f}) shapes={} shapeHit={} bounds=({:.2f},{:.2f},{:.2f},{:.2f}) boundsHit={} result={}",
                _runtime->activeMotion->path, detail::narrow(_motionKey), x, y,
                hasExplicitShape ? 1 : 0, shapeHit ? 1 : 0,
                _boundsMinX, _boundsMinY, _boundsMaxX, _boundsMaxY,
                boundsHit ? 1 : 0, hit ? 1 : 0);
        }
        return hit;
    }

    tjs_error Player::containsCompatMethod(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self =
            ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(!result) {
            return TJS_E_INVALIDPARAM;
        }

        if(numparams >= 3 && param[0] && param[1] && param[2]) {
            *result = tTJSVariant(
                self->hitTestLayer(ttstr(*param[0]), param[1]->AsReal(),
                                   param[2]->AsReal()));
            return TJS_S_OK;
        }
        if(numparams >= 2 && param[0] && param[1]) {
            *result = tTJSVariant(
                self->contains(param[0]->AsReal(), param[1]->AsReal()));
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

    tjs_int Player::countMainTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->mainTimelineLabels.size())
            : 0;
    }

    ttstr Player::getMainTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->mainTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->mainTimelineLabels[idx]);
    }

    tTJSVariant Player::getMainTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->mainTimelineLabels));
    }

    tjs_int Player::countDiffTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->diffTimelineLabels.size())
            : 0;
    }

    ttstr Player::getDiffTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->diffTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->diffTimelineLabels[idx]);
    }

    tTJSVariant Player::getDiffTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->diffTimelineLabels));
    }

    bool Player::getLoopTimeline(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->loopTimelines.find(key);
           it != _runtime->activeMotion->loopTimelines.end()) {
            return it->second;
        }
        return false;
    }

    tjs_int Player::countPlayingTimelines() {
        ensureMotionLoaded();
        return static_cast<tjs_int>(_runtime->playingTimelineLabels.size());
    }

    ttstr Player::getPlayingTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(idx >= 0 &&
           static_cast<size_t>(idx) < _runtime->playingTimelineLabels.size()) {
            return detail::widen(_runtime->playingTimelineLabels[idx]);
        }
        return {};
    }

    tjs_int Player::getPlayingTimelineFlagsAt(tjs_int idx) {
        ensureMotionLoaded();
        if(idx >= 0 &&
           static_cast<size_t>(idx) < _runtime->playingTimelineLabels.size()) {
            const auto &label = _runtime->playingTimelineLabels[idx];
            if(const auto it = _runtime->timelines.find(label);
               it != _runtime->timelines.end()) {
                return it->second.flags;
            }
        }
        return 0;
    }

    tjs_int Player::getTimelineTotalFrameCount(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return static_cast<tjs_int>(it->second.totalFrames);
        }
        if(_runtime->activeMotion) {
            if(const auto it = _runtime->activeMotion->timelineTotalFrames.find(key);
               it != _runtime->activeMotion->timelineTotalFrames.end()) {
                return static_cast<tjs_int>(it->second);
            }
        }
        return 0;
    }

    void Player::playTimeline(ttstr label, tjs_int flags) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return;
        }
        if(_runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto it = _runtime->timelines.find(key);
        if(it == _runtime->timelines.end()) {
            return;
        }

        // Aligned to libkrkr2.so Player_playTimeline (0x672F70):
        // parallel flag first clears the playing-timeline list.
        if((flags & 1) != 0) {
            stopTimeline(TJS_W(""));
        }

        if(!label.IsEmpty()) {
            if(std::find(_runtime->playingTimelineLabels.begin(),
                         _runtime->playingTimelineLabels.end(),
                         key) == _runtime->playingTimelineLabels.end()) {
                _runtime->playingTimelineLabels.push_back(key);
            }
            _runtime->lastExplicitTimelineLabel = key;
        }

        if(const auto *clip = detail::findMotionClip(
               *_runtime->activeMotion, detail::narrow(_chara), key,
               false)) {
            it->second.totalFrames = clip->totalFrames;
            it->second.loop = clip->loop;
            it->second.loopTime = clip->loopTime;
        }
        it->second.flags = flags;
        it->second.playing = true;
        it->second.currentTime = 0.0;
        it->second.blendRatio = 1.0;
        it->second.blendAnimator = {};
        it->second.blendAutoStop = false;
        it->second.controlInitialized = false;
        it->second.controlLastAppliedTime = 0.0;
        it->second.controlFrameCursor.clear();
        it->second.controlTrackValues.clear();
        it->second.controlTrackAnimators.clear();
        if(const auto controlIt =
               _runtime->activeMotion->timelineControlByLabel.find(key);
           controlIt != _runtime->activeMotion->timelineControlByLabel.end()) {
            resetTimelineControlStateLike_0x671A50(
                it->second, controlIt->second, 0.0);
        }
        _allplaying = !_runtime->playingTimelineLabels.empty();
        if(!_allplaying) {
            disableAutoProgress();
        }
    }

    void Player::stopTimeline(ttstr label) {
        const auto key = detail::narrow(label);
        if(label.IsEmpty()) {
            for(auto &[_, state] : _runtime->timelines) {
                state.playing = false;
                state.blendRatio = 1.0;
                state.blendAnimator = {};
                state.blendAutoStop = false;
                state.controlInitialized = false;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
            }
            _runtime->playingTimelineLabels.clear();
        } else {
            if(const auto it = _runtime->timelines.find(key);
               it != _runtime->timelines.end()) {
                it->second.playing = false;
                it->second.blendRatio = 1.0;
                it->second.blendAnimator = {};
                it->second.blendAutoStop = false;
                it->second.controlInitialized = false;
                it->second.controlFrameCursor.clear();
                it->second.controlTrackValues.clear();
                it->second.controlTrackAnimators.clear();
            }
            if(const auto it = std::remove(_runtime->playingTimelineLabels.begin(),
                                           _runtime->playingTimelineLabels.end(),
                                           key);
               it != _runtime->playingTimelineLabels.end()) {
                _runtime->playingTimelineLabels.erase(
                    it, _runtime->playingTimelineLabels.end());
            }
        }

        _allplaying = !_runtime->playingTimelineLabels.empty();
        if(!_allplaying) {
            disableAutoProgress();
        }
    }

    void Player::setTimelineBlendRatio(ttstr label, double ratio) {
        ensureMotionLoaded();
        if(_runtime->timelines.empty() && _runtime->activeMotion) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto &state = _runtime->timelines[key];
        state.label = key;
        state.blendRatio = ratio;
        state.blendAnimator = {};
        state.blendAutoStop = false;
    }

    double Player::getTimelineBlendRatio(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.blendRatio;
        }
        return 1.0;
    }

    void Player::fadeInTimeline(ttstr label, double duration, tjs_int flags) {
        const auto key = detail::narrow(label);
        const bool alreadyPlaying =
            std::find(_runtime->playingTimelineLabels.begin(),
                      _runtime->playingTimelineLabels.end(),
                      key) != _runtime->playingTimelineLabels.end();
        if(!alreadyPlaying) {
            playTimeline(label, 3);
            setTimelineBlendLike_0x6735AC(key, false, 0.0, 0.0, 0.0);
        }
        setTimelineBlendLike_0x6735AC(key, false, 1.0, duration, 0.0);
    }

    void Player::fadeOutTimeline(ttstr label, double duration, tjs_int) {
        setTimelineBlendLike_0x6735AC(detail::narrow(label), true, 0.0,
                                      duration, 0.0);
    }

    tTJSVariant Player::getPlayingTimelineInfoList() {
        ensureMotionLoaded();
        return detail::makeArray(timelineInfoVariants(*_runtime));
    }

    bool Player::playMotionLike_0x6B2284(ttstr label, tjs_int flags) {
        if(!_runtime->activeMotion && _project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(_project)) {
                activateMotion(*_runtime, snapshot);
                syncVariableKeysFromActiveMotion();
            }
        }

        ensureMotionLoaded();
        if(_runtime->activeMotion && _runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines,
                                        *_runtime->activeMotion);
        }

        if(!label.IsEmpty() && !_runtime->activeMotion) {
            setMotion(label);
            ensureMotionLoaded();
            if(_runtime->activeMotion && _runtime->timelines.empty()) {
                detail::primeTimelineStates(_runtime->timelines,
                                            *_runtime->activeMotion);
            }
        }

        if(!_runtime->activeMotion) {
            return false;
        }

        if((flags & PlayFlagForce) != 0) {
            stopTimeline(TJS_W(""));
        }

        const bool chainMode = (flags & PlayFlagChain) != 0;
        const auto playOne = [&](const std::string &timelineLabel,
                                 const bool rememberExplicit) {
            auto &state = _runtime->timelines[timelineLabel];
            state.label = timelineLabel;
            if(const auto *clip = detail::findMotionClip(
                   *_runtime->activeMotion, detail::narrow(_chara),
                   timelineLabel, false)) {
                state.totalFrames = clip->totalFrames;
                state.loop = clip->loop;
                state.loopTime = clip->loopTime;
            }
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            if(!chainMode) {
                state.currentTime = 0.0;
                state.controlInitialized = false;
                state.controlLastAppliedTime = 0.0;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
            }
            if(std::find(_runtime->playingTimelineLabels.begin(),
                         _runtime->playingTimelineLabels.end(),
                         timelineLabel) == _runtime->playingTimelineLabels.end()) {
                _runtime->playingTimelineLabels.push_back(timelineLabel);
            }
            if(rememberExplicit) {
                _runtime->lastExplicitTimelineLabel = timelineLabel;
            }
            if(state.totalFrames <= 0.0 && _runtime->activeMotion) {
                const auto it =
                    _runtime->activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != _runtime->activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            if(_runtime->timelines.find(key) != _runtime->timelines.end()) {
                playOne(key, true);
                started = true;
            }
        }

        if(!started) {
            const auto &primary =
                !_runtime->activeMotion->mainTimelineLabels.empty()
                ? _runtime->activeMotion->mainTimelineLabels
                : _runtime->activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel, false);
                started = true;
            }
        }

        _allplaying = !_runtime->playingTimelineLabels.empty();
        if(_allplaying) {
            claimYuzuSdAutoProgress();
        }
        return started;
    }

    // --- Selector ---
    bool Player::isSelectorTarget(ttstr name) {
        const auto *layers = activeLayersByName();
        if(!layers) {
            return false;
        }
        const auto key = detail::narrow(name);
        return layers->find(key) != layers->end() &&
            _runtime->disabledSelectorTargets.find(key) ==
                _runtime->disabledSelectorTargets.end();
    }

    void Player::deactivateSelectorTarget(ttstr name) {
        _runtime->disabledSelectorTargets[detail::narrow(name)] = true;
    }

    // --- Misc ---
    tTJSVariant Player::getCommandList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(
            detail::stringsToVariants(activeSourceCandidates()));
    }

    bool Player::getD3DAvailable() { return true; }

    void Player::doAlphaMaskOperation() {}

    // Aligned to libkrkr2.so Player_playImpl (0x6B21E8):
    // Called from sub_6BE0C0 at 0x6BE46C with flags = motionFlags | v12.
    // flags: PlayFlagForce(1)=force reload, PlayFlagStealth(16)=set stealth fields only.
    void Player::onFindMotion(ttstr name, int flags) {
        // PlayFlagStealth (0x10): store as stealth motion, don't load
        // Binary: if ((flags & 0x10) && !player->project) { player->motionKey = name; return; }
        if ((flags & PlayFlagStealth) && _project.Type() == tvtVoid) {
            _stealthMotion = name;
            return;
        }

        // PlayFlagForce (0x01): force reload even if same motion is loaded
        // Binary: Player_setMotionImpl skips reload guard when force flag set
        if ((flags & PlayFlagForce) && _motionKey == name) {
            _motionKey = ttstr();  // clear to bypass same-motion guard in findMotion
        }

        // Load the motion (equivalent to Player_setMotionImpl → loadMotion)
        (void)findMotion(name);

        // After loading, prime timelines and start playback
        // (aligned to Player_setMotionImpl post-load behavior)
        if (_runtime->activeMotion && _runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines,
                                        *_runtime->activeMotion);
        }

        // Start all timelines playing (equivalent to playCompat's playOne loop)
        if (_runtime->activeMotion && !_runtime->timelines.empty()) {
            double maxTF = 0.0;
            _runtime->playingTimelineLabels.clear();
            const auto &primary =
                !_runtime->activeMotion->mainTimelineLabels.empty()
                    ? _runtime->activeMotion->mainTimelineLabels
                    : _runtime->activeMotion->diffTimelineLabels;
            for (const auto &timelineLabel : primary) {
                auto &state = _runtime->timelines[timelineLabel];
                state.flags = flags & ~PlayFlagStealth;  // pass flags minus stealth
                state.playing = true;
                state.blendRatio = 1.0;
                state.controlInitialized = false;
                state.controlLastAppliedTime = state.currentTime;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
                _runtime->playingTimelineLabels.push_back(timelineLabel);
                if (state.totalFrames > maxTF) maxTF = state.totalFrames;
            }
            _cachedTotalFrames = maxTF;  // player+1128 cached value
            _allplaying = !_runtime->playingTimelineLabels.empty();
        }

        // Handle pending stealth motion (0x6B226C..0x6B2280)
        if (!_stealthMotion.IsEmpty()) {
            _stealthChara = _chara;
            // stealthMotion is consumed — binary nulls it after use
            _stealthMotion = ttstr();
        }
    }

    tjs_error Player::setDrawAffineTranslateMatrixCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        std::array<double, 6> matrix{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        if(numparams >= 6) {
            for(size_t index = 0; index < matrix.size(); ++index) {
                if(!param[index] || param[index]->Type() == tvtVoid) {
                    return TJS_E_INVALIDPARAM;
                }
                matrix[index] = param[index]->AsReal();
            }
        } else if(numparams == 1 && param[0] && param[0]->Type() == tvtObject &&
                  param[0]->AsObjectNoAddRef() != nullptr) {
            const auto object = *param[0];
            tTJSVariant value;
            if(getObjectProperty(object, TJS_W("m11"), value) &&
               value.Type() != tvtVoid) {
                matrix[0] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m21"), value) &&
               value.Type() != tvtVoid) {
                matrix[1] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m12"), value) &&
               value.Type() != tvtVoid) {
                matrix[2] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m22"), value) &&
               value.Type() != tvtVoid) {
                matrix[3] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m14"), value) &&
               value.Type() != tvtVoid) {
                matrix[4] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m24"), value) &&
               value.Type() != tvtVoid) {
                matrix[5] = value.AsReal();
            }
        } else {
            return TJS_E_BADPARAMCOUNT;
        }

        nativeInstance->_runtime->drawAffineMatrix = matrix;
        const auto motionPath =
            nativeInstance->_runtime && nativeInstance->_runtime->activeMotion
                ? nativeInstance->_runtime->activeMotion->path
                : std::string{};
        const bool isIdentity =
            matrix[0] == 1.0 && matrix[1] == 0.0 && matrix[2] == 0.0 &&
            matrix[3] == 1.0 && matrix[4] == 0.0 && matrix[5] == 0.0;
        detail::logoChainTraceLogf(
            motionPath, "setDrawAffine", "0x6D4F14",
            nativeInstance->_clampedEvalTime,
            "numparams={} matrix=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] nonIdentityFlag={} routeSource={}",
            numparams, matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
            matrix[5], isIdentity ? 0 : 1,
            (numparams >= 6) ? "six-params"
                             : ((numparams == 1) ? "matrix-object" : "invalid"));
        return TJS_S_OK;
    }

    tjs_error Player::captureCanvasCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        if(numparams > 0 && param[0] && param[0]->Type() == tvtObject &&
           param[0]->AsObjectNoAddRef() != nullptr) {
            if(nativeInstance->renderToLayer(param[0]->AsObjectNoAddRef())) {
                if(result) {
                    *result = *param[0];
                }
                return TJS_S_OK;
            }
        }

        if(result) {
            *result = nativeInstance->captureCanvas();
        }
        return TJS_S_OK;
    }

    tjs_error Player::clearCompatMethod(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 1 || !param || !param[0] ||
           param[0]->Type() != tvtObject) {
            return TJS_E_INVALIDPARAM;
        }

        iTJSDispatch2 *target = selectVariantDispatchTarget(param[0]);
        if(auto *adaptor =
               ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                   target, false)) {
            target = adaptor->getPrivateRenderTargetObject();
            if(!target) {
                return TJS_S_OK;
            }
        } else if(auto *resolved = tryResolveLayerDispatch(*param[0])) {
            target = resolved;
        }

        tTJSNI_BaseLayer *layer = nullptr;
        if(!target || TJS_FAILED(target->NativeInstanceSupport(
                          TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                          reinterpret_cast<iTJSNativeInstance **>(&layer))) ||
           !layer) {
            return TJS_S_OK;
        }

        auto *bitmap = layer->GetMainImage();
        if(!bitmap || bitmap->GetWidth() <= 0 || bitmap->GetHeight() <= 0) {
            return TJS_S_OK;
        }
        const tjs_uint32 color =
            numparams >= 2 && param[1]
                ? static_cast<tjs_uint32>(param[1]->AsInteger())
                : 0;
        bitmap->Fill(
            tTVPRect(0, 0, static_cast<tjs_int>(bitmap->GetWidth()),
                     static_cast<tjs_int>(bitmap->GetHeight())),
            color);
        layer->Update(false);
        self->_runtime->clearPresentationRenderReuse();
        return TJS_S_OK;
    }

    // drawCompat — aligned to libkrkr2.so sub_6D5FB8 / Player_drawD3D (0x6D5B90).
    // Logic:
    //   1. param is D3DAdaptor → set _d3dDrawMode and render via D3D path immediately
    //   2. param is SLA → route to SLA target
    //   3. param is Layer → if _d3dDrawMode, render via shared D3DAdaptor+captureCanvas;
    //      else render directly to Layer
    tjs_error Player::drawCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }
        auto *nativeInstance = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(LOGGER && motionDebugEnabled()) {
            LOGGER->info(
                "motion drawCompat entered: objthis={} native={} numparams={} firstArgType={}",
                static_cast<const void *>(objthis),
                static_cast<const void *>(nativeInstance), numparams,
                (numparams > 0 && param && param[0])
                    ? static_cast<int>(param[0]->Type())
                    : -1);
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }
        auto releaseDeferredEndedTimelineHold = [&]() {
            nativeInstance->releaseDeferredEndedTimelineRenderHoldAfterDraw();
        };

        const auto motionPath =
            nativeInstance->_runtime && nativeInstance->_runtime->activeMotion
                ? nativeInstance->_runtime->activeMotion->path
                : std::string{};
        tTJSVariant *arg = (numparams > 0 && param) ? param[0] : nullptr;
        iTJSDispatch2 *paramObj = selectVariantDispatchTarget(arg);
        if(nativeInstance->isYuzuSdPreviewAnimationFrozen() &&
           nativeInstance->restoreFrozenYuzuSdPreviewFrame(paramObj)) {
            if(result) {
                if(arg) {
                    *result = *arg;
                } else {
                    *result = nativeInstance->_runtime->lastCanvas;
                }
            }
            releaseDeferredEndedTimelineHold();
            return TJS_S_OK;
        }
        tTJSNI_BaseLayer *argLayer = nullptr;
        const bool argIsLayer = arg && tryGetLayerObject(*arg, argLayer);
        iTJSDispatch2 *resolvedLayerObject =
            arg ? tryResolveLayerDispatch(*arg) : nullptr;
        if(LOGGER && motionDebugEnabled()) {
            static int probeCount = 0;
            const bool titleProbe =
                motionPath.find("title") != std::string::npos;
            if(!argIsLayer && (titleProbe || probeCount < 32)) {
                ++probeCount;
                tTJSVariantClosure closure;
                if(arg && arg->Type() == tvtObject) {
                    closure = arg->AsObjectClosureNoAddRef();
                } else {
                    closure.Object = nullptr;
                    closure.ObjThis = nullptr;
                }
                LOGGER->info(
                    "motion drawCompat target probe: motion={} argType={} object=[{}] objThis=[{}] selected=[{}] valueLayer=[{}] resolvedObject={}",
                    motionPath, arg ? static_cast<int>(arg->Type()) : -1,
                    describeDispatchLayerProbe(closure.Object),
                    describeDispatchLayerProbe(closure.ObjThis),
                    describeDispatchLayerProbe(paramObj),
                    describeLayerForQueryDebug(argLayer),
                    static_cast<const void *>(resolvedLayerObject));
            }
        }

        if(!paramObj) {
            iTJSDispatch2 *targetLayerObject =
                tryResolveLayerDispatch(nativeInstance->_targetLayer);
            if(targetLayerObject) {
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion drawCompat route: motion={} route=stored-target target={}",
                        motionPath,
                        static_cast<const void *>(targetLayerObject));
                }
                if(nativeInstance->_d3dDrawMode) {
                    nativeInstance->renderViaSharedD3DAdaptor(targetLayerObject);
                } else {
                    nativeInstance->renderToLayer(targetLayerObject);
                }
                if(result) {
                    *result = tTJSVariant(targetLayerObject, targetLayerObject);
                }
                releaseDeferredEndedTimelineHold();
                return TJS_S_OK;
            }
            detail::logoChainTraceLogf(
                motionPath, "drawCompat.dispatch", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "route=no-param drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f})",
                nativeInstance->_runtime->drawAffineMatrix[0],
                nativeInstance->_runtime->drawAffineMatrix[1],
                nativeInstance->_runtime->drawAffineMatrix[2],
                nativeInstance->_runtime->drawAffineMatrix[3],
                nativeInstance->_runtime->drawAffineMatrix[4],
                nativeInstance->_runtime->drawAffineMatrix[5],
                nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
            if(result) {
                *result = nativeInstance->_runtime->lastCanvas;
            }
            releaseDeferredEndedTimelineHold();
            return TJS_S_OK;
        }

        // Direct Layer fast path. TJS may pass Layer as a closure object that
        // also confuses unrelated ncbind native-instance probes, so resolve it
        // before checking D3DAdaptor/SLA wrappers.
        if(argIsLayer || resolvedLayerObject == paramObj) {
            auto *drawTargetObject =
                resolvedLayerObject ? resolvedLayerObject : paramObj;
            if(!argLayer && resolvedLayerObject) {
                tTJSVariant resolvedVar(resolvedLayerObject, resolvedLayerObject);
                tryGetLayerObject(resolvedVar, argLayer);
            }
            if(LOGGER && motionDebugEnabled()) {
                LOGGER->info(
                    "motion drawCompat route: motion={} object={} resolvedObject={} route={} d3dDrawModeBefore={} targetLayer={} drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                    motionPath, static_cast<const void *>(paramObj),
                    static_cast<const void *>(drawTargetObject),
                    nativeInstance->_d3dDrawMode ? "layer-via-d3d" : "layer",
                    nativeInstance->_d3dDrawMode ? 1 : 0,
                    static_cast<const void *>(argLayer),
                    nativeInstance->_runtime->drawAffineMatrix[0],
                    nativeInstance->_runtime->drawAffineMatrix[1],
                    nativeInstance->_runtime->drawAffineMatrix[2],
                    nativeInstance->_runtime->drawAffineMatrix[3],
                    nativeInstance->_runtime->drawAffineMatrix[4],
                    nativeInstance->_runtime->drawAffineMatrix[5]);
            }
            detail::logoChainTraceCheck(
                motionPath, "drawCompat.dispatch", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "Layer -> renderToLayer/renderViaSharedD3DAdaptor",
                nativeInstance->_d3dDrawMode
                    ? "Layer -> renderViaSharedD3DAdaptor"
                    : "Layer -> renderToLayer",
                true, "drawCompat Layer routing mismatch");
            detail::logoChainTraceLogf(
                motionPath, "drawCompat.matrix", "0x6D5FB8",
                nativeInstance->_clampedEvalTime,
                "route={} drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                nativeInstance->_d3dDrawMode ? "layer-via-d3d" : "layer",
                nativeInstance->_runtime->drawAffineMatrix[0],
                nativeInstance->_runtime->drawAffineMatrix[1],
                nativeInstance->_runtime->drawAffineMatrix[2],
                nativeInstance->_runtime->drawAffineMatrix[3],
                nativeInstance->_runtime->drawAffineMatrix[4],
                nativeInstance->_runtime->drawAffineMatrix[5],
                nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
            if(nativeInstance->_d3dDrawMode) {
                nativeInstance->renderViaSharedD3DAdaptor(drawTargetObject);
            } else {
                nativeInstance->renderToLayer(drawTargetObject);
            }
            if(result) *result = *arg;
            releaseDeferredEndedTimelineHold();
            return TJS_S_OK;
        }

        // Step 1: Check if param is D3DAdaptor (libkrkr2.so checks NIS with
        // D3DAdaptor classID). If so, set _d3dDrawMode and render immediately.
        {
            auto *d3dAdaptor =
                ncbInstanceAdaptor<D3DAdaptor>::GetNativeInstance(paramObj, false);
            if(d3dAdaptor) {
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion drawCompat route: motion={} object={} route=d3d d3dDrawModeBefore={}",
                        motionPath, static_cast<const void *>(paramObj),
                        nativeInstance->_d3dDrawMode ? 1 : 0);
                }
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "D3DAdaptor -> Player_drawD3D",
                    "D3DAdaptor -> Player_drawD3D", true,
                    "drawCompat D3D routing mismatch");
                detail::logoChainTraceLogf(
                    motionPath, "drawCompat.matrix", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "route=d3d drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                    nativeInstance->_runtime->drawAffineMatrix[0],
                    nativeInstance->_runtime->drawAffineMatrix[1],
                    nativeInstance->_runtime->drawAffineMatrix[2],
                    nativeInstance->_runtime->drawAffineMatrix[3],
                    nativeInstance->_runtime->drawAffineMatrix[4],
                    nativeInstance->_runtime->drawAffineMatrix[5],
                    nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
                nativeInstance->_d3dDrawMode = true;
                nativeInstance->renderToD3DAdaptor(d3dAdaptor);
                if(result && arg) *result = *arg;
                releaseDeferredEndedTimelineHold();
                return TJS_S_OK;
            }
        }

        // Step 2: Check if param is SLA.
        // Aligned to libkrkr2.so Player_drawCompat (0x6D5FB8):
        // the native code only checks the SeparateLayerAdaptor class ID here.
        // It does not route plain Layer objects through the SLA backend just
        // because they resolve to an owner/target layer.
        {
            auto *sla =
                ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                    paramObj, false);
            if(sla) {
                tTJSNI_BaseLayer *ownerLayer = nullptr;
                if(auto *owner = sla->getOwner()) {
                    tTJSVariant ownerVar(owner, owner);
                    tryGetLayerObject(ownerVar, ownerLayer);
                }
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion drawCompat route: motion={} object={} route=sla owner=[{}] ancestry=[{}]",
                        motionPath, static_cast<const void *>(paramObj),
                        describeLayerForQueryDebug(ownerLayer),
                        describeLayerAncestryForQueryDebug(ownerLayer));
                }
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "SeparateLayerAdaptor -> Player_DrawSLA",
                    "SeparateLayerAdaptor -> Player_DrawSLA", true,
                    "drawCompat SLA routing mismatch");
                detail::logoChainTraceLogf(
                    motionPath, "drawCompat.matrix", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "route=sla drawAffine=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}] cameraOffset=({:.3f},{:.3f}) sampleExpectedYuzu=[1,0,0,1,960,540]",
                    nativeInstance->_runtime->drawAffineMatrix[0],
                    nativeInstance->_runtime->drawAffineMatrix[1],
                    nativeInstance->_runtime->drawAffineMatrix[2],
                    nativeInstance->_runtime->drawAffineMatrix[3],
                    nativeInstance->_runtime->drawAffineMatrix[4],
                    nativeInstance->_runtime->drawAffineMatrix[5],
                    nativeInstance->_cameraOffsetX, nativeInstance->_cameraOffsetY);
                nativeInstance->renderToSeparateLayerAdaptor(paramObj);
                if(result && arg) {
                    *result = *arg;
                }
                releaseDeferredEndedTimelineHold();
                return TJS_S_OK;
            }
        }

        // Step 4: param resolves to a Layer via property chain
        {
            iTJSDispatch2 *resolved = tryResolveSeparateAdaptorOwner(*arg);
            if(resolved) {
                tTJSNI_BaseLayer *resolvedLayer = nullptr;
                tTJSVariant resolvedVar(resolved, resolved);
                tryGetLayerObject(resolvedVar, resolvedLayer);
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion drawCompat route: motion={} object={} resolvedObject={} route={} d3dDrawModeBefore={} target=[{}] ancestry=[{}]",
                        motionPath, static_cast<const void *>(paramObj),
                        static_cast<const void *>(resolved),
                        nativeInstance->_d3dDrawMode
                            ? "resolved-layer-via-d3d"
                            : "resolved-layer",
                        nativeInstance->_d3dDrawMode ? 1 : 0,
                        describeLayerForQueryDebug(resolvedLayer),
                        describeLayerAncestryForQueryDebug(resolvedLayer));
                }
                detail::logoChainTraceCheck(
                    motionPath, "drawCompat.dispatch", "0x6D5FB8",
                    nativeInstance->_clampedEvalTime,
                    "Resolved owner Layer -> renderToLayer/renderViaSharedD3DAdaptor",
                    nativeInstance->_d3dDrawMode
                        ? "Resolved owner Layer -> renderViaSharedD3DAdaptor"
                        : "Resolved owner Layer -> renderToLayer",
                    true, "drawCompat owner-layer routing mismatch");
                if(nativeInstance->_d3dDrawMode) {
                    nativeInstance->renderViaSharedD3DAdaptor(resolved);
                } else {
                    nativeInstance->renderToLayer(resolved);
                }
                if(result) *result = tTJSVariant(resolved, resolved);
                releaseDeferredEndedTimelineHold();
                return TJS_S_OK;
            }
        }

        // Fallback: no SLA/Layer match
        if(LOGGER && motionDebugEnabled()) {
            LOGGER->info(
                "motion drawCompat unresolved target: motion={} selected=[{}]",
                motionPath, describeDispatchLayerProbe(paramObj));
        }
        detail::logoChainTraceCheck(
            motionPath, "drawCompat.dispatch", "0x6D5FB8",
            nativeInstance->_clampedEvalTime,
            "D3DAdaptor | SeparateLayerAdaptor | Layer",
            "unresolved target", false,
            "drawCompat could not classify the target object");
        if(result) {
            *result = nativeInstance->_runtime->lastCanvas;
        }
        releaseDeferredEndedTimelineHold();
        return TJS_S_OK;
    }

    tjs_error Player::playCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        self->releaseDeferredEndedTimelineRenderHoldAfterDraw(true);

        ttstr label;
        tjs_int flags = 0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            if(param[0]->Type() == tvtInteger || param[0]->Type() == tvtReal) {
                flags = param[0]->AsInteger();
            } else {
                label = *param[0];
            }
        }
        if(numparams > 1 && param[1] && param[1]->Type() != tvtVoid) {
            flags = param[1]->AsInteger();
        }

        if(!self->_runtime->activeMotion && self->_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(self->_project)) {
                activateMotion(*self->_runtime, snapshot);
                self->syncVariableKeysFromActiveMotion();
            }
        }

        self->ensureMotionLoaded();
        if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
            detail::primeTimelineStates(self->_runtime->timelines,
                                        *self->_runtime->activeMotion);
        }

        if(!label.IsEmpty() && !self->_runtime->activeMotion) {
            self->setMotion(label);
            self->ensureMotionLoaded();
            if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
                detail::primeTimelineStates(self->_runtime->timelines,
                                            *self->_runtime->activeMotion);
            }
        }

        if(!self->_runtime->activeMotion) {
            if(result) {
                *result = tTJSVariant(false);
            }
            return TJS_S_OK;
        }

        if(motionDebugEnabled() && LOGGER) {
            LOGGER->info(
                "motion play request: motion={} label={} flags={} chara={} timelines={} activeLabels=[{}]",
                self->_runtime->activeMotion->path, detail::narrow(label), flags,
                detail::narrow(self->_chara), self->_runtime->timelines.size(),
                joinStrings(self->_runtime->activeMotion->mainTimelineLabels));
        }

        if((flags & PlayFlagForce) != 0) {
            self->stopTimeline(TJS_W(""));
        }

        const auto playOne = [&](const std::string &timelineLabel,
                                 const bool rememberExplicit) {
            auto &state = self->_runtime->timelines[timelineLabel];
            state.label = timelineLabel;
            if(const auto *clip = detail::findMotionClip(
                   *self->_runtime->activeMotion,
                   detail::narrow(self->_chara), timelineLabel, false)) {
                // Motion labels are reused by many objects in one PSB. Use
                // the selected object's timing instead of the flattened
                // label table, whose last writer may be a different object.
                state.totalFrames = clip->totalFrames;
                state.loop = clip->loop;
                state.loopTime = clip->loopTime;
            }
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            state.currentTime = 0.0;
            state.controlInitialized = false;
            state.controlLastAppliedTime = 0.0;
            state.controlFrameCursor.clear();
            state.controlTrackValues.clear();
            state.controlTrackAnimators.clear();
            if(std::find(self->_runtime->playingTimelineLabels.begin(),
                         self->_runtime->playingTimelineLabels.end(),
                         timelineLabel) ==
               self->_runtime->playingTimelineLabels.end()) {
                self->_runtime->playingTimelineLabels.push_back(timelineLabel);
            }
            if(rememberExplicit) {
                self->_runtime->lastExplicitTimelineLabel = timelineLabel;
            }
            // Ensure totalFrames is set (may be 0 if timeline wasn't primed)
            if(state.totalFrames <= 0.0 && self->_runtime->activeMotion) {
                auto it = self->_runtime->activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != self->_runtime->activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            if(self->_runtime->timelines.find(key) != self->_runtime->timelines.end()) {
                playOne(key, true);
                started = true;
                if(motionDebugEnabled() && LOGGER) {
                    LOGGER->info("motion play exact match: request={} source={}",
                                 key, self->_runtime->activeMotion->path);
                }
            } else if(self->_runtime->activeMotion) {
                const auto aliases = resolveYuzuShortMotionLabels(
                    *self->_runtime->activeMotion, key,
                    detail::narrow(self->_chara));
                for(const auto &alias : aliases) {
                    if(self->_runtime->timelines.find(alias) ==
                       self->_runtime->timelines.end()) {
                        continue;
                    }
                    playOne(alias, true);
                    started = true;
                }
                if(started && motionDebugEnabled() && LOGGER) {
                    LOGGER->info(
                        "motion play yuzu short alias: request={} chara={} source={} resolved=[{}]",
                        key, detail::narrow(self->_chara),
                        self->_runtime->activeMotion->path,
                        joinStrings(aliases));
                }
            }
        }

        if(!started) {
            const auto &primary = !self->_runtime->activeMotion->mainTimelineLabels.empty()
                ? self->_runtime->activeMotion->mainTimelineLabels
                : self->_runtime->activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel, false);
                started = true;
            }
        }

        self->_allplaying = !self->_runtime->playingTimelineLabels.empty();
        if(started) {
            self->_runtime->nodes.clear();
            self->_runtime->nodesBuilt = false;
            self->_runtime->nodeLabelMap.clear();
            self->claimYuzuSdAutoProgress();
        }
        if(motionDebugEnabled() && LOGGER) {
            LOGGER->info(
                "motion play result: motion={} label={} started={} playing=[{}]",
                self->_runtime->activeMotion->path, detail::narrow(label),
                started ? 1 : 0,
                joinStrings(self->_runtime->playingTimelineLabels));
        }
        if(self->_allplaying) {
            self->enableAutoProgress(objthis);
        } else {
            self->disableAutoProgress();
        }

        if(result) {
            *result = tTJSVariant(started);
        }
        return TJS_S_OK;
    }

    tjs_error Player::progressCompatMethod(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        self->ensureMotionLoaded();
        self->noteManualProgress();

        double delta = 0.0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            delta = param[0]->AsReal();
        }
        // Clamp delta to sane range: TJS tick differences can overflow
        // when uint32 wraps (e.g. 4294967381 = 2^32 + 85)
        if(delta < 0 || delta > 60000) {
            delta = 0;
        }
        if(self->isYuzuSdPreviewAnimationFrozen()) {
            if(result) {
                *result = tTJSVariant(self->getProgressCompat());
            }
            return TJS_S_OK;
        }

        self->_runtime->pendingEvents.clear();
        self->frameProgress(delta * kMotionFramesPerMillisecond);
        const auto motionPath =
            self->_runtime && self->_runtime->activeMotion
                ? self->_runtime->activeMotion->path
                : std::string{};
        detail::logoChainTraceCheck(
            motionPath, "progressCompat.dt", "0x6D2A98",
            self->_clampedEvalTime,
            fmt::format("dt_ms*60/1000={:.6f}", delta * kMotionFramesPerMillisecond),
            fmt::format("dt_frames={:.6f}", self->_frameLastTime),
            std::fabs(self->_frameLastTime - delta * kMotionFramesPerMillisecond) <
                0.000001,
            "progressCompat dt(ms)->frame conversion diverged from 0x6D2A98");
        const std::string renderHoldLabel =
            self->beginEndedTimelineRenderHold();

        // Aligned to libkrkr2.so Player_progressCompat (0x6D2A98):
        // progress_inner -> updateLayers -> calcBounds -> dispatchEvents.
        self->ensureNodeTreeBuilt();
        if(!self->_runtime->nodes.empty()) {
            detail::logoChainTraceLogf(
                motionPath, "progressCompat.update", "0x6D2A98",
                self->_clampedEvalTime,
                "timelineCurrentTime={:.3f} pendingEvents={} nodes={}",
                self->_clampedEvalTime, self->_runtime->pendingEvents.size(),
                self->_runtime->nodes.size());
            self->updateLayers();
        }
        self->calcBounds();

        // MotionAffineSourceLayer calls progress() immediately before draw().
        // Rendering an SD here as well races KAG's back/front-page clone and
        // lets the outgoing page overwrite the new composite surface.
        const bool scriptOwnedYuzuSdDraw =
            isYuzuSdPresentationMotionPath(motionPath) &&
            self->_targetLayer.Type() == tvtObject;
        if(!scriptOwnedYuzuSdDraw && !self->_autoProgressRendering &&
           !self->_presentationHoldRendering) {
            iTJSDispatch2 *target = nullptr;
            std::string staleSdTargetName;
            if(self->_targetLayer.Type() == tvtObject) {
                tTJSVariant targetValue = self->_targetLayer;
                target = tryResolveLayerDispatch(targetValue);
            }
            if(!target && objthis) {
                tTJSVariant dispatchValue(objthis, objthis);
                target = tryResolveLayerDispatch(dispatchValue);
            }
            if(!target && self->_runtime->lastCanvas.Type() == tvtObject) {
                target = tryResolveLayerDispatch(self->_runtime->lastCanvas);
            }
            if(target && isYuzuSdPresentationMotionPath(motionPath) &&
               !yuzuSdPresentationTargetIsUsable(target)) {
                staleSdTargetName =
                    yuzuSdPresentationTargetLayerName(target);
                if(LOGGER && motionDebugEnabled()) {
                    LOGGER->info(
                        "motion progress discarded stale sd presentation target: motion={} target={}",
                        motionPath, static_cast<const void *>(target));
                }
                target = nullptr;
                self->_targetLayer.Clear();
            }
            if(!target) {
                target = resolveYuzuTitlePresentationTargetFromLayerTree(
                    motionPath);
                if(target) {
                    self->_targetLayer = tTJSVariant(target, target);
                }
            }
            if(!target) {
                target = resolveRememberedYuzuSdPresentationTarget(
                    motionPath);
                if(target) {
                    self->_targetLayer = tTJSVariant(target, target);
                    if(LOGGER && motionDebugEnabled()) {
                        LOGGER->info(
                            "motion progress reused sd presentation target: motion={} target={}",
                            motionPath, static_cast<const void *>(target));
                    }
                }
            }
            if(!target) {
                target = resolveYuzuSdPresentationTargetFromLayerTree(
                    motionPath,
                    self->_runtime->lastExplicitTimelineLabel,
                    staleSdTargetName);
                if(target) {
                    self->_targetLayer = tTJSVariant(target, target);
                }
            }
            if(target) {
                tTJSVariant targetValue(target, target);
                tTJSNI_BaseLayer *targetLayer = nullptr;
                tryGetLayerObject(targetValue, targetLayer);
                if(layerBelongsToCgViewForQuery(targetLayer)) {
                    if(LOGGER && motionDebugEnabled()) {
                        LOGGER->info(
                            "motion progress render skipped for CG View scripted layer: motion={} target=[{}]",
                            motionPath,
                            describeLayerForQueryDebug(targetLayer));
                    }
                    // CustomCgViewLayer drives its AffineLayer through the SLA
                    // draw that immediately follows progress(). A second direct
                    // render here bypasses the script-owned zoom and produces a
                    // differently sized duplicate frame.
                    target = nullptr;
                }
            }
            if(target) {
                rememberYuzuSdPresentationTarget(motionPath, target);
                self->_autoProgressRendering = true;
                try {
                    if(LOGGER && motionDebugEnabled()) {
                        LOGGER->info(
                            "motion progress render target: motion={} target={}",
                            motionPath, static_cast<const void *>(target));
                    }
                    if(self->_d3dDrawMode) {
                        self->renderViaSharedD3DAdaptor(target);
                    } else {
                        self->renderToLayer(target);
                    }
                } catch(const std::exception &e) {
                    if(LOGGER) {
                        LOGGER->warn(
                            "motion progress render failed: motion={} error={}",
                            motionPath, e.what());
                    }
                } catch(...) {
                    if(LOGGER) {
                        LOGGER->warn(
                            "motion progress render failed: motion={} error=<unknown>",
                            motionPath);
                    }
                }
                self->_autoProgressRendering = false;
            } else if(LOGGER && motionDebugEnabled() &&
                      motionPath.find("title") != std::string::npos) {
                static int missingProgressTargetLogs = 0;
                if(missingProgressTargetLogs < 12) {
                    ++missingProgressTargetLogs;
                    LOGGER->info(
                        "motion progress render target missing: motion={} objthis={} targetLayerType={} lastCanvasType={}",
                        motionPath, static_cast<const void *>(objthis),
                        static_cast<int>(self->_targetLayer.Type()),
                        static_cast<int>(self->_runtime->lastCanvas.Type()));
                }
            }
        }

        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           self->_clampedEvalTime >= 30.0 && self->_clampedEvalTime <= 40.0) {
            std::fprintf(stderr, "SHOTMARK motion=%s frame=%.3f\n",
                         motionPath.c_str(), self->_clampedEvalTime);
        }

        if(!self->deferEndedTimelineRenderHoldUntilDraw(renderHoldLabel)) {
            self->endEndedTimelineRenderHold(renderHoldLabel);
        }
        self->dispatchPendingEvents(objthis);
        if(!self->_allplaying && self->_runtime->playingTimelineLabels.empty()) {
            self->disableAutoProgress();
        }

        if(result) {
            *result = tTJSVariant(self->getProgressCompat());
        }
        return TJS_S_OK;
    }

    tjs_error Player::setVariableCompatMethod(tTJSVariant *, tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2 || !param[0] || !param[1]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        const double ease =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        self->setVariable(ttstr(*param[0]), param[1]->AsReal(), transition,
                          ease);
        // Motion buttons update selector variables from mouse callbacks even
        // after the presentation timeline has stopped.  There may be no next
        // workMotion tick to consume the new value, so evaluate the zero-time
        // state now.  This also makes the first hover event visible instead of
        // lagging one mouse move behind.
        if(transition <= 0.0 && self->_runtime &&
           self->_runtime->activeMotion) {
            self->ensureNodeTreeBuilt();
            if(!self->_runtime->nodes.empty()) {
                self->updateLayers();
                self->calcBounds();
            }
        }
        return TJS_S_OK;
    }

    tjs_error Player::setCoordCompatMethod(tTJSVariant *, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2 || !param[0] || !param[1]) {
            return TJS_E_INVALIDPARAM;
        }

        const double transition =
            (numparams >= 3 && param[2]) ? param[2]->AsReal() : 0.0;
        const double ease =
            (numparams >= 4 && param[3]) ? param[3]->AsReal() : 0.0;
        self->setEmoteCoord(param[0]->AsReal(), param[1]->AsReal(), transition,
                            ease);
        return TJS_S_OK;
    }

    tjs_error Player::isPlayingCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // `playing` describes the selected/root timeline only. `allplaying`
        // is maintained separately by updateLayers and also includes nested
        // child players. Querying one property must not clear the other: the
        // KAG continuous handler reads `playing` after progress(), then relies
        // on `allplaying` on the next tick to finish longer child motions.
        const bool playing = !self->_runtime->playingTimelineLabels.empty();
        if(result) {
            *result = tTJSVariant(playing);
        }
        return TJS_S_OK;
    }

    tjs_error Player::stopCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // Aligned to libkrkr2.so Player_stop (0x6D9A30):
        // Binary simply clears the Player-level playing flag (player+1099).
        // Timeline state is left intact; TJS polls `playing` for edge-triggered
        // stop detection and may still inspect the final motion pose afterward.
        self->_allplaying = false;
        self->disableAutoProgress();

        if(result) {
            *result = tTJSVariant(true);
        }
        return TJS_S_OK;
    }

    tTJSVariant Player::motionList() {
        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;
        for(const auto &[_, snapshot] : _runtime->motionsByKey) {
            if(snapshot && seen.insert(snapshot->path).second) {
                paths.push_back(snapshot->path);
            }
        }
        return detail::makeArray(detail::stringsToVariants(paths));
    }

    void Player::emoteEdit(tTJSVariant args) {
        _directEdit = true;
        _tags = args;
    }

} // namespace motion
